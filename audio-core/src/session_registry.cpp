#include "mt8163_audio_core/session_registry.hpp"
#include "mt8163_audio_core/route_policy.hpp"

#include <atomic>
#include <limits>

namespace mt8163_audio_core {
namespace {
std::atomic<std::uint32_t> g_next_registry_epoch{1U};
}

SessionRegistry::SessionRegistry() noexcept
    : registry_epoch_(allocate_registry_epoch()) {}

SessionRegistry::SessionRegistry(std::uint32_t first_sequence) noexcept
    : registry_epoch_(allocate_registry_epoch()),
      next_sequence_(first_sequence),
      sequence_exhausted_(first_sequence == 0U) {}

std::uint32_t SessionRegistry::allocate_registry_epoch() noexcept {
    std::uint32_t current = g_next_registry_epoch.load(std::memory_order_relaxed);
    for (;;) {
        if (current == 0U) return 0U;
        const std::uint32_t next =
            current == std::numeric_limits<std::uint32_t>::max()
                ? 0U : current + 1U;
        if (g_next_registry_epoch.compare_exchange_weak(
                current, next, std::memory_order_relaxed,
                std::memory_order_relaxed)) return current;
    }
}

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
                           RouteType route, SessionToken* out_token) noexcept {
    if (out_token == nullptr) return false;
    *out_token = SessionToken{};
    if (handle == 0U || registry_epoch_ == 0U || sequence_exhausted_ ||
        !valid_source(source) || !valid_route(route) ||
        validate_source_route(source, route) != RouteValidation::Supported) {
        return false;
    }
    for (const auto& session : sessions_) {
        if (session.state != SessionState::Free &&
            session.token.handle == handle) return false;
    }

    SessionRecord* free_session = nullptr;
    for (auto& session : sessions_) {
        if (session.state == SessionState::Free) {
            free_session = &session;
            break;
        }
    }
    if (free_session == nullptr) return false;

    const std::uint64_t generation =
        (static_cast<std::uint64_t>(registry_epoch_) << 32U) |
        static_cast<std::uint64_t>(next_sequence_);
    const SessionToken token{handle, generation};
    free_session->token = token;
    free_session->source = source;
    free_session->route = route;
    free_session->state = SessionState::Requested;

    if (next_sequence_ == std::numeric_limits<std::uint32_t>::max()) {
        sequence_exhausted_ = true;
    } else {
        ++next_sequence_;
    }
    *out_token = token;
    return true;
}

bool SessionRegistry::transition(SessionToken token,
                                 SessionState next) noexcept {
    for (auto& session : sessions_) {
        if (session.state != SessionState::Free &&
            session.token == token) {
            if (next == SessionState::Free ||
                !valid_transition(session.state, next)) return false;
            session.state = next;
            return true;
        }
    }
    return false;
}

bool SessionRegistry::close(SessionToken token) noexcept {
    for (auto& session : sessions_) {
        if (session.state != SessionState::Free &&
            session.token == token) {
            session = SessionRecord{};
            return true;
        }
    }
    return false;
}

const SessionRecord* SessionRegistry::find(SessionToken token) const noexcept {
    if (!token.valid()) return nullptr;
    for (const auto& session : sessions_) {
        if (session.state != SessionState::Free &&
            session.token == token) return &session;
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
