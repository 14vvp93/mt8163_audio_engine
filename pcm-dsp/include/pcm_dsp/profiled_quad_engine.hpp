#pragma once

#include "pcm_dsp/audio_profile.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

constexpr std::size_t kProfileTransitionMaxFrames = 1024U;
constexpr std::uint32_t kDefaultProfileTransitionMs = 50U;

// Double-buffered profile runner. apply_profile() is called by the control
// thread; process() is called by exactly one audio thread. No locks or heap
// allocations occur in process(). A second update is rejected until the
// current transition has completed and the retired engine is available.
class ProfiledQuadEngine {
public:
    bool configure(std::uint32_t sample_rate_hz,
                   const AudioProfile& initial_profile);
    bool apply_profile(const AudioProfile& profile,
                       std::uint32_t transition_ms =
                           kDefaultProfileTransitionMs);
    bool process(const float* input, float* output, std::size_t frames,
                 float* sub_output = nullptr);

    std::uint32_t sample_rate() const { return sample_rate_hz_; }
    bool transition_pending() const;

private:
    bool prepare_engine(std::size_t slot, const AudioProfile& profile);
    bool process_chunk(const float* input, float* output, std::size_t frames,
                        float* sub_output);

    std::array<QuadEngine, 2> engines_{};
    std::array<AudioProfile, 2> profiles_{};
    std::array<std::atomic<bool>, 2> slot_available_{{true, true}};
    std::atomic<int> active_slot_{0};
    std::atomic<int> pending_slot_{-1};
    std::uint32_t sample_rate_hz_{0};

    std::atomic<int> transition_target_{-1};
    std::uint64_t transition_position_{0};
    std::uint64_t transition_total_{0};
    std::array<float, kProfileTransitionMaxFrames * kOutputChannelCount>
        input_scratch_{};
    std::array<float, kProfileTransitionMaxFrames * kOutputChannelCount>
        target_scratch_{};
    std::array<float, kProfileTransitionMaxFrames> active_sub_scratch_{};
    std::array<float, kProfileTransitionMaxFrames> target_sub_scratch_{};
};

} // namespace pcm_dsp
