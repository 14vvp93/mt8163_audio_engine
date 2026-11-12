#pragma once

#include "mt8163_audio_core/pcm_backend.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

constexpr std::size_t kMaxPcmEndpoints = 8U;
constexpr std::size_t kMaxEndpointBackendStreams = 16U;
constexpr std::size_t kMaxEndpointPcmWriteFrames = 2048U;

// The card/device numbers are supplied by a target-specific runtime profile.
// No MT8163 endpoint number is guessed or enabled by this library.
struct PcmEndpoint {
    RouteType route{RouteType::None};
    std::uint32_t card_index{0U};
    std::uint32_t device_index{0U};
    std::uint32_t open_flags{0U};
    bool enabled{false};
};

// Narrow adapter to the platform PCM API (for example, a tinyalsa wrapper).
// All callbacks must be bounded, thread-safe for independent handles and
// noexcept. write() must be nonblocking and must not allocate or log.
struct PcmDeviceOps {
    void* context{nullptr};
    PcmResult (*open)(void*, std::uint32_t, std::uint32_t, std::uint32_t,
                      std::uint32_t*) noexcept = nullptr;
    PcmResult (*configure)(void*, std::uint32_t, const PcmConfig&) noexcept = nullptr;
    PcmResult (*start)(void*, std::uint32_t) noexcept = nullptr;
    PcmResult (*write)(void*, std::uint32_t, const std::int16_t*,
                       std::size_t) noexcept = nullptr;
    PcmResult (*stop)(void*, std::uint32_t) noexcept = nullptr;
    PcmResult (*close)(void*, std::uint32_t) noexcept = nullptr;
};

// Concrete route/configuration/lifecycle adapter. Lifecycle calls are
// serialized by the control plane; writes may arrive concurrently, but this
// class admits only one platform write per stream and returns WouldBlock for
// overlapping callbacks. The platform supplies a validated endpoint table
// and PcmDeviceOps; card/device IDs are never guessed here.
class EndpointPcmBackend final : public PcmBackend {
public:
    EndpointPcmBackend(const PcmEndpoint* endpoints, std::size_t endpoint_count,
                       const PcmDeviceOps& ops) noexcept;

    PcmResult open(RouteType route, StreamToken* token) noexcept override;
    PcmResult configure(StreamToken token, const PcmConfig& config) noexcept override;
    PcmResult start(StreamToken token) noexcept override;
    PcmResult write(StreamToken token, const std::int16_t* interleaved,
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
        std::atomic<std::uint32_t> state{
            static_cast<std::uint32_t>(PcmStreamState::Closed)};
        std::atomic_flag write_busy = ATOMIC_FLAG_INIT;
    };

    const PcmEndpoint* find_endpoint(RouteType route) const noexcept;
    Slot* find_slot(StreamToken token) noexcept;
    const Slot* find_slot(StreamToken token) const noexcept;
    StreamToken allocate_token() noexcept;

    std::array<PcmEndpoint, kMaxPcmEndpoints> endpoints_{};
    std::size_t endpoint_count_{0U};
    PcmDeviceOps ops_{};
    std::array<Slot, kMaxEndpointBackendStreams> streams_{};
    StreamToken next_token_{1U};
    bool valid_{false};
};

}  // namespace mt8163_audio_core
