#pragma once

#include "mt8163_audio_core/types.hpp"

#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

// This is a transport contract, not an ALSA implementation. Device/card/PCM
// identifiers are deliberately absent until recovered from the target image
// and runtime diagnostics.
enum class PcmFormat : std::uint8_t {
    Pcm16SignedLittleEndian = 0
};

enum class PcmResult : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedRoute,
    UnsupportedFormat,
    NotOpen,
    InvalidState,
    BackendFailure,
    BufferTooLarge,
    CapacityExceeded
};

enum class PcmStreamState : std::uint8_t {
    Closed = 0,
    Open,
    Configured,
    Started,
    Stopped,
    Failed
};

struct PcmConfig {
    std::uint32_t sample_rate_hz{0U};
    std::uint16_t channel_count{0U};
    std::uint16_t period_frames{0U};
    PcmFormat format{PcmFormat::Pcm16SignedLittleEndian};
};

constexpr bool is_pcm_transport_route(RouteType route) noexcept {
    switch (route) {
        case RouteType::PrimaryPcm:
        case RouteType::A2dpPcm:
        case RouteType::HfpSco:
        case RouteType::FmCapturePcm:
        case RouteType::HdmiPcm:
            return true;
        case RouteType::None:
        case RouteType::FmDirectI2s:
        case RouteType::Cd3313AnalogInput:
            return false;
    }
    return false;
}

constexpr bool is_valid_pcm_config(const PcmConfig& config) noexcept {
    return config.sample_rate_hz != 0U &&
           (config.channel_count == 1U || config.channel_count == 2U) &&
           config.period_frames != 0U &&
           config.format == PcmFormat::Pcm16SignedLittleEndian;
}

// Implementations must be preallocated, bounded and noexcept. open/configure/
// start/stop/close belong to the HAL stream lifecycle, not the PCM callback.
// write() is the realtime entry point: it must not allocate, log, block on a
// mutex, or perform control-plane device discovery. The implementation must
// preserve caller buffer ownership and consume exactly frame_count frames.
class PcmBackend {
public:
    using StreamToken = std::uint32_t;

    virtual ~PcmBackend() = default;

    virtual PcmResult open(RouteType route, StreamToken* token) noexcept = 0;
    virtual PcmResult configure(StreamToken token,
                                const PcmConfig& config) noexcept = 0;
    virtual PcmResult start(StreamToken token) noexcept = 0;
    virtual PcmResult write(StreamToken token, const std::int16_t* interleaved,
                            std::size_t frame_count) noexcept = 0;
    virtual PcmResult stop(StreamToken token) noexcept = 0;
    virtual PcmResult close(StreamToken token) noexcept = 0;
};

}  // namespace mt8163_audio_core
