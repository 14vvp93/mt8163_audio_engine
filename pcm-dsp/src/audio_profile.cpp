#include "pcm_dsp/audio_profile.hpp"

#include <cmath>

namespace pcm_dsp {
namespace {

bool finite_in_range(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool valid_filter_type(FilterType type) {
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

bool valid_crossover_order(LinkwitzRileyOrder order) {
    switch (order) {
        case LinkwitzRileyOrder::Db12:
        case LinkwitzRileyOrder::Db24:
        case LinkwitzRileyOrder::Db36:
        case LinkwitzRileyOrder::Db48:
            return true;
        default:
            return false;
    }
}

bool valid_dynamics(const MultibandDynamicsParameters& dynamics,
                    std::uint32_t sample_rate_hz) {
    if (!finite_in_range(dynamics.low_crossover_hz, 30.0F,
                         static_cast<float>(sample_rate_hz) * 0.39F) ||
        !finite_in_range(dynamics.high_crossover_hz,
                         dynamics.low_crossover_hz * 1.5F + 0.001F,
                         static_cast<float>(sample_rate_hz) * 0.399F)) {
        return false;
    }

    for (const auto& channel : dynamics.compressors) {
        for (const auto& compressor : channel) {
            if (!finite_in_range(compressor.threshold_db, -60.0F, 0.0F) ||
                !finite_in_range(compressor.ratio, 1.0F, 20.0F) ||
                !finite_in_range(compressor.attack_ms, 0.1F, 500.0F) ||
                !finite_in_range(compressor.release_ms, 1.0F, 2000.0F) ||
                !finite_in_range(compressor.knee_db, 0.0F, 24.0F) ||
                !finite_in_range(compressor.makeup_gain_db, -12.0F, 12.0F)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool validate_audio_profile(const AudioProfile& profile,
                            std::uint32_t sample_rate_hz) {
    if (profile.schema_version != kAudioProfileSchemaVersion ||
        (sample_rate_hz != 44100U && sample_rate_hz != 48000U)) {
        return false;
    }

    const auto& quad = profile.quad;
    if (!finite_in_range(quad.preamp_db, -24.0F, 12.0F) ||
        !finite_in_range(quad.limiter_threshold, 0.1F, 1.0F) ||
        !finite_in_range(quad.limiter_attack_ms, 0.1F, 100.0F) ||
        !finite_in_range(quad.limiter_release_ms, 1.0F, 1000.0F)) {
        return false;
    }

    for (const auto& channel : quad.channels) {
        if (!finite_in_range(channel.gain_db, -24.0F, 12.0F) ||
            !finite_in_range(channel.delay_ms, 0.0F, kMaxTimeAlignmentMs)) {
            return false;
        }
        for (const auto& band : channel.bands) {
            if (!band.enabled) continue;
            if (!valid_filter_type(band.type) ||
                !finite_in_range(band.frequency_hz, 10.0F,
                                 static_cast<float>(sample_rate_hz) * 0.49F) ||
                !finite_in_range(band.gain_db, -18.0F, 18.0F) ||
                !finite_in_range(band.q, 0.1F, 10.0F) ||
                !finite_in_range(band.shelf_slope, 0.1F, 1.0F)) {
                return false;
            }
        }
    }

    const auto& routing = profile.routing;
    if (!finite_in_range(routing.sub_gain_db, -24.0F, 12.0F) ||
        (routing.bass_management_enabled && !routing.enabled)) {
        return false;
    }
    for (const float coefficient : routing.matrix) {
        if (!finite_in_range(coefficient, -2.0F, 2.0F)) return false;
    }
    if (routing.bass_management_enabled &&
        (!finite_in_range(routing.crossover_frequency_hz, 20.0F,
                          static_cast<float>(sample_rate_hz) * 0.45F) ||
         !valid_crossover_order(routing.crossover_order))) {
        return false;
    }

    const auto& enhancement = profile.enhancements;
    if (!finite_in_range(enhancement.bass_amount, 0.0F, 1.0F) ||
        !finite_in_range(enhancement.bass_frequency_hz, 40.0F, 160.0F) ||
        !finite_in_range(enhancement.clarity_amount, 0.0F, 1.0F) ||
        !finite_in_range(enhancement.clarity_frequency_hz, 1500.0F,
                         static_cast<float>(sample_rate_hz) * 0.45F)) {
        return false;
    }

    return valid_dynamics(profile.dynamics, sample_rate_hz);
}

} // namespace pcm_dsp
