#include "pcm_dsp/quad_engine.hpp"

#include <algorithm>
#include <cmath>

namespace pcm_dsp {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kMinQ = 0.1F;
constexpr float kMaxQ = 10.0F;
constexpr float kMinGainDb = -18.0F;
constexpr float kMaxGainDb = 18.0F;
constexpr float kMinPreampDb = -24.0F;
constexpr float kMaxPreampDb = 12.0F;
constexpr float kMinFrequencyHz = 10.0F;
constexpr float kMinShelfSlope = 0.1F;
constexpr float kMaxShelfSlope = 1.0F;
}

bool QuadEngine::configure(std::uint32_t sample_rate_hz) {
    if (sample_rate_hz != 44100U && sample_rate_hz != 48000U) {
        return false;
    }
    if (sample_rate_hz_ != sample_rate_hz) {
        sample_rate_hz_ = sample_rate_hz;
        reset();
        if (!time_alignment_.configure(sample_rate_hz)) return false;
    }
    return true;
}

void QuadEngine::reset() {
    banks_ = {};
    for (auto& bank : banks_) {
        for (auto& channel : bank.coefficients) {
            for (auto& coefficient : channel) {
                coefficient = {};
            }
        }
        bank.gains = {{1.0F, 1.0F, 1.0F, 1.0F}};
    }
    active_bank_ = 0U;
    limiter_envelope_.fill(0.0F);
    time_alignment_.reset();
}

float QuadEngine::db_to_gain(float db) {
    return std::pow(10.0F, db / 20.0F);
}

float QuadEngine::clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

bool QuadEngine::same_coefficients(const BiquadCoefficients& a,
                                   const BiquadCoefficients& b) {
    return a.b0 == b.b0 && a.b1 == b.b1 && a.b2 == b.b2 &&
           a.a1 == b.a1 && a.a2 == b.a2;
}

QuadEngine::BiquadCoefficients QuadEngine::make_filter(
        float sample_rate, const EqBand& band) {
    BiquadCoefficients c;
    if (!band.enabled || !std::isfinite(band.frequency_hz) ||
        band.frequency_hz < kMinFrequencyHz ||
        band.frequency_hz >= sample_rate * 0.49F) {
        return c;
    }

    const float frequency = clamp(band.frequency_hz,
                                  kMinFrequencyHz, sample_rate * 0.49F);
    const float q = clamp(std::isfinite(band.q) ? band.q : 0.707F,
                          kMinQ, kMaxQ);
    const float gain_db = clamp(std::isfinite(band.gain_db) ? band.gain_db : 0.0F,
                                kMinGainDb, kMaxGainDb);
    const float omega = 2.0F * kPi * frequency / sample_rate;
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float a = std::pow(10.0F, gain_db / 40.0F);
    float a0 = 1.0F;

    switch (band.type) {
        case FilterType::Peaking: {
            if (std::fabs(gain_db) < 0.0001F) return c;
            const float alpha = sine / (2.0F * q);
            a0 = 1.0F + alpha / a;
            c.b0 = (1.0F + alpha * a) / a0;
            c.b1 = (-2.0F * cosine) / a0;
            c.b2 = (1.0F - alpha * a) / a0;
            c.a1 = (-2.0F * cosine) / a0;
            c.a2 = (1.0F - alpha / a) / a0;
            break;
        }
        case FilterType::LowShelf:
        case FilterType::HighShelf: {
            if (std::fabs(gain_db) < 0.0001F) return c;
            const float slope = clamp(
                std::isfinite(band.shelf_slope) ? band.shelf_slope : 1.0F,
                kMinShelfSlope, kMaxShelfSlope);
            const float alpha = (sine * 0.5F) *
                std::sqrt((a + 1.0F / a) * (1.0F / slope - 1.0F) + 2.0F);
            const float beta = 2.0F * std::sqrt(a) * alpha;
            if (band.type == FilterType::LowShelf) {
                c.b0 = a * ((a + 1.0F) - (a - 1.0F) * cosine + beta);
                c.b1 = 2.0F * a * ((a - 1.0F) - (a + 1.0F) * cosine);
                c.b2 = a * ((a + 1.0F) - (a - 1.0F) * cosine - beta);
                a0 = (a + 1.0F) + (a - 1.0F) * cosine + beta;
                c.a1 = -2.0F * ((a - 1.0F) + (a + 1.0F) * cosine);
                c.a2 = (a + 1.0F) + (a - 1.0F) * cosine - beta;
            } else {
                c.b0 = a * ((a + 1.0F) + (a - 1.0F) * cosine + beta);
                c.b1 = -2.0F * a * ((a - 1.0F) + (a + 1.0F) * cosine);
                c.b2 = a * ((a + 1.0F) + (a - 1.0F) * cosine - beta);
                a0 = (a + 1.0F) - (a - 1.0F) * cosine + beta;
                c.a1 = 2.0F * ((a - 1.0F) - (a + 1.0F) * cosine);
                c.a2 = (a + 1.0F) - (a - 1.0F) * cosine - beta;
            }
            c.b0 /= a0;
            c.b1 /= a0;
            c.b2 /= a0;
            c.a1 /= a0;
            c.a2 /= a0;
            break;
        }
        case FilterType::HighPass:
        case FilterType::LowPass: {
            const float alpha = sine / (2.0F * q);
            if (band.type == FilterType::LowPass) {
                c.b0 = (1.0F - cosine) * 0.5F;
                c.b1 = 1.0F - cosine;
                c.b2 = (1.0F - cosine) * 0.5F;
            } else {
                c.b0 = (1.0F + cosine) * 0.5F;
                c.b1 = -(1.0F + cosine);
                c.b2 = (1.0F + cosine) * 0.5F;
            }
            a0 = 1.0F + alpha;
            c.a1 = -2.0F * cosine;
            c.a2 = 1.0F - alpha;
            c.b0 /= a0;
            c.b1 /= a0;
            c.b2 /= a0;
            c.a1 /= a0;
            c.a2 /= a0;
            break;
        }
        default:
            return BiquadCoefficients{};
    }
    return c;
}

float QuadEngine::run_biquad(float input,
                             const BiquadCoefficients& c,
                             BiquadState& state) {
    const float output = c.b0 * input + state.z1;
    state.z1 = c.b1 * input - c.a1 * output + state.z2;
    state.z2 = c.b2 * input - c.a2 * output;
    return output;
}

bool QuadEngine::process(const float* input, float* output,
                         std::size_t frames,
                         const QuadParameters& parameters) {
    if (sample_rate_hz_ == 0U || input == nullptr || output == nullptr) {
        return false;
    }
    if (frames == 0U) return true;
    if (!parameters.enabled) {
        if (input != output) {
            std::copy(input, input + frames * kOutputChannelCount, output);
        }
        reset();
        return true;
    }

    const float sample_rate = static_cast<float>(sample_rate_hz_);
    const float preamp_db = clamp(
        std::isfinite(parameters.preamp_db) ? parameters.preamp_db : 0.0F,
        kMinPreampDb, kMaxPreampDb);
    const float preamp_gain = db_to_gain(preamp_db);
    const std::size_t next_bank = 1U - active_bank_;
    Bank& active = banks_[active_bank_];
    Bank& target = banks_[next_bank];
    bool transition = false;

    for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
        const auto& channel_parameters = parameters.channels[channel];
        const float channel_db = clamp(
            std::isfinite(channel_parameters.gain_db)
                ? channel_parameters.gain_db : 0.0F,
            -24.0F, 12.0F);
        const float gain = preamp_gain * db_to_gain(channel_db);
        if (gain != active.gains[channel]) transition = true;
        target.gains[channel] = gain;

        for (std::size_t band = 0; band < kQuadEqBandCount; ++band) {
            const auto coefficients = make_filter(
                sample_rate, channel_parameters.bands[band]);
            target.coefficients[channel][band] = coefficients;
            const BiquadCoefficients identity{};
            target.filter_enabled[channel][band] =
                !same_coefficients(coefficients, identity);
            if (!same_coefficients(coefficients,
                                   active.coefficients[channel][band])) {
                transition = true;
            }
        }
    }

    if (transition) {
        target.states = active.states;
    } else {
        // No parameter transition: keep using the active state bank.
        target = active;
    }

    const float threshold = clamp(
        std::isfinite(parameters.limiter_threshold)
            ? parameters.limiter_threshold : 0.98F,
        0.1F, 1.0F);
    const float attack_ms = clamp(
        std::isfinite(parameters.limiter_attack_ms)
            ? parameters.limiter_attack_ms : 5.0F,
        0.1F, 100.0F);
    const float release_ms = clamp(
        std::isfinite(parameters.limiter_release_ms)
            ? parameters.limiter_release_ms : 80.0F,
        1.0F, 1000.0F);
    const float attack_coeff = std::exp(
        -1.0F / (attack_ms * 0.001F * sample_rate));
    const float release_coeff = std::exp(
        -1.0F / (release_ms * 0.001F * sample_rate));

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float mix = transition && frames > 1U
            ? static_cast<float>(frame + 1U) / static_cast<float>(frames)
            : (transition ? 1.0F : 0.0F);
        for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
            const std::size_t index = frame * kOutputChannelCount + channel;
            float sample = std::isfinite(input[index]) ? input[index] : 0.0F;
            float processed = sample;
            if (transition) {
                float old_value = sample * active.gains[channel];
                float new_value = sample * target.gains[channel];
                for (std::size_t band = 0; band < kQuadEqBandCount; ++band) {
                    if (active.filter_enabled[channel][band]) {
                        old_value = run_biquad(old_value,
                            active.coefficients[channel][band],
                            active.states[channel][band]);
                    }
                    if (target.filter_enabled[channel][band]) {
                        new_value = run_biquad(new_value,
                            target.coefficients[channel][band],
                            target.states[channel][band]);
                    }
                }
                processed = old_value + (new_value - old_value) * mix;
            } else {
                processed *= active.gains[channel];
                for (std::size_t band = 0; band < kQuadEqBandCount; ++band) {
                    if (active.filter_enabled[channel][band]) {
                        processed = run_biquad(processed,
                            active.coefficients[channel][band],
                            active.states[channel][band]);
                    }
                }
            }

            output[index] = std::isfinite(processed) ? processed : 0.0F;
        }
    }

    std::array<float, kOutputChannelCount> delays{};
    for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
        delays[channel] = parameters.channels[channel].delay_ms;
    }
    if (!time_alignment_.process(output, output, frames, delays)) return false;

    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
            const std::size_t index = frame * kOutputChannelCount + channel;
            float processed = output[index];
            if (parameters.limiter_enabled) {
                const float peak = std::fabs(processed);
                const float coefficient = peak > limiter_envelope_[channel]
                    ? attack_coeff : release_coeff;
                limiter_envelope_[channel] =
                    coefficient * limiter_envelope_[channel] +
                    (1.0F - coefficient) * peak;
                if (limiter_envelope_[channel] > threshold) {
                    processed *= threshold / limiter_envelope_[channel];
                }
                processed = clamp(processed, -threshold, threshold);
            } else {
                processed = clamp(processed, -1.0F, 1.0F);
            }
            output[index] = processed;
        }
    }

    if (transition) {
        active_bank_ = next_bank;
    }
    return true;
}

} // namespace pcm_dsp
