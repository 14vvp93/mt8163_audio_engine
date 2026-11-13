#pragma once

#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"

#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

struct PcmByteTransfer {
    PcmResult result{PcmResult::InvalidState};
    std::size_t bytes_transferred{0U};
};

// Android HAL byte-count bridge for the currently supported PCM16 transport.
// The current backend contract is all-frames-or-error; partial transfers are
// intentionally not fabricated or inferred.
PcmByteTransfer write_pcm16_bytes(PrimaryHalStreamAdapter& adapter,
                                  std::uint32_t handle,
                                  const PcmConfig& config,
                                  const void* buffer,
                                  std::size_t byte_count) noexcept;

PcmByteTransfer read_pcm16_bytes(PrimaryHalStreamAdapter& adapter,
                                 std::uint32_t handle,
                                 const PcmConfig& config,
                                 void* buffer,
                                 std::size_t byte_count) noexcept;

}  // namespace mt8163_audio_core
