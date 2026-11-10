#include "pcm_dsp/enhancement.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main() {
    pcm_dsp::QuadEnhancementProcessor processor;
    pcm_dsp::EnhancementParameters parameters;
    parameters.bass_enabled = true;
    parameters.bass_amount = 0.7F;
    parameters.bass_frequency_hz = 100.0F;
    parameters.clarity_enabled = true;
    parameters.clarity_amount = 0.5F;
    parameters.clarity_frequency_hz = 2500.0F;
    check(processor.configure(48000U, parameters),
          "enhancement accepts 48 kHz profile");

    constexpr std::size_t frames = 4800U;
    std::array<float, frames * 4U> samples{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float bass = 0.35F * std::sin(
            2.0F * 3.14159265358979323846F * 60.0F *
            static_cast<float>(frame) / 48000.0F);
        const float treble = 0.25F * std::sin(
            2.0F * 3.14159265358979323846F * 4000.0F *
            static_cast<float>(frame) / 48000.0F);
        samples[frame * 4U] = bass + treble;
    }
    const auto original = samples;
    processor.process(samples.data(), frames);

    double changed_energy = 0.0;
    double untouched_energy = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double difference = samples[frame * 4U] - original[frame * 4U];
        changed_energy += difference * difference;
        for (std::size_t channel = 1; channel < 4U; ++channel) {
            untouched_energy += samples[frame * 4U + channel] *
                                samples[frame * 4U + channel];
        }
    }
    check(changed_energy > 0.001, "enhancement changes selected channel");
    check(untouched_energy < 1.0e-12, "enhancement keeps channels independent");
    for (const float sample : samples) {
        check(std::isfinite(sample), "enhancement output remains finite");
        check(std::fabs(sample) < 1.0F, "moderate input remains below full scale");
    }

    check(!processor.configure(48000U, [] {
        pcm_dsp::EnhancementParameters invalid;
        invalid.bass_amount = 1.1F;
        return invalid;
    }()), "invalid enhancement amount is rejected");
    std::cout << "All enhancement tests passed\\n";
    return EXIT_SUCCESS;
}
