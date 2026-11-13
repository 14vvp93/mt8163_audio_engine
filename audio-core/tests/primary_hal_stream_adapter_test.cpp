#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"
#include "mt8163_audio_core/pcm_byte_stream_adapter.hpp"

#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdint>

using namespace mt8163_audio_core;

#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
class FakeBackend final : public PcmBackend {
public:
    PcmResult open(RouteType, PcmDirection, StreamToken* token) noexcept override {
        if (token == nullptr || opened) return PcmResult::InvalidArgument;
        opened = true;
        *token = 1U;
        return PcmResult::Ok;
    }
    PcmResult configure(StreamToken, const PcmConfig& value) noexcept override {
        config = value;
        configured = true;
        return PcmResult::Ok;
    }
    PcmResult start(StreamToken) noexcept override {
        started = true;
        return PcmResult::Ok;
    }
    PcmResult write(StreamToken, const std::int16_t* data,
                    std::size_t frames) noexcept override {
        if (!started || data == nullptr || frames == 0U) return PcmResult::InvalidArgument;
        writes += frames;
        return PcmResult::Ok;
    }
    PcmResult read(StreamToken, std::int16_t* data,
                   std::size_t frames) noexcept override {
        if (!started || data == nullptr || frames == 0U) return PcmResult::InvalidArgument;
        for (std::size_t i = 0; i < frames * config.channel_count; ++i) data[i] = 0;
        return PcmResult::Ok;
    }
    PcmResult stop(StreamToken) noexcept override {
        started = false;
        return PcmResult::Ok;
    }
    PcmResult close(StreamToken) noexcept override {
        opened = false;
        configured = false;
        started = false;
        return PcmResult::Ok;
    }
    bool opened{false};
    bool configured{false};
    bool started{false};
    std::size_t writes{0U};
    PcmConfig config{};
};
}

int main() {
    FakeBackend backend;
    PrimaryHalStreamAdapter adapter(backend);
    constexpr std::uint32_t handle = 0x81630001U;
    constexpr PcmConfig config{48000U, 2U, 240U,
                               PcmFormat::Pcm16SignedLittleEndian};
    std::array<std::int16_t, 480U> samples{};

    CHECK(adapter.open(handle, SourceId::LocalMedia, RouteType::PrimaryPcm,
                        PcmDirection::Playback) == PcmResult::Ok);
    CHECK(adapter.stream_count() == 1U);
    CHECK(adapter.configure(handle, config) == PcmResult::Ok);
    CHECK(adapter.start(handle) == PcmResult::Ok);
    CHECK(adapter.write(handle, samples.data(), 240U) == PcmResult::Ok);
    CHECK(backend.writes == 240U);

    const PcmByteTransfer byte_write = write_pcm16_bytes(
        adapter, handle, config, samples.data(), 960U);
    CHECK(byte_write.result == PcmResult::Ok);
    CHECK(byte_write.bytes_transferred == 960U);
    CHECK(backend.writes == 480U);

    const PcmByteTransfer misaligned_write = write_pcm16_bytes(
        adapter, handle, config, samples.data(), 959U);
    CHECK(misaligned_write.result == PcmResult::InvalidArgument);
    CHECK(misaligned_write.bytes_transferred == 0U);

    const PcmByteTransfer oversized_write = write_pcm16_bytes(
        adapter, handle, config, samples.data(), 8196U);
    CHECK(oversized_write.result == PcmResult::BufferTooLarge);
    CHECK(oversized_write.bytes_transferred == 0U);

    const PcmByteTransfer null_write = write_pcm16_bytes(
        adapter, handle, config, nullptr, 4U);
    CHECK(null_write.result == PcmResult::InvalidArgument);
    CHECK(null_write.bytes_transferred == 0U);
    CHECK(adapter.stop(handle) == PcmResult::Ok);
    CHECK(adapter.close(handle) == PcmResult::Ok);
    CHECK(adapter.stream_count() == 0U);
    CHECK(!backend.opened);

    CHECK(adapter.open(handle, SourceId::Microphone,
                        RouteType::PrimaryCapturePcm,
                        PcmDirection::Capture) == PcmResult::Ok);
    CHECK(adapter.configure(handle, config) == PcmResult::Ok);
    CHECK(adapter.start(handle) == PcmResult::Ok);
    const PcmByteTransfer byte_read = read_pcm16_bytes(
        adapter, handle, config, samples.data(), 960U);
    CHECK(byte_read.result == PcmResult::Ok);
    CHECK(byte_read.bytes_transferred == 960U);
    CHECK(samples[0] == 0);
    CHECK(adapter.stop(handle) == PcmResult::Ok);
    CHECK(adapter.close(handle) == PcmResult::Ok);

    CHECK(adapter.open(handle, SourceId::LocalMedia, RouteType::A2dpPcm,
                        PcmDirection::Playback) == PcmResult::UnsupportedRoute);
    CHECK(adapter.open(0U, SourceId::LocalMedia, RouteType::PrimaryPcm,
                        PcmDirection::Playback) == PcmResult::InvalidArgument);
    return 0;
}
