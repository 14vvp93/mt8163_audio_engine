#include "pcm_dsp/normal_playback_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::int16_t> make_signal(std::size_t frames,
                                      std::uint32_t sample_rate_hz) {
    std::vector<std::int16_t> signal(frames * 2U);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto sample = static_cast<std::int16_t>(9000.0F * std::sin(
            2.0F * 3.14159265358979323846F * 1000.0F *
            static_cast<float>(frame) / static_cast<float>(sample_rate_hz)));
        signal[frame * 2U] = sample;
        signal[frame * 2U + 1U] = static_cast<std::int16_t>(sample / 2);
    }
    return signal;
}

pcm_dsp::NormalPlaybackContext eligible_context(std::uint32_t sample_rate_hz) {
    pcm_dsp::NormalPlaybackContext context;
    context.route = pcm_dsp::PlaybackRoute::PrimaryMedia;
    context.audio_mode = pcm_dsp::PlaybackAudioMode::Normal;
    context.format = pcm_dsp::PlaybackSampleFormat::Pcm16;
    context.sample_rate_hz = sample_rate_hz;
    context.channel_count = 2U;
    return context;
}

void expect_bypass(pcm_dsp::NormalPlaybackPcmAdapter& adapter,
                   const std::vector<std::int16_t>& input,
                   const pcm_dsp::Parameters& parameters,
                   pcm_dsp::NormalPlaybackContext context,
                   const char* message) {
    std::vector<std::int16_t> output(input.size(), 0);
    const auto result = adapter.process(input.data(), output.data(),
                                        input.size() / 2U, parameters, context);
    check(result == pcm_dsp::NormalPlaybackResult::Bypassed, message);
    check(output == input, "bypass must preserve PCM16 samples bit-exactly");
}

} // namespace

int main() {
    try {
        constexpr std::uint32_t kRate = 44100U;
        constexpr std::size_t kFrames = 4096U;
        const auto input = make_signal(kFrames, kRate);
        std::vector<std::int16_t> output(input.size(), 0);

        pcm_dsp::Parameters parameters;
        parameters.enabled = true;
        parameters.limiter_enabled = false;
        parameters.bands[5] = {1000.0F, 6.0F, 0.707F, true,
                               pcm_dsp::FilterType::Peaking, 1.0F};

        pcm_dsp::NormalPlaybackPcmAdapter adapter;
        check(adapter.configure(kRate, pcm_dsp::PlaybackSampleFormat::Pcm16, 2U),
              "44.1 kHz PCM16 stereo configures");

        auto context = eligible_context(kRate);
        auto result = adapter.process(input.data(), output.data(), kFrames,
                                      parameters, context);
        check(result == pcm_dsp::NormalPlaybackResult::Processed,
              "eligible primary media route is processed");
        check(output != input, "EQ must alter the eligible stereo signal");

        expect_bypass(adapter, input, parameters, context,
                      "baseline route must be eligible only through explicit context");

        for (const auto route : {
                 pcm_dsp::PlaybackRoute::VoiceCall,
                 pcm_dsp::PlaybackRoute::BluetoothSco,
                 pcm_dsp::PlaybackRoute::FmTransmitter,
                 pcm_dsp::PlaybackRoute::Hdmi,
                 pcm_dsp::PlaybackRoute::Direct,
                 pcm_dsp::PlaybackRoute::Offload,
                 pcm_dsp::PlaybackRoute::Unknown}) {
            auto blocked = context;
            blocked.route = route;
            expect_bypass(adapter, input, parameters, blocked,
                          "non-primary route must bypass");
        }

        for (const auto mode : {
                 pcm_dsp::PlaybackAudioMode::InCall,
                 pcm_dsp::PlaybackAudioMode::InCommunication,
                 pcm_dsp::PlaybackAudioMode::Other}) {
            auto blocked = context;
            blocked.audio_mode = mode;
            expect_bypass(adapter, input, parameters, blocked,
                          "non-normal audio mode must bypass");
        }

        for (const auto format : {
                 pcm_dsp::PlaybackSampleFormat::Pcm32,
                 pcm_dsp::PlaybackSampleFormat::Float32,
                 pcm_dsp::PlaybackSampleFormat::Other}) {
            auto blocked = context;
            blocked.format = format;
            expect_bypass(adapter, input, parameters, blocked,
                          "unsupported PCM format must bypass");
        }

        auto blocked = context;
        blocked.channel_count = 1U;
        expect_bypass(adapter, input, parameters, blocked,
                      "non-stereo channel layout must bypass");

        blocked = context;
        blocked.sample_rate_hz = 48000U;
        expect_bypass(adapter, input, parameters, blocked,
                      "unconfigured sample rate must bypass");

        blocked = context;
        blocked.apdmnr_tuning_enabled = true;
        expect_bypass(adapter, input, parameters, blocked,
                      "APDMNR tuning mode must bypass");

        auto disabled_parameters = parameters;
        disabled_parameters.enabled = false;
        expect_bypass(adapter, input, disabled_parameters, context,
                      "disabled DSP must bypass");

        pcm_dsp::NormalPlaybackPcmAdapter unsupported_format;
        check(!unsupported_format.configure(
                  kRate, pcm_dsp::PlaybackSampleFormat::Pcm32, 2U),
              "PCM32 is not enabled in the PCM16 engine adapter");
        expect_bypass(unsupported_format, input, parameters, context,
                      "unconfigured adapter must bypass");

        pcm_dsp::NormalPlaybackPcmAdapter unsupported_rate;
        check(!unsupported_rate.configure(
                  96000U, pcm_dsp::PlaybackSampleFormat::Pcm16, 2U),
              "unsupported sample rate is rejected");
        auto unsupported_rate_context = eligible_context(96000U);
        expect_bypass(unsupported_rate, input, parameters,
                      unsupported_rate_context,
                      "unsupported sample rate must bypass");

        auto in_place = input;
        pcm_dsp::NormalPlaybackPcmAdapter in_place_adapter;
        check(in_place_adapter.configure(
                  kRate, pcm_dsp::PlaybackSampleFormat::Pcm16, 2U),
              "in-place adapter configures");
        result = in_place_adapter.process(in_place.data(), in_place.data(),
                                          kFrames, parameters, context);
        check(result == pcm_dsp::NormalPlaybackResult::Processed,
              "in-place processing succeeds");
        check(in_place != input, "in-place processing alters the signal");

        result = adapter.process(nullptr, output.data(), kFrames,
                                 parameters, context);
        check(result == pcm_dsp::NormalPlaybackResult::InvalidBuffer,
              "null input buffer is rejected");

        result = adapter.process(input.data(), nullptr, kFrames,
                                 parameters, context);
        check(result == pcm_dsp::NormalPlaybackResult::InvalidBuffer,
              "null output buffer is rejected");

        result = adapter.process(input.data(), output.data(), 0U,
                                 parameters, context);
        check(result == pcm_dsp::NormalPlaybackResult::Bypassed,
              "zero-frame buffer is a no-op bypass");

        std::cout << "Normal playback PCM adapter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Normal playback PCM adapter test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
