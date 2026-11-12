#include "mt8163_audio_core/endpoint_pcm_backend.hpp"

#include <cstdlib>
#include <iostream>

using namespace mt8163_audio_core;

namespace {
struct FakeDevice {
    std::uint32_t next_handle{10U};
    std::uint32_t last_card{0U};
    std::uint32_t last_device{0U};
    PcmDirection last_direction{PcmDirection::Playback};
    std::uint32_t opens{0U}, configures{0U}, starts{0U};
    std::uint32_t writes{0U}, reads{0U}, stops{0U}, closes{0U};
    PcmConfig last_config{};
    PcmResult next_write_result{PcmResult::Ok};
    EndpointPcmBackend* backend{nullptr};
    PcmBackend::StreamToken reenter_token{0U};
    bool reenter_write{false};
    PcmResult nested_write_result{PcmResult::Ok};
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

PcmResult fake_open(void* context, std::uint32_t card, std::uint32_t device,
                    PcmDirection direction, std::uint32_t,
                    std::uint32_t* handle) noexcept {
    auto& fake = *static_cast<FakeDevice*>(context);
    ++fake.opens;
    fake.last_card = card;
    fake.last_device = device;
    fake.last_direction = direction;
    *handle = fake.next_handle++;
    return PcmResult::Ok;
}
PcmResult fake_configure(void* context, std::uint32_t, const PcmConfig& config) noexcept {
    auto& fake = *static_cast<FakeDevice*>(context);
    ++fake.configures;
    fake.last_config = config;
    return PcmResult::Ok;
}
PcmResult fake_start(void* context, std::uint32_t) noexcept {
    ++static_cast<FakeDevice*>(context)->starts;
    return PcmResult::Ok;
}
PcmResult fake_write(void* context, std::uint32_t, const std::int16_t* data,
                     std::size_t frames) noexcept {
    auto& fake = *static_cast<FakeDevice*>(context);
    if (data == nullptr || frames == 0U) return PcmResult::InvalidArgument;
    ++fake.writes;
    if (fake.reenter_write) {
        fake.reenter_write = false;
        fake.nested_write_result = fake.backend->write(fake.reenter_token, data, 1U);
    }
    const PcmResult result = fake.next_write_result;
    fake.next_write_result = PcmResult::Ok;
    return result;
}
PcmResult fake_read(void* context, std::uint32_t, std::int16_t* data,
                    std::size_t frames) noexcept {
    auto& fake = *static_cast<FakeDevice*>(context);
    if (data == nullptr || frames == 0U) return PcmResult::InvalidArgument;
    ++fake.reads;
    return PcmResult::Ok;
}
PcmResult fake_stop(void* context, std::uint32_t) noexcept {
    ++static_cast<FakeDevice*>(context)->stops;
    return PcmResult::Ok;
}
PcmResult fake_close(void* context, std::uint32_t) noexcept {
    ++static_cast<FakeDevice*>(context)->closes;
    return PcmResult::Ok;
}

PcmDeviceOps make_ops(FakeDevice& fake) {
    return PcmDeviceOps{&fake, fake_open, fake_configure, fake_start,
                        fake_write, fake_read, fake_stop, fake_close};
}

void test_endpoint_mapping_and_lifecycle() {
    FakeDevice fake;
    const PcmEndpoint endpoints[] = {
        {RouteType::PrimaryPcm, PcmDirection::Playback, 2U, 0U, 0U, true},
        {RouteType::PrimaryCapturePcm, PcmDirection::Capture, 2U, 1U, 0U, true},
        {RouteType::FmCapturePcm, PcmDirection::Capture, 3U, 1U, 0U, true},
        {RouteType::HfpSco, PcmDirection::Playback, 4U, 0U, 0U, true},
        {RouteType::HfpSco, PcmDirection::Capture, 4U, 1U, 0U, true}
    };
    EndpointPcmBackend backend(endpoints, 5U, make_ops(fake));
    require(backend.valid(), "valid route/direction endpoint table accepted");

    PcmBackend::StreamToken token = 0U;
    require(backend.open(RouteType::FmCapturePcm, PcmDirection::Playback, &token) ==
                PcmResult::UnsupportedRoute,
            "FM capture cannot be opened as playback");
    require(backend.open(RouteType::PrimaryCapturePcm, PcmDirection::Capture, &token) ==
                PcmResult::Ok,
            "primary microphone capture endpoint opens");
    require(token != 0U && fake.last_card == 2U && fake.last_device == 1U &&
            fake.last_direction == PcmDirection::Capture,
            "primary capture maps to supplied card/device/direction");
    require(backend.configure(token, PcmConfig{48000U, 1U, 240U}) == PcmResult::Ok,
            "valid primary capture configuration accepted");
    require(backend.start(token) == PcmResult::Ok, "primary capture starts");
    std::int16_t samples[8]{};
    require(backend.read(token, samples, 4U) == PcmResult::Ok,
            "bounded primary capture read dispatched");
    require(backend.write(token, samples, 4U) == PcmResult::InvalidState,
            "capture endpoint rejects playback write");
    require(backend.stop(token) == PcmResult::Ok, "primary capture stops");
    require(backend.close(token) == PcmResult::Ok, "primary capture closes");
    require(backend.active_streams() == 0U && fake.closes == 1U,
            "close releases backend slot");
}

void test_fm_capture_and_hfp_directions() {
    FakeDevice fake;
    const PcmEndpoint endpoints[] = {
        {RouteType::FmCapturePcm, PcmDirection::Capture, 3U, 1U, 0U, true},
        {RouteType::HfpSco, PcmDirection::Playback, 4U, 0U, 0U, true},
        {RouteType::HfpSco, PcmDirection::Capture, 4U, 1U, 0U, true}
    };
    EndpointPcmBackend backend(endpoints, 3U, make_ops(fake));
    require(backend.valid(), "FM and HFP direction table accepted");
    PcmBackend::StreamToken fm = 0U;
    require(backend.open(RouteType::FmCapturePcm, PcmDirection::Capture, &fm) ==
                PcmResult::Ok,
            "FM capture opens");
    require(backend.configure(fm, PcmConfig{16000U, 2U, 256U}) == PcmResult::Ok &&
            backend.start(fm) == PcmResult::Ok,
            "FM capture configures and starts");
    std::int16_t samples[8]{};
    require(backend.read(fm, samples, 4U) == PcmResult::Ok, "FM capture reads");
    require(backend.stop(fm) == PcmResult::Ok && backend.close(fm) == PcmResult::Ok,
            "FM capture stops and closes");

    PcmBackend::StreamToken downlink = 0U, uplink = 0U;
    require(backend.open(RouteType::HfpSco, PcmDirection::Playback, &downlink) ==
                PcmResult::Ok,
            "HFP downlink opens");
    require(backend.open(RouteType::HfpSco, PcmDirection::Capture, &uplink) ==
                PcmResult::Ok,
            "HFP uplink opens");
    require(backend.configure(downlink, PcmConfig{16000U, 1U, 160U}) == PcmResult::Ok &&
            backend.configure(uplink, PcmConfig{16000U, 1U, 160U}) == PcmResult::Ok,
            "both HFP directions configure");
    require(backend.start(downlink) == PcmResult::Ok &&
            backend.start(uplink) == PcmResult::Ok,
            "both HFP directions start");
    require(backend.write(downlink, samples, 2U) == PcmResult::Ok, "HFP downlink writes");
    require(backend.read(uplink, samples, 2U) == PcmResult::Ok, "HFP uplink reads");
    require(backend.stop(downlink) == PcmResult::Ok &&
            backend.stop(uplink) == PcmResult::Ok,
            "both HFP directions stop");
    require(backend.close(downlink) == PcmResult::Ok &&
            backend.close(uplink) == PcmResult::Ok,
            "both HFP directions close");
}

void test_state_and_error_boundaries() {
    FakeDevice fake;
    const PcmEndpoint endpoints[] = {
        {RouteType::PrimaryPcm, PcmDirection::Playback, 0U, 0U, 0U, true}
    };
    EndpointPcmBackend backend(endpoints, 1U, make_ops(fake));
    PcmBackend::StreamToken token = 0U;
    require(backend.open(RouteType::PrimaryPcm, PcmDirection::Playback, &token) ==
                PcmResult::Ok,
            "stream opens");
    const std::int16_t sample[] = {0};
    require(backend.write(token, sample, 1U) == PcmResult::InvalidState,
            "write before start rejected");
    require(backend.configure(token, PcmConfig{48000U, 4U, 256U}) ==
                PcmResult::InvalidArgument,
            "unverified four-channel format rejected");
    require(backend.configure(token, PcmConfig{48000U, 2U, 256U}) == PcmResult::Ok,
            "valid config accepted");
    require(backend.start(token) == PcmResult::Ok, "stream starts");
    fake.backend = &backend;
    fake.reenter_token = token;
    fake.reenter_write = true;
    require(backend.write(token, sample, 1U) == PcmResult::Ok,
            "outer platform write completes");
    require(fake.nested_write_result == PcmResult::WouldBlock,
            "overlapping write on one stream is rejected without waiting");
    require(backend.write(token, sample, kMaxEndpointPcmTransferFrames + 1U) ==
                PcmResult::BufferTooLarge,
            "oversized callback rejected");
    fake.next_write_result = PcmResult::Xrun;
    require(backend.write(token, sample, 1U) == PcmResult::Xrun,
            "xrun propagated to caller");
    require(backend.write(token, sample, 1U) == PcmResult::InvalidState,
            "xrun transitions backend to failed state");
    require(backend.stop(token) == PcmResult::Ok,
            "failed stream is explicitly stopped before close");
    require(backend.close(token) == PcmResult::Ok,
            "stopped native handle can be closed");
}

void test_invalid_tables() {
    FakeDevice fake;
    const PcmEndpoint duplicate[] = {
        {RouteType::PrimaryPcm, PcmDirection::Playback, 0U, 0U, 0U, true},
        {RouteType::PrimaryPcm, PcmDirection::Playback, 1U, 0U, 0U, true}
    };
    EndpointPcmBackend invalid(duplicate, 2U, make_ops(fake));
    require(!invalid.valid(), "duplicate route/direction pair rejected");
    const PcmEndpoint non_pcm[] = {
        {RouteType::FmDirectI2s, PcmDirection::Playback, 0U, 0U, 0U, true}
    };
    EndpointPcmBackend invalid_route(non_pcm, 1U, make_ops(fake));
    require(!invalid_route.valid(), "direct I2S cannot be registered as PCM");
}
}  // namespace

int main() {
    test_endpoint_mapping_and_lifecycle();
    test_fm_capture_and_hfp_directions();
    test_state_and_error_boundaries();
    test_invalid_tables();
    std::cout << "Endpoint PCM backend tests passed\n";
    return 0;
}
