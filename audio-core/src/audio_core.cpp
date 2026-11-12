#include "mt8163_audio_core/audio_core.hpp"

namespace mt8163_audio_core {

AudioCore::AudioCore() noexcept
    : snapshots_(make_snapshot(0U)) {}

bool AudioCore::open_session(std::uint32_t handle, SourceId source,
                             RouteType route) noexcept {
    if (!sessions_.open(handle, source, route)) return false;
    publish_after_mutation();
    return true;
}

bool AudioCore::transition_session(std::uint32_t handle,
                                   SessionState next) noexcept {
    if (!sessions_.transition(handle, next)) return false;
    publish_after_mutation();
    return true;
}

bool AudioCore::close_session(std::uint32_t handle) noexcept {
    if (!sessions_.close(handle)) return false;
    publish_after_mutation();
    return true;
}

bool AudioCore::set_master_mute(MuteOwner owner, bool muted) noexcept {
    if (!mute_arbiter_.set_master(owner, muted)) return false;
    publish_after_mutation();
    return true;
}

bool AudioCore::set_source_mute(SourceId source, MuteOwner owner,
                                bool muted) noexcept {
    if (!mute_arbiter_.set_source(source, owner, muted)) return false;
    publish_after_mutation();
    return true;
}

void AudioCore::clear_mute_owner(MuteOwner owner) noexcept {
    mute_arbiter_.clear_owner(owner);
    publish_after_mutation();
}

bool AudioCore::set_mode(AudioMode mode) noexcept {
    if (!valid_mode(mode)) return false;
    if (mode_ == mode) return true;
    mode_ = mode;
    publish_after_mutation();
    return true;
}

bool AudioCore::publish_snapshot() noexcept {
    if (!snapshot_dirty_) return true;
    const std::uint32_t next_generation = generation_ + 1U;
    if (!snapshots_.publish(make_snapshot(next_generation))) return false;
    generation_ = next_generation;
    snapshot_dirty_ = false;
    return true;
}

bool AudioCore::snapshot_dirty() const noexcept {
    return snapshot_dirty_;
}

AudioCore::SnapshotHandle AudioCore::try_acquire_snapshot() noexcept {
    return snapshots_.try_acquire();
}

const SessionRegistry& AudioCore::sessions() const noexcept {
    return sessions_;
}

const MuteArbiter& AudioCore::mute_arbiter() const noexcept {
    return mute_arbiter_;
}

AudioMode AudioCore::mode() const noexcept {
    return mode_;
}

bool AudioCore::valid_mode(AudioMode mode) noexcept {
    switch (mode) {
        case AudioMode::Normal:
        case AudioMode::InCall:
        case AudioMode::InCommunication:
            return true;
        default:
            return false;
    }
}

AudioCoreSnapshot AudioCore::make_snapshot(
    std::uint32_t generation) const noexcept {
    AudioCoreSnapshot result{};
    result.generation = generation;
    result.master_mute_owners = mute_arbiter_.master_owner_mask();
    for (std::size_t i = 1U; i < kSourceCount; ++i) {
        const auto source = static_cast<SourceId>(i);
        result.source_mute_owners[i] =
            mute_arbiter_.source_owner_mask(source);
    }
    result.mode = mode_;
    result.session_count = static_cast<std::uint8_t>(sessions_.size());
    result.active_session_count = static_cast<std::uint8_t>(
        sessions_.count(SessionState::Active));
    result.suspended_session_count = static_cast<std::uint8_t>(
        sessions_.count(SessionState::Suspended));
    return result;
}

void AudioCore::publish_after_mutation() noexcept {
    snapshot_dirty_ = true;
    (void)publish_snapshot();
}

}  // namespace mt8163_audio_core
