#include "pcm_dsp/crossover.hpp"

#include <algorithm>
#include <cmath>

namespace pcm_dsp {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kQButterworth = 0.7071067811865475F;
constexpr float kQ3rdOrder = 1.0F;
constexpr float kQ4thLow = 0.541196100146197F;
constexpr float kQ4thHigh = 1.306562964876377F;
}

LinkwitzRileyCrossover::Coefficients LinkwitzRileyCrossover::first_order(
        float sample_rate, float frequency, bool high) {
    Coefficients c;
    const float k = std::tan(kPi * frequency / sample_rate);
    const float norm = 1.0F / (1.0F + k);
    c.b0 = high ? norm : k * norm;
    c.b1 = high ? -norm : k * norm;
    c.b2 = 0.0F;
    c.a1 = (k - 1.0F) * norm;
    c.a2 = 0.0F;
    return c;
}

LinkwitzRileyCrossover::Coefficients LinkwitzRileyCrossover::second_order(
        float sample_rate, float frequency, float q, bool high) {
    Coefficients c;
    const float omega = 2.0F * kPi * frequency / sample_rate;
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float alpha = sine / (2.0F * q);
    const float a0 = 1.0F + alpha;
    if (high) {
        c.b0 = (1.0F + cosine) * 0.5F / a0;
        c.b1 = -(1.0F + cosine) / a0;
        c.b2 = c.b0;
    } else {
        c.b0 = (1.0F - cosine) * 0.5F / a0;
        c.b1 = (1.0F - cosine) / a0;
        c.b2 = c.b0;
    }
    c.a1 = -2.0F * cosine / a0;
    c.a2 = (1.0F - alpha) / a0;
    return c;
}

float LinkwitzRileyCrossover::run(
        float input, const Coefficients& c, State& state) {
    const float output = c.b0 * input + state.z1;
    state.z1 = c.b1 * input - c.a1 * output + state.z2;
    state.z2 = c.b2 * input - c.a2 * output;
    return output;
}

bool LinkwitzRileyCrossover::configure(
        std::uint32_t sample_rate_hz, float frequency_hz,
        LinkwitzRileyOrder order) {
    if ((sample_rate_hz != 44100U && sample_rate_hz != 48000U) ||
        !std::isfinite(frequency_hz) || frequency_hz < 20.0F ||
        frequency_hz >= static_cast<float>(sample_rate_hz) * 0.45F) {
        return false;
    }
    const auto raw_order = static_cast<unsigned>(order);
    if (raw_order != 2U && raw_order != 4U &&
        raw_order != 6U && raw_order != 8U) {
        return false;
    }

    sample_rate_hz_ = sample_rate_hz;
    frequency_hz_ = frequency_hz;
    order_ = order;
    section_count_ = 0U;
    invert_high_ = ((raw_order / 2U) % 2U) != 0U;

    const auto add_first = [&]() {
        Section section;
        section.low = first_order(static_cast<float>(sample_rate_hz),
                                  frequency_hz, false);
        section.high = first_order(static_cast<float>(sample_rate_hz),
                                   frequency_hz, true);
        sections_[section_count_++] = section;
    };
    const auto add_second = [&](float q) {
        Section section;
        section.low = second_order(static_cast<float>(sample_rate_hz),
                                   frequency_hz, q, false);
        section.high = second_order(static_cast<float>(sample_rate_hz),
                                    frequency_hz, q, true);
        sections_[section_count_++] = section;
    };

    // A Linkwitz-Riley Nth-order branch is two cascaded Butterworth
    // N/2-order prototypes. Orders here are slopes in dB/octave.
    switch (order) {
        case LinkwitzRileyOrder::Db12:
            add_first(); add_first();
            break;
        case LinkwitzRileyOrder::Db24:
            add_second(kQButterworth); add_second(kQButterworth);
            break;
        case LinkwitzRileyOrder::Db36:
            add_first(); add_second(kQ3rdOrder);
            add_first(); add_second(kQ3rdOrder);
            break;
        case LinkwitzRileyOrder::Db48:
            add_second(kQ4thLow); add_second(kQ4thHigh);
            add_second(kQ4thLow); add_second(kQ4thHigh);
            break;
    }
    reset();
    return true;
}

void LinkwitzRileyCrossover::reset() {
    for (auto& section : sections_) {
        section.low_state = {};
        section.high_state = {};
    }
}

void LinkwitzRileyCrossover::process(
        float input, float& low_output, float& high_output) {
    float low = std::isfinite(input) ? input : 0.0F;
    float high = low;
    for (std::size_t i = 0; i < section_count_; ++i) {
        low = run(low, sections_[i].low, sections_[i].low_state);
        high = run(high, sections_[i].high, sections_[i].high_state);
    }
    low_output = low;
    high_output = invert_high_ ? -high : high;
}

} // namespace pcm_dsp
