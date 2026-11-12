#include "mt8163_audio_core/session_registry.hpp"

#include <cstdlib>
#include <iostream>

using mt8163_audio_core::RouteType;
using mt8163_audio_core::SessionRegistry;
using mt8163_audio_core::SessionState;
using mt8163_audio_core::SourceId;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_session_lifecycle() {
    SessionRegistry sessions;
    require(sessions.open(10U, SourceId::LocalMedia, RouteType::PrimaryPcm),
            "local media session opens");
    const auto* session = sessions.find(10U);
    require(session != nullptr && session->state == SessionState::Requested,
            "new session starts requested");
    require(sessions.transition(10U, SessionState::Active),
            "requested session becomes active");
    require(sessions.transition(10U, SessionState::Suspended),
            "active session can suspend");
    require(sessions.transition(10U, SessionState::Active),
            "suspended session can resume");
    require(sessions.transition(10U, SessionState::Stopping),
            "active session can stop");
    require(sessions.close(10U), "stopping session closes");
    require(sessions.find(10U) == nullptr && sessions.size() == 0U,
            "closed session releases its slot");
}

void test_duplicate_and_invalid_inputs() {
    SessionRegistry sessions;
    require(!sessions.open(0U, SourceId::LocalMedia, RouteType::PrimaryPcm),
            "zero handle is rejected");
    require(!sessions.open(1U, SourceId::None, RouteType::PrimaryPcm),
            "NONE source is rejected");
    require(!sessions.open(1U, SourceId::LocalMedia, RouteType::None),
            "NONE route is rejected");
    require(sessions.open(1U, SourceId::LocalMedia, RouteType::PrimaryPcm),
            "valid session opens");
    require(!sessions.open(1U, SourceId::Navigation, RouteType::PrimaryPcm),
            "duplicate handle is rejected");
    require(!sessions.transition(1U, SessionState::Free),
            "caller cannot transition a live session to Free");
    require(sessions.transition(1U, SessionState::Failed),
            "requested session may enter failed state");
    require(!sessions.transition(1U, SessionState::Active),
            "failed session is terminal until close");
}

void test_capacity_and_multiple_sources() {
    SessionRegistry sessions;
    for (std::uint32_t i = 1U; i <= mt8163_audio_core::kMaxAudioSessions; ++i) {
        const SourceId source = (i % 2U == 0U)
            ? SourceId::Navigation : SourceId::LocalMedia;
        require(sessions.open(i, source, RouteType::PrimaryPcm),
                "session slot opens up to fixed capacity");
    }
    require(sessions.size() == mt8163_audio_core::kMaxAudioSessions,
            "registry reports full capacity");
    require(!sessions.open(100U, SourceId::Notification, RouteType::PrimaryPcm),
            "registry rejects overflow without allocation");
    require(sessions.count(SessionState::Requested) ==
                mt8163_audio_core::kMaxAudioSessions,
            "all sessions remain requested");
    require(sessions.close(5U), "one session slot can be released");
    require(sessions.open(100U, SourceId::Notification, RouteType::PrimaryPcm),
            "released slot can be reused");
}

}  // namespace

int main() {
    test_session_lifecycle();
    test_duplicate_and_invalid_inputs();
    test_capacity_and_multiple_sources();
    std::cout << "AudioCore session registry tests passed\n";
    return 0;
}
