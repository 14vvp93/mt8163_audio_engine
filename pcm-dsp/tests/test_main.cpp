#include "pcm_dsp/engine.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::int16_t> make_test_signal(std::size_t frames) {
    std::vector<std::int16_t> signal(frames * 2U);
    for (std::size_t i = 0; i < frames; ++i) {
        const float sample = 9000.0F * std::sin(
            2.0F * 3.14159265358979323846F * 1000.0F *
            static_cast<float>(i) / 48000.0F);
        signal[i * 2U] = static_cast<std::int16_t>(sample);
        signal[i * 2U + 1U] = static_cast<std::int16_t>(sample);
    }
    return signal;
}
}

int main() {
    using pcm_dsp::Engine;
    using pcm_dsp::FilterType;
    using pcm_dsp::Parameters;

    try {
        Engine engine;
        check(!engine.configure(32000U), "unsupported sample rate rejected");
        check(engine.configure(44100U), "44.1 kHz accepted");
        check(engine.configure(48000U), "48 kHz accepted");

        const std::vector<std::int16_t> input{
            1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000
        };
        std::vector<std::int16_t> output(input.size(), 0);

        Parameters parameters;
        check(engine.process(input.data(), output.data(), 4U, parameters),
              "bypass processing succeeds");
        check(output == input, "bypass is bit-exact");

        parameters.enabled = true;
        parameters.limiter_enabled = false;
        parameters.preamp_db = -6.0F;
        check(engine.process(input.data(), output.data(), 4U, parameters),
              "preamp processing succeeds");
        check(std::abs(static_cast<int>(output[0])) < std::abs(static_cast<int>(input[0])),
              "negative preamp attenuates signal");

        engine.reset();
        parameters.preamp_db = 0.0F;
        parameters.limiter_enabled = true;
        parameters.limiter_threshold = 0.8F;
        const std::vector<std::int16_t> hot{32767, -32768, 30000, -30000};
        std::vector<std::int16_t> limited(hot.size(), 0);
        check(engine.process(hot.data(), limited.data(), 2U, parameters),
              "limiter processing succeeds");
        for (const auto sample : limited) {
            check(std::abs(static_cast<int>(sample)) <= 26215,
                  "limiter output respects configured ceiling");
        }

        const auto tone = make_test_signal(2048U);
        std::vector<std::int16_t> filtered(tone.size(), 0);
        const FilterType filter_types[] = {
            FilterType::Peaking, FilterType::LowShelf, FilterType::HighShelf,
            FilterType::HighPass, FilterType::LowPass
        };
        for (const auto type : filter_types) {
            engine.reset();
            Parameters filter_parameters;
            filter_parameters.enabled = true;
            filter_parameters.limiter_enabled = false;
            filter_parameters.bands[0].enabled = true;
            filter_parameters.bands[0].type = type;
            filter_parameters.bands[0].frequency_hz = 1000.0F;
            filter_parameters.bands[0].gain_db =
                (type == FilterType::HighPass || type == FilterType::LowPass)
                    ? 0.0F : 6.0F;
            check(engine.process(tone.data(), filtered.data(), 2048U,
                                 filter_parameters),
                  "all filter types process successfully");
            check(filtered != tone, "each filter type changes the test signal");
            for (const auto sample : filtered) {
                check(std::isfinite(static_cast<float>(sample)),
                      "filter output remains finite");
            }
        }

        // Change filter parameters between blocks. The engine interpolates
        // normalized coefficients across the next block rather than stepping.
        engine.reset();
        Parameters transition;
        transition.enabled = true;
        transition.limiter_enabled = false;
        transition.bands[0] = {1000.0F, 0.0F, 0.707F, true,
                               FilterType::LowPass, 1.0F};
        check(engine.process(tone.data(), filtered.data(), 2048U, transition),
              "initial transition filter processes");
        transition.bands[0].frequency_hz = 5000.0F;
        check(engine.process(tone.data(), filtered.data(), 2048U, transition),
              "updated filter coefficients transition across block");

        std::cout << "All PCM DSP tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
