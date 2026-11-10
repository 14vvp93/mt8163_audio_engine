#include "pcm_dsp/profiled_quad_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace pcm_dsp;
    try {
        ProfiledQuadEngine engine;
        AudioProfile initial;
        initial.quad.enabled = true;
        check(engine.configure(48000U, initial),
              "initial profile configures");

        AudioProfile next = initial;
        next.quad.channels[0].gain_db = -12.0F;
        check(engine.apply_profile(next, 50U),
              "valid profile is queued");
        check(engine.transition_pending(),
              "queued profile reports pending transition");
        check(!engine.apply_profile(initial, 50U),
              "second update is rejected while target slot is reserved");

        constexpr std::size_t frames = 256U;
        std::vector<float> input(frames * kOutputChannelCount, 0.25F);
        std::vector<float> output(input.size(), 0.0F);
        std::size_t processed = 0U;
        while (engine.transition_pending() && processed < 10000U) {
            check(engine.process(input.data(), output.data(), frames),
                  "transition processing succeeds");
            processed += frames;
            for (const float value : output) {
                check(std::isfinite(value), "transition output remains finite");
            }
        }
        check(!engine.transition_pending(), "transition completes");
        check(processed >= 2048U, "transition spans multiple audio blocks");

        check(engine.apply_profile(initial, 10U),
              "retired engine slot becomes reusable");
        check(!engine.apply_profile(next, 0U),
              "zero-duration transition is rejected");
        AudioProfile invalid = initial;
        invalid.quad.preamp_db = 100.0F;
        check(!engine.apply_profile(invalid, 50U),
              "invalid profile is rejected before publication");

        ProfiledQuadEngine sub_engine;
        AudioProfile no_sub;
        no_sub.quad.enabled = true;
        check(sub_engine.configure(48000U, no_sub),
              "subwoofer test engine configures");
        AudioProfile with_sub = no_sub;
        with_sub.routing.enabled = true;
        with_sub.routing.bass_management_enabled = true;
        with_sub.routing.crossover_frequency_hz = 90.0F;
        check(sub_engine.apply_profile(with_sub, 10U),
              "subwoofer profile is queued");
        std::vector<float> sub_output(frames, 0.0F);
        check(sub_engine.process(input.data(), output.data(), frames),
              "old profile continues when sub buffer is absent");
        check(sub_engine.transition_pending(),
              "subwoofer transition remains queued without its output buffer");
        check(sub_engine.process(input.data(), output.data(), frames,
                                 sub_output.data()),
              "subwoofer transition starts when its output buffer is supplied");

        std::cout << "Profile transition tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Profile transition test failed: " << error.what() << '\n';
        return 1;
    }
}
