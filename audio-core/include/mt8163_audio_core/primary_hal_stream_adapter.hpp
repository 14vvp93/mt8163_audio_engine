#pragma once

#include "mt8163_audio_core/audio_core.hpp"
#include "mt8163_audio_core/pcm_stream_broker.hpp"

#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

class PrimaryHalStreamAdapter final {
public:
    explicit PrimaryHalStreamAdapter(PcmBackend& backend) noexcept;
    PrimaryHalStreamAdapter(const PrimaryHalStreamAdapter&) = delete;
    PrimaryHalStreamAdapter& operator=(const PrimaryHalStreamAdapter&) = delete;

    PcmResult open(std::uint32_t handle, SourceId source, RouteType route,
                   PcmDirection direction) noexcept;
    PcmResult configure(std::uint32_t handle, const PcmConfig& config) noexcept;
    PcmResult start(std::uint32_t handle) noexcept;
    PcmResult write(std::uint32_t handle, const std::int16_t* samples,
                    std::size_t frames) noexcept;
    PcmResult read(std::uint32_t handle, std::int16_t* samples,
                   std::size_t frames) noexcept;
    PcmTransferResult write_transfer(std::uint32_t handle,
                                     const std::int16_t* samples,
                                     std::size_t frames) noexcept;
    PcmTransferResult read_transfer(std::uint32_t handle, std::int16_t* samples,
                                    std::size_t frames) noexcept;
    PcmResult stop(std::uint32_t handle) noexcept;
    PcmResult close(std::uint32_t handle) noexcept;

    AudioCore& audio_core() noexcept { return audio_core_; }
    const AudioCore& audio_core() const noexcept { return audio_core_; }
    std::size_t stream_count() const noexcept { return streams_.size(); }

private:
    AudioCore audio_core_{};
    PcmStreamBroker streams_;
};

}  // namespace mt8163_audio_core
