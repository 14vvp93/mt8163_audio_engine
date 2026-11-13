#pragma once

#include "mt8163_audio_core/types.hpp"

#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

enum class PcmDirection : std::uint8_t {
    Playback = 0,
    Capture
};

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
    WouldBlock,
    Xrun,
    DeviceLost,
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

struct PcmTransferResult {
    PcmResult result{PcmResult::InvalidState};
    std::size_t frames_transferred{0U};
};

constexpr bool is_pcm_transport_route(RouteType route) noexcept {
    switch (route) {
        case RouteType::PrimaryPcm:
        case RouteType::PrimaryCapturePcm:
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

constexpr bool is_valid_pcm_route_direction(RouteType route,
                                            PcmDirection direction) noexcept {
    switch (route) {
        case RouteType::PrimaryPcm:
        case RouteType::A2dpPcm:
        case RouteType::HdmiPcm:
            return direction == PcmDirection::Playback;
        case RouteType::PrimaryCapturePcm:
        case RouteType::FmCapturePcm:
            return direction == PcmDirection::Capture;
        case RouteType::HfpSco:
            return direction == PcmDirection::Playback ||
                   direction == PcmDirection::Capture;
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

class PcmBackend {
public:
    using StreamToken = std::uint32_t;
    virtual ~PcmBackend() = default;
    virtual PcmResult open(RouteType route, PcmDirection direction,
                           StreamToken* token) noexcept = 0;
    virtual PcmResult configure(StreamToken token,
                                const PcmConfig& config) noexcept = 0;
    // start() failure may leave native state partially active; callers must attempt stop().
    virtual PcmResult start(StreamToken token) noexcept = 0;
    virtual PcmResult write(StreamToken token, const std::int16_t* interleaved,
                            std::size_t frame_count) noexcept = 0;
    virtual PcmResult read(StreamToken token, std::int16_t* interleaved,
                           std::size_t frame_count) noexcept = 0;
    virtual PcmTransferResult write_transfer(StreamToken token,
                                             const std::int16_t* interleaved,
                                             std::size_t frame_count) noexcept {
        const PcmResult result = write(token, interleaved, frame_count);
        return {result, result == PcmResult::Ok ? frame_count : 0U};
    }
    virtual PcmTransferResult read_transfer(StreamToken token,
                                            std::int16_t* interleaved,
                                            std::size_t frame_count) noexcept {
        const PcmResult result = read(token, interleaved, frame_count);
        return {result, result == PcmResult::Ok ? frame_count : 0U};
    }
    virtual PcmResult stop(StreamToken token) noexcept = 0;
    virtual PcmResult close(StreamToken token) noexcept = 0;
};

}  // namespace mt8163_audio_core
