#include "mt8163_audio_core/pcm_stream_broker.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace mt8163_audio_core;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FakePcmBackend final : public PcmBackend {
public:
    PcmResult open(RouteType, PcmDirection direction, StreamToken* token) noexcept override {
        if (token == nullptr) return PcmResult::InvalidArgument;
        *token = ++last_token;
        last_direction = direction;
        ++opens;
        return open_result;
    }
    PcmResult configure(StreamToken, const PcmConfig&) noexcept override {
        ++configures;
        return configure_result;
    }
    PcmResult start(StreamToken) noexcept override {
        ++starts;
        return start_result;
    }
    PcmResult write(StreamToken, const std::int16_t* data,
                    std::size_t frames) noexcept override {
        ++writes;
        last_frames = frames;
        while (block_writes.load(std::memory_order_acquire) &&
               !release_blocked_write.load(std::memory_order_acquire)) std::this_thread::yield();
        return data == nullptr ? PcmResult::InvalidArgument : write_result;
    }
    PcmResult read(StreamToken, std::int16_t* data,
                   std::size_t frames) noexcept override {
        ++reads;
        last_frames = frames;
        return data == nullptr ? PcmResult::InvalidArgument : read_result;
    }
    PcmResult stop(StreamToken) noexcept override {
        ++stops;
        return stop_result;
    }
    PcmResult close(StreamToken) noexcept override {
        ++closes;
        return close_result;
    }

    StreamToken last_token{0U};
    PcmDirection last_direction{PcmDirection::Playback};
    std::atomic<std::size_t> opens{0U}, configures{0U}, starts{0U};
    std::atomic<std::size_t> writes{0U}, reads{0U}, stops{0U}, closes{0U};
    std::atomic<std::size_t> last_frames{0U};
    std::atomic<bool> block_writes{false}, release_blocked_write{false};
    PcmResult open_result{PcmResult::Ok}, configure_result{PcmResult::Ok};
    PcmResult start_result{PcmResult::Ok}, write_result{PcmResult::Ok};
    PcmResult read_result{PcmResult::Ok}, stop_result{PcmResult::Ok};
    PcmResult close_result{PcmResult::Ok};
};

void test_complete_lifecycle() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{44100U, 2U, 256U};
    require(broker.open(1U, SourceId::LocalMedia, RouteType::PrimaryPcm) == PcmResult::Ok,
            "open supported primary playback route");
    require(broker.configure(1U, config) == PcmResult::Ok, "configure stream");
    require(broker.start(1U) == PcmResult::Ok, "start configured stream");
    const std::int16_t samples[8]{};
    require(broker.write(1U, samples, 4U) == PcmResult::Ok, "write bounded PCM frames");
    require(backend.last_frames == 4U, "backend receives frame count unchanged");
    require(broker.stop(1U) == PcmResult::Ok, "stop active stream");
    require(broker.close(1U) == PcmResult::Ok, "close stopped stream");
    require(broker.size() == 0U, "closed stream releases fixed slot");
    require(backend.opens.load() == 1U && backend.configures.load() == 1U &&
            backend.starts.load() == 1U && backend.writes.load() == 1U &&
            backend.stops.load() == 1U && backend.closes.load() == 1U,
            "backend receives exactly one call per lifecycle operation");
}

void test_primary_microphone_capture() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{48000U, 1U, 240U};
    std::int16_t samples[8]{};
    require(broker.open(29U, SourceId::Microphone, RouteType::PrimaryCapturePcm,
                        PcmDirection::Capture) == PcmResult::Ok,
            "primary microphone capture opens");
    require(backend.last_direction == PcmDirection::Capture,
            "primary microphone direction reaches backend");
    require(broker.configure(29U, config) == PcmResult::Ok, "configure microphone capture");
    require(broker.start(29U) == PcmResult::Ok, "start microphone capture");
    require(broker.read(29U, samples, 4U) == PcmResult::Ok,
            "read microphone capture frames");
    require(broker.write(29U, samples, 4U) == PcmResult::InvalidState,
            "microphone capture rejects playback writes");
    require(broker.stop(29U) == PcmResult::Ok, "stop microphone capture");
    require(broker.close(29U) == PcmResult::Ok, "close microphone capture");
}

void test_capture_and_hfp_bidirectional_routes() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{16000U, 1U, 160U};
    std::int16_t samples[8]{};

    require(broker.open(30U, SourceId::FmRadio, RouteType::FmCapturePcm,
                        PcmDirection::Playback) == PcmResult::UnsupportedRoute,
            "FM capture endpoint cannot be opened as playback");
    require(broker.open(31U, SourceId::FmRadio, RouteType::FmCapturePcm,
                        PcmDirection::Capture) == PcmResult::Ok,
            "FM capture route opens in capture direction");
    require(backend.last_direction == PcmDirection::Capture,
            "capture direction reaches backend");
    require(broker.configure(31U, config) == PcmResult::Ok, "configure FM capture");
    require(broker.start(31U) == PcmResult::Ok, "start FM capture");
    require(broker.read(31U, samples, 4U) == PcmResult::Ok, "capture reads bounded frames");
    require(broker.write(31U, samples, 4U) == PcmResult::InvalidState,
            "capture stream rejects playback writes");
    require(broker.stop(31U) == PcmResult::Ok, "stop FM capture");
    require(broker.close(31U) == PcmResult::Ok, "close FM capture");

    require(broker.open(32U, SourceId::BluetoothHfp, RouteType::HfpSco,
                        PcmDirection::Playback) == PcmResult::Ok,
            "HFP downlink playback route opens");
    require(broker.open(33U, SourceId::BluetoothHfp, RouteType::HfpSco,
                        PcmDirection::Capture) == PcmResult::Ok,
            "HFP microphone uplink capture route opens independently");
    require(broker.configure(32U, config) == PcmResult::Ok &&
            broker.configure(33U, config) == PcmResult::Ok,
            "configure both HFP directions");
    require(broker.start(32U) == PcmResult::Ok &&
            broker.start(33U) == PcmResult::Ok,
            "start both HFP directions");
    require(broker.write(32U, samples, 4U) == PcmResult::Ok, "HFP downlink writes");
    require(broker.read(33U, samples, 4U) == PcmResult::Ok, "HFP uplink reads");
    require(broker.stop(32U) == PcmResult::Ok && broker.stop(33U) == PcmResult::Ok,
            "stop both HFP directions");
    require(broker.close(32U) == PcmResult::Ok && broker.close(33U) == PcmResult::Ok,
            "close both HFP directions");
}

void test_invalid_order_and_arguments() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const std::int16_t sample = 0;
    require(broker.start(9U) == PcmResult::NotOpen, "unknown handle cannot start");
    require(broker.open(2U, SourceId::FmRadio, RouteType::FmDirectI2s) ==
                PcmResult::UnsupportedRoute,
            "direct I2S is not sent to PCM backend");
    require(broker.open(3U, SourceId::LocalMedia, RouteType::PrimaryPcm) == PcmResult::Ok,
            "valid stream opens");
    require(broker.write(3U, &sample, 1U) == PcmResult::InvalidState,
            "write before start rejected");
    require(broker.configure(3U, PcmConfig{48000U, 4U, 256U}) ==
                PcmResult::InvalidArgument,
            "unproven four-channel PCM rejected");
    require(broker.configure(3U, PcmConfig{48000U, 2U, 128U}) == PcmResult::Ok,
            "valid configuration accepted");
    require(broker.start(3U) == PcmResult::Ok, "stream starts");
    require(broker.write(3U, nullptr, 1U) == PcmResult::InvalidArgument,
            "null buffer rejected");
    require(broker.write(3U, &sample, 0U) == PcmResult::InvalidArgument,
            "zero frame write rejected");
    require(broker.write(3U, &sample, kMaxPcmWriteFrames + 1U) ==
                PcmResult::BufferTooLarge,
            "oversized write rejected");
    backend.write_result = PcmResult::WouldBlock;
    require(broker.write(3U, &sample, 1U) == PcmResult::WouldBlock,
            "nonblocking backend backpressure is propagated");
    backend.write_result = PcmResult::Xrun;
    require(broker.write(3U, &sample, 1U) == PcmResult::Xrun,
            "PCM xrun is propagated without callback recovery");
    backend.write_result = PcmResult::DeviceLost;
    require(broker.write(3U, &sample, 1U) == PcmResult::DeviceLost,
            "device loss is propagated to the control plane");
    backend.write_result = PcmResult::Ok;
    require(broker.close(3U) == PcmResult::InvalidState,
            "active stream cannot be closed before stop");
    require(broker.stop(3U) == PcmResult::Ok, "active stream stops");
    require(broker.write(3U, &sample, 1U) == PcmResult::InvalidState,
            "writes are rejected after stop closes callback admission");
    require(broker.close(3U) == PcmResult::Ok, "stopped stream closes");
}

void test_stop_failure_can_be_retried() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{48000U, 2U, 240U};

    require(broker.open(41U, SourceId::LocalMedia, RouteType::PrimaryPcm) ==
                PcmResult::Ok, "retry stream opens");
    require(broker.configure(41U, config) == PcmResult::Ok,
            "retry stream configures");
    require(broker.start(41U) == PcmResult::Ok, "retry stream starts");

    backend.stop_result = PcmResult::BackendFailure;
    require(broker.stop(41U) == PcmResult::BackendFailure,
            "first stop failure is propagated");
    const std::int16_t sample = 0;
    require(broker.write(41U, &sample, 1U) == PcmResult::InvalidState,
            "failed stop keeps transfer admission closed");

    backend.stop_result = PcmResult::Ok;
    require(broker.stop(41U) == PcmResult::Ok,
            "stop can be retried after a backend failure");
    require(broker.close(41U) == PcmResult::Ok,
            "stream closes after successful stop retry");
    require(backend.stops.load() == 2U,
            "backend receives both stop attempts");
}

void test_fixed_capacity() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    for (std::uint32_t i = 1U; i <= kMaxPcmStreams; ++i) {
        require(broker.open(i, SourceId::LocalMedia, RouteType::PrimaryPcm) == PcmResult::Ok,
                "fixed stream capacity can be filled");
    }
    require(broker.size() == kMaxPcmStreams, "all fixed stream slots are occupied");
    require(broker.open(100U, SourceId::LocalMedia, RouteType::PrimaryPcm) ==
                PcmResult::CapacityExceeded,
            "capacity exhaustion is reported explicitly");
    for (std::uint32_t i = 1U; i <= kMaxPcmStreams; ++i) {
        require(broker.close(i) == PcmResult::Ok, "unconfigured stream closes");
    }
}

void test_callback_write_while_opening_another_stream() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{44100U, 2U, 256U};
    const std::int16_t samples[2]{};
    require(broker.open(11U, SourceId::LocalMedia, RouteType::PrimaryPcm) == PcmResult::Ok,
            "first stream opens");
    require(broker.configure(11U, config) == PcmResult::Ok, "first stream configures");
    require(broker.start(11U) == PcmResult::Ok, "first stream starts");

    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread callback([&]() {
        while (!done.load(std::memory_order_acquire)) {
            if (broker.write(11U, samples, 1U) != PcmResult::Ok) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    });
    while (backend.writes.load(std::memory_order_acquire) == 0U &&
           !failed.load(std::memory_order_relaxed)) std::this_thread::yield();
    require(!failed.load(std::memory_order_relaxed), "callback starts successfully");
    require(broker.open(12U, SourceId::Navigation, RouteType::PrimaryPcm) == PcmResult::Ok,
            "second stream opens while first callback is active");
    done.store(true, std::memory_order_release);
    callback.join();
    require(!failed.load(std::memory_order_relaxed),
            "first stream callback remains valid while another slot opens");
    require(backend.writes.load() > 0U, "callback executed writes during concurrent open");
    require(broker.close(12U) == PcmResult::Ok, "second unconfigured stream closes");
    require(broker.stop(11U) == PcmResult::Ok, "first stream stops after callback joins");
    require(broker.close(11U) == PcmResult::Ok, "first stream closes");
}

void test_stop_waits_for_in_flight_callback() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{44100U, 2U, 256U};
    const std::int16_t samples[2]{};
    require(broker.open(21U, SourceId::LocalMedia, RouteType::PrimaryPcm) == PcmResult::Ok,
            "stream opens for shutdown race test");
    require(broker.configure(21U, config) == PcmResult::Ok, "stream configures");
    require(broker.start(21U) == PcmResult::Ok, "stream starts");

    backend.block_writes.store(true, std::memory_order_release);
    std::atomic<PcmResult> callback_result{PcmResult::BackendFailure};
    std::thread callback([&]() {
        callback_result.store(broker.write(21U, samples, 1U), std::memory_order_release);
    });
    while (backend.writes.load(std::memory_order_acquire) == 0U) std::this_thread::yield();
    require(broker.stop(21U) == PcmResult::WouldBlock,
            "stop closes admission but does not stop backend during an active write");
    require(backend.stops.load() == 0U, "backend stop is deferred until callback exits");
    require(broker.write(21U, samples, 1U) == PcmResult::InvalidState,
            "new callback writes are rejected after admission closes");
    backend.release_blocked_write.store(true, std::memory_order_release);
    callback.join();
    require(callback_result.load(std::memory_order_acquire) == PcmResult::Ok,
            "already admitted callback completes normally");
    require(broker.stop(21U) == PcmResult::Ok,
            "stop retry succeeds after in-flight callback exits");
    require(backend.stops.load() == 1U, "backend stop runs exactly once");
    require(broker.close(21U) == PcmResult::Ok, "stream closes after callback drain");
}

void test_reopen_same_handle_while_callback_runs() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{48000U, 2U, 240U};
    const std::int16_t sample = 0;
    std::atomic<bool> finish{false};
    std::atomic<bool> failed{false};

    std::thread callback([&] {
        while (!finish.load(std::memory_order_acquire)) {
            const PcmResult result = broker.write(77U, &sample, 1U);
            if (result != PcmResult::Ok && result != PcmResult::InvalidState &&
                result != PcmResult::NotOpen) {
                failed.store(true, std::memory_order_release);
                return;
            }
        }
    });

    for (std::uint32_t iteration = 0U; iteration < 500U; ++iteration) {
        require(broker.open(77U, SourceId::LocalMedia, RouteType::PrimaryPcm) ==
                    PcmResult::Ok,
                "same public handle can be reopened after close");
        require(broker.configure(77U, config) == PcmResult::Ok,
                "reopened stream configures");
        require(broker.start(77U) == PcmResult::Ok,
                "reopened stream starts");
        PcmResult stop_result = PcmResult::WouldBlock;
        while (stop_result == PcmResult::WouldBlock) {
            stop_result = broker.stop(77U);
            if (stop_result == PcmResult::WouldBlock) std::this_thread::yield();
        }
        require(stop_result == PcmResult::Ok,
                "stop drains callbacks before backend shutdown");
        require(broker.close(77U) == PcmResult::Ok,
                "reopened stream closes cleanly");
        require(!failed.load(std::memory_order_acquire),
                "callback observes only valid lifecycle results during handle reuse");
    }

    finish.store(true, std::memory_order_release);
    callback.join();
    require(!failed.load(std::memory_order_acquire),
            "callback remains safe across repeated close and reopen cycles");
}

void test_backend_error_propagation() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    backend.configure_result = PcmResult::BackendFailure;
    require(broker.open(4U, SourceId::Navigation, RouteType::PrimaryPcm) == PcmResult::Ok,
            "open stream before injected error");
    require(broker.configure(4U, PcmConfig{48000U, 2U, 256U}) ==
                PcmResult::BackendFailure,
            "configure error propagated");
    PcmStreamRecord failed_record{};
    require(broker.get_record(4U, &failed_record) &&
            failed_record.state == PcmStreamState::Failed,
            "failed stream state recorded");
    require(broker.close(4U) == PcmResult::Ok, "failed stream can be closed");
}
}  // namespace

int main() {
    test_complete_lifecycle();
    test_primary_microphone_capture();
    test_capture_and_hfp_bidirectional_routes();
    test_invalid_order_and_arguments();
    test_fixed_capacity();
    test_callback_write_while_opening_another_stream();
    test_stop_waits_for_in_flight_callback();
    test_backend_error_propagation();
    test_reopen_same_handle_while_callback_runs();
    std::cout << "PCM stream broker tests passed\n";
    return 0;
}
