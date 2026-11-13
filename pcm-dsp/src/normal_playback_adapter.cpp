#include "pcm_dsp/normal_playback_adapter.hpp"

#include <cstring>
#include <limits>

namespace pcm_dsp {

bool NormalPlaybackPcmAdapter::configure(std::uint32_t sample_rate_hz,
                                         PlaybackSampleFormat format,
                                         std::uint32_t channel_count) noexcept {
    reset();
    if (format != PlaybackSampleFormat::Pcm16 || channel_count != 2U) {
        return false;
    }

    configured_ = engine_.configure(sample_rate_hz);
    if (configured_) {
        sample_rate_hz_ = sample_rate_hz;
    }
    return configured_;
}

void NormalPlaybackPcmAdapter::reset() noexcept {
    engine_.reset();
    sample_rate_hz_ = 0U;
    configured_ = false;
    was_processing_ = false;
}

NormalPlaybackResult NormalPlaybackPcmAdapter::process(
    const std::int16_t* input,
    std::int16_t* output,
    std::size_t frames,
    const Parameters& parameters,
    const NormalPlaybackContext& context) noexcept {
    if (frames == 0U) {
        return NormalPlaybackResult::Bypassed;
    }
    constexpr std::size_t kBytesPerSample = sizeof(std::int16_t);
    constexpr std::size_t kBytesPerFrame = 2U * kBytesPerSample;
    if (input == nullptr || output == nullptr ||
        reinterpret_cast<std::uintptr_t>(input) % alignof(std::int16_t) != 0U ||
        reinterpret_cast<std::uintptr_t>(output) % alignof(std::int16_t) != 0U ||
        frames > std::numeric_limits<std::size_t>::max() / kBytesPerFrame) {
        return NormalPlaybackResult::InvalidBuffer;
    }

    const std::size_t byte_count = frames * kBytesPerFrame;
    const auto input_address = reinterpret_cast<std::uintptr_t>(input);
    const auto output_address = reinterpret_cast<std::uintptr_t>(output);
    const auto max_address = std::numeric_limits<std::uintptr_t>::max();
    if (byte_count > max_address - input_address ||
        byte_count > max_address - output_address) {
        return NormalPlaybackResult::InvalidBuffer;
    }

    const bool exact_alias = input_address == output_address;
    const bool overlaps = input_address < output_address + byte_count &&
                          output_address < input_address + byte_count;
    const bool partial_overlap = overlaps && !exact_alias;

    const bool eligible = configured_ &&
                          context.route == PlaybackRoute::PrimaryMedia &&
                          context.audio_mode == PlaybackAudioMode::Normal &&
                          context.format == PlaybackSampleFormat::Pcm16 &&
                          context.channel_count == 2U &&
                          context.sample_rate_hz == sample_rate_hz_ &&
                          !context.apdmnr_tuning_enabled &&
                          parameters.enabled;

    if (!eligible || partial_overlap) {
        if (was_processing_) {
            engine_.reset();
            was_processing_ = false;
        }
        if (!exact_alias) {
            std::memmove(output, input, byte_count);
        }
        return NormalPlaybackResult::Bypassed;
    }

    if (!engine_.process(input, output, frames, parameters)) {
        engine_.reset();
        was_processing_ = false;
        if (!exact_alias) {
            std::memmove(output, input, byte_count);
        }
        return NormalPlaybackResult::Bypassed;
    }

    was_processing_ = true;
    return NormalPlaybackResult::Processed;
}

} // namespace pcm_dsp
