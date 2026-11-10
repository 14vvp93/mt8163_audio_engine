#pragma once

#include "pcm_dsp/crossover.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

constexpr std::size_t kDynamicsChannelCount = 4U;
constexpr std::size_t kDynamicsBandCount = 3U;

enum class DynamicsBand : std::uint8_t {
    Low = 0,
    Mid = 1,
    High = 2
};

struct CompressorParameters {
    bool enabled{false};
    float threshold_db{-18.0F};
    float ratio{2.0F};
    float attack_ms{10.0F};
    float release_ms{120.0F};
    float knee_db{6.0F};
    float makeup_gain_db{0.0F};
};

struct MultibandDynamicsParameters {
    bool enabled{false};
    float low_crossover_hz{250.0F};
    float high_crossover_hz{4000.0F};
    std::array<std::array<CompressorParameters, kDynamicsBandCount>,
               kDynamicsChannelCount> compressors{};
};

// Three-band, four-channel compressor. Channel order: FL, FR, RL, RR.
// Configure outside the audio callback. Processing is allocation- and lock-free.
// Input and output may alias. Samples are normalized Float32 audio.
class MultibandDynamicsProcessor {
public:
    bool configure(std::uint32_t sample_rate_hz,
                   const MultibandDynamicsParameters& parameters);
    void reset();
    bool process(const float* input, float* output, std::size_t frames);

    std::uint32_t sample_rate() const { return sample_rate_hz_; }

private:
    struct CompressorState {
        float envelope{0.0F};
        float attack_coefficient{0.0F};
        float release_coefficient{0.0F};
        float makeup_gain{1.0F};
    };

    static bool valid_parameters(std::uint32_t sample_rate_hz,
                                 const MultibandDynamicsParameters& parameters);
    static float db_to_gain(float db);
    static float compress_sample(float sample,
                                 const CompressorParameters& parameters,
                                 CompressorState& state);

    std::uint32_t sample_rate_hz_{0U};
    MultibandDynamicsParameters parameters_{};
    std::array<LinkwitzRileyCrossover, kDynamicsChannelCount> low_split_{};
    std::array<LinkwitzRileyCrossover, kDynamicsChannelCount> high_split_{};
    std::array<std::array<CompressorState, kDynamicsBandCount>,
               kDynamicsChannelCount> states_{};
};

} // namespace pcm_dsp
