#include "pcm_dsp/quad_engine.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846F;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

float measure_gain(pcm_dsp::FilterType type, float tone_hz,
                   float filter_hz, float gain_db) {
    constexpr std::uint32_t sample_rate = 48000U;
    constexpr std::size_t block_frames = 256U;
    constexpr std::size_t total_frames = sample_rate;
    pcm_dsp::QuadEngine engine;
    check(engine.configure(sample_rate), "measurement engine configures");

    pcm_dsp::QuadParameters parameters;
    parameters.enabled = true;
    parameters.limiter_enabled = false;
    auto& band = parameters.channels[0].bands[0];
    band.enabled = true;
    band.type = type;
    band.frequency_hz = filter_hz;
    band.gain_db = gain_db;
    band.q = 0.70710678F;
    band.shelf_slope = 1.0F;

    std::vector<float> input(block_frames * 4U);
    std::vector<float> output(input.size());
    double input_energy = 0.0;
    double output_energy = 0.0;
    std::size_t measured_samples = 0U;

    for (std::size_t offset = 0; offset < total_frames;
         offset += block_frames) {
        const std::size_t count = std::min(block_frames, total_frames - offset);
        for (std::size_t frame = 0; frame < count; ++frame) {
            const float sample = 0.05F * std::sin(
                2.0F * kPi * tone_hz *
                static_cast<float>(offset + frame) /
                static_cast<float>(sample_rate));
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                input[frame * 4U + channel] = sample;
            }
        }
        check(engine.process(input.data(), output.data(), count, parameters),
              "frequency-response block processes");
        if (offset + count > total_frames / 2U) {
            const std::size_t first = offset < total_frames / 2U
                ? total_frames / 2U - offset : 0U;
            for (std::size_t frame = first; frame < count; ++frame) {
                const double in = input[frame * 4U];
                const double out = output[frame * 4U];
                input_energy += in * in;
                output_energy += out * out;
                ++measured_samples;
            }
        }
    }
    check(measured_samples > 0U, "frequency response has samples");
    return static_cast<float>(std::sqrt(output_energy / input_energy));
}

void test_frequency_response() {
    const float expected_boost = std::pow(10.0F, 6.0F / 20.0F);
    const float peak = measure_gain(pcm_dsp::FilterType::Peaking,
                                   1000.0F, 1000.0F, 6.0F);
    check(std::fabs(peak - expected_boost) < 0.06F,
          "peaking filter has expected center-frequency gain");

    const float low_shelf = measure_gain(pcm_dsp::FilterType::LowShelf,
                                         100.0F, 1000.0F, 6.0F);
    check(std::fabs(low_shelf - expected_boost) < 0.08F,
          "low shelf has expected low-frequency gain");

    const float high_shelf = measure_gain(pcm_dsp::FilterType::HighShelf,
                                          10000.0F, 1000.0F, 6.0F);
    check(std::fabs(high_shelf - expected_boost) < 0.08F,
          "high shelf has expected high-frequency gain");

    const float high_pass_stop = measure_gain(pcm_dsp::FilterType::HighPass,
                                              100.0F, 1000.0F, 0.0F);
    const float high_pass_pass = measure_gain(pcm_dsp::FilterType::HighPass,
                                              10000.0F, 1000.0F, 0.0F);
    check(high_pass_stop < 0.15F && high_pass_pass > 0.95F,
          "high-pass stopband and passband behave correctly");

    const float low_pass_pass = measure_gain(pcm_dsp::FilterType::LowPass,
                                             100.0F, 1000.0F, 0.0F);
    const float low_pass_stop = measure_gain(pcm_dsp::FilterType::LowPass,
                                             10000.0F, 1000.0F, 0.0F);
    check(low_pass_pass > 0.95F && low_pass_stop < 0.15F,
          "low-pass passband and stopband behave correctly");
}

void test_extreme_transitions() {
    constexpr std::uint32_t sample_rate = 48000U;
    constexpr std::size_t block_frames = 256U;
    constexpr int blocks = 600;
    pcm_dsp::QuadEngine engine;
    check(engine.configure(sample_rate), "stress engine configures");

    pcm_dsp::QuadParameters parameters;
    parameters.enabled = true;
    parameters.limiter_enabled = true;
    parameters.limiter_threshold = 0.95F;

    std::vector<float> input(block_frames * 4U);
    std::vector<float> output(input.size());
    for (std::size_t frame = 0; frame < block_frames; ++frame) {
        const float sample = 0.12F * std::sin(
            2.0F * kPi * 997.0F * static_cast<float>(frame) /
            static_cast<float>(sample_rate));
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            input[frame * 4U + channel] = sample;
        }
    }

    for (int block = 0; block < blocks; ++block) {
        if ((block % 3) == 0) {
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                auto& channel_parameters = parameters.channels[channel];
                channel_parameters.gain_db =
                    ((block / 3 + static_cast<int>(channel)) % 2 == 0)
                        ? 6.0F : -6.0F;
                for (std::size_t band = 0;
                     band < pcm_dsp::kQuadEqBandCount; ++band) {
                    auto& eq = channel_parameters.bands[band];
                    eq.enabled = true;
                    eq.type = static_cast<pcm_dsp::FilterType>(
                        (band + static_cast<std::size_t>(block / 3) +
                         channel) % 5U);
                    eq.frequency_hz = 20.0F * std::pow(
                        900.0F, static_cast<float>(band) / 31.0F);
                    eq.gain_db = ((band + static_cast<std::size_t>(block / 3)) %
                                  2U == 0U) ? 18.0F : -18.0F;
                    eq.q = (band % 4U == 0U) ? 10.0F : 0.70710678F;
                    eq.shelf_slope = 1.0F;
                }
            }
        }

        check(engine.process(input.data(), output.data(), block_frames,
                             parameters),
              "extreme parameter transition processes");
        for (const float sample : output) {
            check(std::isfinite(sample), "stress output remains finite");
            check(std::fabs(sample) <= 0.95001F,
                  "stress output respects limiter ceiling");
        }
    }
}

void test_invalid_routing_and_input_sanitization() {
    pcm_dsp::QuadEngine engine;
    check(engine.configure(44100U), "44.1 kHz engine configures");

    pcm_dsp::QuadOutputRoutingParameters invalid;
    invalid.enabled = true;
    invalid.matrix[5] = std::numeric_limits<float>::quiet_NaN();
    check(!engine.configure_routing(invalid),
          "non-finite routing coefficient is rejected");

    pcm_dsp::QuadParameters parameters;
    parameters.enabled = true;
    parameters.limiter_enabled = false;
    const std::array<float, 8> input{{
        std::numeric_limits<float>::quiet_NaN(), 0.1F,
        std::numeric_limits<float>::infinity(), -0.1F,
        0.2F, -0.2F, 0.3F, -0.3F
    }};
    std::array<float, 8> output{};
    check(engine.process(input.data(), output.data(), 2U, parameters),
          "non-finite input block is sanitized");
    for (const float sample : output) {
        check(std::isfinite(sample), "sanitized output is finite");
    }
    check(output[0] == 0.0F && output[2] == 0.0F,
          "NaN and infinity input samples become silence");
}
} // namespace

int main() {
    try {
        test_frequency_response();
        test_extreme_transitions();
        test_invalid_routing_and_input_sanitization();
        std::cout << "All DSP stability lab tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "DSP STABILITY LAB FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
