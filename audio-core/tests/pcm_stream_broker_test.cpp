#include "mt8163_audio_core/pcm_stream_broker.hpp"

#include <cstdlib>
#include <iostream>

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
    PcmResult open(RouteType, StreamToken* token) noexcept override {
        if (token == nullptr) return PcmResult::InvalidArgument;
        *token = ++last_token;
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
        return data == nullptr ? PcmResult::InvalidArgument : write_result;
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
    std::size_t opens{0U}, configures{0U}, starts{0U}, writes{0U}, stops{0U}, closes{0U};
    std::size_t last_frames{0U};
    PcmResult open_result{PcmResult::Ok}, configure_result{PcmResult::Ok};
    PcmResult start_result{PcmResult::Ok}, write_result{PcmResult::Ok};
    PcmResult stop_result{PcmResult::Ok}, close_result{PcmResult::Ok};
};

void test_complete_lifecycle() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    const PcmConfig config{44100U, 2U, 256U};

    require(broker.open(1U, SourceId::LocalMedia, RouteType::PrimaryPcm) == PcmResult::Ok,
            "open supported primary PCM route");
    require(broker.configure(1U, config) == PcmResult::Ok, "configure stream");
    require(broker.start(1U) == PcmResult::Ok, "start configured stream");

    const std::int16_t samples[8]{};
    require(broker.write(1U, samples, 4U) == PcmResult::Ok, "write bounded PCM frames");
    require(backend.last_frames == 4U, "backend receives frame count unchanged");
    require(broker.stop(1U) == PcmResult::Ok, "stop active stream");
    require(broker.close(1U) == PcmResult::Ok, "close stopped stream");
    require(broker.size() == 0U, "closed stream releases fixed slot");
    require(backend.opens == 1U && backend.configures == 1U &&
            backend.starts == 1U && backend.writes == 1U &&
            backend.stops == 1U && backend.closes == 1U,
            "backend receives exactly one call per lifecycle operation");
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
    require(broker.close(3U) == PcmResult::InvalidState,
            "active stream cannot be closed before callback quiescence and stop");
    require(broker.stop(3U) == PcmResult::Ok, "active stream stops");
    require(broker.close(3U) == PcmResult::Ok, "stopped stream closes");
}

void test_fixed_capacity() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    for (std::uint32_t i = 1U; i <= kMaxPcmStreams; ++i) {
        require(broker.open(i, SourceId::LocalMedia, RouteType::PrimaryPcm) ==
                    PcmResult::Ok,
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

void test_backend_error_propagation() {
    FakePcmBackend backend;
    PcmStreamBroker broker(backend);
    backend.configure_result = PcmResult::BackendFailure;
    require(broker.open(4U, SourceId::Navigation, RouteType::PrimaryPcm) == PcmResult::Ok,
            "open stream before injected error");
    require(broker.configure(4U, PcmConfig{48000U, 2U, 256U}) ==
                PcmResult::BackendFailure,
            "configure error propagated");
    require(broker.find(4U)->state == PcmStreamState::Failed,
            "failed stream state recorded");
    require(broker.close(4U) == PcmResult::Ok, "failed stream can be closed");
}
}  // namespace

int main() {
    test_complete_lifecycle();
    test_invalid_order_and_arguments();
    test_fixed_capacity();
    test_backend_error_propagation();
    std::cout << "PCM stream broker tests passed\n";
    return 0;
}
