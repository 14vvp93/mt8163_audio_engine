#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace mt8163_audio_core;

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

    assert(adapter.open(handle, SourceId::LocalMedia, RouteType::PrimaryPcm,
                        PcmDirection::Playback) == PcmResult::Ok);
    assert(adapter.stream_count() == 1U);
    assert(adapter.configure(handle, config) == PcmResult::Ok);
    assert(adapter.start(handle) == PcmResult::Ok);
    assert(adapter.write(handle, samples.data(), 240U) == PcmResult::Ok);
    assert(backend.writes == 240U);
    assert(adapter.stop(handle) == PcmResult::Ok);
    assert(adapter.close(handle) == PcmResult::Ok);
    assert(adapter.stream_count() == 0U);
    assert(!backend.opened);

    assert(adapter.open(handle, SourceId::LocalMedia, RouteType::A2dpPcm,
                        PcmDirection::Playback) == PcmResult::UnsupportedRoute);
    assert(adapter.open(0U, SourceId::LocalMedia, RouteType::PrimaryPcm,
                        PcmDirection::Playback) == PcmResult::InvalidArgument);
    return 0;
}
