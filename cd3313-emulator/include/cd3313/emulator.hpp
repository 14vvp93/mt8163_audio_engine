#pragma once
#include "cd3313/protocol.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
struct EmulatorState {
    std::optional<std::uint32_t> master_volume_step;
    std::optional<std::uint8_t> master_volume_register_value;
    std::uint32_t master_mute_reason_mask{0};
    bool initialization_sequence_applied{false};
    std::optional<std::uint32_t> master_volume_percentage;
    std::optional<std::uint32_t> volume_percentage_opaque_arg1;
    std::optional<std::array<std::uint8_t, 4>> channel_level_indices;
    std::optional<std::array<std::uint8_t, 4>> speaker_register_bytes;
    // Mirrors the driver's 16-entry cache selected by the command byte's high nibble.
    // These are cached command bytes, not a claim of a complete silicon register map.
    std::array<std::optional<std::uint8_t>, 16> command_page_cache{};
    std::optional<std::uint32_t> bass_boost_target;
    std::optional<std::uint32_t> bass_boost_opaque_arg0;
    std::optional<std::uint8_t> bass_register_value;
    std::optional<std::uint32_t> treble_target;
    std::optional<std::uint32_t> treble_opaque_arg0;
    std::optional<std::uint8_t> treble_register_value;
    std::optional<bool> loudness_enabled;
    std::optional<std::uint8_t> control_register_value;
    std::optional<bool> dsp_awake;
    // Recovered input-routing state. Channel IDs 0..6; driver sentinel -1 maps to 6.
    std::uint32_t current_input_channel{6U};
    std::uint32_t active_input_channel{6U};
    bool mixing_mode{false};
    bool hfp_active{false};
    std::array<std::uint8_t, 7> input_gain{};
    // Two-bit selector recovered from set_input_channel() and its .rodata table.
    std::optional<std::uint8_t> input_selector_code;
    // Current physical-volume command byte tracked separately from the requested target.
    std::optional<std::uint8_t> current_volume_register_value;
    // Raw output levels requested by set_input_channel(); not physical GPIO readback.
    std::optional<bool> input_gpio_396_level;
    std::optional<bool> input_gpio_398_level;
    std::array<std::uint32_t, 7> input_mute_reason_masks{};
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

enum class InputTransitionKind {
    AttenuationWrite,
    SelectorWrite,
    Gpio396Write,
    Gpio398Write,
    MasterMuteReason31,
    Delay
};

struct InputTransitionEvent {
    std::uint64_t sequence{};
    InputTransitionKind kind{};
    std::uint32_t channel{};
    std::uint32_t value{};
    std::uint32_t delay_ms{};
};

enum class RegisterWriteSource {
    ProbeGuardWrite,
    InitializationSequence,
    MasterVolumeTransition,
    BassTransition,
    TrebleTransition,
    LoudnessControl,
    InputGainControl,
    InputSelectorControl,
    InputSwitchAttenuation,
    IoctlFourChannelCommand,
    RawCommandByte
};

struct RegisterWriteEvent {
    std::uint64_t sequence{};
    RegisterWriteSource source{};
    std::uint8_t page{};
    std::uint8_t command_byte{};
    std::optional<std::uint8_t> previous_command_byte;
};

class Emulator {
public:
    ParseResult write(const std::vector<std::uint8_t>& bytes);
    // Models the separate cmd_write() path, which accepts a raw decimal byte;
    // this is deliberately not parsed as an ioctl frame.
    void write_raw_command_byte(std::uint8_t command_byte);
    // Models the successful 0x3F probe guard write and startup mute reason 31.
    void apply_recovered_probe_setup();
    // Replays the exact byte order recovered from event_callback(event=0).
    void apply_recovered_initialization_sequence();
    const std::optional<DriverCommand>& last_command() const { return last_command_; }
    std::uint64_t accepted_commands() const { return accepted_commands_; }
    std::uint64_t rejected_commands() const { return rejected_commands_; }
    const EmulatorState& state() const { return state_; }
    const std::vector<EmulatorEvent>& journal() const { return journal_; }
    const std::vector<RegisterWriteEvent>& register_journal() const {
        return register_journal_;
    }
    const std::vector<InputTransitionEvent>& input_transition_journal() const {
        return input_transition_journal_;
    }
    void reset();

private:
    void record_command_byte(std::uint8_t command_byte, RegisterWriteSource source);
    void record_input_switch(std::uint32_t channel, std::uint32_t delay_ms);
    std::optional<DriverCommand> last_command_;
    std::uint64_t accepted_commands_{0};
    std::uint64_t rejected_commands_{0};
    std::uint64_t next_sequence_{1};
    std::uint64_t next_register_sequence_{1};
    std::uint64_t next_input_transition_sequence_{1};
    EmulatorState state_{};
    std::vector<EmulatorEvent> journal_;
    std::vector<RegisterWriteEvent> register_journal_;
    std::vector<InputTransitionEvent> input_transition_journal_;
};
} // namespace cd3313
