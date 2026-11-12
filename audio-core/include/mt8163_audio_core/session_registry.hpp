#pragma once

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
    std::uint32_t handle{0U};
    SourceId source{SourceId::None};
    RouteType route{RouteType::None};
    SessionState state{SessionState::Free};
};

// Fixed-capacity control-plane registry. It owns logical session identity and
// lifecycle only; route execution and PCM transport remain separate modules.
// Access is serialized by the AudioCore control context.
class SessionRegistry {
public:
    bool open(std::uint32_t handle, SourceId source, RouteType route) noexcept;
    bool transition(std::uint32_t handle, SessionState next) noexcept;
    bool close(std::uint32_t handle) noexcept;

    const SessionRecord* find(std::uint32_t handle) const noexcept;
    std::size_t count(SessionState state) const noexcept;
    std::size_t size() const noexcept;
    const std::array<SessionRecord, kMaxAudioSessions>& records() const noexcept;

private:
    static bool valid_source(SourceId source) noexcept;
    static bool valid_route(RouteType route) noexcept;
    static bool valid_transition(SessionState current,
                                 SessionState next) noexcept;

    std::array<SessionRecord, kMaxAudioSessions> sessions_{};
};

}  // namespace mt8163_audio_core
