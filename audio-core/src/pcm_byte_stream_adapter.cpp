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

PcmByteTransfer write_pcm16_bytes(PrimaryHalStreamAdapter& adapter,
                                  std::uint32_t handle,
                                  const PcmConfig& config,
                                  const void* buffer,
                                  std::size_t byte_count) noexcept {
    std::size_t frames = 0U;
    const PcmResult validation =
        validate_pcm16_buffer(handle, config, buffer, byte_count, &frames);
    if (validation != PcmResult::Ok) return {validation, 0U};

    const PcmResult result = adapter.write(
        handle, static_cast<const std::int16_t*>(buffer), frames);
    return {result, result == PcmResult::Ok ? byte_count : 0U};
}

PcmByteTransfer read_pcm16_bytes(PrimaryHalStreamAdapter& adapter,
                                 std::uint32_t handle,
                                 const PcmConfig& config,
                                 void* buffer,
                                 std::size_t byte_count) noexcept {
    std::size_t frames = 0U;
    const PcmResult validation =
        validate_pcm16_buffer(handle, config, buffer, byte_count, &frames);
    if (validation != PcmResult::Ok) return {validation, 0U};

    const PcmResult result = adapter.read(
        handle, static_cast<std::int16_t*>(buffer), frames);
    return {result, result == PcmResult::Ok ? byte_count : 0U};
}

}  // namespace mt8163_audio_core
