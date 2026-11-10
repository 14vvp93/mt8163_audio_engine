#include "pcm_dsp/engine.hpp"
#include "pcm_dsp/quad_engine.hpp"
#include "pcm_dsp/time_alignment.hpp"
#include "pcm_dsp/crossover.hpp"

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


        // Four-channel Float32 engine: bit-exact bypass and independent gains.
        pcm_dsp::QuadEngine quad;
        check(!quad.configure(32000U), "quad rejects unsupported sample rate");
        check(quad.configure(48000U), "quad accepts 48 kHz");
        const std::vector<float> quad_input{
            0.10F, 0.10F, 0.10F, 0.10F,
            -0.20F, -0.20F, -0.20F, -0.20F
        };
        std::vector<float> quad_output(quad_input.size(), 0.0F);
        pcm_dsp::QuadParameters quad_parameters;
        check(quad.process(quad_input.data(), quad_output.data(), 2U,
                           quad_parameters), "quad bypass succeeds");
        check(quad_output == quad_input, "quad bypass is bit-exact");

        quad_parameters.enabled = true;
        quad_parameters.limiter_enabled = false;
        quad_parameters.channels[0].gain_db = 6.0F;
        quad_parameters.channels[1].gain_db = 0.0F;
        quad_parameters.channels[2].gain_db = -6.0F;
        quad_parameters.channels[3].gain_db = 0.0F;
        check(quad.process(quad_input.data(), quad_output.data(), 2U,
                           quad_parameters), "quad channel gains process");
        check(quad_output[0] > quad_output[1], "front-left gain is independent");
        check(quad_output[2] == quad_output[1], "front-right remains unchanged");
        check(quad_output[4] < quad_output[5], "rear-left attenuation is independent");
        check(quad_output[6] == quad_output[5], "rear-right remains unchanged");

        // Exercise all 32 slots and abrupt extreme parameter changes.
        std::vector<float> stress_input(4096U * 4U);
        std::vector<float> stress_output(stress_input.size());
        for (std::size_t i = 0; i < 4096U; ++i) {
            const float value = 0.2F * std::sin(
                2.0F * 3.14159265358979323846F * 997.0F *
                static_cast<float>(i) / 48000.0F);
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                stress_input[i * 4U + channel] = value;
            }
        }
        quad.reset();
        pcm_dsp::QuadParameters stress_parameters;
        stress_parameters.enabled = true;
        stress_parameters.limiter_enabled = true;
        for (std::size_t band = 0; band < pcm_dsp::kQuadEqBandCount; ++band) {
            auto& eq = stress_parameters.channels[0].bands[band];
            eq.enabled = true;
            eq.type = (band % 5U == 0U) ? FilterType::LowShelf
                    : (band % 5U == 1U) ? FilterType::HighShelf
                    : (band % 5U == 2U) ? FilterType::HighPass
                    : (band % 5U == 3U) ? FilterType::LowPass
                                        : FilterType::Peaking;
            eq.frequency_hz = 20.0F * std::pow(
                1000.0F, static_cast<float>(band) / 31.0F);
            eq.gain_db = (band % 2U == 0U) ? 12.0F : -12.0F;
            eq.q = 0.707F;
        }
        for (int pass = 0; pass < 12; ++pass) {
            stress_parameters.channels[0].bands[pass].gain_db =
                (pass % 2 == 0) ? 18.0F : -18.0F;
            check(quad.process(stress_input.data(), stress_output.data(),
                               4096U, stress_parameters),
                  "quad stress transition succeeds");
            for (const float sample : stress_output) {
                check(std::isfinite(sample), "quad output is finite");
                check(std::fabs(sample) <= 0.981F,
                      "quad limiter respects ceiling");
            }
        }


        // Four-channel delay: after the transition block, an impulse on FR
        // must emerge exactly 48 samples later at 48 kHz (1.0 ms).
        pcm_dsp::QuadTimeAlignment alignment;
        check(!alignment.configure(32000U), "time alignment rejects unsupported rate");
        check(alignment.configure(48000U), "time alignment accepts 48 kHz");
        std::array<float, 4> delays{{0.0F, 1.0F, 0.0F, 0.0F}};
        std::vector<float> silence(64U * 4U, 0.0F);
        std::vector<float> aligned(64U * 4U, 0.0F);
        check(alignment.process(silence.data(), aligned.data(), 64U, delays),
              "time alignment settles delay change");
        std::vector<float> impulse(64U * 4U, 0.0F);
        impulse[0] = 1.0F; impulse[1] = 1.0F;
        impulse[2] = 1.0F; impulse[3] = 1.0F;
        check(alignment.process(impulse.data(), aligned.data(), 64U, delays),
              "time alignment processes impulse");
        check(aligned[0] == 1.0F, "FL zero delay is immediate");
        check(aligned[1] == 0.0F, "FR delayed output has no immediate impulse");
        check(aligned[48U * 4U + 1U] > 0.99F,
              "FR 1 ms delay equals 48 samples");
        check(aligned[2] == 1.0F && aligned[3] == 1.0F,
              "rear channels remain independently undelayed");

        // QuadEngine integrates time alignment before the output limiter.
        quad.reset();
        pcm_dsp::QuadParameters aligned_parameters;
        aligned_parameters.enabled = true;
        aligned_parameters.limiter_enabled = false;
        aligned_parameters.channels[1].delay_ms = 1.0F;
        std::vector<float> quad_silence(64U * 4U, 0.0F);
        std::vector<float> quad_aligned(64U * 4U, 0.0F);
        check(quad.process(quad_silence.data(), quad_aligned.data(), 64U,
                           aligned_parameters),
              "QuadEngine delay transition block succeeds");
        std::vector<float> quad_impulse(64U * 4U, 0.0F);
        quad_impulse[0] = quad_impulse[1] =
            quad_impulse[2] = quad_impulse[3] = 1.0F;
        check(quad.process(quad_impulse.data(), quad_aligned.data(), 64U,
                           aligned_parameters),
              "QuadEngine integrated delay processes impulse");
        check(quad_aligned[0] == 1.0F && quad_aligned[2] == 1.0F &&
              quad_aligned[3] == 1.0F,
              "QuadEngine undelayed channels remain immediate");
        check(quad_aligned[1] == 0.0F &&
              quad_aligned[48U * 4U + 1U] > 0.99F,
              "QuadEngine FR alignment is exactly 1 ms at 48 kHz");

        // Verify Linkwitz-Riley branches sum to unity after settling.
        for (const auto order : {
                pcm_dsp::LinkwitzRileyOrder::Db12,
                pcm_dsp::LinkwitzRileyOrder::Db24,
                pcm_dsp::LinkwitzRileyOrder::Db36,
                pcm_dsp::LinkwitzRileyOrder::Db48}) {
            pcm_dsp::LinkwitzRileyCrossover crossover;
            check(crossover.configure(48000U, 1000.0F, order),
                  "Linkwitz-Riley crossover configures");
            double square_sum = 0.0;
            constexpr std::size_t count = 48000U;
            for (std::size_t i = 0; i < count; ++i) {
                const float sample = std::sin(
                    2.0F * 3.14159265358979323846F * 1000.0F *
                    static_cast<float>(i) / 48000.0F);
                float low = 0.0F;
                float high = 0.0F;
                crossover.process(sample, low, high);
                if (i >= 24000U) {
                    const double sum = static_cast<double>(low + high);
                    square_sum += sum * sum;
                }
            }
            const double rms = std::sqrt(square_sum / 24000.0);
            check(std::fabs(rms - 0.70710678) < 0.015,
                  "crossover branches sum flat at crossover frequency");
        }

        std::cout << "All PCM DSP tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
