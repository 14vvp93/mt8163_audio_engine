#include "mt8163_audio_core/session_registry.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

using mt8163_audio_core::RouteType;
using mt8163_audio_core::SessionRegistry;
using mt8163_audio_core::SessionState;
using mt8163_audio_core::SessionToken;
using mt8163_audio_core::SourceId;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

SessionToken open_session(SessionRegistry& sessions, std::uint32_t handle,
                          SourceId source = SourceId::LocalMedia) {
    SessionToken token{};
    require(sessions.open(handle, source, RouteType::PrimaryPcm, &token),
            "valid session opens and returns token");
    require(token.valid() && token.handle == handle,
            "returned token contains handle and nonzero generation");
    return token;
}

void test_session_lifecycle() {
    SessionRegistry sessions;
    const SessionToken token = open_session(sessions, 10U);
    const auto* session = sessions.find(token);
    require(session != nullptr && session->state == SessionState::Requested,
            "new session starts requested");
    require(sessions.transition(token, SessionState::Active),
            "requested session becomes active");
    require(sessions.transition(token, SessionState::Suspended),
            "active session can suspend");
    require(sessions.transition(token, SessionState::Active),
            "suspended session can resume");
    require(sessions.transition(token, SessionState::Stopping),
            "active session can stop");
    require(sessions.close(token), "stopping session closes");
    require(sessions.find(token) == nullptr && sessions.size() == 0U,
            "closed session releases its slot");
}

void test_duplicate_and_invalid_inputs() {
    SessionRegistry sessions;
    SessionToken token{};
    require(!sessions.open(0U, SourceId::LocalMedia, RouteType::PrimaryPcm, &token),
            "zero handle is rejected");
    require(!sessions.open(1U, SourceId::None, RouteType::PrimaryPcm, &token),
            "NONE source is rejected");
    require(!sessions.open(1U, SourceId::LocalMedia, RouteType::None, &token),
            "NONE route is rejected");
    require(!sessions.open(1U, SourceId::BluetoothA2dp, RouteType::PrimaryPcm, &token),
            "A2DP cannot claim the primary PCM route");
    require(!sessions.open(1U, SourceId::Aux, RouteType::PrimaryPcm, &token),
            "analog AUX cannot claim a PCM route without evidence");
    const SessionToken live = open_session(sessions, 1U);
    require(!sessions.open(1U, SourceId::Navigation, RouteType::PrimaryPcm, &token),
            "duplicate handle is rejected");
    require(!sessions.transition(live, SessionState::Free),
            "caller cannot transition a live session to Free");
    require(sessions.transition(live, SessionState::Failed),
            "requested session may enter failed state");
    require(!sessions.transition(live, SessionState::Active),
            "failed session is terminal until close");
    require(!sessions.find(SessionToken{}), "default token is invalid");
    require(!sessions.close(SessionToken{live.handle, live.generation + 1U}),
            "mismatched generation cannot close a live session");
}

void test_capacity_and_multiple_sources() {
    SessionRegistry sessions;
    SessionToken tokens[mt8163_audio_core::kMaxAudioSessions]{};
    for (std::uint32_t i = 1U; i <= mt8163_audio_core::kMaxAudioSessions; ++i) {
        const SourceId source = (i % 2U == 0U)
            ? SourceId::Navigation : SourceId::LocalMedia;
        require(sessions.open(i, source, RouteType::PrimaryPcm, &tokens[i - 1U]),
                "session slot opens up to fixed capacity");
    }
    require(sessions.size() == mt8163_audio_core::kMaxAudioSessions,
            "registry reports full capacity");
    SessionToken overflow{};
    require(!sessions.open(100U, SourceId::Notification, RouteType::PrimaryPcm,
                           &overflow),
            "registry rejects overflow without allocation");
    require(sessions.count(SessionState::Requested) ==
                mt8163_audio_core::kMaxAudioSessions,
            "all sessions remain requested");
    require(sessions.close(tokens[4U]), "one session slot can be released");
    const SessionToken replacement = open_session(
        sessions, 100U, SourceId::Notification);
    require(!sessions.transition(tokens[4U], SessionState::Active),
            "stale token cannot transition a reused slot");
    require(!sessions.close(tokens[4U]),
            "stale token cannot close a replacement session");
    require(sessions.find(replacement) != nullptr,
            "replacement session remains live");
}

void test_same_handle_reopen_rejects_stale_token() {
    SessionRegistry sessions;
    const SessionToken old_token = open_session(sessions, 0x8163U);
    require(sessions.close(old_token), "old session closes");
    const SessionToken new_token = open_session(sessions, 0x8163U);
    require(old_token.handle == new_token.handle &&
            old_token.generation != new_token.generation,
            "same handle receives a distinct generation");
    require(sessions.find(old_token) == nullptr,
            "stale token cannot find replacement");
    require(!sessions.transition(old_token, SessionState::Failed),
            "stale token cannot mutate replacement");
    require(!sessions.close(old_token),
            "stale token cannot close replacement");
    require(sessions.find(new_token) != nullptr &&
            sessions.find(new_token)->state == SessionState::Requested,
            "replacement remains unchanged");
}

void test_foreign_registry_token_and_generation_exhaustion() {
    SessionRegistry first;
    SessionRegistry second;
    const SessionToken token = open_session(first, 7U);
    require(second.find(token) == nullptr,
            "token from another registry cannot identify a session");

    SessionRegistry last_generation(
        std::numeric_limits<std::uint64_t>::max());
    const SessionToken last = open_session(last_generation, 8U);
    require(last.generation == std::numeric_limits<std::uint64_t>::max(),
            "maximum generation may be issued exactly once");
    SessionToken exhausted{};
    require(!last_generation.open(9U, SourceId::LocalMedia,
                                  RouteType::PrimaryPcm, &exhausted),
            "generation exhaustion fails closed");
    require(!exhausted.valid(),
            "failed open does not return a usable token");
}

}  // namespace

int main() {
    test_session_lifecycle();
    test_duplicate_and_invalid_inputs();
    test_capacity_and_multiple_sources();
    test_same_handle_reopen_rejects_stale_token();
    test_foreign_registry_token_and_generation_exhaustion();
    std::cout << "AudioCore session registry tests passed\n";
    return 0;
}
