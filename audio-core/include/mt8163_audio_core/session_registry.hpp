#pragma once

#include "mt8163_audio_core/session_token.hpp"
#include "mt8163_audio_core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

constexpr std::size_t kMaxAudioSessions = 16U;

enum class SessionState : std::uint8_t {
    Free = 0,
    Requested,
    Active,
    Suspended,
    Stopping,
    Failed
};

struct SessionRecord {
    SessionToken token{};
    SourceId source{SourceId::None};
    RouteType route{RouteType::None};
    SessionState state{SessionState::Free};
};

class SessionRegistry {
public:
    SessionRegistry() noexcept;
    explicit SessionRegistry(std::uint32_t first_sequence) noexcept;

    bool open(std::uint32_t handle, SourceId source, RouteType route,
              SessionToken* out_token) noexcept;
    bool transition(SessionToken token, SessionState next) noexcept;
    bool close(SessionToken token) noexcept;

    const SessionRecord* find(SessionToken token) const noexcept;
    std::size_t count(SessionState state) const noexcept;
    std::size_t size() const noexcept;
    const std::array<SessionRecord, kMaxAudioSessions>& records() const noexcept;

private:
    static std::uint32_t allocate_registry_epoch() noexcept;
    static bool valid_source(SourceId source) noexcept;
    static bool valid_route(RouteType route) noexcept;
    static bool valid_transition(SessionState current,
                                 SessionState next) noexcept;

    std::array<SessionRecord, kMaxAudioSessions> sessions_{};
    std::uint32_t registry_epoch_{0U};
    std::uint32_t next_sequence_{1U};
    bool sequence_exhausted_{false};
};

}  // namespace mt8163_audio_core
