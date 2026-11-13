#include "mt8163_audio_core/pcm_byte_stream_adapter.hpp"

#include <cstdint>
#include <limits>

namespace mt8163_audio_core {
namespace {
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "PCM byte stream gate requires lock-free 32-bit atomics");

PcmResult validate_pcm16_buffer(std::uint32_t handle, const PcmConfig& config,
                                const void* buffer, std::size_t byte_count,
                                std::size_t* frame_count) noexcept {
    if (frame_count == nullptr || handle == 0U || buffer == nullptr ||
        byte_count == 0U || config.sample_rate_hz == 0U ||
        (config.channel_count != 1U && config.channel_count != 2U) ||
        config.period_frames == 0U) return PcmResult::InvalidArgument;
    if (config.format != PcmFormat::Pcm16SignedLittleEndian) {
        return PcmResult::UnsupportedFormat;
    }
    if (reinterpret_cast<std::uintptr_t>(buffer) % alignof(std::int16_t) != 0U) {
        return PcmResult::InvalidArgument;
    }
    const std::size_t bytes_per_frame =
        static_cast<std::size_t>(config.channel_count) * sizeof(std::int16_t);
    if (byte_count % bytes_per_frame != 0U) return PcmResult::InvalidArgument;
    *frame_count = byte_count / bytes_per_frame;
    if (*frame_count == 0U) return PcmResult::InvalidArgument;
    if (*frame_count > kMaxPcmWriteFrames) return PcmResult::BufferTooLarge;
    return PcmResult::Ok;
}
}

PcmByteStreamAdapter::PcmByteStreamAdapter(PrimaryHalStreamAdapter& adapter,
                                           std::uint32_t handle,
                                           PcmDirection direction) noexcept
    : adapter_(adapter), handle_(handle), direction_(direction),
      valid_(handle != 0U &&
             (direction == PcmDirection::Playback || direction == PcmDirection::Capture)) {}

PcmResult PcmByteStreamAdapter::configure(const PcmConfig& config) noexcept {
    if (!valid_ || closed_.load(std::memory_order_acquire) != 0U) {
        return PcmResult::InvalidArgument;
    }
    if (started_.load(std::memory_order_acquire) != 0U ||
        (transfer_gate_.load(std::memory_order_acquire) & kTransferCountMask) != 0U) {
        return PcmResult::InvalidState;
    }
    if (!is_valid_pcm_config(config)) return PcmResult::InvalidArgument;
    if (configured_.load(std::memory_order_acquire) != 0U) {
        const bool same_config =
            config_.sample_rate_hz == config.sample_rate_hz &&
            config_.channel_count == config.channel_count &&
            config_.period_frames == config.period_frames &&
            config_.format == config.format;
        return same_config ? PcmResult::Ok : PcmResult::InvalidState;
    }
    const PcmResult result = adapter_.configure(handle_, config);
    if (result == PcmResult::Ok) {
        config_ = config;
        configured_.store(1U, std::memory_order_release);
    } else {
        configured_.store(0U, std::memory_order_release);
    }
    return result;
}

PcmResult PcmByteStreamAdapter::start() noexcept {
    if (!valid_ || closed_.load(std::memory_order_acquire) != 0U) {
        return PcmResult::InvalidArgument;
    }
    if (configured_.load(std::memory_order_acquire) == 0U ||
        started_.load(std::memory_order_acquire) != 0U ||
        (transfer_gate_.load(std::memory_order_acquire) & kTransferCountMask) != 0U) {
        return PcmResult::InvalidState;
    }
    const PcmResult result = adapter_.start(handle_);
    if (result == PcmResult::Ok) {
        transfer_gate_.store(kTransferGateOpen, std::memory_order_release);
        started_.store(1U, std::memory_order_release);
    }
    return result;
}

PcmByteTransfer PcmByteStreamAdapter::transfer(const void* buffer,
                                               std::size_t byte_count,
                                               PcmDirection direction) noexcept {
    if (!valid_ || direction != direction_) return {PcmResult::InvalidState, 0U};
    std::uint32_t gate = transfer_gate_.load(std::memory_order_acquire);
    for (;;) {
        if ((gate & kTransferGateOpen) == 0U) {
            return {PcmResult::InvalidState, 0U};
        }
        if ((gate & kTransferCountMask) == kTransferCountMask) {
            return {PcmResult::CapacityExceeded, 0U};
        }
        if (transfer_gate_.compare_exchange_weak(
                gate, gate + 1U, std::memory_order_acq_rel,
                std::memory_order_acquire)) break;
    }

    std::size_t frames = 0U;
    const PcmResult validation =
        validate_pcm16_buffer(handle_, config_, buffer, byte_count, &frames);
    PcmTransferResult result{validation, 0U};
    if (validation == PcmResult::Ok) {
        result = direction == PcmDirection::Playback
            ? adapter_.write_transfer(handle_, static_cast<const std::int16_t*>(buffer), frames)
            : adapter_.read_transfer(handle_, const_cast<std::int16_t*>(
                  static_cast<const std::int16_t*>(buffer)), frames);
    }
    transfer_gate_.fetch_sub(1U, std::memory_order_release);

    if (result.frames_transferred > frames ||
        (result.result == PcmResult::Ok && result.frames_transferred == 0U)) {
        return {PcmResult::BackendFailure, 0U};
    }
    const std::size_t bytes_per_frame =
        static_cast<std::size_t>(config_.channel_count) * sizeof(std::int16_t);
    if (result.frames_transferred >
        std::numeric_limits<std::size_t>::max() / bytes_per_frame) {
        return {PcmResult::BackendFailure, 0U};
    }
    return {result.result, result.frames_transferred * bytes_per_frame};
}

PcmByteTransfer PcmByteStreamAdapter::write(const void* buffer,
                                            std::size_t byte_count) noexcept {
    return transfer(buffer, byte_count, PcmDirection::Playback);
}

PcmByteTransfer PcmByteStreamAdapter::read(void* buffer,
                                           std::size_t byte_count) noexcept {
    return transfer(buffer, byte_count, PcmDirection::Capture);
}

PcmResult PcmByteStreamAdapter::stop() noexcept {
    if (!valid_ || started_.load(std::memory_order_acquire) == 0U) {
        return PcmResult::InvalidState;
    }
    transfer_gate_.fetch_and(kTransferCountMask, std::memory_order_acq_rel);
    if ((transfer_gate_.load(std::memory_order_acquire) & kTransferCountMask) != 0U) {
        return PcmResult::WouldBlock;
    }
    const PcmResult result = adapter_.stop(handle_);
    if (result == PcmResult::Ok) {
        started_.store(0U, std::memory_order_release);
        configured_.store(0U, std::memory_order_release);
    }
    return result;
}

PcmResult PcmByteStreamAdapter::close() noexcept {
    if (!valid_) return PcmResult::InvalidArgument;
    if (started_.load(std::memory_order_acquire) != 0U) {
        const PcmResult stop_result = stop();
        if (stop_result != PcmResult::Ok) return stop_result;
    }
    if ((transfer_gate_.load(std::memory_order_acquire) & kTransferCountMask) != 0U) {
        return PcmResult::WouldBlock;
    }
    const PcmResult result = adapter_.close(handle_);
    if (result == PcmResult::Ok) {
        closed_.store(1U, std::memory_order_release);
        configured_.store(0U, std::memory_order_release);
        started_.store(0U, std::memory_order_release);
    }
    return result;
}

}  // namespace mt8163_audio_core
