#include "mt8163_audio_core/hardware_broker.hpp"

#include <cstdlib>
#include <iostream>

using namespace mt8163_audio_core;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FakeBackend final : public HardwareBackend {
public:
    HardwareResult execute(const HardwareCommand& command) noexcept override {
        last = command;
        ++calls;
        return result;
    }

    HardwareCommand last{};
    unsigned calls{0U};
    HardwareResult result{HardwareResult::Applied};
};

void test_no_backend_and_validation() {
    HardwareCommandBroker broker;
    const HardwareCommand command{
        HardwareCommandKind::SetMasterMute,
        SourceId::None,
        MuteOwner::AccPowerState,
        true
    };
    require(broker.dispatch(command) == HardwareResult::NoBackend,
            "dispatch without backend is rejected");
    require(broker.dispatch(HardwareCommand{}) ==
                HardwareResult::InvalidCommand,
            "default source-selection command is invalid");
    require(!broker.has_backend(), "broker starts detached");
}

void test_semantic_commands_reach_backend() {
    HardwareCommandBroker broker;
    FakeBackend backend;
    require(broker.attach(&backend), "first backend attaches");
    require(broker.has_backend(), "backend is attached");
    FakeBackend second_backend;
    require(!broker.attach(&second_backend),
            "a second backend cannot take hardware ownership");
    require(!broker.attach(nullptr), "null backend is rejected");

    const HardwareCommand master{
        HardwareCommandKind::SetMasterMute,
        SourceId::None,
        MuteOwner::AccPowerState,
        true
    };
    require(broker.dispatch(master) == HardwareResult::Applied,
            "master mute reaches backend");
    require(backend.calls == 1U, "backend called exactly once");
    require(backend.last.kind == HardwareCommandKind::SetMasterMute,
            "command kind preserved");
    require(backend.last.owner == MuteOwner::AccPowerState,
            "owner identity preserved");

    const HardwareCommand source{
        HardwareCommandKind::SetSourceMute,
        SourceId::Navigation,
        MuteOwner::UserRequest,
        true
    };
    require(broker.dispatch(source) == HardwareResult::Applied,
            "source mute reaches backend");
    require(backend.calls == 2U, "second command dispatched once");
    require(backend.last.source == SourceId::Navigation,
            "source identity preserved");

    const HardwareCommand select{
        HardwareCommandKind::SelectSource,
        SourceId::FmRadio,
        MuteOwner::AccPowerState,
        false
    };
    require(broker.dispatch(select) == HardwareResult::Applied,
            "logical source selection reaches backend");
    require(backend.last.source == SourceId::FmRadio,
            "selected source preserved");
}

void test_invalid_commands_never_reach_backend() {
    HardwareCommandBroker broker;
    FakeBackend backend;
    require(broker.attach(&backend), "backend attaches");

    HardwareCommand invalid_owner{
        HardwareCommandKind::SetMasterMute,
        SourceId::None,
        MuteOwner::Count,
        true
    };
    require(broker.dispatch(invalid_owner) == HardwareResult::InvalidCommand,
            "invalid owner is rejected");
    require(backend.calls == 0U, "invalid command never reaches backend");

    HardwareCommand invalid_source{
        HardwareCommandKind::SetSourceMute,
        SourceId::Count,
        MuteOwner::UserRequest,
        true
    };
    require(broker.dispatch(invalid_source) == HardwareResult::InvalidCommand,
            "invalid source is rejected");
    require(backend.calls == 0U, "backend remains untouched");
}

void test_backend_failure_is_propagated() {
    HardwareCommandBroker broker;
    FakeBackend backend;
    backend.result = HardwareResult::TransportError;
    require(broker.attach(&backend), "backend attaches");
    const HardwareCommand command{
        HardwareCommandKind::SelectSource,
        SourceId::LocalMedia,
        MuteOwner::AccPowerState,
        false
    };
    require(broker.dispatch(command) == HardwareResult::TransportError,
            "backend transport error is propagated");
}

}  // namespace

int main() {
    test_no_backend_and_validation();
    test_semantic_commands_reach_backend();
    test_invalid_commands_never_reach_backend();
    test_backend_failure_is_propagated();
    std::cout << "Hardware broker tests passed\n";
    return 0;
}
