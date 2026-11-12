#include "pcm_dsp/fm_adapter.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::int16_t> make_signal(std::size_t frames) {
    std::vector<std::int16_t> signal(frames * 2U);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto sample = static_cast<std::int16_t>(9000.0F * std::sin(
            2.0F * 3.14159265358979323846F * 1000.0F *
            static_cast<float>(frame) / 32000.0F));
        signal[frame * 2U] = sample;
        signal[frame * 2U + 1U] = sample;
    }
    return signal;
}
} // namespace

int main() {
    try {
        constexpr std::size_t kFrames = 3200U;
        const auto input = make_signal(kFrames);
        std::vector<std::int16_t> output(input.size(), 0);
        pcm_dsp::Parameters parameters;
        parameters.enabled = true;
        parameters.limiter_enabled = false;
        parameters.bands[5] = {1000.0F, 6.0F, 0.707F, true,
                               pcm_dsp::FilterType::Peaking, 1.0F};

        pcm_dsp::FmPcmAdapter adapter;
        check(adapter.configure(32000U, 2U),
              "native MT6627 32 kHz stereo profile accepted");

        auto result = adapter.process(input.data(), output.data(), kFrames,
                                      parameters, true, true);
        check(result == pcm_dsp::FmProcessResult::Processed,
              "active FM route is processed in normal audio mode");
        check(output != input, "FM EQ changes the signal");

        std::fill(output.begin(), output.end(), 0);
        result = adapter.process(input.data(), output.data(), kFrames,
                                 parameters, false, true);
        check(result == pcm_dsp::FmProcessResult::Bypassed,
              "inactive FM route bypasses");
        check(output == input, "inactive FM bypass is bit-exact");

        std::fill(output.begin(), output.end(), 0);
        result = adapter.process(input.data(), output.data(), kFrames,
                                 parameters, true, false);
        check(result == pcm_dsp::FmProcessResult::Bypassed,
              "non-normal audio mode bypasses");
        check(output == input, "non-normal mode bypass is bit-exact");

        pcm_dsp::FmPcmAdapter unsupported_rate;
        check(!unsupported_rate.configure(44100U, 2U),
              "FM adapter rejects unverified 44.1 kHz profile");
        std::fill(output.begin(), output.end(), 0);
        result = unsupported_rate.process(input.data(), output.data(), kFrames,
                                          parameters, true, true);
        check(result == pcm_dsp::FmProcessResult::Bypassed,
              "unsupported sample rate bypasses");
        check(output == input, "unsupported rate bypass is bit-exact");

        pcm_dsp::FmPcmAdapter unsupported_channels;
        check(!unsupported_channels.configure(32000U, 1U),
              "FM adapter rejects unverified mono layout");
        std::fill(output.begin(), output.end(), 0);
        result = unsupported_channels.process(input.data(), output.data(),
                                              kFrames, parameters, true, true);
        check(result == pcm_dsp::FmProcessResult::Bypassed,
              "unsupported channel layout bypasses");
        check(output == input, "unsupported channel bypass is bit-exact");

        auto in_place = input;
        pcm_dsp::FmPcmAdapter in_place_adapter;
        check(in_place_adapter.configure(32000U, 2U),
              "in-place adapter configures");
        result = in_place_adapter.process(in_place.data(), in_place.data(),
                                          kFrames, parameters, true, true);
        check(result == pcm_dsp::FmProcessResult::Processed,
              "in-place FM processing succeeds");
        check(in_place != input, "in-place FM processing changes signal");

        result = adapter.process(nullptr, output.data(), kFrames, parameters,
                                 true, true);
        check(result == pcm_dsp::FmProcessResult::InvalidBuffer,
              "null PCM buffer is rejected");

        std::cout << "FM PCM adapter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FM PCM adapter test failure: " << error.what() << '\n';
        return 1;
    }
}
