#include "mt8163_audio_core/endpoint_pcm_backend.hpp"

#include <cstdlib>
#include <iostream>

using namespace mt8163_audio_core;

namespace {
struct FakeDevice {
    std::uint32_t next_handle{10U};
    std::uint32_t last_card{0U};
    std::uint32_t last_device{0U};
    std::uint32_t opens{0U};
    std::uint32_t configures{0U};
    std::uint32_t starts{0U};
    std::uint32_t writes{0U};
    std::uint32_t stops{0U};
    std::uint32_t closes{0U};
    PcmConfig last_config{};
    PcmResult next_write_result{PcmResult::Ok};
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

PcmResult fake_open(void* context, std::uint32_t card, std::uint32_t device,
                    std::uint32_t, std::uint32_t* handle) noexcept {
    auto& fake = *static_cast<FakeDevice*>(context);
    ++fake.opens;
    fake.last_card = card;
    fake.last_device = device;
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
    const PcmResult result = fake.next_write_result;
    fake.next_write_result = PcmResult::Ok;
    return result;
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
                        fake_write, fake_stop, fake_close};
}

void test_endpoint_mapping_and_lifecycle() {
    FakeDevice fake;
    const PcmEndpoint endpoints[] = {
        {RouteType::PrimaryPcm, 2U, 0U, 0U, true},
        {RouteType::FmCapturePcm, 3U, 1U, 0U, false}
    };
    EndpointPcmBackend backend(endpoints, 2U, make_ops(fake));
    require(backend.valid(), "valid endpoint table accepted");

    PcmBackend::StreamToken token = 0U;
    require(backend.open(RouteType::FmCapturePcm, &token) == PcmResult::UnsupportedRoute,
            "disabled endpoint cannot be opened");
    require(backend.open(RouteType::PrimaryPcm, &token) == PcmResult::Ok,
            "configured primary endpoint opens");
    require(token != 0U && fake.last_card == 2U && fake.last_device == 0U,
            "logical route maps to the supplied card/device");
    require(backend.configure(token, PcmConfig{48000U, 2U, 256U}) == PcmResult::Ok,
            "valid stream configuration accepted");
    require(backend.start(token) == PcmResult::Ok, "configured stream starts");
    const std::int16_t samples[] = {1, -1, 2, -2};
    require(backend.write(token, samples, 2U) == PcmResult::Ok,
            "bounded PCM write dispatched");
    require(backend.stop(token) == PcmResult::Ok, "started stream stops");
    require(backend.close(token) == PcmResult::Ok, "stopped stream closes");
    require(backend.active_streams() == 0U && fake.closes == 1U,
            "close releases the backend slot");
}

void test_state_and_error_boundaries() {
    FakeDevice fake;
    const PcmEndpoint endpoints[] = {{RouteType::PrimaryPcm, 0U, 0U, 0U, true}};
    EndpointPcmBackend backend(endpoints, 1U, make_ops(fake));
    PcmBackend::StreamToken token = 0U;
    require(backend.open(RouteType::PrimaryPcm, &token) == PcmResult::Ok,
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
    require(backend.write(token, sample, kMaxEndpointPcmWriteFrames + 1U) ==
                PcmResult::BufferTooLarge,
            "oversized callback rejected");
    fake.next_write_result = PcmResult::Xrun;
    require(backend.write(token, sample, 1U) == PcmResult::Xrun,
            "xrun propagated to caller");
    require(backend.write(token, sample, 1U) == PcmResult::InvalidState,
            "xrun transitions backend to failed state");
    require(backend.close(token) == PcmResult::Ok,
            "failed but stopped native handle can be closed");
}

void test_invalid_tables() {
    FakeDevice fake;
    const PcmEndpoint duplicate[] = {
        {RouteType::PrimaryPcm, 0U, 0U, 0U, true},
        {RouteType::PrimaryPcm, 1U, 0U, 0U, true}
    };
    EndpointPcmBackend invalid(duplicate, 2U, make_ops(fake));
    require(!invalid.valid(), "duplicate enabled route rejected");

    const PcmEndpoint non_pcm[] = {
        {RouteType::FmDirectI2s, 0U, 0U, 0U, true}
    };
    EndpointPcmBackend invalid_route(non_pcm, 1U, make_ops(fake));
    require(!invalid_route.valid(), "direct I2S cannot be registered as PCM");
}
}  // namespace

int main() {
    test_endpoint_mapping_and_lifecycle();
    test_state_and_error_boundaries();
    test_invalid_tables();
    std::cout << "Endpoint PCM backend tests passed\n";
    return 0;
}
