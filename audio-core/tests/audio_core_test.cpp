#include "mt8163_audio_core/audio_core.hpp"

#include <cstdlib>
#include <iostream>

using mt8163_audio_core::AudioCore;
using mt8163_audio_core::AudioMode;
using mt8163_audio_core::HardwareBackend;
using mt8163_audio_core::HardwareCommand;
using mt8163_audio_core::HardwareCommandKind;
using mt8163_audio_core::HardwareResult;
using mt8163_audio_core::MuteOwner;
using mt8163_audio_core::RouteType;
using mt8163_audio_core::SessionState;
using mt8163_audio_core::SourceId;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_initial_snapshot() {
    AudioCore core;
    auto snapshot = core.try_acquire_snapshot();
    require(static_cast<bool>(snapshot), "initial snapshot is available");
    require(snapshot->generation == 0U, "initial generation is zero");
    require(snapshot->mode == AudioMode::Normal, "initial mode is normal");
    require(snapshot->session_count == 0U, "initial session count is zero");
    require(snapshot->master_mute_owners == 0U,
            "initial master mute is clear");
}

void test_control_state_is_published() {
    AudioCore core;
    require(core.open_session(11U, SourceId::LocalMedia,
                              RouteType::PrimaryPcm),
            "media session opens");
    require(core.transition_session(11U, SessionState::Active),
            "media session becomes active");
    require(core.set_master_mute(MuteOwner::AccPowerState, true),
            "ACC master mute is accepted");
    require(core.set_source_mute(SourceId::Navigation,
                                 MuteOwner::UserRequest, true),
            "navigation mute is accepted");
    require(core.set_mode(AudioMode::InCall), "call mode is accepted");

    auto snapshot = core.try_acquire_snapshot();
    require(static_cast<bool>(snapshot), "published snapshot is available");
    require(snapshot->generation == 5U,
            "each state mutation advances snapshot generation");
    require(snapshot->session_count == 1U, "session count is published");
    require(snapshot->active_session_count == 1U,
            "active session count is published");
    require(snapshot->sessions[0].handle == 11U,
            "session handle is published to the PCM plane");
    require(snapshot->sessions[0].source == SourceId::LocalMedia,
            "session source is published to the PCM plane");
    require(snapshot->sessions[0].route == RouteType::PrimaryPcm,
            "session route is published to the PCM plane");
    require(snapshot->sessions[0].state == SessionState::Active,
            "session lifecycle is published to the PCM plane");
    require(snapshot->master_mute_owners == (1U << 0U),
            "master mute owner mask is published");
    require(snapshot->source_mute_owners[
                static_cast<std::size_t>(SourceId::Navigation)] == (1U << 9U),
            "source mute owner mask is published");
    require(snapshot->mode == AudioMode::InCall,
            "audio mode is published");
    require(!core.snapshot_dirty(), "successful publication clears dirty flag");
}

void test_dirty_snapshot_retries_latest_committed_state() {
    AudioCore core;

    auto initial = core.try_acquire_snapshot();
    require(static_cast<bool>(initial), "initial snapshot slot is pinned");

    require(core.set_mode(AudioMode::InCall),
            "first mutation publishes to a second slot");
    auto second = core.try_acquire_snapshot();
    require(static_cast<bool>(second), "second snapshot slot is pinned");

    require(core.set_master_mute(MuteOwner::AccPowerState, true),
            "second mutation publishes to the third slot");
    auto third = core.try_acquire_snapshot();
    require(static_cast<bool>(third), "third snapshot slot is pinned");

    require(core.set_source_mute(SourceId::Navigation,
                                 MuteOwner::UserRequest, true),
            "control mutation commits when all inactive slots are pinned");
    require(core.snapshot_dirty(),
            "failed publication leaves snapshot marked dirty");

    initial.reset();
    require(core.publish_snapshot(),
            "control loop can publish after a reader releases a slot");
    require(!core.snapshot_dirty(),
            "successful retry clears dirty state");

    auto current = core.try_acquire_snapshot();
    require(static_cast<bool>(current), "retried snapshot is readable");
    require(current->generation == 3U,
            "retry publishes the latest committed generation");
    require(current->mode == AudioMode::InCall,
            "retry preserves earlier committed mode");
    require(current->master_mute_owners == (1U << 0U),
            "retry preserves earlier committed master mute");
    require(current->source_mute_owners[
                static_cast<std::size_t>(SourceId::Navigation)] == (1U << 9U),
            "retry includes the mutation that initially could not publish");
}

void test_owner_scoped_clear_and_close() {
    AudioCore core;
    require(core.set_master_mute(MuteOwner::AccPowerState, true),
            "ACC mute is set");
    require(core.set_master_mute(MuteOwner::SilenceMode, true),
            "silence mute is set");
    core.clear_mute_owner(MuteOwner::AccPowerState);
    require(core.mute_arbiter().master_muted(),
            "clearing ACC does not clear silence owner");
    require(core.open_session(22U, SourceId::BluetoothA2dp,
                              RouteType::A2dpPcm),
            "A2DP session opens");
    require(core.close_session(22U), "A2DP session closes");
    auto snapshot = core.try_acquire_snapshot();
    require(snapshot->master_mute_owners == (1U << 1U),
            "remaining mute owner is preserved in snapshot");
    require(snapshot->session_count == 0U,
            "closed session is removed from snapshot");
}

class FakeAudioHardware final : public HardwareBackend {
public:
    HardwareResult execute(const HardwareCommand& command) noexcept override {
        last = command;
        ++calls;
        return HardwareResult::Applied;
    }

    HardwareCommand last{};
    unsigned calls{0U};
};

void test_audio_core_is_single_hardware_command_owner() {
    AudioCore core;
    FakeAudioHardware backend;
    require(core.attach_hardware_backend(&backend),
            "AudioCore accepts its first hardware backend");
    const HardwareCommand command{
        HardwareCommandKind::SetMasterMute,
        SourceId::None,
        MuteOwner::AccPowerState,
        true
    };
    require(core.dispatch_hardware_command(command) == HardwareResult::Applied,
            "AudioCore dispatches semantic command through its broker");
    require(backend.calls == 1U, "one backend execution is observed");
    require(backend.last.owner == MuteOwner::AccPowerState,
            "AudioCore preserves the mute owner identity");
}

void test_invalid_mode_and_session() {
    AudioCore core;
    require(!core.set_mode(static_cast<AudioMode>(0xFFU)),
            "invalid audio mode is rejected");
    require(!core.open_session(0U, SourceId::LocalMedia,
                               RouteType::PrimaryPcm),
            "invalid session handle is rejected");
    require(core.mode() == AudioMode::Normal,
            "invalid mode does not alter state");
}

}  // namespace

int main() {
    test_initial_snapshot();
    test_control_state_is_published();
    test_dirty_snapshot_retries_latest_committed_state();
    test_owner_scoped_clear_and_close();
    test_invalid_mode_and_session();
    test_audio_core_is_single_hardware_command_owner();
    std::cout << "AudioCore coordinator tests passed\n";
    return 0;
}
