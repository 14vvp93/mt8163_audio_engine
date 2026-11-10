#include "pcm_dsp/quad_engine.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main() {
    bool passed = true;
    constexpr std::uint32_t rate = 48000U;
    constexpr std::size_t frames = 8192U;
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

    pcm_dsp::QuadEngine engine;
    passed &= check(engine.configure(rate), "configure QuadEngine");

    pcm_dsp::MultibandDynamicsParameters dynamics;
    dynamics.enabled = true;
    auto& front_left_low = dynamics.compressors[0][0];
    front_left_low.enabled = true;
    front_left_low.threshold_db = -12.0F;
    front_left_low.ratio = 6.0F;
    front_left_low.attack_ms = 0.5F;
    front_left_low.release_ms = 80.0F;
    front_left_low.knee_db = 0.0F;
    passed &= check(engine.configure_dynamics(dynamics),
                    "configure integrated dynamics");

    pcm_dsp::QuadParameters parameters;
    parameters.enabled = true;
    parameters.limiter_enabled = false;
    passed &= check(engine.process(input.data(), output.data(), frames, parameters),
                    "process integrated dynamics");

    double fl_energy = 0.0;
    double fr_energy = 0.0;
    for (std::size_t frame = 1024U; frame < frames; ++frame) {
        const double fl = output[frame * 4U];
        const double fr = output[frame * 4U + 1U];
        fl_energy += fl * fl;
        fr_energy += fr * fr;
    }
    const double fl_rms = std::sqrt(fl_energy / (frames - 1024U));
    const double fr_rms = std::sqrt(fr_energy / (frames - 1024U));
    passed &= check(fl_rms < fr_rms * 0.75,
                    "front-left low band is compressed in QuadEngine");
    passed &= check(fr_rms > 0.45 && fr_rms < 0.65,
                    "unprocessed front-right retains expected level");

    return passed ? 0 : 1;
}
