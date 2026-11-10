#pragma once

#include "pcm_dsp/quad_engine.hpp"

#include <cstdint>

namespace pcm_dsp {

// Versioned, platform-independent snapshot of every four-channel DSP control.
// Keep this structure immutable while an audio block is being processed.
constexpr std::uint32_t kAudioProfileSchemaVersion = 1U;

struct AudioProfile {
    std::uint32_t schema_version{kAudioProfileSchemaVersion};
    QuadParameters quad{};
    QuadOutputRoutingParameters routing{};
    MultibandDynamicsParameters dynamics{};
    EnhancementParameters enhancements{};
};

// Validates the complete profile before any part of it is applied to an engine.
// The sample rate must match the already-configured QuadEngine.
bool validate_audio_profile(const AudioProfile& profile,
                            std::uint32_t sample_rate_hz);

} // namespace pcm_dsp
