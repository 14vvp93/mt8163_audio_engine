#pragma once

#include "mt8163_audio_core/pcm_backend.hpp"
#include "mt8163_audio_core/route_policy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

constexpr std::size_t kMaxPcmStreams = 16U;
constexpr std::size_t kMaxPcmWriteFrames = 2048U;
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "32-bit atomics must be lock-free on the target ABI");

// Control-plane view. Do not retain pointers into broker-owned storage.
struct PcmStreamRecord {
    std::uint32_t handle{0U};
    PcmBackend::StreamToken backend_token{0U};
    SourceId source{SourceId::None};
    RouteType route{RouteType::None};
    PcmConfig config{};
    PcmStreamState state{PcmStreamState::Closed};
};

// Fixed-capacity lifecycle adapter. Lifecycle calls and get_record() belong
// to one serialized control context. write() may run concurrently with opening
// or configuring other slots. Before stop()/close(), the owner must quiesce
// this stream's callback. Backend write() must be bounded and nonblocking.
class PcmStreamBroker {
public:
    explicit PcmStreamBroker(PcmBackend& backend) noexcept : backend_(backend) {}

    PcmResult open(std::uint32_t handle, SourceId source, RouteType route) noexcept;
    PcmResult configure(std::uint32_t handle, const PcmConfig& config) noexcept;
    PcmResult start(std::uint32_t handle) noexcept;
    PcmResult write(std::uint32_t handle, const std::int16_t* interleaved,
                    std::size_t frame_count) noexcept;
    PcmResult stop(std::uint32_t handle) noexcept;
    PcmResult close(std::uint32_t handle) noexcept;

    bool get_record(std::uint32_t handle, PcmStreamRecord* out) const noexcept;
    std::size_t size() const noexcept;

private:
    struct Slot {
        std::atomic<std::uint32_t> handle{0U};
        std::atomic<std::uint32_t> backend_token{0U};
        std::atomic<PcmStreamState> state{PcmStreamState::Closed};
        SourceId source{SourceId::None};
        RouteType route{RouteType::None};
        PcmConfig config{};
    };

    Slot* find_control(std::uint32_t handle) noexcept;
    const Slot* find_control(std::uint32_t handle) const noexcept;

    PcmBackend& backend_;
    std::array<Slot, kMaxPcmStreams> streams_{};
};

}  // namespace mt8163_audio_core
