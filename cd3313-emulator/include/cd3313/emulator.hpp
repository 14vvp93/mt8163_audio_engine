#pragma once
#include "cd3313/protocol.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
struct EmulatorState {
    std::optional<std::uint32_t> master_volume_step;
    std::uint32_t master_mute_reason_mask{0};
    std::optional<std::uint32_t> master_volume_percentage;
    std::optional<std::uint32_t> volume_percentage_opaque_arg1;
    std::optional<std::array<std::uint8_t, 4>> channel_level_indices;
    std::optional<std::array<std::uint8_t, 4>> speaker_register_bytes;
    std::optional<std::uint32_t> bass_boost_target;
    std::optional<std::uint32_t> bass_boost_opaque_arg0;
    std::optional<std::uint32_t> treble_target;
    std::optional<std::uint32_t> treble_opaque_arg0;
    std::optional<bool> loudness_enabled;
    std::optional<bool> dsp_awake;
    // Requested GPIO state only; physical routing is not inferred.
    std::optional<bool> subwoofer_gpio_enabled;
    bool master_muted() const { return master_mute_reason_mask != 0U; }
};

struct EmulatorEvent {
    std::uint64_t sequence{};
    std::uint8_t command_id{};
    bool modeled{};
    DriverCommand command;
    EmulatorState before;
    EmulatorState after;
};

class Emulator {
public:
    ParseResult write(const std::vector<std::uint8_t>& bytes);
    const std::optional<DriverCommand>& last_command() const { return last_command_; }
    std::uint64_t accepted_commands() const { return accepted_commands_; }
    std::uint64_t rejected_commands() const { return rejected_commands_; }
    const EmulatorState& state() const { return state_; }
    const std::vector<EmulatorEvent>& journal() const { return journal_; }
    void reset();

private:
    std::optional<DriverCommand> last_command_;
    std::uint64_t accepted_commands_{0};
    std::uint64_t rejected_commands_{0};
    std::uint64_t next_sequence_{1};
    EmulatorState state_{};
    std::vector<EmulatorEvent> journal_;
};
} // namespace cd3313
