#include "mt8163_audio_core/pcm_byte_stream_adapter.hpp"

#include <limits>

namespace mt8163_audio_core {
namespace {

PcmByteTransfer transfer_bytes(PrimaryHalStreamAdapter& adapter,
                               std::uint32_t handle,
                               const PcmConfig& config,
                               void* buffer,
                               std::size_t byte_count,
                               PcmDirection direction) noexcept {
    if (handle == 0U || buffer == nullptr || byte_count == 0U ||
        !is_valid_pcm_config(config)) {
        return {PcmResult::InvalidArgument, 0U};
    }

    if (config.format != PcmFormat::Pcm16SignedLittleEndian) {
        return {PcmResult::UnsupportedFormat, 0U};
    }

    constexpr std::size_t bytes_per_sample = sizeof(std::int16_t);
    const std::size_t channels = static_cast<std::size_t>(config.channel_count);
    if (channels > std::numeric_limits<std::size_t>::max() / bytes_per_sample) {
        return {PcmResult::InvalidArgument, 0U};
    }

    const std::size_t bytes_per_frame = channels * bytes_per_sample;
    if (byte_count % bytes_per_frame != 0U) {
        return {PcmResult::InvalidArgument, 0U};
    }

    const std::size_t frame_count = byte_count / bytes_per_frame;
    if (frame_count == 0U) return {PcmResult::InvalidArgument, 0U};
    if (frame_count > kMaxPcmWriteFrames) {
        return {PcmResult::BufferTooLarge, 0U};
    }

    PcmResult result = PcmResult::InvalidState;
    if (direction == PcmDirection::Playback) {
        result = adapter.write(handle, static_cast<const std::int16_t*>(buffer),
                               frame_count);
    } else if (direction == PcmDirection::Capture) {
        result = adapter.read(handle, static_cast<std::int16_t*>(buffer),
                              frame_count);
    } else {
        return {PcmResult::InvalidArgument, 0U};
    }

    return {result, result == PcmResult::Ok ? byte_count : 0U};
}

}  // namespace

PcmByteTransfer write_pcm16_bytes(PrimaryHalStreamAdapter& adapter,
                                  std::uint32_t handle,
                                  const PcmConfig& config,
                                  const void* buffer,
                                  std::size_t byte_count) noexcept {
    return transfer_bytes(adapter, handle, config, const_cast<void*>(buffer),
                          byte_count, PcmDirection::Playback);
}

PcmByteTransfer read_pcm16_bytes(PrimaryHalStreamAdapter& adapter,
                                 std::uint32_t handle,
                                 const PcmConfig& config,
                                 void* buffer,
                                 std::size_t byte_count) noexcept {
    return transfer_bytes(adapter, handle, config, buffer, byte_count,
                          PcmDirection::Capture);
}

}  // namespace mt8163_audio_core
