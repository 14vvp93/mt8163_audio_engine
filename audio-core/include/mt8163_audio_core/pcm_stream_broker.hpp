#pragma once

#include "mt8163_audio_core/pcm_backend.hpp"
#include "mt8163_audio_core/session_token.hpp"
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

struct PcmStreamRecord {
    SessionToken session{};
    PcmBackend::StreamToken backend_token{0U};
    SourceId source{SourceId::None};
    RouteType route{RouteType::None};
    PcmDirection direction{PcmDirection::Playback};
    PcmConfig config{};
    PcmStreamState state{PcmStreamState::Closed};
    bool backend_stop_required{false};
};

// Control-plane methods (open/configure/start/stop/close/get_record) must be
// serialized by one HAL control context. Transfer methods are callback-safe and
// may run concurrently with stop(); stop() closes admission and drains them.
class PcmStreamBroker {
public:
    explicit PcmStreamBroker(PcmBackend& backend) noexcept : backend_(backend) {}

    PcmResult open(SessionToken session, SourceId source, RouteType route,
                   PcmDirection direction = PcmDirection::Playback) noexcept;
    PcmResult configure(SessionToken session, const PcmConfig& config) noexcept;
    PcmResult start(SessionToken session) noexcept;
    PcmResult write(SessionToken session, const std::int16_t* interleaved,
                    std::size_t frame_count) noexcept;
    PcmResult read(SessionToken session, std::int16_t* interleaved,
                   std::size_t frame_count) noexcept;
    PcmTransferResult write_transfer(SessionToken session,
                                     const std::int16_t* interleaved,
                                     std::size_t frame_count) noexcept;
    PcmTransferResult read_transfer(SessionToken session, std::int16_t* interleaved,
                                    std::size_t frame_count) noexcept;
    PcmResult stop(SessionToken session) noexcept;
    PcmResult close(SessionToken session) noexcept;

    bool get_record(SessionToken session, PcmStreamRecord* out) const noexcept;
    std::size_t size() const noexcept;

private:
    struct Slot {
        std::atomic<std::uint32_t> handle{0U};
        std::uint64_t session_generation{0U};
        std::atomic<std::uint32_t> backend_token{0U};
        std::atomic<std::uint32_t> state{static_cast<std::uint32_t>(PcmStreamState::Closed)};
        std::atomic<std::uint32_t> transfer_gate{0U};
        std::atomic<std::uint32_t> transfer_generation{0U};
        bool backend_stop_required{false};
        SourceId source{SourceId::None};
        RouteType route{RouteType::None};
        PcmDirection direction{PcmDirection::Playback};
        PcmConfig config{};
    };

    Slot* find_control(SessionToken session) noexcept;
    const Slot* find_control(SessionToken session) const noexcept;

    PcmBackend& backend_;
    std::array<Slot, kMaxPcmStreams> streams_{};
};

}  // namespace mt8163_audio_core
