#pragma once

#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

struct PcmByteTransfer {
    PcmResult result{PcmResult::InvalidState};
    std::size_t bytes_transferred{0U};
};

// The owner must quiesce callback dispatch before destroying this object.
class PcmByteStreamAdapter final {
public:
    PcmByteStreamAdapter(PrimaryHalStreamAdapter& adapter, SessionToken session,
                         PcmDirection direction) noexcept;
    PcmByteStreamAdapter(const PcmByteStreamAdapter&) = delete;
    PcmByteStreamAdapter& operator=(const PcmByteStreamAdapter&) = delete;

    PcmResult configure(const PcmConfig& config) noexcept;
    PcmResult start() noexcept;
    PcmByteTransfer write(const void* buffer, std::size_t byte_count) noexcept;
    PcmByteTransfer read(void* buffer, std::size_t byte_count) noexcept;
    PcmResult stop() noexcept;
    PcmResult close() noexcept;

    bool configured() const noexcept {
        return configured_.load(std::memory_order_acquire) != 0U;
    }
    bool started() const noexcept {
        return started_.load(std::memory_order_acquire) != 0U;
    }
    SessionToken session() const noexcept { return session_; }
    PcmDirection direction() const noexcept { return direction_; }

private:
    PcmByteTransfer transfer(const void* buffer, std::size_t byte_count,
                             PcmDirection direction) noexcept;

    static constexpr std::uint32_t kTransferGateOpen = 0x80000000U;
    static constexpr std::uint32_t kTransferCountMask = 0x7FFFFFFFU;

    PrimaryHalStreamAdapter& adapter_;
    const SessionToken session_;
    const PcmDirection direction_;
    PcmConfig config_{};
    std::atomic<std::uint32_t> transfer_gate_{0U};
    std::atomic<std::uint32_t> configured_{0U};
    std::atomic<std::uint32_t> started_{0U};
    std::atomic<std::uint32_t> stop_required_{0U};
    std::atomic<std::uint32_t> closed_{0U};
    bool valid_{false};
};

}  // namespace mt8163_audio_core
