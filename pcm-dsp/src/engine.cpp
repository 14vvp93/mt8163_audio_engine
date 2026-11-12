#include "pcm_dsp/engine.hpp"

#include <algorithm>
#include <cmath>

namespace pcm_dsp {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kMinQ = 0.1F;
constexpr float kMaxQ = 10.0F;
constexpr float kMaxEqGainDb = 18.0F;
constexpr float kMinEqGainDb = -18.0F;
constexpr float kMaxPreampDb = 12.0F;
constexpr float kMinPreampDb = -24.0F;
constexpr float kMinFrequencyHz = 10.0F;
constexpr float kMaxShelfSlope = 1.0F;
constexpr float kMinShelfSlope = 0.1F;
}

bool Engine::configure(std::uint32_t sample_rate_hz) {
    if (sample_rate_hz != 44100U && sample_rate_hz != 48000U) {
        return false;
    }
    if (sample_rate_hz_ != sample_rate_hz) {
        sample_rate_hz_ = sample_rate_hz;
        reset();
    }
    return true;
}

void Engine::reset() {
    for (auto& band : states_) {
        for (auto& channel : band) {
            channel = {};
        }
    }
    for (auto& coefficients : current_coefficients_) {
        coefficients = {};
    }
    for (auto& band : transition_states_) {
        for (auto& channel : band) channel = {};
    }
    limiter_envelope_ = 0.0F;
}

float Engine::db_to_gain(float db) {
    return std::pow(10.0F, db / 20.0F);
}

float Engine::clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

std::int16_t Engine::to_pcm16(float sample) {
    const float scaled = clamp(sample, -1.0F, 1.0F) * 32767.0F;
    return static_cast<std::int16_t>(std::lrint(scaled));
}

Engine::BiquadCoefficients Engine::make_filter(float sample_rate,
                                               const EqBand& band) {
    BiquadCoefficients c;
    if (!band.enabled || band.frequency_hz < kMinFrequencyHz ||
        band.frequency_hz >= sample_rate * 0.49F) {
        return c;
    }

    const float frequency = clamp(band.frequency_hz,
                                  kMinFrequencyHz, sample_rate * 0.49F);
    const float q = clamp(band.q, kMinQ, kMaxQ);
    const float gain_db = clamp(band.gain_db, kMinEqGainDb, kMaxEqGainDb);
    const float omega = 2.0F * kPi * frequency / sample_rate;
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float a = std::pow(10.0F, gain_db / 40.0F);
    float a0 = 1.0F;

    switch (band.type) {
        case FilterType::Peaking: {
            if (std::fabs(gain_db) < 0.0001F) {
                return c;
            }
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
            if (std::fabs(gain_db) < 0.0001F) {
                return c;
            }
            const float slope = clamp(band.shelf_slope,
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

bool Engine::coefficients_equal(const BiquadCoefficients& left,
                                 const BiquadCoefficients& right) {
    return left.b0 == right.b0 && left.b1 == right.b1 &&
           left.b2 == right.b2 && left.a1 == right.a1 &&
           left.a2 == right.a2;
}

float Engine::run_biquad(float input,
                         const BiquadCoefficients& c,
                         BiquadState& state) {
    const float output = c.b0 * input + state.z1;
    state.z1 = c.b1 * input - c.a1 * output + state.z2;
    state.z2 = c.b2 * input - c.a2 * output;
    return output;
}

bool Engine::process(const std::int16_t* input,
                    std::int16_t* output,
                    std::size_t frames,
                    const Parameters& parameters) {
    if (sample_rate_hz_ == 0U || input == nullptr || output == nullptr) {
        return false;
    }
    if (!parameters.enabled) {
        if (input != output) {
            std::copy(input, input + frames * 2U, output);
        }
        reset();
        return true;
    }

    const float sample_rate = static_cast<float>(sample_rate_hz_);
    const float preamp = db_to_gain(clamp(parameters.preamp_db,
                                          kMinPreampDb, kMaxPreampDb));
    const float balance = clamp(parameters.balance, -1.0F, 1.0F);
    const float left_gain = balance > 0.0F ? 1.0F - balance : 1.0F;
    const float right_gain = balance < 0.0F ? 1.0F + balance : 1.0F;

    std::array<BiquadCoefficients, kEqBandCount> target_coefficients{};
    bool coefficients_changed = false;
    for (std::size_t band = 0; band < kEqBandCount; ++band) {
        target_coefficients[band] = make_filter(sample_rate, parameters.bands[band]);
        coefficients_changed = coefficients_changed ||
            !coefficients_equal(current_coefficients_[band],
                                target_coefficients[band]);
    }

    // Never interpolate normalized IIR denominator coefficients: intermediate
    // a1/a2 values can move poles outside the unit circle. Run both stable
    // filter banks in parallel and crossfade their outputs instead.
    if (coefficients_changed) {
        // Filter state coordinates depend on the active coefficient set.
        // Start the target bank clean rather than transplanting potentially
        // incompatible delay states from a different filter topology.
        for (auto& band : transition_states_) {
            for (auto& channel : band) channel = {};
        }
    }

    const float threshold = clamp(parameters.limiter_threshold, 0.1F, 1.0F);
    const float attack_ms = clamp(parameters.limiter_attack_ms, 0.1F, 100.0F);
    const float release_ms = clamp(parameters.limiter_release_ms, 1.0F, 1000.0F);
    const float attack_coeff = std::exp(-1.0F / (attack_ms * 0.001F * sample_rate));
    const float release_coeff = std::exp(-1.0F / (release_ms * 0.001F * sample_rate));

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float amount = frames > 0U
            ? static_cast<float>(frame + 1U) / static_cast<float>(frames)
            : 1.0F;
        const std::size_t index = frame * 2U;
        const float source_left = static_cast<float>(input[index]) / 32768.0F;
        const float source_right = static_cast<float>(input[index + 1U]) / 32768.0F;
        const float dry_left = source_left * preamp * left_gain;
        const float dry_right = source_right * preamp * right_gain;
        float left = dry_left;
        float right = dry_right;

        if (coefficients_changed) {
            float old_left = dry_left;
            float old_right = dry_right;
            float new_left = dry_left;
            float new_right = dry_right;
            for (std::size_t band = 0; band < kEqBandCount; ++band) {
                old_left = run_biquad(old_left, current_coefficients_[band],
                                      states_[band][0]);
                old_right = run_biquad(old_right, current_coefficients_[band],
                                       states_[band][1]);
                new_left = run_biquad(new_left, target_coefficients[band],
                                      transition_states_[band][0]);
                new_right = run_biquad(new_right, target_coefficients[band],
                                       transition_states_[band][1]);
            }
            left = old_left + (new_left - old_left) * amount;
            right = old_right + (new_right - old_right) * amount;
        } else {
            for (std::size_t band = 0; band < kEqBandCount; ++band) {
                left = run_biquad(left, current_coefficients_[band],
                                  states_[band][0]);
                right = run_biquad(right, current_coefficients_[band],
                                   states_[band][1]);
            }
        }

        if (parameters.limiter_enabled) {
            const float peak = std::max(std::fabs(left), std::fabs(right));
            const float coefficient = peak > limiter_envelope_
                                          ? attack_coeff : release_coeff;
            limiter_envelope_ = coefficient * limiter_envelope_ +
                                (1.0F - coefficient) * peak;
            if (limiter_envelope_ > threshold) {
                const float gain = threshold / limiter_envelope_;
                left *= gain;
                right *= gain;
            }
            left = clamp(left, -threshold, threshold);
            right = clamp(right, -threshold, threshold);
        }

        output[index] = to_pcm16(left);
        output[index + 1U] = to_pcm16(right);
    }

    if (coefficients_changed) {
        states_ = transition_states_;
        current_coefficients_ = target_coefficients;
    }
    return true;
}

} // namespace pcm_dsp
