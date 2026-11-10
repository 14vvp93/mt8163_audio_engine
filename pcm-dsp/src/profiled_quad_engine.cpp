#include "pcm_dsp/profiled_quad_engine.hpp"

#include <algorithm>
#include <cmath>

namespace pcm_dsp {

bool ProfiledQuadEngine::prepare_engine(std::size_t slot,
                                        const AudioProfile& profile) {
    QuadEngine& engine = engines_[slot];
    engine.reset();
    if (!engine.configure(sample_rate_hz_) ||
        !engine.configure_routing(profile.routing) ||
        !engine.configure_dynamics(profile.dynamics)) {
        return false;
    }
    profiles_[slot] = profile;
    return true;
}

bool ProfiledQuadEngine::configure(std::uint32_t sample_rate_hz,
                                   const AudioProfile& initial_profile) {
    if (!validate_audio_profile(initial_profile, sample_rate_hz)) return false;
    sample_rate_hz_ = sample_rate_hz;
    if (!prepare_engine(0U, initial_profile)) {
        sample_rate_hz_ = 0U;
        return false;
    }
    engines_[1] = QuadEngine{};
    profiles_[1] = AudioProfile{};
    active_slot_.store(0, std::memory_order_relaxed);
    pending_slot_.store(-1, std::memory_order_relaxed);
    slot_available_[0].store(false, std::memory_order_relaxed);
    slot_available_[1].store(true, std::memory_order_relaxed);
    transition_target_ = -1;
    transition_position_ = 0U;
    transition_total_ = 0U;
    return true;
}

bool ProfiledQuadEngine::apply_profile(const AudioProfile& profile,
                                       std::uint32_t transition_ms) {
    if (sample_rate_hz_ == 0U ||
        !validate_audio_profile(profile, sample_rate_hz_) ||
        transition_ms == 0U || transition_ms > 1000U) {
        return false;
    }

    const int active = active_slot_.load(std::memory_order_acquire);
    const std::size_t target = static_cast<std::size_t>(1 - active);
    bool expected = true;
    if (!slot_available_[target].compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
        return false;
    }

    if (!prepare_engine(target, profile)) {
        slot_available_[target].store(true, std::memory_order_release);
        return false;
    }

    // Publish non-atomic transition data before the release operation.
    transition_total_ = static_cast<std::uint64_t>(sample_rate_hz_) *
                        transition_ms / 1000U;
    int no_pending = -1;
    if (!pending_slot_.compare_exchange_strong(
            no_pending, static_cast<int>(target),
            std::memory_order_release, std::memory_order_relaxed)) {
        slot_available_[target].store(true, std::memory_order_release);
        return false;
    }
    return true;
}

bool ProfiledQuadEngine::transition_pending() const {
    return pending_slot_.load(std::memory_order_acquire) >= 0 ||
           transition_target_.load(std::memory_order_acquire) >= 0;
}

bool ProfiledQuadEngine::process(const float* input, float* output,
                                 std::size_t frames, float* sub_output) {
    if (sample_rate_hz_ == 0U || input == nullptr || output == nullptr) {
        return false;
    }
    const int active = active_slot_.load(std::memory_order_acquire);
    const int pending = pending_slot_.load(std::memory_order_acquire);
    const int current_target = transition_target_.load(std::memory_order_acquire);
    const int target = current_target >= 0 ? current_target : pending;
    if (target >= 0 &&
        (profiles_[static_cast<std::size_t>(active)].routing.bass_management_enabled ||
         profiles_[static_cast<std::size_t>(target)].routing.bass_management_enabled) &&
        sub_output == nullptr) {
        return false;
    }

    std::size_t offset = 0U;
    while (offset < frames) {
        const std::size_t chunk = std::min(
            kProfileTransitionMaxFrames, frames - offset);
        process_chunk(input + offset * kOutputChannelCount,
                      output + offset * kOutputChannelCount,
                      chunk, sub_output != nullptr ? sub_output + offset : nullptr);
        offset += chunk;
    }
    return true;
}

void ProfiledQuadEngine::process_chunk(const float* input, float* output,
                                      std::size_t frames, float* sub_output) {
    if (transition_target_.load(std::memory_order_relaxed) < 0) {
        const int pending_candidate = pending_slot_.load(std::memory_order_acquire);
        const bool pending_needs_sub = pending_candidate >= 0 &&
            profiles_[static_cast<std::size_t>(pending_candidate)]
                .routing.bass_management_enabled;
        // A profile requiring a subwoofer buffer must remain queued until the
        // caller supplies that buffer; never start a half-configured transition.
        if (pending_candidate >= 0 && (!pending_needs_sub || sub_output != nullptr)) {
            const int pending = pending_slot_.exchange(-1, std::memory_order_acq_rel);
            if (pending >= 0) {
                transition_position_ = 0U;
                transition_target_.store(pending, std::memory_order_release);
            }
        }
    }

    const int active = active_slot_.load(std::memory_order_relaxed);
    const int target_slot = transition_target_.load(std::memory_order_relaxed);
    if (target_slot < 0) {
        const bool ok = engines_[static_cast<std::size_t>(active)].process(
            input, output, frames, profiles_[static_cast<std::size_t>(active)].quad,
            sub_output);
        (void)ok; // Configuration and buffer requirements were validated.
        return;
    }

    const std::size_t samples = frames * kOutputChannelCount;
    std::copy(input, input + samples, input_scratch_.begin());
    float* active_sub = sub_output != nullptr ? active_sub_scratch_.data() : nullptr;
    float* target_sub = sub_output != nullptr ? target_sub_scratch_.data() : nullptr;

    const bool active_ok = engines_[static_cast<std::size_t>(active)].process(
        input_scratch_.data(), output, frames,
        profiles_[static_cast<std::size_t>(active)].quad, active_sub);
    const bool target_ok = engines_[static_cast<std::size_t>(target_slot)].process(
        input_scratch_.data(), target_scratch_.data(), frames,
        profiles_[static_cast<std::size_t>(target_slot)].quad, target_sub);
    if (!active_ok || !target_ok) {
        std::fill(output, output + samples, 0.0F);
        if (sub_output != nullptr) std::fill(sub_output, sub_output + frames, 0.0F);
        return;
    }

    const float denominator = static_cast<float>(
        std::max<std::uint64_t>(1U, transition_total_));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float mix = std::min(1.0F,
            static_cast<float>(transition_position_ + frame + 1U) / denominator);
        for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
            const std::size_t index = frame * kOutputChannelCount + channel;
            output[index] += (target_scratch_[index] - output[index]) * mix;
        }
        if (sub_output != nullptr) {
            sub_output[frame] = active_sub_scratch_[frame] +
                (target_sub_scratch_[frame] - active_sub_scratch_[frame]) * mix;
        }
    }

    transition_position_ += frames;
    if (transition_position_ >= transition_total_) {
        const int retired = active;
        active_slot_.store(target_slot, std::memory_order_release);
        transition_target_.store(-1, std::memory_order_release);
        transition_position_ = 0U;
        slot_available_[static_cast<std::size_t>(retired)].store(
            true, std::memory_order_release);
    }
}

} // namespace pcm_dsp
