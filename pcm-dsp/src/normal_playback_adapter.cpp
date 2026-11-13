#include "pcm_dsp/normal_playback_adapter.hpp"

#include <algorithm>
#include <limits>

namespace pcm_dsp {

bool NormalPlaybackPcmAdapter::configure(std::uint32_t sample_rate_hz,
                                         PlaybackSampleFormat format,
                                         std::uint32_t channel_count) noexcept {
    reset();
    if (format != PlaybackSampleFormat::Pcm16 || channel_count != 2U) {
        return false;
    }

    configured_ = engine_.configure(sample_rate_hz);
    if (configured_) {
        sample_rate_hz_ = sample_rate_hz;
    }
    return configured_;
}

void NormalPlaybackPcmAdapter::reset() noexcept {
    engine_.reset();
    sample_rate_hz_ = 0U;
    configured_ = false;
    was_processing_ = false;
}

NormalPlaybackResult NormalPlaybackPcmAdapter::process(
    const std::int16_t* input,
    std::int16_t* output,
    std::size_t frames,
    const Parameters& parameters,
    const NormalPlaybackContext& context) noexcept {
    if (frames == 0U) {
        return NormalPlaybackResult::Bypassed;
    }
    if (input == nullptr || output == nullptr ||
        frames > std::numeric_limits<std::size_t>::max() / 2U) {
        return NormalPlaybackResult::InvalidBuffer;
    }

    const std::size_t sample_count = frames * 2U;
    const bool eligible = configured_ &&
                          context.route == PlaybackRoute::PrimaryMedia &&
                          context.audio_mode == PlaybackAudioMode::Normal &&
                          context.format == PlaybackSampleFormat::Pcm16 &&
                          context.channel_count == 2U &&
                          context.sample_rate_hz == sample_rate_hz_ &&
                          !context.apdmnr_tuning_enabled &&
                          parameters.enabled;

    if (!eligible) {
        if (was_processing_) {
            engine_.reset();
            was_processing_ = false;
        }
        if (input != output) {
            std::copy_n(input, sample_count, output);
        }
        return NormalPlaybackResult::Bypassed;
    }

    if (!engine_.process(input, output, frames, parameters)) {
        engine_.reset();
        was_processing_ = false;
        if (input != output) {
            std::copy_n(input, sample_count, output);
        }
        return NormalPlaybackResult::Bypassed;
    }

    was_processing_ = true;
    return NormalPlaybackResult::Processed;
}

} // namespace pcm_dsp
