#pragma once

#include "pcm_dsp/engine.hpp"

#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

enum class FmProcessResult : std::uint8_t {
    Processed,
    Bypassed,
    InvalidBuffer
};

// Conservative adapter for the recovered MT6627 FM capture profile.
// It intentionally processes only native 32 kHz, PCM16 stereo buffers.
// The caller must confirm that the buffer belongs to the active FM route
// and that the device is in normal audio mode before enabling processing.
class FmPcmAdapter {
public:
    bool configure(std::uint32_t sample_rate_hz,
                   std::uint32_t channel_count) noexcept;
    void reset() noexcept;

    FmProcessResult process(const std::int16_t* input,
                            std::int16_t* output,
                            std::size_t frames,
                            const Parameters& parameters,
                            bool fm_route_active,
                            bool normal_audio_mode) noexcept;

    bool configured() const noexcept { return configured_; }

private:
    Engine engine_;
    bool configured_{false};
    bool was_processing_{false};
};

} // namespace pcm_dsp
