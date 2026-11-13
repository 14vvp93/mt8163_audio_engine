#include "audio_core/audio_state.hpp"

#include <cmath>
#include <limits>

namespace audio_core {
namespace {

constexpr std::uint32_t kKnownMuteReasons =
    static_cast<std::uint32_t>(MuteReason::User) |
    static_cast<std::uint32_t>(MuteReason::System) |
    static_cast<std::uint32_t>(MuteReason::Call) |
    static_cast<std::uint32_t>(MuteReason::Reverse) |
    static_cast<std::uint32_t>(MuteReason::SourceTransition);

} // namespace

AudioStateSnapshot AudioState::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool AudioState::valid_source(AudioSource source) noexcept {
    switch (source) {
        case AudioSource::None:
        case AudioSource::Music:
        case AudioSource::BluetoothA2dp:
        case AudioSource::FmRadio:
        case AudioSource::Aux:
            return true;
        default:
            return false;
    }
}

bool AudioState::valid_route(OutputRoute route) noexcept {
    switch (route) {
        case OutputRoute::None:
        case OutputRoute::Speaker:
        case OutputRoute::WiredHeadset:
        case OutputRoute::WiredHeadphones:
            return true;
        default:
            return false;
    }
}

bool AudioState::valid_mute_reason(MuteReason reason) noexcept {
    const auto value = static_cast<std::uint32_t>(reason);
    return value != 0U && (value & (value - 1U)) == 0U &&
           (value & kKnownMuteReasons) == value;
}

bool AudioState::advance_generation_locked() noexcept {
    if (state_.generation == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    ++state_.generation;
    return true;
}

bool AudioState::set_source(AudioSource source) {
    if (!valid_source(source)) {
        return false;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.source == source) {
        return true;
    }
    if (!advance_generation_locked()) {
        return false;
    }
    state_.source = source;
    return true;
}

bool AudioState::set_route(OutputRoute route) {
    if (!valid_route(route)) {
        return false;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.route == route) {
        return true;
    }
    if (!advance_generation_locked()) {
        return false;
    }
    state_.route = route;
    return true;
}

bool AudioState::set_master_volume(float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
        return false;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.master_volume == volume) {
        return true;
    }
    if (!advance_generation_locked()) {
        return false;
    }
    state_.master_volume = volume;
    return true;
}

bool AudioState::set_mute(MuteReason reason, bool enabled) {
    if (!valid_mute_reason(reason)) {
        return false;
    }

    const auto bit = static_cast<std::uint32_t>(reason);
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto updated = enabled ? state_.mute_reasons | bit
                                 : state_.mute_reasons & ~bit;
    if (updated == state_.mute_reasons) {
        return true;
    }
    if (!advance_generation_locked()) {
        return false;
    }
    state_.mute_reasons = updated;
    return true;
}

bool AudioState::set_dsp_enabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.dsp_enabled == enabled) {
        return true;
    }
    if (!advance_generation_locked()) {
        return false;
    }
    state_.dsp_enabled = enabled;
    return true;
}

} // namespace audio_core
