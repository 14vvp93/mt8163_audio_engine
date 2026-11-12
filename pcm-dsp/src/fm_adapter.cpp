#include "pcm_dsp/fm_adapter.hpp"

#include <algorithm>
#include <limits>

namespace pcm_dsp {

bool FmPcmAdapter::configure(std::uint32_t sample_rate_hz,
                             std::uint32_t channel_count) noexcept {
    reset();
    configured_ = sample_rate_hz == 32000U &&
                  channel_count == 2U &&
                  engine_.configure(sample_rate_hz);
    return configured_;
}

void FmPcmAdapter::reset() noexcept {
    engine_.reset();
    was_processing_ = false;
}

FmProcessResult FmPcmAdapter::process(const std::int16_t* input,
                                      std::int16_t* output,
                                      std::size_t frames,
                                      const Parameters& parameters,
                                      bool fm_route_active,
                                      bool normal_audio_mode) noexcept {
    if (frames == 0U) return FmProcessResult::Bypassed;
    if (input == nullptr || output == nullptr ||
        frames > std::numeric_limits<std::size_t>::max() / 2U) {
        return FmProcessResult::InvalidBuffer;
    }

    const std::size_t sample_count = frames * 2U;
    const bool should_process = configured_ && fm_route_active &&
                               normal_audio_mode && parameters.enabled;
    if (!should_process) {
        if (was_processing_) engine_.reset();
        was_processing_ = false;
        if (input != output) {
            std::copy_n(input, sample_count, output);
        }
        return FmProcessResult::Bypassed;
    }

    if (!engine_.process(input, output, frames, parameters)) {
        engine_.reset();
        was_processing_ = false;
        if (input != output) {
            std::copy_n(input, sample_count, output);
        }
        return FmProcessResult::Bypassed;
    }

    was_processing_ = true;
    return FmProcessResult::Processed;
}

} // namespace pcm_dsp
