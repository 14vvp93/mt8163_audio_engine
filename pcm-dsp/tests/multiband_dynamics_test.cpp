#include "pcm_dsp/multiband_dynamics.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

float rms(const std::array<float, 4U * 48000U>& data,
          std::size_t channel, std::size_t begin_frame) {
    double sum = 0.0;
    std::size_t count = 0U;
    for (std::size_t frame = begin_frame; frame < 48000U; ++frame) {
        const double value = data[frame * 4U + channel];
        sum += value * value;
        ++count;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}
}

int main() {
    bool passed = true;
    constexpr std::uint32_t rate = 48000U;
    constexpr std::size_t frames = 48000U;
    std::array<float, frames * 4U> input{};
    std::array<float, frames * 4U> output{};

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float sample = 0.8F * std::sin(
            2.0F * 3.14159265358979323846F * 100.0F *
            static_cast<float>(frame) / static_cast<float>(rate));
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            input[frame * 4U + channel] = sample;
        }
    }

    pcm_dsp::MultibandDynamicsProcessor processor;
    pcm_dsp::MultibandDynamicsParameters parameters;
    parameters.enabled = false;
    passed &= check(processor.configure(rate, parameters),
                    "configure bypass processor");
    passed &= check(processor.process(input.data(), output.data(), frames),
                    "process bypass");
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != output[i]) {
            passed &= check(false, "bypass must be bit-exact");
            break;
        }
    }

    parameters.enabled = true;
    auto& low_fl = parameters.compressors[0][0];
    low_fl.enabled = true;
    low_fl.threshold_db = -12.0F;
    low_fl.ratio = 4.0F;
    low_fl.attack_ms = 1.0F;
    low_fl.release_ms = 100.0F;
    low_fl.knee_db = 0.0F;
    passed &= check(processor.configure(rate, parameters),
                    "configure compressor");
    passed &= check(processor.process(input.data(), output.data(), frames),
                    "process compressed signal");

    const float compressed_fl = rms(output, 0U, 12000U);
    const float untouched_fr = rms(output, 1U, 12000U);
    passed &= check(compressed_fl < untouched_fr * 0.75F,
                    "front-left low band should be compressed independently");
    passed &= check(untouched_fr > 0.45F && untouched_fr < 0.65F,
                    "uncompressed channel should retain expected level");

    std::array<float, 4U * 128U> invalid_input{};
    std::array<float, 4U * 128U> invalid_output{};
    invalid_input[0] = std::numeric_limits<float>::quiet_NaN();
    invalid_input[1] = std::numeric_limits<float>::infinity();
    passed &= check(processor.process(invalid_input.data(),
                                      invalid_output.data(), 128U),
                    "process non-finite input");
    for (float value : invalid_output) {
        passed &= check(std::isfinite(value), "output must remain finite");
        if (!passed) break;
    }

    auto invalid = parameters;
    invalid.high_crossover_hz = invalid.low_crossover_hz;
    passed &= check(!processor.configure(rate, invalid),
                    "reject invalid crossover order");

    return passed ? 0 : 1;
}
