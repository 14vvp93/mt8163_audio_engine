#include "pcm_dsp/enhancement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pcm_dsp {

bool QuadEnhancementProcessor::configure(
        std::uint32_t sample_rate_hz,
        const EnhancementParameters& parameters) {
    if ((sample_rate_hz != 44100U && sample_rate_hz != 48000U) ||
        !std::isfinite(parameters.bass_amount) ||
        parameters.bass_amount < 0.0F || parameters.bass_amount > 1.0F ||
        !std::isfinite(parameters.bass_frequency_hz) ||
        parameters.bass_frequency_hz < 40.0F ||
        parameters.bass_frequency_hz > 160.0F ||
        !std::isfinite(parameters.clarity_amount) ||
        parameters.clarity_amount < 0.0F || parameters.clarity_amount > 1.0F ||
        !std::isfinite(parameters.clarity_frequency_hz) ||
        parameters.clarity_frequency_hz < 1500.0F ||
        parameters.clarity_frequency_hz > 8000.0F ||
        parameters.clarity_frequency_hz >=
            static_cast<float>(sample_rate_hz) * 0.45F) {
        return false;
    }

    sample_rate_hz_ = sample_rate_hz;
    parameters_ = parameters;
    constexpr float kTwoPi = 6.28318530717958647692F;
    bass_coefficient_ = 1.0F - std::exp(
        -kTwoPi * parameters.bass_frequency_hz /
        static_cast<float>(sample_rate_hz));
    clarity_coefficient_ = 1.0F - std::exp(
        -kTwoPi * parameters.clarity_frequency_hz /
        static_cast<float>(sample_rate_hz));
    reset();
    return true;
}

void QuadEnhancementProcessor::reset() {
    states_ = {};
}

void QuadEnhancementProcessor::process(float* interleaved, std::size_t frames) {
    if (interleaved == nullptr || sample_rate_hz_ == 0U ||
        (!parameters_.bass_enabled && !parameters_.clarity_enabled)) {
        return;
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < states_.size(); ++channel) {
            const std::size_t index = frame * states_.size() + channel;
            const float input = std::isfinite(interleaved[index])
                ? interleaved[index] : 0.0F;
            ChannelState& state = states_[channel];
            float output = input;

            if (parameters_.bass_enabled && parameters_.bass_amount > 0.0F) {
                state.bass_lowpass += bass_coefficient_ *
                    (input - state.bass_lowpass);
                const float low = std::max(-1.0F,
                    std::min(1.0F, state.bass_lowpass));
                // Cubic waveshaping adds restrained odd harmonics.
                const float third_harmonic = low * low * low - 0.75F * low;
                output += third_harmonic *
                    (parameters_.bass_amount * 0.75F);
            }

            if (parameters_.clarity_enabled &&
                parameters_.clarity_amount > 0.0F) {
                state.clarity_lowpass += clarity_coefficient_ *
                    (input - state.clarity_lowpass);
                const float high = std::max(-1.0F, std::min(
                    1.0F, input - state.clarity_lowpass));
                const float third_harmonic =
                    high * high * high - 0.75F * high;
                output += third_harmonic *
                    (parameters_.clarity_amount * 0.45F);
            }

            interleaved[index] = std::isfinite(output) ? output : 0.0F;
        }
    }
}

} // namespace pcm_dsp
