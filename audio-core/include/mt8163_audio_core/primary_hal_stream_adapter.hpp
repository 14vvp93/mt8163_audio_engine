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
                   PcmDirection direction, SessionToken* out_token) noexcept;
    PcmResult configure(SessionToken session, const PcmConfig& config) noexcept;
    PcmResult start(SessionToken session) noexcept;
    PcmResult write(SessionToken session, const std::int16_t* samples,
                    std::size_t frames) noexcept;
    PcmResult read(SessionToken session, std::int16_t* samples,
                   std::size_t frames) noexcept;
    PcmTransferResult write_transfer(SessionToken session,
                                     const std::int16_t* samples,
                                     std::size_t frames) noexcept;
    PcmTransferResult read_transfer(SessionToken session, std::int16_t* samples,
                                    std::size_t frames) noexcept;
    PcmResult stop(SessionToken session) noexcept;
    PcmResult close(SessionToken session) noexcept;

    AudioCore& audio_core() noexcept { return audio_core_; }
    const AudioCore& audio_core() const noexcept { return audio_core_; }
    std::size_t stream_count() const noexcept { return streams_.size(); }

private:
    AudioCore audio_core_{};
    PcmStreamBroker streams_;
};

}  // namespace mt8163_audio_core
