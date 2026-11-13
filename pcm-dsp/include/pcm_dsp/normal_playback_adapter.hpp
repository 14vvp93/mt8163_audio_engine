#pragma once

#include "pcm_dsp/engine.hpp"
#include "pcm_dsp/stereo_profile.hpp"

#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

enum class PlaybackRoute : std::uint8_t {
    PrimaryMedia,
    VoiceCall,
    BluetoothSco,
    FmTransmitter,
    Hdmi,
    Direct,
    Offload,
    Unknown
};

enum class PlaybackAudioMode : std::uint8_t {
    Normal,
    InCall,
    InCommunication,
    Other
};

enum class PlaybackSampleFormat : std::uint8_t {
    Pcm16,
    Pcm32,
    Float32,
    Other
};

enum class NormalPlaybackResult : std::uint8_t {
    Processed,
    Bypassed,
    InvalidBuffer
};

struct NormalPlaybackContext {
    PlaybackRoute route{PlaybackRoute::Unknown};
    PlaybackAudioMode audio_mode{PlaybackAudioMode::Other};
    PlaybackSampleFormat format{PlaybackSampleFormat::Other};
    std::uint32_t sample_rate_hz{0};
    std::uint32_t channel_count{0};
    bool apdmnr_tuning_enabled{false};
};

class NormalPlaybackPcmAdapter {
public:
    bool configure(std::uint32_t sample_rate_hz,
                   PlaybackSampleFormat format,
                   std::uint32_t channel_count) noexcept;
    void reset() noexcept;

    NormalPlaybackResult process(const std::int16_t* input,
                                 std::int16_t* output,
                                 std::size_t frames,
                                 const Parameters& parameters,
                                 const NormalPlaybackContext& context) noexcept;

    NormalPlaybackResult process(const std::int16_t* input,
                                 std::int16_t* output,
                                 std::size_t frames,
                                 const StereoPlaybackProfile& profile,
                                 const NormalPlaybackContext& context) noexcept;

    bool configured() const noexcept { return configured_; }

private:
    Engine engine_;
    std::uint32_t sample_rate_hz_{0};
    bool configured_{false};
    bool was_processing_{false};
};

} // namespace pcm_dsp
