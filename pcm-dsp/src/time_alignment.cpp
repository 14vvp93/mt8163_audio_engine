#include "pcm_dsp/time_alignment.hpp"

#include <algorithm>
#include <cmath>

namespace pcm_dsp {

bool QuadTimeAlignment::configure(std::uint32_t sample_rate_hz) {
    if (sample_rate_hz != 44100U && sample_rate_hz != 48000U) return false;
    if (sample_rate_hz_ != sample_rate_hz) {
        sample_rate_hz_ = sample_rate_hz;
        reset();
    }
    return true;
}

void QuadTimeAlignment::reset() {
    for (auto& channel : delay_line_) channel.fill(0.0F);
    current_delay_.fill(0U);
    write_index_ = 0U;
}

bool QuadTimeAlignment::process(
        const float* input, float* output, std::size_t frames,
        const std::array<float, kTimeAlignmentChannels>& delay_ms) {
    if (sample_rate_hz_ == 0U || input == nullptr || output == nullptr) {
        return false;
    }
    if (frames == 0U) return true;

    std::array<std::size_t, kTimeAlignmentChannels> target_delay{};
    bool changed = false;
    for (std::size_t channel = 0; channel < kTimeAlignmentChannels; ++channel) {
        const float requested = std::isfinite(delay_ms[channel])
            ? std::max(0.0F, std::min(delay_ms[channel], kMaxTimeAlignmentMs))
            : 0.0F;
        const auto samples = static_cast<std::size_t>(
            std::lround(requested * static_cast<float>(sample_rate_hz_) / 1000.0F));
        target_delay[channel] = std::min(samples, kTimeAlignmentBufferSize - 1U);
        changed = changed || target_delay[channel] != current_delay_[channel];
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float mix = changed
            ? static_cast<float>(frame + 1U) / static_cast<float>(frames)
            : 1.0F;
        for (std::size_t channel = 0; channel < kTimeAlignmentChannels; ++channel) {
            const std::size_t index = frame * kTimeAlignmentChannels + channel;
            const float sample = std::isfinite(input[index]) ? input[index] : 0.0F;
            delay_line_[channel][write_index_] = sample;

            const std::size_t old_index =
                (write_index_ + kTimeAlignmentBufferSize - current_delay_[channel]) %
                kTimeAlignmentBufferSize;
            const std::size_t new_index =
                (write_index_ + kTimeAlignmentBufferSize - target_delay[channel]) %
                kTimeAlignmentBufferSize;
            const float old_value = delay_line_[channel][old_index];
            const float new_value = delay_line_[channel][new_index];
            output[index] = changed
                ? old_value + (new_value - old_value) * mix
                : new_value;
        }
        write_index_ = (write_index_ + 1U) % kTimeAlignmentBufferSize;
    }

    if (changed) current_delay_ = target_delay;
    return true;
}

} // namespace pcm_dsp
