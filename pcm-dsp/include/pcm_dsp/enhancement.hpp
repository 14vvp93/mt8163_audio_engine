#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

// Psychoacoustic harmonic enhancement controls. Amounts are normalized 0..1.
// This creates controlled harmonics; it does not synthesize missing fundamentals.
struct EnhancementParameters {
    bool bass_enabled{false};
    float bass_amount{0.0F};
    float bass_frequency_hz{90.0F};
    bool clarity_enabled{false};
    float clarity_amount{0.0F};
    float clarity_frequency_hz{2500.0F};
};

class QuadEnhancementProcessor {
public:
    bool configure(std::uint32_t sample_rate_hz,
                   const EnhancementParameters& parameters);
    void reset();
    void process(float* interleaved, std::size_t frames);

private:
    struct ChannelState {
        float bass_lowpass{0.0F};
        float clarity_lowpass{0.0F};
    };

    std::uint32_t sample_rate_hz_{0};
    EnhancementParameters parameters_{};
    std::array<ChannelState, 4> states_{};
    float bass_coefficient_{0.0F};
    float clarity_coefficient_{0.0F};
};

} // namespace pcm_dsp
