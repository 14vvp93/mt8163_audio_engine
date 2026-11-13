#pragma once

#include "pcm_dsp/engine.hpp"

#include <cstdint>
#include <type_traits>

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

static_assert(std::is_trivially_copyable<StereoPlaybackProfile>::value,
              "Stereo profiles must support immutable snapshot publication");

bool validate_stereo_profile(const StereoPlaybackProfile& profile,
                             std::uint32_t sample_rate_hz) noexcept;

} // namespace pcm_dsp
