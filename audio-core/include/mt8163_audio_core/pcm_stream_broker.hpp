#pragma once

#include "mt8163_audio_core/pcm_backend.hpp"
#include "mt8163_audio_core/route_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

constexpr std::size_t kMaxPcmStreams = 16U;
constexpr std::size_t kMaxPcmWriteFrames = 2048U;

struct PcmStreamRecord {
    std::uint32_t handle{0U};
    PcmBackend::StreamToken backend_token{0U};
    SourceId source{SourceId::None};
    RouteType route{RouteType::None};
    PcmConfig config{};
    PcmStreamState state{PcmStreamState::Closed};
};

// Fixed-capacity lifecycle adapter. The owner must serialize lifecycle calls
// and quiesce the stream callback before stop()/close(). write() is bounded
// and allocation-free; it does not acquire locks or mutate stream state.
class PcmStreamBroker {
public:
    explicit PcmStreamBroker(PcmBackend& backend) noexcept : backend_(backend) {}

    PcmResult open(std::uint32_t handle, SourceId source, RouteType route) noexcept;
    PcmResult configure(std::uint32_t handle, const PcmConfig& config) noexcept;
    PcmResult start(std::uint32_t handle) noexcept;
    PcmResult write(std::uint32_t handle, const std::int16_t* interleaved,
                    std::size_t frame_count) noexcept;
    PcmResult stop(std::uint32_t handle) noexcept;
    PcmResult close(std::uint32_t handle) noexcept;

    const PcmStreamRecord* find(std::uint32_t handle) const noexcept;
    std::size_t size() const noexcept;

private:
    PcmStreamRecord* find_mutable(std::uint32_t handle) noexcept;
    PcmBackend& backend_;
    std::array<PcmStreamRecord, kMaxPcmStreams> streams_{};
};

}  // namespace mt8163_audio_core
