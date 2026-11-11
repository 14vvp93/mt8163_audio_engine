#include "pcm_dsp/profiled_quad_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr float kPi = 3.14159265358979323846F;
constexpr std::size_t kIntegrationFrames = 48000U;
constexpr std::array<float, 3U> kBandFrequencies{{60.0F, 1000.0F, 10000.0F}};

float measure_component(const std::vector<float>& audio, std::size_t channel,
                        float frequency_hz, std::size_t begin_frame,
                        std::size_t end_frame) {
    double sine = 0.0;
    double cosine = 0.0;
    const std::size_t count = end_frame - begin_frame;
    for (std::size_t frame = begin_frame; frame < end_frame; ++frame) {
        const double phase = 2.0 * static_cast<double>(kPi) *
            static_cast<double>(frequency_hz) * static_cast<double>(frame) /
            48000.0;
        const double sample = audio[frame * pcm_dsp::kOutputChannelCount + channel];
        sine += sample * std::sin(phase);
        cosine += sample * std::cos(phase);
    }
    const double scale = 2.0 / static_cast<double>(count);
    return static_cast<float>(std::sqrt(sine * sine + cosine * cosine) * scale);
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

        // Verify a complete profile transition carries multiband dynamics
        // into the active QuadEngine, not merely into the stored snapshot.
        ProfiledQuadEngine dynamics_engine;
        AudioProfile dynamics_initial;
        dynamics_initial.quad.enabled = true;
        check(dynamics_engine.configure(48000U, dynamics_initial),
              "dynamics transition engine configures");
        AudioProfile dynamics_profile = dynamics_initial;
        dynamics_profile.dynamics.enabled = true;
        auto& fl_low = dynamics_profile.dynamics.compressors[0][0];
        fl_low.enabled = true;
        fl_low.threshold_db = -12.0F;
        fl_low.ratio = 6.0F;
        fl_low.attack_ms = 0.5F;
        fl_low.release_ms = 80.0F;
        fl_low.knee_db = 0.0F;
        check(dynamics_engine.apply_profile(dynamics_profile, 50U),
              "profile with multiband compressor is queued");
        constexpr std::size_t dynamics_frames = 8192U;
        std::vector<float> dynamics_input(dynamics_frames * kOutputChannelCount);
        std::vector<float> dynamics_output(dynamics_input.size(), 0.0F);
        for (std::size_t frame = 0; frame < dynamics_frames; ++frame) {
            const float sample = 0.8F * std::sin(
                2.0F * 3.14159265358979323846F * 100.0F *
                static_cast<float>(frame) / 48000.0F);
            for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
                dynamics_input[frame * kOutputChannelCount + channel] = sample;
            }
        }
        check(dynamics_engine.process(dynamics_input.data(),
                                      dynamics_output.data(), dynamics_frames),
              "profiled dynamics transition processes");
        check(!dynamics_engine.transition_pending(),
              "profiled dynamics transition completes");
        double fl_energy = 0.0;
        double fr_energy = 0.0;
        for (std::size_t frame = 4096U; frame < dynamics_frames; ++frame) {
            const double fl = dynamics_output[frame * kOutputChannelCount];
            const double fr = dynamics_output[frame * kOutputChannelCount + 1U];
            fl_energy += fl * fl;
            fr_energy += fr * fr;
        }
        const double fl_rms = std::sqrt(fl_energy / (dynamics_frames - 4096U));
        const double fr_rms = std::sqrt(fr_energy / (dynamics_frames - 4096U));
        check(fl_rms < fr_rms * 0.75,
              "profiled FL low-band compressor affects only FL");

        // End-to-end profile-transition coverage for every independent
        // channel/band compressor. Compare each profile against an uncompressed
        // reference generated by the same QuadEngine path.
        ProfiledQuadEngine reference_engine;
        AudioProfile reference_profile;
        reference_profile.quad.enabled = true;
        check(reference_engine.configure(48000U, reference_profile),
              "all-channel dynamics reference engine configures");
        std::vector<float> integration_input(
            kIntegrationFrames * kOutputChannelCount, 0.0F);
        std::vector<float> reference_output(integration_input.size(), 0.0F);
        for (std::size_t frame = 0; frame < kIntegrationFrames; ++frame) {
            const float time = static_cast<float>(frame) / 48000.0F;
            const float sample = 0.30F * (
                std::sin(2.0F * kPi * kBandFrequencies[0] * time) +
                std::sin(2.0F * kPi * kBandFrequencies[1] * time) +
                std::sin(2.0F * kPi * kBandFrequencies[2] * time));
            for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
                integration_input[frame * kOutputChannelCount + channel] = sample;
            }
        }
        check(reference_engine.process(integration_input.data(),
                                       reference_output.data(),
                                       kIntegrationFrames),
              "all-channel dynamics reference processes");
        std::array<std::array<float, 3U>, 4U> reference_amplitudes{};
        for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
            for (std::size_t band = 0; band < kBandFrequencies.size(); ++band) {
                reference_amplitudes[channel][band] = measure_component(
                    reference_output, channel, kBandFrequencies[band],
                    9600U, kIntegrationFrames);
            }
        }

        for (std::size_t target_channel = 0; target_channel < kOutputChannelCount;
             ++target_channel) {
            for (std::size_t target_band = 0; target_band < kBandFrequencies.size();
                 ++target_band) {
                ProfiledQuadEngine channel_band_engine;
                AudioProfile channel_band_profile;
                channel_band_profile.quad.enabled = true;
                channel_band_profile.dynamics.enabled = true;
                auto& compressor = channel_band_profile.dynamics
                    .compressors[target_channel][target_band];
                compressor.enabled = true;
                compressor.threshold_db = -24.0F;
                compressor.ratio = 8.0F;
                compressor.attack_ms = 0.5F;
                compressor.release_ms = 100.0F;
                compressor.knee_db = 0.0F;
                check(channel_band_engine.configure(48000U, reference_profile),
                      "channel/band engine configures with neutral profile");
                check(channel_band_engine.apply_profile(channel_band_profile, 50U),
                      "channel/band dynamics profile queues atomically");
                std::vector<float> channel_band_output(integration_input.size(), 0.0F);
                check(channel_band_engine.process(integration_input.data(),
                                                  channel_band_output.data(),
                                                  kIntegrationFrames),
                      "channel/band profile transition processes");
                check(!channel_band_engine.transition_pending(),
                      "channel/band profile transition completes");

                for (std::size_t channel = 0; channel < kOutputChannelCount; ++channel) {
                    for (std::size_t band = 0; band < kBandFrequencies.size(); ++band) {
                        const float measured = measure_component(
                            channel_band_output, channel, kBandFrequencies[band],
                            9600U, kIntegrationFrames);
                        if (channel == target_channel && band == target_band) {
                            check(measured < reference_amplitudes[channel][band] * 0.80F,
                                  "profile transition compresses selected channel/band");
                        } else {
                            check(measured > reference_amplitudes[channel][band] * 0.97F,
                                  "profile transition preserves unselected channel/bands");
                        }
                    }
                }
            }
        }

        std::cout << "Profile transition tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Profile transition test failed: " << error.what() << '\n';
        return 1;
    }
}
