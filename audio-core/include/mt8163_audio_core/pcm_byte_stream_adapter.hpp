#pragma once

#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"

#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

struct PcmByteTransfer {
    PcmResult result{PcmResult::InvalidState};
    std::size_t bytes_transferred{0U};
};

// Per-stream HAL byte/frame bridge. The configured format is cached once and
// cannot be supplied differently by individual realtime transfer calls.
// The current PCM backend contract is all-frames-or-error.
class PcmByteStreamAdapter final {
public:
    PcmByteStreamAdapter(PrimaryHalStreamAdapter& adapter,
                        std::uint32_t handle,
                        PcmDirection direction) noexcept;

    PcmByteStreamAdapter(const PcmByteStreamAdapter&) = delete;
    PcmByteStreamAdapter& operator=(const PcmByteStreamAdapter&) = delete;

    PcmResult configure(const PcmConfig& config) noexcept;
    PcmResult start() noexcept;
    PcmByteTransfer write(const void* buffer, std::size_t byte_count) noexcept;
    PcmByteTransfer read(void* buffer, std::size_t byte_count) noexcept;
    PcmResult stop() noexcept;
    PcmResult close() noexcept;

    bool configured() const noexcept { return configured_; }
    bool started() const noexcept { return started_; }
    std::uint32_t handle() const noexcept { return handle_; }
    PcmDirection direction() const noexcept { return direction_; }

private:
    PcmByteTransfer transfer(const void* buffer, std::size_t byte_count,
                             PcmDirection direction) noexcept;

    PrimaryHalStreamAdapter& adapter_;
    const std::uint32_t handle_;
    const PcmDirection direction_;
    PcmConfig config_{};
    bool valid_{false};
    bool configured_{false};
    bool started_{false};
};

}  // namespace mt8163_audio_core
