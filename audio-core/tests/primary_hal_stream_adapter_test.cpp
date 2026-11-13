#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"
#include "mt8163_audio_core/pcm_byte_stream_adapter.hpp"

#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <thread>

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
        return start_result;
    }
    PcmResult write(StreamToken, const std::int16_t* data,
                    std::size_t frames) noexcept override {
        if (!started || data == nullptr || frames == 0U) return PcmResult::InvalidArgument;
        writes += frames;
        if (block_writes.load(std::memory_order_acquire)) {
            write_entered.store(true, std::memory_order_release);
            while (!release_write.load(std::memory_order_acquire)) std::this_thread::yield();
        }
        return PcmResult::Ok;
    }
    PcmResult read(StreamToken, std::int16_t* data,
                   std::size_t frames) noexcept override {
        if (!started || data == nullptr || frames == 0U) return PcmResult::InvalidArgument;
        for (std::size_t i = 0; i < frames * config.channel_count; ++i) data[i] = 0;
        return PcmResult::Ok;
    }
    PcmResult stop(StreamToken) noexcept override {
        ++stops;
        started = false;
        return PcmResult::Ok;
    }
    PcmResult close(StreamToken) noexcept override {
        opened = false;
        configured = false;
        started = false;
        return PcmResult::Ok;
    }
    PcmTransferResult write_transfer(StreamToken token, const std::int16_t* data,
                                     std::size_t frames) noexcept override {
        const std::size_t transfer_frames = next_transfer_frames == 0U
            ? frames : (next_transfer_frames < frames ? next_transfer_frames : frames);
        const PcmResult transfer_status = write(token, data, transfer_frames);
        const PcmResult result = next_transfer_result;
        next_transfer_frames = 0U;
        next_transfer_result = PcmResult::Ok;
        return {transfer_status == PcmResult::Ok ? result : transfer_status,
                transfer_status == PcmResult::Ok ? transfer_frames : 0U};
    }
    bool opened{false};
    bool configured{false};
    bool started{false};
    std::size_t writes{0U};
    std::size_t stops{0U};
    std::size_t next_transfer_frames{0U};
    PcmResult next_transfer_result{PcmResult::Ok};
    PcmResult start_result{PcmResult::Ok};
    PcmConfig config{};
    std::atomic<bool> block_writes{false};
    std::atomic<bool> write_entered{false};
    std::atomic<bool> release_write{false};
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
    CHECK(adapter.open(handle, SourceId::LocalMedia, RouteType::PrimaryPcm,
                       PcmDirection::Playback) == PcmResult::InvalidArgument);
    CHECK(adapter.stream_count() == 1U);
    PcmByteStreamAdapter output(adapter, handle, PcmDirection::Playback);
    PcmConfig unsupported_config = config;
    unsupported_config.format = static_cast<PcmFormat>(99U);
    CHECK(output.configure(unsupported_config) == PcmResult::UnsupportedFormat);
    CHECK(!output.configured());
    CHECK(output.configure(config) == PcmResult::Ok);
    CHECK(output.configured());
    CHECK(output.configure(config) == PcmResult::Ok);
    PcmConfig changed_config = config;
    changed_config.sample_rate_hz = 44100U;
    CHECK(output.configure(changed_config) == PcmResult::InvalidState);
    CHECK(output.start() == PcmResult::Ok);
    CHECK(output.started());

    const PcmByteTransfer byte_write = output.write(samples.data(), 960U);
    CHECK(byte_write.result == PcmResult::Ok);
    CHECK(byte_write.bytes_transferred == 960U);
    CHECK(backend.writes == 240U);

    backend.next_transfer_frames = 1U;
    const PcmByteTransfer short_write = output.write(samples.data(), 16U);
    CHECK(short_write.result == PcmResult::Ok);
    CHECK(short_write.bytes_transferred == 4U);

    backend.next_transfer_frames = 1U;
    backend.next_transfer_result = PcmResult::Xrun;
    const PcmByteTransfer partial_xrun = output.write(samples.data(), 16U);
    CHECK(partial_xrun.result == PcmResult::Xrun);
    CHECK(partial_xrun.bytes_transferred == 4U);

    const PcmByteTransfer misaligned_write = output.write(samples.data(), 959U);
    CHECK(misaligned_write.result == PcmResult::InvalidArgument);
    CHECK(misaligned_write.bytes_transferred == 0U);

    const PcmByteTransfer oversized_write = output.write(samples.data(), 8196U);
    CHECK(oversized_write.result == PcmResult::BufferTooLarge);
    CHECK(oversized_write.bytes_transferred == 0U);

    const PcmByteTransfer null_write = output.write(nullptr, 4U);
    CHECK(null_write.result == PcmResult::InvalidArgument);
    CHECK(null_write.bytes_transferred == 0U);

    alignas(std::int16_t) std::array<std::uint8_t, 8U> unaligned_storage{};
    const PcmByteTransfer unaligned_buffer =
        output.write(unaligned_storage.data() + 1U, 4U);
    CHECK(unaligned_buffer.result == PcmResult::InvalidArgument);
    CHECK(unaligned_buffer.bytes_transferred == 0U);

    const PcmByteTransfer empty_write = output.write(samples.data(), 0U);
    CHECK(empty_write.result == PcmResult::InvalidArgument);
    CHECK(empty_write.bytes_transferred == 0U);

    backend.block_writes.store(true, std::memory_order_release);
    std::atomic<PcmResult> concurrent_result{PcmResult::BackendFailure};
    std::thread writer([&] {
        concurrent_result.store(output.write(samples.data(), 4U).result, std::memory_order_release);
    });
    while (!backend.write_entered.load(std::memory_order_acquire)) std::this_thread::yield();
    CHECK(output.stop() == PcmResult::WouldBlock);
    CHECK(output.write(samples.data(), 4U).result == PcmResult::InvalidState);
    backend.release_write.store(true, std::memory_order_release);
    writer.join();
    CHECK(concurrent_result.load(std::memory_order_acquire) == PcmResult::Ok);
    CHECK(output.stop() == PcmResult::Ok);
    backend.block_writes.store(false, std::memory_order_release);
    CHECK(output.configure(config) == PcmResult::Ok);
    CHECK(output.start() == PcmResult::Ok);
    CHECK(output.stop() == PcmResult::Ok);
    CHECK(output.close() == PcmResult::Ok);
    CHECK(!output.configured());
    CHECK(!output.started());
    CHECK(adapter.stream_count() == 0U);
    CHECK(!backend.opened);

    CHECK(adapter.open(handle, SourceId::Microphone,
                       RouteType::PrimaryCapturePcm,
                       PcmDirection::Capture) == PcmResult::Ok);
    PcmByteStreamAdapter input(adapter, handle, PcmDirection::Capture);
    CHECK(input.configure(config) == PcmResult::Ok);
    CHECK(input.start() == PcmResult::Ok);
    const PcmByteTransfer byte_read = input.read(samples.data(), 960U);
    CHECK(byte_read.result == PcmResult::Ok);
    CHECK(byte_read.bytes_transferred == 960U);
    CHECK(samples[0] == 0);
    CHECK(input.write(samples.data(), 4U).result == PcmResult::InvalidState);
    CHECK(input.stop() == PcmResult::Ok);
    CHECK(input.close() == PcmResult::Ok);

    CHECK(adapter.open(handle, SourceId::LocalMedia, RouteType::A2dpPcm,
                       PcmDirection::Playback) == PcmResult::UnsupportedRoute);
    CHECK(adapter.open(0U, SourceId::LocalMedia, RouteType::PrimaryPcm,
                       PcmDirection::Playback) == PcmResult::InvalidArgument);

    constexpr std::uint32_t failed_start_handle = 0x81630002U;
    CHECK(adapter.open(failed_start_handle, SourceId::LocalMedia,
                       RouteType::PrimaryPcm, PcmDirection::Playback) == PcmResult::Ok);
    PcmByteStreamAdapter failed_start_output(
        adapter, failed_start_handle, PcmDirection::Playback);
    CHECK(failed_start_output.configure(config) == PcmResult::Ok);
    backend.start_result = PcmResult::BackendFailure;
    CHECK(failed_start_output.start() == PcmResult::BackendFailure);
    CHECK(!failed_start_output.started());
    CHECK(failed_start_output.stop() == PcmResult::Ok);
    CHECK(!backend.started);
    CHECK(failed_start_output.close() == PcmResult::Ok);
    CHECK(adapter.stream_count() == 0U);

    constexpr std::uint32_t direct_failed_start_handle = 0x81630003U;
    CHECK(adapter.open(direct_failed_start_handle, SourceId::LocalMedia,
                       RouteType::PrimaryPcm, PcmDirection::Playback) == PcmResult::Ok);
    CHECK(adapter.configure(direct_failed_start_handle, config) == PcmResult::Ok);
    backend.start_result = PcmResult::BackendFailure;
    CHECK(adapter.start(direct_failed_start_handle) == PcmResult::BackendFailure);
    const std::size_t stops_before_close = backend.stops;
    CHECK(adapter.close(direct_failed_start_handle) == PcmResult::Ok);
    CHECK(backend.stops == stops_before_close + 1U);
    CHECK(!backend.started);
    CHECK(adapter.stream_count() == 0U);
    backend.start_result = PcmResult::Ok;
    return 0;
}
