#include "cd3313/emulator.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace cd3313 {
namespace {
std::uint32_t read_le32(const std::vector<std::uint8_t>& payload,
                        std::size_t offset) {
    return static_cast<std::uint32_t>(payload[offset]) |
           (static_cast<std::uint32_t>(payload[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(payload[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(payload[offset + 3U]) << 24U);
}

constexpr std::array<std::uint8_t, 16> kSpeakerAttenuationCodes{{
    0x1F, 0x1C, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10,
    0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00
}};

bool apply_command(const DriverCommand& command, EmulatorState& state,
                   bool& modeled) {
    const auto& payload = command.payload;
    modeled = true;
    switch (command.id) {
    case 0x10: {
        const std::uint32_t step = read_le32(payload, 0);
        if (step > 40U) return false;
        state.master_volume_step = step;
        return true;
    }
    case 0x11: {
        const std::uint32_t mute = read_le32(payload, 0);
        const std::uint32_t reason = read_le32(payload, 4);
        if (mute > 1U || reason > 31U) return false;
        const std::uint32_t mask = 1U << reason;
        if (mute != 0U) state.master_mute_reason_mask |= mask;
        else state.master_mute_reason_mask &= ~mask;
        return true;
    }
    case 0x12: {
        const std::uint32_t percentage = read_le32(payload, 0);
        if (percentage < 1U || percentage > 100U) return false;
        state.master_volume_percentage = percentage;
        state.volume_percentage_opaque_arg1 = read_le32(payload, 4);
        return true;
    }
    case 0x15: {
        std::array<std::uint8_t, 4> indices{};
        std::array<std::uint8_t, 4> registers{};
        constexpr std::array<std::uint8_t, 4> prefixes{{0x80, 0xA0, 0xC0, 0xE0}};
        for (std::size_t channel = 0; channel < indices.size(); ++channel) {
            const std::uint32_t index = read_le32(payload, channel * 4U);
            if (index >= kSpeakerAttenuationCodes.size()) return false;
            indices[channel] = static_cast<std::uint8_t>(index);
            registers[channel] = static_cast<std::uint8_t>(
                prefixes[channel] | kSpeakerAttenuationCodes[index]);
        }
        state.channel_level_indices = indices;
        state.speaker_register_bytes = registers;
        return true;
    }
    case 0x55: {
        state.bass_boost_opaque_arg0 = read_le32(payload, 0);
        state.bass_boost_target = std::min(read_le32(payload, 4), 14U);
        return true;
    }
    case 0x56:
        state.loudness_enabled = read_le32(payload, 0) != 0U;
        return true;
    case 0x5C: {
        state.treble_opaque_arg0 = read_le32(payload, 0);
        state.treble_target = std::min(read_le32(payload, 4), 14U);
        return true;
    }
    case 0xA3:
        state.subwoofer_gpio_enabled = read_le32(payload, 0) != 0U;
        return true;
    case 0xF1:
        state.dsp_awake = read_le32(payload, 0) != 0U;
        return true;
    default:
        modeled = false;
        return true;
    }
}
} // namespace

ParseResult Emulator::write(const std::vector<std::uint8_t>& bytes) {
    ParseResult result = parse_driver_command(bytes);
    if (!result) {
        ++rejected_commands_;
        return result;
    }
    EmulatorState before = state_;
    bool modeled = false;
    if (!apply_command(*result.command, state_, modeled)) {
        state_ = before;
        ++rejected_commands_;
        return {{}, ParseError::InvalidArgumentValue};
    }
    last_command_ = result.command;
    ++accepted_commands_;
    if (result.command->id == 0x15 && state_.speaker_register_bytes.has_value()) {
        for (const auto byte : *state_.speaker_register_bytes)
            record_command_byte(byte, RegisterWriteSource::IoctlFourChannelCommand);
    }
    journal_.push_back(EmulatorEvent{
        next_sequence_++, result.command->id, modeled, *result.command,
        std::move(before), state_
    });
    return result;
}

void Emulator::record_command_byte(std::uint8_t command_byte,
                                   RegisterWriteSource source) {
    const std::uint8_t page = static_cast<std::uint8_t>(command_byte >> 4U);
    const auto previous = state_.command_page_cache[page];
    state_.command_page_cache[page] = command_byte;
    register_journal_.push_back(RegisterWriteEvent{
        next_register_sequence_++, source, page, command_byte, previous
    });
}

void Emulator::write_raw_command_byte(std::uint8_t command_byte) {
    record_command_byte(command_byte, RegisterWriteSource::RawCommandByte);
}

void Emulator::reset() {
    last_command_.reset();
    accepted_commands_ = 0;
    rejected_commands_ = 0;
    next_sequence_ = 1;
    next_register_sequence_ = 1;
    state_ = EmulatorState{};
    journal_.clear();
    register_journal_.clear();
}
} // namespace cd3313
