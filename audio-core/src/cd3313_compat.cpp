#include "mt8163_audio_core/cd3313_compat.hpp"

#include "mt8163_audio_core/types.hpp"

namespace mt8163_audio_core {
namespace {

bool owner_active(std::uint32_t mask, MuteOwner owner) noexcept {
    const auto index = static_cast<std::uint8_t>(owner);
    return (mask & (std::uint32_t{1U} << index)) != 0U;
}

void add_reason(std::uint32_t& reasons, std::uint8_t reason) noexcept {
    reasons |= std::uint32_t{1U} << reason;
}

}  // namespace

std::uint32_t cd3313_master_reason_mask(
    std::uint32_t audio_core_owner_mask) noexcept {
    std::uint32_t reasons = 0U;
    if (owner_active(audio_core_owner_mask, MuteOwner::AccPowerState)) {
        add_reason(reasons, 2U);
    }
    if (owner_active(audio_core_owner_mask, MuteOwner::SilenceMode)) {
        add_reason(reasons, 1U);
    }
    if (owner_active(audio_core_owner_mask, MuteOwner::SilenceTransition)) {
        add_reason(reasons, 24U);
    }
    if (owner_active(audio_core_owner_mask,
                     MuteOwner::BluetoothCallTransition)) {
        add_reason(reasons, 3U);
    }
    return reasons;
}

std::uint32_t cd3313_radio_input_reason_mask(
    std::uint32_t audio_core_owner_mask) noexcept {
    std::uint32_t reasons = 0U;
    if (owner_active(audio_core_owner_mask, MuteOwner::RadioRetune)) {
        add_reason(reasons, 1U);
    }
    if (owner_active(audio_core_owner_mask, MuteOwner::RadioScan) ||
        owner_active(audio_core_owner_mask, MuteOwner::RadioPacketMute)) {
        add_reason(reasons, 2U);
    }
    if (owner_active(audio_core_owner_mask, MuteOwner::RadioUserMute)) {
        add_reason(reasons, 4U);
    }
    if (owner_active(audio_core_owner_mask, MuteOwner::RadioStartupMute)) {
        add_reason(reasons, 16U);
    }
    return reasons;
}

}  // namespace mt8163_audio_core
