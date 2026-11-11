#include "pcm_dsp/multiband_dynamics.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr std::size_t kFrames = 48000U;
constexpr std::size_t kChannels = 4U;
using Audio = std::array<float, kFrames * kChannels>;

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

float component_amplitude(const Audio& audio, std::size_t channel,
                          float frequency_hz, std::uint32_t sample_rate,
                          std::size_t begin_frame) {
    double sine = 0.0;
    double cosine = 0.0;
    const std::size_t count = kFrames - begin_frame;
    for (std::size_t frame = begin_frame; frame < kFrames; ++frame) {
        const double phase = 2.0 * static_cast<double>(kPi) *
            static_cast<double>(frequency_hz) * static_cast<double>(frame) /
            static_cast<double>(sample_rate);
        const double value = audio[frame * kChannels + channel];
        sine += value * std::sin(phase);
        cosine += value * std::cos(phase);
    }
    const double scale = 2.0 / static_cast<double>(count);
    return static_cast<float>(std::sqrt(sine * sine + cosine * cosine) * scale);
}
} // namespace

int main() {
    bool passed = true;
    constexpr std::uint32_t rate = 48000U;
    constexpr std::array<float, 3U> frequencies{{60.0F, 1000.0F, 10000.0F}};

    Audio input{};
    Audio output{};
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(rate);
        const float sample = 0.30F * (
            std::sin(2.0F * kPi * frequencies[0] * t) +
            std::sin(2.0F * kPi * frequencies[1] * t) +
            std::sin(2.0F * kPi * frequencies[2] * t));
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            input[frame * kChannels + channel] = sample;
        }
    }

    // Establish the enabled-but-uncompressed reference, including crossover
    // response, so every channel/band comparison uses the same signal path.
    pcm_dsp::MultibandDynamicsParameters baseline_parameters;
    baseline_parameters.enabled = true;
    pcm_dsp::MultibandDynamicsProcessor baseline_processor;
    passed &= check(baseline_processor.configure(rate, baseline_parameters),
                    "configure uncompressed multiband reference");
    Audio baseline{};
    passed &= check(baseline_processor.process(input.data(), baseline.data(),
                                               kFrames),
                    "process uncompressed multiband reference");

    std::array<std::array<float, 3U>, 4U> baseline_amplitudes{};
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        for (std::size_t band = 0; band < frequencies.size(); ++band) {
            baseline_amplitudes[channel][band] = component_amplitude(
                baseline, channel, frequencies[band], rate, 9600U);
        }
    }

    // Exercise every independent compressor: FL/FR/RL/RR × Low/Mid/High.
    // In each run exactly one compressor is enabled. Its selected component
    // must be reduced while all other channel/band components remain intact.
    pcm_dsp::MultibandDynamicsParameters last_parameters;
    for (std::size_t target_channel = 0; target_channel < kChannels;
         ++target_channel) {
        for (std::size_t target_band = 0; target_band < frequencies.size();
             ++target_band) {
            pcm_dsp::MultibandDynamicsParameters parameters;
            parameters.enabled = true;
            auto& compressor =
                parameters.compressors[target_channel][target_band];
            compressor.enabled = true;
            compressor.threshold_db = -24.0F;
            compressor.ratio = 8.0F;
            compressor.attack_ms = 0.5F;
            compressor.release_ms = 100.0F;
            compressor.knee_db = 0.0F;

            pcm_dsp::MultibandDynamicsProcessor processor;
            passed &= check(processor.configure(rate, parameters),
                            "configure one isolated channel/band compressor");
            passed &= check(processor.process(input.data(), output.data(),
                                               kFrames),
                            "process isolated channel/band compressor");
            last_parameters = parameters;

            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                for (std::size_t band = 0; band < frequencies.size(); ++band) {
                    const float measured = component_amplitude(
                        output, channel, frequencies[band], rate, 9600U);
                    if (channel == target_channel && band == target_band) {
                        passed &= check(
                            measured < baseline_amplitudes[channel][band] * 0.80F,
                            "selected compressor reduces its own channel/band");
                    } else {
                        passed &= check(
                            measured > baseline_amplitudes[channel][band] * 0.97F,
                            "unselected channel/bands remain unchanged");
                    }
                }
            }
        }
    }

    // The same final configuration and input must produce identical in-place
    // and out-of-place output.
    pcm_dsp::MultibandDynamicsProcessor in_place_processor;
    Audio in_place = input;
    passed &= check(in_place_processor.configure(rate, last_parameters),
                    "configure in-place processor");
    passed &= check(in_place_processor.process(in_place.data(), in_place.data(),
                                               kFrames),
                    "process in-place multiband signal");
    for (std::size_t i = 0; i < output.size(); ++i) {
        passed &= check(std::fabs(output[i] - in_place[i]) < 1.0e-6F,
                        "in-place and out-of-place processing match");
        if (!passed) break;
    }

    return passed ? 0 : 1;
}
