#pragma once

#include <cstdint>

namespace mt8163_audio_core {

// Convert AudioCore owner masks into the legacy CD3313 bit-index masks.
// These functions only translate state; they do not issue hardware commands.
std::uint32_t cd3313_master_reason_mask(
    std::uint32_t audio_core_owner_mask) noexcept;
std::uint32_t cd3313_radio_input_reason_mask(
    std::uint32_t audio_core_owner_mask) noexcept;

}  // namespace mt8163_audio_core
