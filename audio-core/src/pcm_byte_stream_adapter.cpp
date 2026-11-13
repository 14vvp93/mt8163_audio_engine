#include "mt8163_audio_core/pcm_byte_stream_adapter.hpp"

#include <cstdint>

namespace mt8163_audio_core {
namespace {

PcmResult validate_pcm16_buffer(std::uint32_t handle,
                                const PcmConfig& config,
                                const void* buffer,
                                std::size_t byte_count,
                                std::size_t* frame_count) noexcept {
    if (frame_count == nullptr || handle == 0U || buffer == nullptr ||
        byte_count == 0U || config.sample_rate_hz == 0U ||
        (config.channel_count != 1U && config.channel_count != 2U) ||
        config.period_frames == 0U) {
        return PcmResult::InvalidArgument;
    }
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

}  // namespace

PcmByteStreamAdapter::PcmByteStreamAdapter(PrimaryHalStreamAdapter& adapter,
                                           std::uint32_t handle,
                                           PcmDirection direction) noexcept
    : adapter_(adapter), handle_(handle), direction_(direction),
      valid_(handle != 0U &&
             (direction == PcmDirection::Playback ||
              direction == PcmDirection::Capture)) {}

PcmResult PcmByteStreamAdapter::configure(const PcmConfig& config) noexcept {
    if (!valid_) return PcmResult::InvalidArgument;
    if (started_) return PcmResult::InvalidState;
    if (config.sample_rate_hz == 0U ||
        (config.channel_count != 1U && config.channel_count != 2U) ||
        config.period_frames == 0U) {
        return PcmResult::InvalidArgument;
    }
    if (config.format != PcmFormat::Pcm16SignedLittleEndian) {
        return PcmResult::UnsupportedFormat;
    }
    const PcmResult result = adapter_.configure(handle_, config);
    if (result == PcmResult::Ok) {
        config_ = config;
        configured_ = true;
    } else {
        config_ = PcmConfig{};
        configured_ = false;
    }
    return result;
}

PcmResult PcmByteStreamAdapter::start() noexcept {
    if (!valid_) return PcmResult::InvalidArgument;
    if (!configured_ || started_) return PcmResult::InvalidState;
    const PcmResult result = adapter_.start(handle_);
    if (result == PcmResult::Ok) started_ = true;
    return result;
}

PcmByteTransfer PcmByteStreamAdapter::transfer(void* buffer,
                                               std::size_t byte_count,
                                               PcmDirection direction) noexcept {
    if (!valid_) return {PcmResult::InvalidArgument, 0U};
    if (!configured_ || !started_ || direction != direction_) {
        return {PcmResult::InvalidState, 0U};
    }

    std::size_t frames = 0U;
    const PcmResult validation = validate_pcm16_buffer(
        handle_, config_, buffer, byte_count, &frames);
    if (validation != PcmResult::Ok) return {validation, 0U};

    const PcmResult result = direction == PcmDirection::Playback
        ? adapter_.write(handle_, static_cast<const std::int16_t*>(buffer), frames)
        : adapter_.read(handle_, static_cast<std::int16_t*>(buffer), frames);
    return {result, result == PcmResult::Ok ? byte_count : 0U};
}

PcmByteTransfer PcmByteStreamAdapter::write(const void* buffer,
                                            std::size_t byte_count) noexcept {
    return transfer(const_cast<void*>(buffer), byte_count, PcmDirection::Playback);
}

PcmByteTransfer PcmByteStreamAdapter::read(void* buffer,
                                           std::size_t byte_count) noexcept {
    return transfer(buffer, byte_count, PcmDirection::Capture);
}

PcmResult PcmByteStreamAdapter::stop() noexcept {
    if (!valid_) return PcmResult::InvalidArgument;
    if (!started_) return PcmResult::InvalidState;
    const PcmResult result = adapter_.stop(handle_);
    if (result == PcmResult::Ok) started_ = false;
    return result;
}

PcmResult PcmByteStreamAdapter::close() noexcept {
    if (!valid_) return PcmResult::InvalidArgument;
    const PcmResult result = adapter_.close(handle_);
    if (result == PcmResult::Ok) {
        config_ = PcmConfig{};
        configured_ = false;
        started_ = false;
    }
    return result;
}

}  // namespace mt8163_audio_core
