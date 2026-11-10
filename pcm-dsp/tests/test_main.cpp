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
}

int main() {
    using pcm_dsp::Engine;
    using pcm_dsp::Parameters;

    try {
        Engine engine;
        check(!engine.configure(32000U), "unsupported sample rate rejected");
        check(engine.configure(44100U), "44.1 kHz accepted");

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

        engine.reset();
        parameters.limiter_enabled = false;
        parameters.bands[5].enabled = true;
        parameters.bands[5].gain_db = 6.0F;
        const std::vector<std::int16_t> tone(512U * 2U, 1000);
        std::vector<std::int16_t> equalized(tone.size(), 0);
        check(engine.process(tone.data(), equalized.data(), 512U, parameters),
              "EQ processing succeeds");
        check(equalized != tone, "enabled EQ changes signal");

        std::cout << "All PCM DSP tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
