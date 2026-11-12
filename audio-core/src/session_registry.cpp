#include "mt8163_audio_core/session_registry.hpp"
#include "mt8163_audio_core/route_policy.hpp"

namespace mt8163_audio_core {

bool SessionRegistry::valid_source(SourceId source) noexcept {
    return source != SourceId::None &&
           static_cast<std::size_t>(source) < kSourceCount;
}

bool SessionRegistry::valid_route(RouteType route) noexcept {
    return route != RouteType::None &&
           static_cast<std::uint8_t>(route) <=
               static_cast<std::uint8_t>(RouteType::HdmiPcm);
}

bool SessionRegistry::valid_transition(SessionState current,
                                       SessionState next) noexcept {
    if (current == next) return true;
    switch (current) {
        case SessionState::Requested:
            return next == SessionState::Active ||
                   next == SessionState::Suspended ||
                   next == SessionState::Stopping ||
                   next == SessionState::Failed;
        case SessionState::Active:
            return next == SessionState::Suspended ||
                   next == SessionState::Stopping ||
                   next == SessionState::Failed;
        case SessionState::Suspended:
            return next == SessionState::Active ||
                   next == SessionState::Stopping ||
                   next == SessionState::Failed;
        case SessionState::Stopping:
        case SessionState::Failed:
        case SessionState::Free:
        default:
            return false;
    }
}

bool SessionRegistry::open(std::uint32_t handle, SourceId source,
                           RouteType route) noexcept {
    if (handle == 0U || !valid_source(source) || !valid_route(route) ||
        validate_source_route(source, route) != RouteValidation::Supported ||
        find(handle) != nullptr) {
        return false;
    }
    for (auto& session : sessions_) {
        if (session.state == SessionState::Free) {
            session.handle = handle;
            session.source = source;
            session.route = route;
            session.state = SessionState::Requested;
            return true;
        }
    }
    return false;
}

bool SessionRegistry::transition(std::uint32_t handle,
                                 SessionState next) noexcept {
    for (auto& session : sessions_) {
        if (session.state != SessionState::Free && session.handle == handle) {
            if (next == SessionState::Free ||
                !valid_transition(session.state, next)) {
                return false;
            }
            session.state = next;
            return true;
        }
    }
    return false;
}

bool SessionRegistry::close(std::uint32_t handle) noexcept {
    for (auto& session : sessions_) {
        if (session.state != SessionState::Free && session.handle == handle) {
            session = SessionRecord{};
            return true;
        }
    }
    return false;
}

const SessionRecord* SessionRegistry::find(std::uint32_t handle) const noexcept {
    if (handle == 0U) return nullptr;
    for (const auto& session : sessions_) {
        if (session.state != SessionState::Free && session.handle == handle) {
            return &session;
        }
    }
    return nullptr;
}

std::size_t SessionRegistry::count(SessionState state) const noexcept {
    std::size_t result = 0U;
    for (const auto& session : sessions_) {
        if (session.state == state) ++result;
    }
    return result;
}

std::size_t SessionRegistry::size() const noexcept {
    return kMaxAudioSessions - count(SessionState::Free);
}

const std::array<SessionRecord, kMaxAudioSessions>&
SessionRegistry::records() const noexcept {
    return sessions_;
}

}  // namespace mt8163_audio_core
