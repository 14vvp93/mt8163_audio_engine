#include "cd3313/emulator.hpp"
#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
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

constexpr std::array<std::uint8_t, 16> kRegBufSlotByCommandPage{{
    0, 0, 0, 0, 5, 5, 6, 7, 3, 3, 4, 4, 1, 1, 2, 2
}};

constexpr std::array<std::uint8_t, 7> kInputSelectorCodes{{
    2, 1, 0, 3, 0, 2, 3
}};

constexpr std::array<std::uint8_t, 16> kSpeakerAttenuationCodes{{
    0x1F, 0x1C, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10,
    0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00
}};

constexpr std::array<std::uint8_t, 8> kRecoveredInitializationBytes{{
    0x3F, 0xC0, 0xE0, 0x80, 0xA0, 0x47, 0x6F, 0x7F
}};

// cd3313_voltable_40 at .rodata offset 0x28, recovered from the ARM32 module.
constexpr std::array<std::uint8_t, 41> kMasterVolumeCodes{{
    0x3F, 0x2A, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1F,
    0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15,
    0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B,
    0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x00
}};

// cd3313_equalizer at .rodata offset 0x8C, including all 15 recovered bytes.
constexpr std::array<std::uint8_t, 15> kBassTrebleCodes{{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08
}};

std::uint32_t bass_treble_target(std::uint32_t argument) {
    const std::uint32_t decremented = argument == 0U ? 0U : argument - 1U;
    return (std::min(decremented, 14U) / 2U) + 3U;
}

std::uint8_t decode_bass_treble_index(std::uint8_t register_byte) {
    const std::uint8_t nibble = static_cast<std::uint8_t>(register_byte & 0x0FU);
    return nibble > 7U ? static_cast<std::uint8_t>(22U - nibble) : nibble;
}

void apply_active_input_register_fields(EmulatorState& state) {
    const std::uint32_t channel = state.active_input_channel;
    state.input_selector_code = kInputSelectorCodes[channel];
    // The recovered routine drives GPIO 396 low only for channel 2,
    // and GPIO 398 low only for channel 0. Electrical meaning is unknown.
    state.input_gpio_396_level = channel != 2U;
    state.input_gpio_398_level = channel != 0U;
    if (state.control_register_value.has_value()) {
        const std::uint8_t old = *state.control_register_value;
        const std::uint8_t gain = state.input_gain[channel];
        state.control_register_value = static_cast<std::uint8_t>(
            (old & static_cast<std::uint8_t>(~0x1BU)) |
            static_cast<std::uint8_t>(gain << 3U) |
            *state.input_selector_code);
    }
}

bool update_master_mute_gpio(EmulatorState& state) {
    const bool reason24 =
        (state.master_mute_reason_mask & (1U << 24U)) != 0U;
    const bool any_mute_reason = state.master_mute_reason_mask != 0U;
    const bool raw_level =
        (any_mute_reason && !state.hfp_active) || reason24 || !state.acc_on;
    state.gpio_393_level = raw_level;
    return raw_level;
}

std::uint8_t calculate_master_volume_register(std::uint32_t step,
                                              std::uint32_t percentage) {
    const std::uint32_t bounded_percentage = std::min(percentage, 100U);
    const std::uint32_t percentage_attenuation =
        (100U - bounded_percentage) / 3U;
    const std::uint32_t target =
        static_cast<std::uint32_t>(kMasterVolumeCodes[step]) +
        percentage_attenuation;
    return static_cast<std::uint8_t>(std::min(target, 63U));
}

bool apply_command(const DriverCommand& command, EmulatorState& state,
                   bool& modeled) {
    const auto& payload = command.payload;
    modeled = true;
    switch (command.id) {
    case 0x01: {
        const std::uint32_t requested = read_le32(payload, 0);
        const std::uint32_t channel =
            requested == 0xFFFFFFFFU ? 6U : requested;
        if (channel > 6U) return false;
        state.current_input_channel = channel;
        if (!state.mixing_mode) {
            state.active_input_channel = channel;
            apply_active_input_register_fields(state);
            if (state.input_mute_reason_masks[channel] != 0U)
                state.master_mute_reason_mask |= (1U << 31U);
            else
                state.master_mute_reason_mask &= ~(1U << 31U);
        }
        return true;
    }
    case 0x03: {
        const std::uint32_t channel = read_le32(payload, 0);
        const std::uint32_t gain = read_le32(payload, 4);
        if (channel > 6U || gain > 3U) return false;
        state.input_gain[channel] = static_cast<std::uint8_t>(gain);
        if (state.control_register_value.has_value()) {
            state.control_register_value = static_cast<std::uint8_t>(
                (*state.control_register_value & ~0x18U) | (gain << 3U));
        }
        return true;
    }
    case 0x04: {
        const std::uint32_t requested = read_le32(payload, 0);
        const std::uint32_t channel =
            requested == 0xFFFFFFFFU ? 6U : requested;
        const std::uint32_t mute = read_le32(payload, 4);
        const std::uint32_t reason = read_le32(payload, 8);
        if (channel > 6U || mute > 1U || reason > 31U) return false;
        const std::uint32_t mask = 1U << reason;
        if (mute != 0U) state.input_mute_reason_masks[channel] |= mask;
        else state.input_mute_reason_masks[channel] &= ~mask;
        if (channel == 1U && reason == 16U)
            state.hfp_active = mute == 0U;
        if (channel == state.active_input_channel) {
            const bool input_muted =
                state.input_mute_reason_masks[channel] != 0U;
            if (input_muted)
                state.master_mute_reason_mask |= (1U << 31U);
            else
                state.master_mute_reason_mask &= ~(1U << 31U);
            update_master_mute_gpio(state);
        }
        return true;
    }
    case 0x10: {
        const std::uint32_t step = read_le32(payload, 0);
        if (step > 40U) return false;
        state.master_volume_step = step;
        state.master_volume_register_value = calculate_master_volume_register(
            step, state.master_volume_percentage.value_or(100U));
        return true;
    }
    case 0x11: {
        const std::uint32_t mute = read_le32(payload, 0);
        const std::uint32_t reason = read_le32(payload, 4);
        if (mute > 1U || reason > 31U) return false;
        const std::uint32_t mask = 1U << reason;
        if (mute != 0U) state.master_mute_reason_mask |= mask;
        else state.master_mute_reason_mask &= ~mask;
        // event_callback treats master-mute reason 2 as the ACC state latch.
        if (reason == 2U) state.acc_on = mute == 0U;
        update_master_mute_gpio(state);
        return true;
    }
    case 0x12: {
        const std::uint32_t percentage = read_le32(payload, 0);
        if (percentage < 1U || percentage > 100U) return false;
        state.master_volume_percentage = percentage;
        state.volume_percentage_opaque_arg1 = read_le32(payload, 4);
        const std::uint32_t step = state.master_volume_step.value_or(0U);
        state.master_volume_step = step;
        state.master_volume_register_value =
            calculate_master_volume_register(step, percentage);
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
    case 0x20: {
        const std::uint32_t enabled = read_le32(payload, 0);
        if (enabled > 1U) return false;
        state.mixing_mode = enabled != 0U;
        state.active_input_channel = state.mixing_mode
            ? 1U : state.current_input_channel;
        apply_active_input_register_fields(state);
        if (state.input_mute_reason_masks[state.active_input_channel] != 0U)
            state.master_mute_reason_mask |= (1U << 31U);
        else
            state.master_mute_reason_mask &= ~(1U << 31U);
        return true;
    }
    case 0x55: {
        state.bass_boost_opaque_arg0 = read_le32(payload, 0);
        const std::uint32_t target = bass_treble_target(read_le32(payload, 4));
        state.bass_boost_target = target;
        state.bass_register_value = static_cast<std::uint8_t>(
            0x60U | kBassTrebleCodes[target]);
        return true;
    }
    case 0x56: {
        const bool enabled = read_le32(payload, 0) != 0U;
        state.loudness_enabled = enabled;
        if (state.control_register_value.has_value()) {
            std::uint8_t control = *state.control_register_value;
            if (enabled) control = static_cast<std::uint8_t>(control & ~0x04U);
            else control = static_cast<std::uint8_t>(control | 0x04U);
            state.control_register_value = control;
        }
        return true;
    }
    case 0x5C: {
        state.treble_opaque_arg0 = read_le32(payload, 0);
        const std::uint32_t target = bass_treble_target(read_le32(payload, 4));
        state.treble_target = target;
        state.treble_register_value = static_cast<std::uint8_t>(
            0x70U | kBassTrebleCodes[target]);
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

std::optional<std::string> format_driver_read_report(const EmulatorState& state) {
    if (!state.master_volume_step.has_value() ||
        state.current_input_channel >= 7U ||
        input_channel_name(static_cast<std::uint8_t>(state.current_input_channel)).empty()) {
        return std::nullopt;
    }
    for (const auto& value : state.reg_bufs_cache) {
        if (!value.has_value()) return std::nullopt;
    }

    std::ostringstream report;
    report << "Chip CD3313\\nRead reg Data:\\n";
    constexpr std::array<const char*, 8> labels{{
        "index 00xx xxxx: 0x", "index 110x xxxx: 0x",
        "index 111x xxxx: 0x", "index 100x xxxx: 0x",
        "index 101x xxxx: 0x", "index 010x xxxx: 0x",
        "index 0110 xxxx: 0x", "index 0111 xxxx: 0x"
    }};
    constexpr std::array<const char*, 8> descriptions{{
        " (master volume)\\n", " (RL volume)\\n",
        " (RR volume)\\n", " (FL volume)\\n",
        " (FR volume)\\n", " (channel)\\n",
        " (bass gain)\\n", " (treble gain)\\n"
    }};
    for (std::size_t i = 0; i < state.reg_bufs_cache.size(); ++i) {
        report << labels[i] << std::hex << std::nouppercase << std::setfill('0')
               << std::setw(2)
               << static_cast<unsigned>(*state.reg_bufs_cache[i])
               << descriptions[i];
    }

    report << std::dec << std::setfill(' ') << "master channel : "
           << input_channel_name(static_cast<std::uint8_t>(state.current_input_channel))
           << '(' << state.current_input_channel << ")\\n"
           << "master volume   : " << *state.master_volume_step << "\\n"
           << "master mute     : 0x" << std::hex << std::nouppercase
           << state.master_mute_reason_mask << "\\n"
           << std::dec << "mixing state    : " << (state.mixing_mode ? 1 : 0) << "\\n"
           << "hfp activity    : " << (state.hfp_active ? 1 : 0) << "\\n"
           << "acc activity    : " << (state.acc_on ? 1 : 0) << "\\n"
           << "loudness on     : "
           << (state.loudness_enabled.value_or(false) ? 4 : 0) << "\\n"
           << "channel status:\\n";

    for (std::size_t i = 0; i < state.input_gain.size(); ++i) {
        report << "channel " << std::right << std::setw(6)
               << input_channel_name(static_cast<std::uint8_t>(i))
               << '(' << i << ") : mute:" << std::hex << std::nouppercase
               << state.input_mute_reason_masks[i] << std::dec
               << ", gain " << static_cast<unsigned>(state.input_gain[i]) << "\\n";
    }
    return report.str();
}

void Emulator::record_input_switch(std::uint32_t channel,
                                     std::uint32_t delay_ms) {
    const auto record_event = [this, channel](InputTransitionKind kind,
                                              std::uint32_t value,
                                              std::uint32_t delay) {
        input_transition_journal_.push_back(InputTransitionEvent{
            next_input_transition_sequence_++, kind, channel, value, delay
        });
    };
    const auto record_delay = [&record_event, delay_ms]() {
        if (delay_ms != 0U)
            record_event(InputTransitionKind::Delay, 0U, delay_ms);
    };
    const auto write_attenuation = [this, &record_event](
                                       std::uint8_t value) {
        record_command_byte(value, RegisterWriteSource::InputSwitchAttenuation);
        state_.current_volume_register_value = value;
        record_event(InputTransitionKind::AttenuationWrite, value, 0U);
    };

    std::uint8_t current = state_.current_volume_register_value.value_or(
        state_.command_page_cache[0x03].has_value()
            ? static_cast<std::uint8_t>(*state_.command_page_cache[0x03] & 0x3FU)
            : 0x3FU);

    // set_input_channel() first ramps the output attenuation toward 0x3F
    // in three-code increments, sleeping for the caller-provided interval.
    while (current < 0x3FU) {
        const unsigned next = static_cast<unsigned>(current) + 3U;
        current = static_cast<std::uint8_t>(next > 0x3FU ? 0x3FU : next);
        write_attenuation(current);
        record_delay();
    }
    // The driver writes the terminal 0x3F byte even when it was already there.
    write_attenuation(0x3FU);
    record_delay();

    if (state_.control_register_value.has_value()) {
        record_command_byte(*state_.control_register_value,
                            RegisterWriteSource::InputSelectorControl);
        record_event(InputTransitionKind::SelectorWrite,
                     *state_.control_register_value, 0U);
    } else {
        record_event(InputTransitionKind::SelectorWrite,
                     state_.input_selector_code.value_or(0U), 0U);
    }

    const std::uint32_t gpio396 = state_.input_gpio_396_level.value_or(false) ? 1U : 0U;
    const std::uint32_t gpio398 = state_.input_gpio_398_level.value_or(false) ? 1U : 0U;
    record_event(InputTransitionKind::Gpio396Write, gpio396, 0U);
    record_event(InputTransitionKind::Gpio398Write, gpio398, 0U);
    const bool channel_muted = state_.input_mute_reason_masks[channel] != 0U;
    const bool master_reason31 =
        (state_.master_mute_reason_mask & (1U << 31U)) != 0U;
    record_event(InputTransitionKind::MasterMuteReason31,
                 master_reason31 ? 1U : 0U, 0U);
    const bool gpio393 = update_master_mute_gpio(state_);
    record_event(InputTransitionKind::Gpio393Write, gpio393 ? 1U : 0U, 0U);
    record_delay();

    // The original routine intentionally leaves attenuation at 0x3F for a
    // muted input unless HFP is active. Otherwise it restores the user volume.
    if (channel_muted && !state_.hfp_active) {
        record_delay();
        write_attenuation(0x3FU);
        return;
    }

    const std::uint32_t step = state_.master_volume_step.value_or(0U);
    const std::uint32_t percentage =
        state_.master_volume_percentage.value_or(100U);
    const std::uint8_t target =
        calculate_master_volume_register(step, percentage);
    current = 0x3FU;
    while (current > target) {
        current = static_cast<std::uint8_t>(
            current > static_cast<unsigned>(target) + 3U
                ? current - 3U : target);
        write_attenuation(current);
        record_delay();
    }
    // The driver performs a final register write after the stepped ramp.
    write_attenuation(target);
}

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
    if ((result.command->id == 0x10 || result.command->id == 0x12) &&
        state_.master_volume_step.has_value()) {
        std::optional<std::uint8_t> previous_volume = before.reg_bufs_cache[0];
        if (!previous_volume) previous_volume = before.current_volume_register_value;
        if (!previous_volume) previous_volume = before.master_volume_register_value;

        if (previous_volume.has_value()) {
            const std::uint32_t step = *state_.master_volume_step;
            if (step == 0U && *previous_volume != 0x3FU) {
                state_.master_mute_reason_mask |= (1U << 29U);
                update_master_mute_gpio(state_);
            } else if (step != 0U && *previous_volume == 0x3FU) {
                state_.master_mute_reason_mask &= ~(1U << 29U);
                update_master_mute_gpio(state_);
            }
        }
    }
    if ((result.command->id == 0x10 || result.command->id == 0x12) &&
        state_.master_volume_register_value.has_value()) {
        const std::uint8_t target = *state_.master_volume_register_value;
        if (before.master_volume_register_value.has_value()) {
            std::uint8_t current = static_cast<std::uint8_t>(
                *before.master_volume_register_value & 0x3FU);
            while (current != target) {
                current = static_cast<std::uint8_t>(
                    current > target ? current - 1U : current + 1U);
                record_command_byte(current, RegisterWriteSource::MasterVolumeTransition);
            }
        } else {
            record_command_byte(target, RegisterWriteSource::MasterVolumeTransition);
        }
    }
    if ((result.command->id == 0x10 || result.command->id == 0x12) &&
        state_.master_volume_register_value.has_value()) {
        state_.current_volume_register_value = state_.master_volume_register_value;
    }
    if (result.command->id == 0xF1) {
        const bool requested_awake = read_le32(result.command->payload, 0) != 0U;
        if (!requested_awake) {
            apply_recovered_shutdown();
        } else if (state_.shutdown_started) {
            // command 0xF1 uses the same wakeup routine as PM event codes 0/1.
            (void)apply_recovered_pm_event(16U, 0U);
        }
        state_.dsp_awake = !state_.shutdown_started;
    }
    if (result.command->id == 0x11) {
        const std::uint32_t mute = read_le32(result.command->payload, 0);
        const std::uint32_t reason = read_le32(result.command->payload, 4);
        if (mute != 0U && reason == 24U) {
            // event_callback() removes event 3 and reposts it for 1000 ms later.
            event_queue_requests_.push_back(EventQueueRequest{
                next_event_queue_sequence_++, EventQueueRequestSource::MasterMuteReason24,
                EventQueueRequestKind::RemoveQueuedEvent, 3U, 0U
            });
            event_queue_requests_.push_back(EventQueueRequest{
                next_event_queue_sequence_++, EventQueueRequestSource::MasterMuteReason24,
                EventQueueRequestKind::PostDelayedEvent, 3U, 1000U
            });
        }
        const bool gpio393 = state_.gpio_393_level.value_or(false);
        input_transition_journal_.push_back(InputTransitionEvent{
            next_input_transition_sequence_++, InputTransitionKind::Gpio393Write,
            state_.active_input_channel, gpio393 ? 1U : 0U, 0U
        });
    }
    if (result.command->id == 0x04) {
        const std::uint32_t requested = read_le32(result.command->payload, 0);
        const std::uint32_t channel =
            requested == 0xFFFFFFFFU ? 6U : requested;
        if (channel == before.active_input_channel) {
            // The driver calls set_master_mute() for an active-channel mute
            // update, which writes GPIO 393 even when its raw level is unchanged.
            const bool gpio393 = state_.gpio_393_level.value_or(false);
            input_transition_journal_.push_back(InputTransitionEvent{
                next_input_transition_sequence_++, InputTransitionKind::Gpio393Write,
                state_.active_input_channel, gpio393 ? 1U : 0U, 0U
            });
        }
    }
    if (result.command->id == 0x55 && state_.bass_register_value.has_value()) {
        const std::uint8_t target = *state_.bass_boost_target;
        if (before.bass_register_value.has_value()) {
            std::uint8_t current = decode_bass_treble_index(*before.bass_register_value);
            while (current != target) {
                current = static_cast<std::uint8_t>(
                    current < target ? current + 1U : current - 1U);
                record_command_byte(static_cast<std::uint8_t>(
                    0x60U | kBassTrebleCodes[current]),
                    RegisterWriteSource::BassTransition);
            }
        } else {
            record_command_byte(*state_.bass_register_value,
                                RegisterWriteSource::BassTransition);
        }
    }
    if (result.command->id == 0x5C && state_.treble_register_value.has_value()) {
        const std::uint8_t target = *state_.treble_target;
        if (before.treble_register_value.has_value()) {
            std::uint8_t current = decode_bass_treble_index(*before.treble_register_value);
            while (current != target) {
                current = static_cast<std::uint8_t>(
                    current < target ? current + 1U : current - 1U);
                record_command_byte(static_cast<std::uint8_t>(
                    0x70U | kBassTrebleCodes[current]),
                    RegisterWriteSource::TrebleTransition);
            }
        } else {
            record_command_byte(*state_.treble_register_value,
                                RegisterWriteSource::TrebleTransition);
        }
    }
    bool perform_input_switch = false;
    std::uint32_t input_switch_delay_ms = 0U;
    if (result.command->id == 0x01 && !before.mixing_mode) {
        perform_input_switch = true;
        input_switch_delay_ms = 8U;
    } else if (result.command->id == 0x20) {
        const bool requested_mixing = read_le32(result.command->payload, 0) != 0U;
        perform_input_switch = requested_mixing || before.mixing_mode;
        input_switch_delay_ms = requested_mixing ? 0U : 8U;
    }
    if (perform_input_switch)
        record_input_switch(state_.active_input_channel, input_switch_delay_ms);
    if (result.command->id == 0x03 &&
        before.control_register_value.has_value() &&
        state_.control_register_value.has_value() &&
        before.control_register_value != state_.control_register_value) {
        record_command_byte(*state_.control_register_value,
                            RegisterWriteSource::InputGainControl);
    }
    if (result.command->id == 0x56 &&
        before.control_register_value.has_value() &&
        state_.control_register_value.has_value() &&
        before.control_register_value != state_.control_register_value) {
        record_command_byte(*state_.control_register_value,
                            RegisterWriteSource::LoudnessControl);
    }
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
    const std::uint8_t cache_slot = kRegBufSlotByCommandPage[page];
    const auto previous_page_byte = state_.command_page_cache[page];
    const auto previous_cache_slot_byte = state_.reg_bufs_cache[cache_slot];
    state_.command_page_cache[page] = command_byte;
    state_.reg_bufs_cache[cache_slot] = command_byte;
    register_journal_.push_back(RegisterWriteEvent{
        next_register_sequence_++, source, page, command_byte,
        previous_page_byte, cache_slot, previous_cache_slot_byte
    });
}

void Emulator::write_raw_command_byte(std::uint8_t command_byte) {
    record_command_byte(command_byte, RegisterWriteSource::RawCommandByte);
}

RawWriteTextResult Emulator::write_raw_command_text(std::string_view text,
                                                    std::size_t file_position) {
    constexpr std::size_t kTextBufferSize = 12U;
    RawWriteTextResult result;
    result.reported_count = text.size();

    const std::size_t available =
        file_position < kTextBufferSize ? kTextBufferSize - file_position : 0U;
    result.copied_count = std::min(text.size(), available);
    result.new_file_position = file_position + result.copied_count;

    std::array<char, kTextBufferSize> buffer{};
    if (result.copied_count != 0U) {
        std::copy_n(text.data(), result.copied_count,
                    buffer.data() + file_position);
    }

    // The driver does not append a NUL after copying all 12 available bytes.
    // Avoid parsing that unterminated stack buffer in the host emulator.
    if (result.copied_count == kTextBufferSize) {
        result.unterminated_buffer = true;
        return result;
    }

    // cmd_write() passes the persistent VFS file position through unchanged.
    // A nonzero offset leaves a leading NUL in this freshly zeroed buffer.
    if (file_position != 0U || buffer[0] == '\0') return result;

    std::size_t length = 0U;
    while (length < buffer.size() && buffer[length] != '\0') ++length;
    std::string_view token(buffer.data(), length);
    if (token.empty()) return result;

    if (token.back() == '\n') token.remove_suffix(1U);
    if (token.empty()) return result;

    if (token.front() == '+') token.remove_prefix(1U);
    if (token.size() >= 2U && token[0] == '0' &&
        (token[1] == 'x' || token[1] == 'X')) {
        token.remove_prefix(2U);
    }
    if (token.empty()) return result;

    std::uint32_t value = 0U;
    for (const char ch : token) {
        std::uint32_t digit = 0U;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<std::uint32_t>(ch - 'a') + 10U;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<std::uint32_t>(ch - 'A') + 10U;
        } else {
            return result;
        }
        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 16U)
            return result;
        value = value * 16U + digit;
    }

    result.parsed = true;
    result.command_byte = static_cast<std::uint8_t>(value & 0xFFU);
    write_raw_command_byte(*result.command_byte);
    return result;
}

void Emulator::apply_recovered_probe_setup() {
    // dsp_probe retries this 0x3F write up to three times on I2C failure.
    // The offline model records one successful attempt; it does not emulate bus errors.
    record_command_byte(0x3F, RegisterWriteSource::ProbeGuardWrite);
    state_.master_volume_step = 0U;
    state_.master_volume_register_value = 0x3FU;
    state_.current_volume_register_value = 0x3FU;
    state_.master_volume_percentage = 100U;
    state_.acc_on = true;
    state_.current_input_channel = 6U;
    state_.active_input_channel = 6U;
    state_.input_selector_code = 3U;
    state_.control_register_value = 0x47U;
    state_.channel_level_indices = std::array<std::uint8_t, 4>{{15, 15, 15, 15}};
    state_.speaker_register_bytes =
        std::array<std::uint8_t, 4>{{0x80, 0xA0, 0xC0, 0xE0}};
    state_.bass_register_value = 0x6FU;
    state_.treble_register_value = 0x7FU;
    state_.loudness_enabled = false;
    state_.input_gpio_396_level = false;
    state_.input_gpio_398_level = false;
    state_.subwoofer_gpio_enabled = false;
    state_.reg_bufs_cache = {{
        0x3F, 0xC0, 0xE0, 0x80, 0xA0, 0x47, 0x6F, 0x7F
    }};
    state_.master_mute_reason_mask |= (1U << 31U);
    const bool gpio393 = update_master_mute_gpio(state_);
    input_transition_journal_.push_back(InputTransitionEvent{
        next_input_transition_sequence_++, InputTransitionKind::Gpio393Write,
        state_.active_input_channel, gpio393 ? 1U : 0U, 0U
    });
}

bool Emulator::apply_recovered_system_mute_event(std::uint32_t event) {
    std::uint32_t reason = 0U;
    if (event == 2U) reason = 30U;
    else if (event == 3U) reason = 24U;
    else return false;

    state_.master_mute_reason_mask &= ~(1U << reason);
    const bool gpio393 = update_master_mute_gpio(state_);
    input_transition_journal_.push_back(InputTransitionEvent{
        next_input_transition_sequence_++, InputTransitionKind::Gpio393Write,
        state_.active_input_channel, gpio393 ? 1U : 0U, 0U
    });
    return true;
}

void Emulator::apply_recovered_shutdown() {
    // dsp_shutdown() marks shutdown in progress before changing mute state.
    state_.shutdown_started = true;
    state_.dsp_awake = false;
    state_.master_mute_reason_mask |= (1U << 30U);

    // Unlike set_master_mute(), shutdown computes GPIO 393 directly:
    // (!hfp_active || !acc_on || reason24_is_set).
    const bool gpio393 = !state_.hfp_active || !state_.acc_on ||
                         (state_.master_mute_reason_mask & (1U << 24U)) != 0U;
    state_.gpio_393_level = gpio393;
    shutdown_journal_.push_back(ShutdownEvent{
        next_shutdown_sequence_++, ShutdownEventKind::Gpio393Write,
        gpio393 ? 1U : 0U
    });

    // The driver requests removal of event IDs 0 and 1, in this order.
    shutdown_journal_.push_back(ShutdownEvent{
        next_shutdown_sequence_++, ShutdownEventKind::RemoveQueuedEvent, 0U
    });
    shutdown_journal_.push_back(ShutdownEvent{
        next_shutdown_sequence_++, ShutdownEventKind::RemoveQueuedEvent, 1U
    });

    // dsp_shutdown() sends its stack-local final byte (0x3F) over I2C.
    record_command_byte(0x3FU, RegisterWriteSource::ShutdownFinalWrite);
    shutdown_journal_.push_back(ShutdownEvent{
        next_shutdown_sequence_++, ShutdownEventKind::FinalI2cWrite, 0x3FU
    });
}

int Emulator::apply_recovered_pm_event(std::uint32_t action,
                                         std::uint32_t event_code) {
    // pm_event_notify() ignores every action except the recovered PM action 16.
    if (action != 16U) return 0;
    if (event_code > 4U) return -22;

    if (event_code == 2U || event_code == 3U || event_code == 4U) {
        apply_recovered_shutdown();
        return 0;
    }

    // Event codes 0/1 only wake the device when dsp_shutdown() marked it asleep.
    if (!state_.shutdown_started) return 0;

    // Restore the cached master-volume byte. This is a cache update, not an
    // immediate I2C transfer; the calculation matches set_master_volume().
    const std::uint32_t step = state_.master_volume_step.value_or(0U);
    const std::uint32_t percentage =
        state_.master_volume_percentage.value_or(100U);
    const std::uint8_t restored =
        calculate_master_volume_register(step, percentage);
    state_.shutdown_started = false;
    state_.dsp_awake = true;
    state_.current_volume_register_value = restored;
    state_.master_volume_register_value = restored;
    state_.command_page_cache[(restored >> 4U) & 0x0FU] = restored;

    const auto record = [this](WakeupEventKind kind, std::uint32_t event_id,
                               std::uint32_t delay_ms) {
        wakeup_journal_.push_back(WakeupEvent{
            next_wakeup_sequence_++, kind, event_id, delay_ms
        });
    };

    // Preserve the exact call order from dsp_wakeup.part.1().
    record(WakeupEventKind::RemoveQueuedEvent, 0U, 0U);
    record(WakeupEventKind::PostDelayedEvent, 0U, 50U);
    record(WakeupEventKind::RemoveQueuedEvent, 2U, 0U);
    record(WakeupEventKind::PostDelayedEvent, 2U, 1200U);
    record(WakeupEventKind::PostDelayedEvent, 2U, 1500U);
    return 0;
}

void Emulator::apply_recovered_initialization_sequence() {
    for (unsigned pass = 0; pass < 2U; ++pass) {
        for (const auto command_byte : kRecoveredInitializationBytes)
            record_command_byte(command_byte, RegisterWriteSource::InitializationSequence);
    }
    // event_callback(event=0) writes the control byte at offset +17 once more.
    record_command_byte(0x47, RegisterWriteSource::InitializationSequence);

    state_.channel_level_indices = std::array<std::uint8_t, 4>{{15, 15, 15, 15}};
    state_.speaker_register_bytes = std::array<std::uint8_t, 4>{{
        0x80, 0xA0, 0xC0, 0xE0
    }};
    state_.bass_register_value = 0x6FU;
    state_.treble_register_value = 0x7FU;
    state_.control_register_value = 0x47U;
    state_.current_volume_register_value = 0x3FU;
    state_.loudness_enabled = false;
    state_.initialization_sequence_applied = true;
}

void Emulator::reset() {
    last_command_.reset();
    accepted_commands_ = 0;
    rejected_commands_ = 0;
    next_sequence_ = 1;
    next_register_sequence_ = 1;
    next_input_transition_sequence_ = 1;
    next_shutdown_sequence_ = 1;
    next_wakeup_sequence_ = 1;
    next_event_queue_sequence_ = 1;
    state_ = EmulatorState{};
    journal_.clear();
    register_journal_.clear();
    input_transition_journal_.clear();
    shutdown_journal_.clear();
    wakeup_journal_.clear();
    event_queue_requests_.clear();
}
} // namespace cd3313
