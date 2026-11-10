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

Engine::BiquadCoefficients Engine::make_peaking(float sample_rate,
                                                const EqBand& band) {
    BiquadCoefficients c;
    if (!band.enabled || std::fabs(band.gain_db) < 0.0001F ||
        band.frequency_hz <= 0.0F || band.frequency_hz >= sample_rate * 0.49F) {
        return c;
    }

    const float frequency = clamp(band.frequency_hz, 10.0F, sample_rate * 0.49F);
    const float q = clamp(band.q, kMinQ, kMaxQ);
    const float gain_db = clamp(band.gain_db, kMinEqGainDb, kMaxEqGainDb);
    const float a = std::pow(10.0F, gain_db / 40.0F);
    const float omega = 2.0F * kPi * frequency / sample_rate;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) / (2.0F * q);
    const float a0 = 1.0F + alpha / a;

    c.b0 = (1.0F + alpha * a) / a0;
    c.b1 = (-2.0F * cosine) / a0;
    c.b2 = (1.0F - alpha * a) / a0;
    c.a1 = (-2.0F * cosine) / a0;
    c.a2 = (1.0F - alpha / a) / a0;
    return c;
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
        return true;
    }

    const float sample_rate = static_cast<float>(sample_rate_hz_);
    const float preamp = db_to_gain(clamp(parameters.preamp_db,
                                          kMinPreampDb, kMaxPreampDb));
    const float balance = clamp(parameters.balance, -1.0F, 1.0F);
    const float left_gain = balance > 0.0F ? 1.0F - balance : 1.0F;
    const float right_gain = balance < 0.0F ? 1.0F + balance : 1.0F;

    std::array<BiquadCoefficients, kEqBandCount> coefficients{};
    for (std::size_t band = 0; band < kEqBandCount; ++band) {
        coefficients[band] = make_peaking(sample_rate, parameters.bands[band]);
    }

    const float threshold = clamp(parameters.limiter_threshold, 0.1F, 1.0F);
    const float attack_ms = clamp(parameters.limiter_attack_ms, 0.1F, 100.0F);
    const float release_ms = clamp(parameters.limiter_release_ms, 1.0F, 1000.0F);
    const float attack_coeff = std::exp(-1.0F / (attack_ms * 0.001F * sample_rate));
    const float release_coeff = std::exp(-1.0F / (release_ms * 0.001F * sample_rate));

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t index = frame * 2U;
        float left = static_cast<float>(input[index]) / 32768.0F;
        float right = static_cast<float>(input[index + 1U]) / 32768.0F;

        left *= preamp * left_gain;
        right *= preamp * right_gain;

        for (std::size_t band = 0; band < kEqBandCount; ++band) {
            left = run_biquad(left, coefficients[band], states_[band][0]);
            right = run_biquad(right, coefficients[band], states_[band][1]);
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
            // Final safety ceiling prevents integer wrap/clipping even on
            // sudden transients before the envelope has settled.
            left = clamp(left, -threshold, threshold);
            right = clamp(right, -threshold, threshold);
        }

        output[index] = to_pcm16(left);
        output[index + 1U] = to_pcm16(right);
    }
    return true;
}

} // namespace pcm_dsp
