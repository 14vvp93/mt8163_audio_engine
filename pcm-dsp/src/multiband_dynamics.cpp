#include "pcm_dsp/multiband_dynamics.hpp"

#include <algorithm>
#include <cmath>

namespace pcm_dsp {
namespace {
constexpr float kMinimumEnvelope = 1.0e-9F;
}

float MultibandDynamicsProcessor::db_to_gain(float db) {
    return std::pow(10.0F, db / 20.0F);
}

bool MultibandDynamicsProcessor::valid_parameters(
        std::uint32_t sample_rate_hz,
        const MultibandDynamicsParameters& parameters) {
    if (sample_rate_hz != 44100U && sample_rate_hz != 48000U) return false;
    if (!std::isfinite(parameters.low_crossover_hz) ||
        !std::isfinite(parameters.high_crossover_hz) ||
        parameters.low_crossover_hz < 30.0F ||
        parameters.high_crossover_hz <= parameters.low_crossover_hz * 1.5F ||
        parameters.high_crossover_hz >=
            static_cast<float>(sample_rate_hz) * 0.40F) {
        return false;
    }

    for (const auto& channel : parameters.compressors) {
        for (const auto& band : channel) {
            if (!std::isfinite(band.threshold_db) ||
                !std::isfinite(band.ratio) ||
                !std::isfinite(band.attack_ms) ||
                !std::isfinite(band.release_ms) ||
                !std::isfinite(band.knee_db) ||
                !std::isfinite(band.makeup_gain_db) ||
                band.threshold_db < -60.0F || band.threshold_db > 0.0F ||
                band.ratio < 1.0F || band.ratio > 20.0F ||
                band.attack_ms < 0.1F || band.attack_ms > 500.0F ||
                band.release_ms < 1.0F || band.release_ms > 2000.0F ||
                band.knee_db < 0.0F || band.knee_db > 24.0F ||
                band.makeup_gain_db < -12.0F ||
                band.makeup_gain_db > 12.0F) {
                return false;
            }
        }
    }
    return true;
}

bool MultibandDynamicsProcessor::configure(
        std::uint32_t sample_rate_hz,
        const MultibandDynamicsParameters& parameters) {
    if (!valid_parameters(sample_rate_hz, parameters)) return false;

    for (std::size_t channel = 0; channel < kDynamicsChannelCount; ++channel) {
        if (!low_split_[channel].configure(
                sample_rate_hz, parameters.low_crossover_hz,
                LinkwitzRileyOrder::Db24) ||
            !high_split_[channel].configure(
                sample_rate_hz, parameters.high_crossover_hz,
                LinkwitzRileyOrder::Db24)) {
            return false;
        }
    }

    sample_rate_hz_ = sample_rate_hz;
    parameters_ = parameters;
    const float rate = static_cast<float>(sample_rate_hz_);
    for (std::size_t channel = 0; channel < kDynamicsChannelCount; ++channel) {
        for (std::size_t band = 0; band < kDynamicsBandCount; ++band) {
            const auto& p = parameters_.compressors[channel][band];
            auto& state = states_[channel][band];
            state.envelope = 0.0F;
            state.attack_coefficient =
                std::exp(-1.0F / (0.001F * p.attack_ms * rate));
            state.release_coefficient =
                std::exp(-1.0F / (0.001F * p.release_ms * rate));
            state.makeup_gain = db_to_gain(p.makeup_gain_db);
        }
    }
    return true;
}

void MultibandDynamicsProcessor::reset() {
    for (auto& crossover : low_split_) crossover.reset();
    for (auto& crossover : high_split_) crossover.reset();
    for (auto& channel : states_) {
        for (auto& state : channel) state.envelope = 0.0F;
    }
}

float MultibandDynamicsProcessor::compress_sample(
        float sample, const CompressorParameters& parameters,
        CompressorState& state) {
    if (!parameters.enabled) return sample;

    const float magnitude = std::fabs(sample);
    const float coefficient = magnitude > state.envelope
        ? state.attack_coefficient : state.release_coefficient;
    state.envelope = coefficient * state.envelope +
                     (1.0F - coefficient) * magnitude;

    const float level_db = 20.0F * std::log10(
        std::max(state.envelope, kMinimumEnvelope));
    const float over_db = level_db - parameters.threshold_db;
    float reduction_db = 0.0F;
    const float slope = (1.0F / parameters.ratio) - 1.0F;
    const float half_knee = parameters.knee_db * 0.5F;

    if (parameters.knee_db > 0.0F &&
        over_db > -half_knee && over_db < half_knee) {
        const float knee_position = over_db + half_knee;
        reduction_db = slope * knee_position * knee_position /
                       (2.0F * parameters.knee_db);
    } else if (over_db >= half_knee) {
        reduction_db = slope * over_db;
    }

    const float gain = state.makeup_gain * db_to_gain(reduction_db);
    const float output = sample * gain;
    return std::isfinite(output) ? output : 0.0F;
}

bool MultibandDynamicsProcessor::process(
        const float* input, float* output, std::size_t frames) {
    if (sample_rate_hz_ == 0U || input == nullptr || output == nullptr) {
        return false;
    }

    if (!parameters_.enabled) {
        if (input != output) {
            const std::size_t sample_count = frames * kDynamicsChannelCount;
            for (std::size_t i = 0; i < sample_count; ++i) output[i] = input[i];
        }
        return true;
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < kDynamicsChannelCount; ++channel) {
            const std::size_t index = frame * kDynamicsChannelCount + channel;
            const float sample = std::isfinite(input[index]) ? input[index] : 0.0F;
            float low = 0.0F;
            float upper = 0.0F;
            float mid = 0.0F;
            float high = 0.0F;

            low_split_[channel].process(sample, low, upper);
            high_split_[channel].process(upper, mid, high);

            const float low_out = compress_sample(
                low, parameters_.compressors[channel][0], states_[channel][0]);
            const float mid_out = compress_sample(
                mid, parameters_.compressors[channel][1], states_[channel][1]);
            const float high_out = compress_sample(
                high, parameters_.compressors[channel][2], states_[channel][2]);
            const float mixed = low_out + mid_out + high_out;
            output[index] = std::isfinite(mixed) ? mixed : 0.0F;
        }
    }
    return true;
}

} // namespace pcm_dsp
