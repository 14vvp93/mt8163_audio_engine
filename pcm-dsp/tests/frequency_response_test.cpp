#include "pcm_dsp/quad_engine.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846F;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

float measure_gain_db(std::uint32_t sample_rate, float test_frequency,
                      float eq_frequency, float gain_db) {
    constexpr std::size_t kFrames = 16384U;
    constexpr std::size_t kMeasureFrames = 8192U;
    std::vector<float> input(kFrames * 4U);
    std::vector<float> output(kFrames * 4U);

    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        const float phase = 2.0F * kPi * test_frequency *
            static_cast<float>(frame) / static_cast<float>(sample_rate);
        const float sample = 0.1F * std::sin(phase);
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            input[frame * 4U + channel] = sample;
        }
    }

    pcm_dsp::QuadEngine engine;
    check(engine.configure(sample_rate), "engine accepts supported sample rate");

    pcm_dsp::QuadParameters parameters;
    parameters.enabled = true;
    parameters.limiter_enabled = false;
    for (auto& channel : parameters.channels) {
        channel.bands[0] = {
            eq_frequency, gain_db, 1.0F, true,
            pcm_dsp::FilterType::Peaking, 1.0F
        };
    }

    check(engine.process(input.data(), output.data(), kFrames / 2U, parameters),
          "first EQ block processes");
    check(engine.process(input.data() + kFrames * 2U,
                         output.data() + kFrames * 2U,
                         kFrames / 2U, parameters),
          "steady-state EQ block processes");

    double input_energy = 0.0;
    double output_energy = 0.0;
    const std::size_t start = kFrames - kMeasureFrames;
    for (std::size_t frame = start; frame < kFrames; ++frame) {
        const double in = input[frame * 4U];
        const double out = output[frame * 4U];
        input_energy += in * in;
        output_energy += out * out;
    }
    const double ratio = std::sqrt(output_energy / input_energy);
    return static_cast<float>(20.0 * std::log10(ratio));
}
}

int main() {
    try {
        for (const std::uint32_t rate : {44100U, 48000U}) {
            const float center = measure_gain_db(rate, 1000.0F, 1000.0F, 6.0F);
            const float low = measure_gain_db(rate, 100.0F, 1000.0F, 6.0F);
            const float high = measure_gain_db(rate, 10000.0F, 1000.0F, 6.0F);
            check(center > 5.5F && center < 6.5F,
                  "peaking EQ produces the requested gain at its center frequency");
            check(std::fabs(low) < 0.25F,
                  "peaking EQ leaves distant low frequencies unchanged");
            check(std::fabs(high) < 0.25F,
                  "peaking EQ leaves distant high frequencies unchanged");
        }
        std::cout << "All EQ frequency-response tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "EQ frequency-response test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
