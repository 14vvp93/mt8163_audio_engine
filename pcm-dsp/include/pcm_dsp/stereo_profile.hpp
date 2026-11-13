#pragma once

#include "pcm_dsp/engine.hpp"

#include <cstdint>

namespace pcm_dsp {

constexpr std::uint32_t kStereoProfileSchemaVersion = 1U;

enum class StereoProfileId : std::uint8_t {
    PrimaryMedia = 0,
    Custom
};

struct StereoPlaybackProfile {
    std::uint32_t schema_version{kStereoProfileSchemaVersion};
    StereoProfileId id{StereoProfileId::PrimaryMedia};
    Parameters parameters{};
};

bool validate_stereo_profile(const StereoPlaybackProfile& profile,
                             std::uint32_t sample_rate_hz) noexcept;

} // namespace pcm_dsp
