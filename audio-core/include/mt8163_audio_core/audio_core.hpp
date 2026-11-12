#pragma once

#include "mt8163_audio_core/hardware_broker.hpp"
#include "mt8163_audio_core/mute_arbiter.hpp"
#include "mt8163_audio_core/session_registry.hpp"
#include "mt8163_audio_core/snapshot_publisher.hpp"
#include "mt8163_audio_core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mt8163_audio_core {

// Immutable control-state view consumed by the audio thread. This is a
// transport snapshot only: it contains no pointers, strings or owning objects.
struct AudioCoreSnapshot {
    std::uint32_t generation{0U};
    std::uint32_t master_mute_owners{0U};
    std::array<std::uint32_t, kSourceCount> source_mute_owners{};
    AudioMode mode{AudioMode::Normal};
    std::uint8_t session_count{0U};
    std::uint8_t active_session_count{0U};
    std::uint8_t suspended_session_count{0U};
    std::uint8_t reserved{0U};
};

static_assert(std::is_trivially_copyable<AudioCoreSnapshot>::value,
              "AudioCoreSnapshot must be safe for bounded publication");

class AudioCore {
public:
    using SnapshotHandle =
        SnapshotPublisher<AudioCoreSnapshot>::ReadHandle;

    AudioCore() noexcept;
    AudioCore(const AudioCore&) = delete;
    AudioCore& operator=(const AudioCore&) = delete;

    // Control-plane API. All mutations must be serialized by one HAL control
    // context. These methods never access hardware or block on PCM activity.
    bool open_session(std::uint32_t handle, SourceId source,
                      RouteType route) noexcept;
    bool transition_session(std::uint32_t handle,
                            SessionState next) noexcept;
    bool close_session(std::uint32_t handle) noexcept;

    bool set_master_mute(MuteOwner owner, bool muted) noexcept;
    bool set_source_mute(SourceId source, MuteOwner owner,
                         bool muted) noexcept;
    void clear_mute_owner(MuteOwner owner) noexcept;

    bool set_mode(AudioMode mode) noexcept;

    // Hardware control is routed through this broker from the control context
    // only. The attached backend is non-owning and must outlive AudioCore.
    void attach_hardware_backend(HardwareBackend* backend) noexcept;
    HardwareResult dispatch_hardware_command(
        const HardwareCommand& command) noexcept;

    // A mutation attempts publication. If all inactive slots are pinned,
    // state remains committed in the control plane and snapshot_dirty()
    // becomes true. The control loop must retry publish_snapshot().
    bool publish_snapshot() noexcept;
    bool snapshot_dirty() const noexcept;

    // Audio-thread API: bounded acquisition, no mutex or allocation. If it
    // fails, the caller must keep using its previous snapshot for that block.
    SnapshotHandle try_acquire_snapshot() noexcept;

    // Control-plane inspection for tests/diagnostics only.
    const SessionRegistry& sessions() const noexcept;
    const MuteArbiter& mute_arbiter() const noexcept;
    AudioMode mode() const noexcept;

private:
    static bool valid_mode(AudioMode mode) noexcept;
    AudioCoreSnapshot make_snapshot(std::uint32_t generation) const noexcept;
    void publish_after_mutation() noexcept;

    SessionRegistry sessions_{};
    MuteArbiter mute_arbiter_{};
    HardwareCommandBroker hardware_broker_{};
    AudioMode mode_{AudioMode::Normal};
    std::uint32_t generation_{0U};
    bool snapshot_dirty_{false};
    SnapshotPublisher<AudioCoreSnapshot> snapshots_;
};

}  // namespace mt8163_audio_core
