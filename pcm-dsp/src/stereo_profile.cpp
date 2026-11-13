#include "pcm_dsp/stereo_profile.hpp"

#include <cmath>

namespace pcm_dsp {
namespace {

bool finite_in_range(float value, float minimum, float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool valid_filter_type(FilterType type) noexcept {
    switch (type) {
        case FilterType::Peaking:
        case FilterType::LowShelf:
        case FilterType::HighShelf:
        case FilterType::HighPass:
        case FilterType::LowPass:
            return true;
        default:
            return false;
    }
}

bool valid_profile_id(StereoProfileId id) noexcept {
    switch (id) {
        case StereoProfileId::PrimaryMedia:
        case StereoProfileId::Custom:
            return true;
        default:
            return false;
    }
}

} // namespace

bool validate_stereo_profile(const StereoPlaybackProfile& profile,
                             std::uint32_t sample_rate_hz) noexcept {
    if (profile.schema_version != kStereoProfileSchemaVersion ||
        !valid_profile_id(profile.id) ||
        (sample_rate_hz != 32000U && sample_rate_hz != 44100U &&
         sample_rate_hz != 48000U)) {
        return false;
    }

    const auto& parameters = profile.parameters;
    if (!finite_in_range(parameters.preamp_db, -24.0F, 12.0F) ||
        !finite_in_range(parameters.balance, -1.0F, 1.0F) ||
        !finite_in_range(parameters.limiter_threshold, 0.1F, 1.0F) ||
        !finite_in_range(parameters.limiter_attack_ms, 0.1F, 100.0F) ||
        !finite_in_range(parameters.limiter_release_ms, 1.0F, 1000.0F)) {
        return false;
    }

    const float maximum_frequency =
        static_cast<float>(sample_rate_hz) * 0.49F;
    for (const auto& band : parameters.bands) {
        if (!valid_filter_type(band.type) ||
            !finite_in_range(band.frequency_hz, 10.0F, maximum_frequency) ||
            !finite_in_range(band.gain_db, -18.0F, 18.0F) ||
            !finite_in_range(band.q, 0.1F, 10.0F) ||
            !finite_in_range(band.shelf_slope, 0.1F, 1.0F)) {
            return false;
        }
    }

    return true;
}

} // namespace pcm_dsp
