#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

constexpr std::size_t kTimeAlignmentChannels = 4;
constexpr std::size_t kTimeAlignmentBufferSize = 1024;
constexpr float kMaxTimeAlignmentMs = 20.0F;

// Fixed-memory four-channel fractional-block time alignment.
// Delay changes are crossfaded over one process block.
class QuadTimeAlignment {
public:
    bool configure(std::uint32_t sample_rate_hz);
    void reset();
    bool process(const float* input, float* output, std::size_t frames,
                 const std::array<float, kTimeAlignmentChannels>& delay_ms);

    std::uint32_t sample_rate() const { return sample_rate_hz_; }

private:
    std::uint32_t sample_rate_hz_{0};
    std::array<std::array<float, kTimeAlignmentBufferSize>,
               kTimeAlignmentChannels> delay_line_{};
    std::array<std::size_t, kTimeAlignmentChannels> current_delay_{};
    std::size_t write_index_{0};
};

} // namespace pcm_dsp
