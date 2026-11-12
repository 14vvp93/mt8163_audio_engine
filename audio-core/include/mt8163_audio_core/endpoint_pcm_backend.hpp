#pragma once

#include "mt8163_audio_core/pcm_backend.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

constexpr std::size_t kMaxPcmEndpoints = 8U;
constexpr std::size_t kMaxEndpointBackendStreams = 16U;
constexpr std::size_t kMaxEndpointPcmTransferFrames = 2048U;

struct PcmEndpoint {
    RouteType route{RouteType::None};
    PcmDirection direction{PcmDirection::Playback};
    std::uint32_t card_index{0U};
    std::uint32_t device_index{0U};
    std::uint32_t open_flags{0U};
    bool enabled{false};
};

struct PcmDeviceOps {
    void* context{nullptr};
    PcmResult (*open)(void*, std::uint32_t, std::uint32_t, PcmDirection,
                      std::uint32_t, std::uint32_t*) noexcept = nullptr;
    PcmResult (*configure)(void*, std::uint32_t, const PcmConfig&) noexcept = nullptr;
    PcmResult (*start)(void*, std::uint32_t) noexcept = nullptr;
    PcmResult (*write)(void*, std::uint32_t, const std::int16_t*,
                       std::size_t) noexcept = nullptr;
    PcmResult (*read)(void*, std::uint32_t, std::int16_t*,
                      std::size_t) noexcept = nullptr;
    PcmResult (*stop)(void*, std::uint32_t) noexcept = nullptr;
    PcmResult (*close)(void*, std::uint32_t) noexcept = nullptr;
};

// Route/direction-aware PCM adapter. The platform supplies only endpoints
// verified from target runtime evidence. It serializes read/write callbacks
// per stream and returns WouldBlock rather than waiting on an overlapping
// callback. Lifecycle calls are serialized by the control plane.
class EndpointPcmBackend final : public PcmBackend {
public:
    EndpointPcmBackend(const PcmEndpoint* endpoints, std::size_t endpoint_count,
                       const PcmDeviceOps& ops) noexcept;

    PcmResult open(RouteType route, PcmDirection direction,
                   StreamToken* token) noexcept override;
    PcmResult configure(StreamToken token, const PcmConfig& config) noexcept override;
    PcmResult start(StreamToken token) noexcept override;
    PcmResult write(StreamToken token, const std::int16_t* interleaved,
                    std::size_t frame_count) noexcept override;
    PcmResult read(StreamToken token, std::int16_t* interleaved,
                   std::size_t frame_count) noexcept override;
    PcmResult stop(StreamToken token) noexcept override;
    PcmResult close(StreamToken token) noexcept override;

    bool valid() const noexcept { return valid_; }
    std::size_t active_streams() const noexcept;

private:
    struct Slot {
        StreamToken token{0U};
        std::uint32_t native_handle{0U};
        PcmConfig config{};
        PcmDirection direction{PcmDirection::Playback};
        std::atomic<std::uint32_t> state{
            static_cast<std::uint32_t>(PcmStreamState::Closed)};
        std::atomic_flag transfer_busy = ATOMIC_FLAG_INIT;
    };

    const PcmEndpoint* find_endpoint(RouteType route, PcmDirection direction) const noexcept;
    Slot* find_slot(StreamToken token) noexcept;
    const Slot* find_slot(StreamToken token) const noexcept;
    StreamToken allocate_token() noexcept;
    PcmResult transfer(StreamToken token, std::int16_t* buffer,
                       std::size_t frame_count, PcmDirection direction) noexcept;

    std::array<PcmEndpoint, kMaxPcmEndpoints> endpoints_{};
    std::size_t endpoint_count_{0U};
    PcmDeviceOps ops_{};
    std::array<Slot, kMaxEndpointBackendStreams> streams_{};
    StreamToken next_token_{1U};
    bool valid_{false};
};

}  // namespace mt8163_audio_core
