#include "cd3313/emulator.hpp"
#include "cd3313/channel_control.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace cd3313;
    try {
        check(known_write_commands().size() == 12,
              "expected 12 recovered command specifications");
        check(read_device_identifier_payload() ==
                  std::array<std::uint8_t, 8>{{'c', 'd', '3', '3', '1', '3', 0U, 0U}},
              "0xF0 read ioctl returns the static cd3313 identifier bytes");
        check(known_write_commands()[1].id == 0x03 &&
              known_write_commands()[1].minimum_size_words == 3,
              "0x03 minimum size field");
        check(known_write_commands()[6].id == 0x15 &&
              known_write_commands()[6].minimum_size_words == 2 &&
              known_write_commands()[6].required_size_words == 4,
              "0x15 retains driver minimum but requires four argument words");

        const DriverCommand volume{0x10, 1, {0x28, 0x00, 0x00, 0x00}};
        const auto encoded = encode_driver_command(volume);
        check(encoded == std::vector<std::uint8_t>(
                  {0x00, 0x10, 0x01, 0x40, 0x28, 0x00, 0x00, 0x00}),
              "write ioctl header and payload encoding");

        const auto typed_volume = encode_driver_command(make_master_volume_command(40));
        check(typed_volume == encoded,
              "typed master-volume builder matches the recovered wire frame");
        const auto typed_mute = encode_driver_command(make_master_mute_command(true, 24));
        check(typed_mute == std::vector<std::uint8_t>(
                  {0x00, 0x11, 0x02, 0x40, 0x01, 0, 0, 0, 0x18, 0, 0, 0}),
              "typed master-mute builder encodes mute and reason words");
        const auto typed_balance = encode_driver_command(
            make_four_channel_level_command(15, 14, 13, 12));
        check(typed_balance == std::vector<std::uint8_t>(
                  {0x00, 0x15, 0x04, 0x40,
                   0x0F, 0, 0, 0, 0x0E, 0, 0, 0,
                   0x0D, 0, 0, 0, 0x0C, 0, 0, 0}),
              "typed four-channel level builder encodes FL/FR/RL/RR indices");
        check(encode_driver_command(make_input_channel_command(-1)) ==
                  std::vector<std::uint8_t>(
                      {0x00, 0x01, 0x01, 0x40, 0xFF, 0xFF, 0xFF, 0xFF}),
              "typed input-channel builder encodes the -1 default-channel sentinel");
        check(encode_driver_command(make_input_gain_command(2, 3, 0x12345678U)) ==
                  std::vector<std::uint8_t>(
                      {0x00, 0x03, 0x03, 0x40,
                       0x02, 0, 0, 0, 0x03, 0, 0, 0,
                       0x78, 0x56, 0x34, 0x12}),
              "typed input-gain builder preserves the opaque third word");
        check(encode_driver_command(make_input_mute_command(1, true, 16)) ==
                  std::vector<std::uint8_t>(
                      {0x00, 0x04, 0x03, 0x40,
                       0x01, 0, 0, 0, 0x01, 0, 0, 0, 0x10, 0, 0, 0}),
              "typed input-mute builder encodes channel, mute, and reason");
        check(encode_driver_command(make_input_mixing_command(
                  true, 0xAABBCCDDU, 0x11223344U)) ==
                  std::vector<std::uint8_t>(
                      {0x00, 0x20, 0x03, 0x40,
                       0x01, 0, 0, 0, 0xDD, 0xCC, 0xBB, 0xAA,
                       0x44, 0x33, 0x22, 0x11}),
              "typed mixing builder retains the two opaque trailing words");
        bool rejected_volume = false;
        try { (void)make_master_volume_command(41); }
        catch (const std::invalid_argument&) { rejected_volume = true; }
        check(rejected_volume, "typed volume builder rejects steps above 40");
        bool rejected_mute_reason = false;
        try { (void)make_master_mute_command(true, 32); }
        catch (const std::invalid_argument&) { rejected_mute_reason = true; }
        check(rejected_mute_reason, "typed mute builder rejects invalid bit positions");
        bool rejected_level = false;
        try { (void)make_four_channel_level_command(15, 16, 15, 15); }
        catch (const std::invalid_argument&) { rejected_level = true; }
        check(rejected_level, "typed channel builder rejects invalid level indices");

        const auto parsed = parse_driver_command(encoded);
        check(static_cast<bool>(parsed), "0x10 driver command should parse");
        check(parsed.command->id == 0x10 && parsed.command->size_field == 1,
              "command ID and size field extraction");
        check(parsed.command->payload == volume.payload,
              "opaque argument bytes must be preserved");

        check(parse_driver_command({}).error == ParseError::TruncatedHeader,
              "empty frame is a truncated header");
        check(parse_driver_command({0x00, 0x10, 0x01}).error ==
                  ParseError::TruncatedHeader, "short header");
        check(parse_driver_command({0x00, 0x10, 0x01, 0x80, 0x28, 0, 0, 0}).error ==
                  ParseError::UnsupportedDirection, "read direction is not a write command");
        check(parse_driver_command({0x00, 0x7F, 0x01, 0x40, 0, 0, 0, 0}).error ==
                  ParseError::UnknownCommand, "unknown command");
        check(parse_driver_command({0x00, 0x03, 0x02, 0x40, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 0}).error ==
                  ParseError::SizeFieldTooSmall, "size field below driver minimum");
        check(parse_driver_command({0x00, 0x15, 0x02, 0x40,
                                    0, 0, 0, 0, 0, 0, 0, 0}).error ==
                  ParseError::InsufficientArgumentData,
              "0x15 must reject two words although the driver table accepts them");
        check(parse_driver_command({0x00, 0x15, 0x03, 0x40,
                                    0, 0, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0}).error ==
                  ParseError::InsufficientArgumentData,
              "0x15 must reject three words because the handler reads four arguments");
        const std::vector<std::uint8_t> fader_frame{
            0x00, 0x15, 0x04, 0x40,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0
        };
        check(static_cast<bool>(parse_driver_command(fader_frame)),
              "0x15 accepts four complete 32-bit channel arguments");
        auto invalid_fader_frame = fader_frame;
        invalid_fader_frame[4] = 16;
        check(parse_driver_command(invalid_fader_frame).error ==
                  ParseError::InvalidArgumentValue,
              "0x15 rejects an index outside the 16-entry speaker table");
        check(parse_driver_command({0x00, 0x10, 0x01, 0x40, 0x28, 0, 0}).error ==
                  ParseError::PayloadSizeMismatch, "truncated payload");
        check(static_cast<bool>(parse_driver_command(
                  {0x00, 0x56, 0x01, 0x40, 0, 0, 0, 0})),
              "special 0x56 dispatcher branch");
        check(static_cast<bool>(parse_driver_command(
                  {0x00, 0xA3, 0x01, 0x40, 1, 0, 0, 0})),
              "special 0xA3 dispatcher branch");


        Emulator state_emulator;
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_volume_command(23)))),
              "state emulator accepts master volume");
        check(state_emulator.state().master_volume_step == 23U &&
              state_emulator.state().master_volume_register_value == 0x11U &&
              state_emulator.register_journal().size() == 1U,
              "master volume step maps through the recovered 41-entry register table");
        const DriverCommand percentage_command{
            0x12, 2, {80, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD}};
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(percentage_command))),
              "master-volume percentage command is accepted");
        check(state_emulator.state().master_volume_percentage == 80U &&
              state_emulator.state().volume_percentage_opaque_arg1 == 0xDDCCBBAAU &&
              state_emulator.state().master_volume_register_value == 0x17U &&
              state_emulator.register_journal().size() == 7U &&
              state_emulator.register_journal().back().command_byte == 0x17U,
              "volume percentage adds the recovered attenuation offset and ramps register bytes");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 4)))),
              "first mute reason is accepted");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 9)))),
              "second mute reason is accepted");
        check(state_emulator.state().master_muted() &&
              state_emulator.state().master_mute_reason_mask ==
                  ((1U << 4U) | (1U << 9U)),
              "independent mute reasons are accumulated");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(false, 4)))),
              "clearing one mute reason is accepted");
        check(state_emulator.state().master_muted() &&
              state_emulator.state().master_mute_reason_mask == (1U << 9U),
              "clearing one reason preserves another active mute");

        check(state_emulator.state().gpio_393_level == true &&
              state_emulator.state().acc_on == false,
              "GPIO 393 remains high while ACC state is off");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 2)))) &&
              state_emulator.state().acc_on &&
              state_emulator.state().gpio_393_level == true,
              "master mute reason 2 latches ACC on and keeps active mute asserted");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(false, 9)))) &&
              state_emulator.state().gpio_393_level == false,
              "clearing the final ordinary mute releases GPIO 393 while ACC is on");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 24)))) &&
              state_emulator.state().gpio_393_level == true,
              "master mute reason 24 independently forces GPIO 393 high");
        check(state_emulator.event_queue_requests().size() == 2U &&
              state_emulator.event_queue_requests()[0].kind == EventQueueRequestKind::RemoveQueuedEvent &&
              state_emulator.event_queue_requests()[0].event_id == 3U &&
              state_emulator.event_queue_requests()[1].kind == EventQueueRequestKind::PostDelayedEvent &&
              state_emulator.event_queue_requests()[1].event_id == 3U &&
              state_emulator.event_queue_requests()[1].delay_ms == 1000U,
              "asserting master mute reason 24 replaces event 3 with a 1000 ms delayed clear");
        check(state_emulator.apply_recovered_system_mute_event(3U) &&
              (state_emulator.state().master_mute_reason_mask & (1U << 24U)) == 0U &&
              state_emulator.state().gpio_393_level == false,
              "system notifier event 3 clears reason 24 and recomputes GPIO 393");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 30)))) &&
              (state_emulator.state().master_mute_reason_mask & (1U << 30U)) != 0U,
              "reason 30 can be asserted before the system notifier");
        check(state_emulator.apply_recovered_system_mute_event(2U) &&
              (state_emulator.state().master_mute_reason_mask & (1U << 30U)) == 0U,
              "system notifier event 2 clears reason 30");
        check(!state_emulator.apply_recovered_system_mute_event(1U),
              "unmapped system notifier events are rejected");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(false, 2)))) &&
              !state_emulator.state().acc_on &&
              state_emulator.state().gpio_393_level == true,
              "ACC-off latch forces GPIO 393 high even after ordinary mute clears");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 2)))) &&
              state_emulator.state().acc_on,
              "ACC-on latch can be restored through reason 2");
        check(static_cast<bool>(state_emulator.write(encode_driver_command(
                  make_four_channel_level_command(15, 14, 13, 0)))),
              "four-channel state command is accepted");
        const auto& channel_state = state_emulator.state();
        check(channel_state.channel_level_indices.has_value() &&
              (*channel_state.channel_level_indices ==
                  std::array<std::uint8_t, 4>{{15, 14, 13, 0}}),
              "speaker indices are retained in FL/FR/RL/RR order");
        check(channel_state.speaker_register_bytes.has_value() &&
              (*channel_state.speaker_register_bytes ==
                  std::array<std::uint8_t, 4>{{0x80, 0xA2, 0xC4, 0xFF}}),
              "speaker register image uses recovered prefixes and table");

        check(channel_state.command_page_cache[0x8].has_value() &&
              *channel_state.command_page_cache[0x8] == 0x80 &&
              channel_state.command_page_cache[0xA].has_value() &&
              *channel_state.command_page_cache[0xA] == 0xA2 &&
              channel_state.command_page_cache[0xC].has_value() &&
              *channel_state.command_page_cache[0xC] == 0xC4 &&
              channel_state.command_page_cache[0xF].has_value() &&
              *channel_state.command_page_cache[0xF] == 0xFF,
              "command-page cache tracks the four confirmed speaker command bytes");
        check(!channel_state.command_page_cache[0x0].has_value() &&
              state_emulator.register_journal().size() == 11U,
              "unobserved command pages remain unknown and four writes are journaled");
        check(state_emulator.register_journal()[7].sequence == 8U &&
              state_emulator.register_journal()[7].source ==
                  RegisterWriteSource::IoctlFourChannelCommand &&
              state_emulator.register_journal()[7].command_byte == 0x80,
              "first speaker write has a deterministic journal record");

        state_emulator.write_raw_command_byte(0x6A);
        check(state_emulator.state().command_page_cache[0x6].has_value() &&
              *state_emulator.state().command_page_cache[0x6] == 0x6A,
              "raw command-byte path updates the matching command page");
        check(state_emulator.register_journal().size() == 12U &&
              state_emulator.register_journal().back().source ==
                  RegisterWriteSource::RawCommandByte &&
              !state_emulator.register_journal().back().previous_command_byte.has_value(),
              "raw register command is journaled separately from ioctl commands");
        state_emulator.write_raw_command_byte(0x6B);
        check(state_emulator.register_journal().back().previous_command_byte == 0x6A &&
              *state_emulator.state().command_page_cache[0x6] == 0x6B,
              "register journal preserves the previous cached byte on overwrite");

        Emulator selector_emulator;
        selector_emulator.apply_recovered_initialization_sequence();
        constexpr std::array<std::uint8_t, 7> expected_selector_codes{{
            2, 1, 0, 3, 0, 2, 3
        }};
        for (std::uint32_t channel = 0; channel < expected_selector_codes.size(); ++channel) {
            check(static_cast<bool>(selector_emulator.write(encode_driver_command(
                      make_input_channel_command(static_cast<std::int32_t>(channel))))),
                  "input selector accepts each recovered channel ID");
            check(selector_emulator.state().input_selector_code ==
                      expected_selector_codes[channel] &&
                  selector_emulator.state().control_register_value.has_value() &&
                  ((*selector_emulator.state().control_register_value & 0x03U) ==
                      expected_selector_codes[channel]),
                  "input channel maps to the recovered two-bit control field");
            check(selector_emulator.state().input_gpio_396_level == (channel != 2U) &&
                  selector_emulator.state().input_gpio_398_level == (channel != 0U),
                  "input channel reproduces the two recovered raw GPIO output levels");
        }
        check(selector_emulator.register_journal().size() > 18U &&
              selector_emulator.register_journal()[18].source ==
                  RegisterWriteSource::InputSelectorControl,
              "input selector byte is journaled between attenuation writes");
        const auto& first_switch = selector_emulator.input_transition_journal();
        check(first_switch.size() >= 9U &&
              first_switch[0].kind == InputTransitionKind::AttenuationWrite &&
              first_switch[0].value == 0x3FU &&
              first_switch[1].kind == InputTransitionKind::Delay &&
              first_switch[1].delay_ms == 8U &&
              first_switch[2].kind == InputTransitionKind::SelectorWrite &&
              first_switch[3].kind == InputTransitionKind::Gpio396Write &&
              first_switch[4].kind == InputTransitionKind::Gpio398Write &&
              first_switch[5].kind == InputTransitionKind::MasterMuteReason31 &&
              first_switch[6].kind == InputTransitionKind::Gpio393Write &&
              first_switch[7].kind == InputTransitionKind::Delay,
              "input switch journal preserves the recovered attenuation-selector-GPIO-mute order");

        Emulator ramp_emulator;
        ramp_emulator.apply_recovered_initialization_sequence();
        check(static_cast<bool>(ramp_emulator.write(encode_driver_command(
                  make_master_volume_command(23)))),
              "input switch ramp fixture sets a non-muted user volume");
        check(ramp_emulator.state().current_volume_register_value == 0x11U,
              "current hardware volume is tracked separately from requested volume");
        check(static_cast<bool>(ramp_emulator.write(encode_driver_command(
                  make_input_channel_command(2)))),
              "input switch runs with a non-default volume");
        const auto& ramp_events = ramp_emulator.input_transition_journal();
        check(ramp_events.front().kind == InputTransitionKind::AttenuationWrite &&
              ramp_events.front().value == 0x14U &&
              ramp_events.front().delay_ms == 0U,
              "input switch starts the three-code attenuation ramp from the actual volume");
        check(ramp_events.back().kind == InputTransitionKind::AttenuationWrite &&
              ramp_events.back().value == 0x11U &&
              ramp_emulator.state().current_volume_register_value == 0x11U,
              "input switch restores the requested volume after selector and GPIO changes");

        Emulator muted_switch_emulator;
        muted_switch_emulator.apply_recovered_initialization_sequence();
        check(static_cast<bool>(muted_switch_emulator.write(encode_driver_command(
                  make_input_mute_command(3, true, 4)))),
              "muted input fixture accepts an independent mute reason");
        check(static_cast<bool>(muted_switch_emulator.write(encode_driver_command(
                  make_input_channel_command(3)))),
              "switching to a muted input is accepted");
        check(muted_switch_emulator.state().current_volume_register_value == 0x3FU &&
              muted_switch_emulator.input_transition_journal().back().kind ==
                  InputTransitionKind::AttenuationWrite &&
              muted_switch_emulator.input_transition_journal().back().value == 0x3FU,
              "a muted non-HFP input remains at maximum attenuation");
        check(static_cast<bool>(selector_emulator.write(encode_driver_command(
                  make_input_channel_command(-1)))) &&
              selector_emulator.state().input_selector_code == 3U,
              "default channel sentinel selects selector code 3");
        check(static_cast<bool>(selector_emulator.write(encode_driver_command(
                  make_input_channel_command(5)))) &&
              selector_emulator.state().input_selector_code == 2U,
              "channel 5 selector code is restored before mixing");
        const auto before_mixing_enable_events =
            selector_emulator.input_transition_journal().size();
        check(static_cast<bool>(selector_emulator.write(encode_driver_command(
                  make_input_mixing_command(true)))) &&
              selector_emulator.state().active_input_channel == 1U &&
              selector_emulator.state().input_selector_code == 1U,
              "mixing mode switches selector to channel 1");
        const auto after_mixing_enable_events =
            selector_emulator.input_transition_journal().size();
        check(after_mixing_enable_events > before_mixing_enable_events,
              "mixing enable records an input-switch transition");
        for (std::size_t i = before_mixing_enable_events;
             i < after_mixing_enable_events; ++i) {
            check(selector_emulator.input_transition_journal()[i].kind !=
                      InputTransitionKind::Delay,
                  "mixing enable uses the recovered zero-millisecond delay");
        }
        check(static_cast<bool>(selector_emulator.write(encode_driver_command(
                  make_input_channel_command(2)))) &&
              selector_emulator.state().current_input_channel == 2U &&
              selector_emulator.state().active_input_channel == 1U &&
              selector_emulator.state().input_selector_code == 1U &&
              selector_emulator.input_transition_journal().size() ==
                  after_mixing_enable_events,
              "channel selection during mixing is remembered without switching active selector");
        check(static_cast<bool>(selector_emulator.write(encode_driver_command(
                  make_input_mixing_command(false)))) &&
              selector_emulator.state().active_input_channel == 2U &&
              selector_emulator.state().input_selector_code == 0U,
              "leaving mixing restores remembered channel selector");

        const auto make_two_word_command = [](std::uint8_t id,
                                               std::uint32_t first,
                                               std::uint32_t second) {
            DriverCommand command{id, 2, {}};
            const std::uint32_t words[] = {first, second};
            for (const auto word : words) {
                for (unsigned shift = 0; shift < 32; shift += 8)
                    command.payload.push_back(
                        static_cast<std::uint8_t>((word >> shift) & 0xFFU));
            }
            return encode_driver_command(command);
        };
        check(static_cast<bool>(state_emulator.write(
                  make_two_word_command(0x55, 0x12345678U, 18U))),
              "bass command is accepted");
        check(state_emulator.state().bass_boost_target == 10U &&
              state_emulator.state().bass_boost_opaque_arg0 == 0x12345678U &&
              state_emulator.state().bass_register_value == 0x6CU,
              "bass target is bounded and opaque first argument is preserved");
        check(static_cast<bool>(state_emulator.write(
                  make_two_word_command(0x5C, 0x87654321U, 7U))),
              "treble command is accepted");
        check(state_emulator.state().treble_target == 6U &&
              state_emulator.state().treble_opaque_arg0 == 0x87654321U &&
              state_emulator.state().treble_register_value == 0x76U,
              "treble target and opaque first argument are retained");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(DriverCommand{0x56, 1, {1, 0, 0, 0}}))),
              "loudness command is accepted");
        check(state_emulator.state().loudness_enabled == true,
              "loudness state is modeled");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(DriverCommand{0xA3, 1, {1, 0, 0, 0}}))),
              "subwoofer GPIO request is accepted");
        check(state_emulator.state().subwoofer_gpio_enabled == true,
              "subwoofer GPIO request state is modeled without claiming physical routing");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(DriverCommand{0xF1, 1, {1, 0, 0, 0}}))),
              "DSP power request is accepted");
        check(state_emulator.state().dsp_awake == true,
              "DSP awake request state is modeled");
        check(state_emulator.journal().size() == 17U &&
              state_emulator.journal().front().sequence == 1U &&
              state_emulator.journal().back().sequence == 17U,
              "journal sequence IDs are deterministic and monotonic");
        for (std::size_t i = 0; i < state_emulator.journal().size(); ++i) {
            check(state_emulator.journal()[i].sequence == i + 1U,
                  "journal sequence IDs are contiguous and strictly increasing");
        }
        check(!state_emulator.journal().front().before.master_volume_step.has_value() &&
              state_emulator.journal().front().after.master_volume_step == 23U,
              "journal captures before/after snapshots");

        const auto before_invalid = state_emulator.state();
        check(state_emulator.write(encode_driver_command(
                  DriverCommand{0x10, 1, {41, 0, 0, 0}})).error ==
                  ParseError::InvalidArgumentValue,
              "state model rejects an out-of-range raw volume command");
        check(state_emulator.state().master_volume_step ==
                  before_invalid.master_volume_step &&
              state_emulator.journal().size() == 17U &&
              state_emulator.journal().back().sequence == 17U,
              "rejected transition is atomic and is not journaled");

        Emulator unknown_command_emulator;
        check(static_cast<bool>(unknown_command_emulator.write(
                  encode_driver_command(DriverCommand{0xF0, 1, {0, 0, 0, 0}}))),
              "recognized command with unresolved semantics is retained");
        check(unknown_command_emulator.journal().size() == 1U &&
              !unknown_command_emulator.journal().front().modeled &&
              unknown_command_emulator.journal().front().command.id == 0xF0,
              "unresolved semantics remain visible as an unmodeled journal event");

        state_emulator.reset();
        check(state_emulator.journal().empty() &&
              state_emulator.accepted_commands() == 0U &&
              state_emulator.state().master_mute_reason_mask == 0U &&
              !state_emulator.state().acc_on &&
              !state_emulator.state().gpio_393_level.has_value() &&
              !state_emulator.state().master_volume_step.has_value(),
              "reset clears journal and restores synthetic baseline");

        const auto neutral = calculate_channel_levels({0, 0});
        check(neutral.front_left == 15 && neutral.front_right == 15 &&
              neutral.rear_left == 15 && neutral.rear_right == 15,
              "centered controls produce 0 dB on all channels");
        check(speaker_register_byte(0, 15) == 0x80 &&
              speaker_register_byte(1, 15) == 0xA0 &&
              speaker_register_byte(2, 15) == 0xC0 &&
              speaker_register_byte(3, 15) == 0xE0,
              "neutral register bytes");
        check(speaker_register_byte(0, 0) == 0x9F &&
              speaker_register_byte(3, 0) == 0xFF,
              "maximum attenuation register bytes");
        const auto right = calculate_channel_levels({100, 0});
        check(right.front_left == 0 && right.rear_left == 0 &&
              right.front_right == 15 && right.rear_right == 15,
              "right balance attenuates left channels");
        const auto front = calculate_channel_levels({0, 100});
        check(front.front_left == 15 && front.front_right == 15 &&
              front.rear_left == 0 && front.rear_right == 0,
              "front fader attenuates rear channels");
        const auto rear = calculate_channel_levels({0, -100});
        check(rear.front_left == 0 && rear.front_right == 0 &&
              rear.rear_left == 15 && rear.rear_right == 15,
              "rear fader attenuates front channels");
        const auto corner = calculate_channel_levels({100, 100});
        check(corner.front_left == 0 && corner.front_right == 15 &&
              corner.rear_left == 0 && corner.rear_right == 0,
              "combined controls compose per channel");
        const auto clamped = calculate_channel_levels({250, -250});
        check(clamped.front_left == 0 && clamped.front_right == 0 &&
              clamped.rear_left == 0 && clamped.rear_right == 15,
              "out-of-range slider values are clamped");

        const auto rear_compensation = calculate_channel_levels({0, 0, 100});
        check(rear_compensation.front_left == 10 &&
              rear_compensation.front_right == 10 &&
              rear_compensation.rear_left == 15 &&
              rear_compensation.rear_right == 15,
              "rear compensation attenuates front pair by approximately 12 dB");

        const auto no_rear_compensation = calculate_channel_levels({0, 0, 0});
        check(no_rear_compensation.front_left == 15 &&
              no_rear_compensation.front_right == 15 &&
              no_rear_compensation.rear_left == 15 &&
              no_rear_compensation.rear_right == 15,
              "zero rear compensation preserves neutral output");

        const auto rear_compensation_clamped =
            calculate_channel_levels({0, 0, 250});
        check(rear_compensation_clamped.front_left == 10 &&
              rear_compensation_clamped.front_right == 10 &&
              rear_compensation_clamped.rear_left == 15 &&
              rear_compensation_clamped.rear_right == 15,
              "rear compensation clamps at 100 percent");

        Emulator emulator;
        check(static_cast<bool>(emulator.write(encoded)),
              "emulator accepts a valid driver command");
        check(!emulator.write({0x00, 0x03, 0x02, 0x40, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0}),
              "emulator rejects a size-field violation");
        check(emulator.accepted_commands() == 1, "accepted command counter");
        check(emulator.rejected_commands() == 1, "rejected command counter");
        check(emulator.last_command()->id == 0x10,
              "last valid command retained");
        emulator.reset();
        check(!emulator.last_command() && emulator.accepted_commands() == 0,
              "reset clears emulator state");

        state_emulator.reset();
        check(state_emulator.journal().empty() &&
              state_emulator.register_journal().empty() &&
              state_emulator.shutdown_journal().empty() &&
              state_emulator.wakeup_journal().empty() &&
              state_emulator.event_queue_requests().empty() &&
              !state_emulator.state().command_page_cache[0x6].has_value(),
              "reset clears command-page cache and all transition journals");
        state_emulator.write_raw_command_byte(0x6C);
        check(state_emulator.register_journal().size() == 1U &&
              state_emulator.register_journal().front().sequence == 1U,
              "reset restarts register-journal sequence numbering");
        state_emulator.reset();

        Emulator input_emulator;
        input_emulator.apply_recovered_initialization_sequence();
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(make_master_mute_command(true, 2)))) &&
              input_emulator.state().acc_on,
              "input mute fixture enables the recovered ACC latch");
        const DriverCommand input_gain_command{
            0x03, 3, {
                2, 0, 0, 0, 3, 0, 0, 0,
                0xBE, 0xBA, 0xFE, 0xCA
            }};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(input_gain_command))),
              "input-gain command with recovered three-word minimum is accepted");
        check(input_emulator.state().input_gain[2] == 3U &&
              input_emulator.state().control_register_value == 0x5FU &&
              input_emulator.register_journal().size() == 18U &&
              input_emulator.register_journal().back().source ==
                  RegisterWriteSource::InputGainControl,
              "input gain is cached per channel and updates bits 3..4 of control byte");
        const auto input_journal_before_invalid = input_emulator.journal().size();
        check(!input_emulator.write(encode_driver_command(DriverCommand{
                  0x03, 3, {7, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}})) &&
              input_emulator.journal().size() == input_journal_before_invalid,
              "input gain rejects channel IDs outside the recovered 0..6 range atomically");

        const DriverCommand select_input_one{
            0x01, 1, {1, 0, 0, 0}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(select_input_one))) &&
              input_emulator.state().current_input_channel == 1U &&
              input_emulator.state().active_input_channel == 1U,
              "input-select command updates the selected and active channel");
        const DriverCommand mute_input_one{
            0x04, 3, {
                1, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0
            }};
        const auto gpio_journal_before_hfp_mute =
            input_emulator.input_transition_journal().size();
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(mute_input_one))) &&
              input_emulator.state().input_mute_reason_masks[1] == (1U << 16U) &&
              input_emulator.state().hfp_active &&
              input_emulator.state().gpio_393_level == false &&
              (input_emulator.state().master_mute_reason_mask & (1U << 31U)) != 0U,
              "HFP suppresses the ordinary GPIO 393 mute request while ACC is on");
        check(input_emulator.input_transition_journal().size() ==
                  gpio_journal_before_hfp_mute + 1U &&
              input_emulator.input_transition_journal().back().kind ==
                  InputTransitionKind::Gpio393Write &&
              input_emulator.input_transition_journal().back().value == 0U,
              "active-channel HFP mute records the GPIO 393 write even when the raw level stays low");
        const DriverCommand add_input_mute_reason{
            0x04, 3, {1, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(add_input_mute_reason))),
              "independent input mute reason is accepted");
        const DriverCommand clear_hfp_mute{
            0x04, 3, {1, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(clear_hfp_mute))) &&
              !input_emulator.state().hfp_active &&
              input_emulator.state().input_mute_reason_masks[1] == (1U << 3U) &&
              input_emulator.state().gpio_393_level == true &&
              (input_emulator.state().master_mute_reason_mask & (1U << 31U)) != 0U,
              "clearing HFP restores GPIO 393 mute when another input-mute reason remains");
        const DriverCommand clear_last_input_mute{
            0x04, 3, {1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(clear_last_input_mute))) &&
              input_emulator.state().input_mute_reason_masks[1] == 0U &&
              (input_emulator.state().master_mute_reason_mask & (1U << 31U)) == 0U,
              "clearing the final active-channel input mute releases master mute reason 31");

        const DriverCommand enable_mixing{
            0x20, 3, {1, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(enable_mixing))) &&
              input_emulator.state().mixing_mode &&
              input_emulator.state().active_input_channel == 1U,
              "mixing mode selects the recovered channel-1 input path");
        const DriverCommand select_input_three{
            0x01, 1, {3, 0, 0, 0}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(select_input_three))) &&
              input_emulator.state().current_input_channel == 3U &&
              input_emulator.state().active_input_channel == 1U,
              "channel selection is remembered without replacing the active mixing path");
        const DriverCommand disable_mixing{
            0x20, 3, {0, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(disable_mixing))) &&
              !input_emulator.state().mixing_mode &&
              input_emulator.state().active_input_channel == 3U,
              "disabling mixing restores the remembered input channel");
        const DriverCommand select_default_channel{
            0x01, 1, {0xFF, 0xFF, 0xFF, 0xFF}};
        check(static_cast<bool>(input_emulator.write(
                  encode_driver_command(select_default_channel))) &&
              input_emulator.state().current_input_channel == 6U &&
              input_emulator.state().active_input_channel == 6U,
              "driver channel sentinel -1 maps to channel 6");
        const auto input_event_count = input_emulator.journal().size();
        check(!input_emulator.write(encode_driver_command(DriverCommand{
                  0x20, 3, {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}})) &&
              input_emulator.journal().size() == input_event_count,
              "mixing command rejects non-boolean values without changing the journal");

        Emulator startup_emulator;
        startup_emulator.apply_recovered_probe_setup();
        check(startup_emulator.state().master_mute_reason_mask == (1U << 31U) &&
              startup_emulator.state().acc_on &&
              startup_emulator.state().gpio_393_level == true &&
              startup_emulator.input_transition_journal().size() == 1U &&
              startup_emulator.input_transition_journal().front().kind ==
                  InputTransitionKind::Gpio393Write &&
              startup_emulator.input_transition_journal().front().value == 1U &&
              startup_emulator.register_journal().size() == 1U &&
              startup_emulator.register_journal().front().command_byte == 0x3FU &&
              startup_emulator.register_journal().front().source ==
                  RegisterWriteSource::ProbeGuardWrite,
              "probe setup records the initial 0x3F guard write and startup mute reason");
        startup_emulator.apply_recovered_initialization_sequence();
        const std::array<std::uint8_t, 8> startup_bytes{{
            0x3F, 0xC0, 0xE0, 0x80, 0xA0, 0x47, 0x6F, 0x7F
        }};
        check(startup_emulator.state().initialization_sequence_applied &&
              startup_emulator.state().master_volume_step == 0U &&
              startup_emulator.state().master_volume_register_value == 0x3FU &&
              startup_emulator.state().master_volume_percentage == 100U,
              "startup model restores recovered initial master-volume state");
        check(startup_emulator.state().channel_level_indices ==
                  std::array<std::uint8_t, 4>{{15, 15, 15, 15}} &&
              startup_emulator.state().speaker_register_bytes ==
                  std::array<std::uint8_t, 4>{{0x80, 0xA0, 0xC0, 0xE0}} &&
              startup_emulator.state().bass_register_value == 0x6FU &&
              startup_emulator.state().treble_register_value == 0x7FU &&
              startup_emulator.state().control_register_value == 0x47U &&
              startup_emulator.state().loudness_enabled == false,
              "startup model restores neutral four-channel output registers");
        check(startup_emulator.register_journal().size() == 18U,
              "probe guard plus event_callback's 8 bytes twice and final control-byte rewrite are journaled");
        for (std::size_t i = 0; i < startup_bytes.size(); ++i) {
            check(startup_emulator.register_journal()[i + 1U].command_byte == startup_bytes[i] &&
                  startup_emulator.register_journal()[i + 1U].source ==
                      RegisterWriteSource::InitializationSequence,
                  "first initialization pass matches recovered byte order");
            check(startup_emulator.register_journal()[i + 9U].command_byte == startup_bytes[i],
                  "second initialization pass repeats the recovered byte order");
        }
        check(startup_emulator.register_journal().back().command_byte == 0x47 &&
              startup_emulator.register_journal().back().sequence == 18U,
              "initialization ends by rewriting the 0x47 control byte");
        check(*startup_emulator.state().command_page_cache[0x3] == 0x3F &&
              *startup_emulator.state().command_page_cache[0xC] == 0xC0 &&
              *startup_emulator.state().command_page_cache[0xE] == 0xE0 &&
              *startup_emulator.state().command_page_cache[0x8] == 0x80 &&
              *startup_emulator.state().command_page_cache[0xA] == 0xA0 &&
              *startup_emulator.state().command_page_cache[0x4] == 0x47 &&
              *startup_emulator.state().command_page_cache[0x6] == 0x6F &&
              *startup_emulator.state().command_page_cache[0x7] == 0x7F,
              "startup image populates all eight observed command pages");
        startup_emulator.reset();
        check(!startup_emulator.state().initialization_sequence_applied &&
              startup_emulator.register_journal().empty(),
              "reset clears recovered startup image and write history");

        Emulator shutdown_default;
        shutdown_default.apply_recovered_shutdown();
        check(shutdown_default.state().shutdown_started &&
              (shutdown_default.state().master_mute_reason_mask & (1U << 30U)) != 0U &&
              shutdown_default.state().gpio_393_level == true &&
              shutdown_default.shutdown_journal().size() == 4U,
              "shutdown sets its flag and reason 30 and computes GPIO high when HFP is inactive");
        check(shutdown_default.shutdown_journal()[0].kind == ShutdownEventKind::Gpio393Write &&
              shutdown_default.shutdown_journal()[0].value == 1U &&
              shutdown_default.shutdown_journal()[1].kind == ShutdownEventKind::RemoveQueuedEvent &&
              shutdown_default.shutdown_journal()[1].value == 0U &&
              shutdown_default.shutdown_journal()[2].kind == ShutdownEventKind::RemoveQueuedEvent &&
              shutdown_default.shutdown_journal()[2].value == 1U &&
              shutdown_default.shutdown_journal()[3].kind == ShutdownEventKind::FinalI2cWrite &&
              shutdown_default.shutdown_journal()[3].value == 0x3FU &&
              shutdown_default.register_journal().size() == 1U &&
              shutdown_default.register_journal().back().source == RegisterWriteSource::ShutdownFinalWrite &&
              shutdown_default.register_journal().back().command_byte == 0x3FU,
              "shutdown journal preserves GPIO, event-removal, and final I2C write order");

        Emulator shutdown_hfp;
        check(static_cast<bool>(shutdown_hfp.write(encode_driver_command(
                  make_master_mute_command(true, 2)))),
              "shutdown HFP case enables ACC latch");
        check(static_cast<bool>(shutdown_hfp.write(encode_driver_command(DriverCommand{
                  0x04, 3, {1, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0}}))) &&
              shutdown_hfp.state().hfp_active,
              "shutdown HFP case activates HFP state");
        shutdown_hfp.apply_recovered_shutdown();
        check(shutdown_hfp.state().gpio_393_level == false,
              "shutdown leaves GPIO low when HFP and ACC are active and reason 24 is clear");

        Emulator shutdown_reason24;
        check(static_cast<bool>(shutdown_reason24.write(encode_driver_command(
                  make_master_mute_command(true, 2)))),
              "shutdown reason-24 case enables ACC latch");
        check(static_cast<bool>(shutdown_reason24.write(encode_driver_command(DriverCommand{
                  0x04, 3, {1, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0}}))) &&
              static_cast<bool>(shutdown_reason24.write(encode_driver_command(
                  make_master_mute_command(true, 24)))),
              "shutdown reason-24 case activates HFP and reason 24");
        shutdown_reason24.apply_recovered_shutdown();
        check(shutdown_reason24.state().gpio_393_level == true,
              "shutdown reason 24 forces GPIO high even while HFP and ACC are active");

        Emulator pm_wakeup;
        check(static_cast<bool>(pm_wakeup.write(encode_driver_command(
                  make_master_volume_command(20)))) &&
              static_cast<bool>(pm_wakeup.write(encode_driver_command(
                  DriverCommand{0x12, 2, {80, 0, 0, 0, 0, 0, 0, 0}}))),
              "PM wakeup fixture sets master volume and percentage");
        pm_wakeup.apply_recovered_shutdown();
        check(pm_wakeup.apply_recovered_pm_event(16U, 0U) == 0 &&
              !pm_wakeup.state().shutdown_started &&
              pm_wakeup.state().master_volume_register_value == 0x1AU &&
              pm_wakeup.state().current_volume_register_value == 0x1AU,
              "PM event 0 wakes a shut-down DSP and restores the cached volume byte");
        check(pm_wakeup.wakeup_journal().size() == 5U &&
              pm_wakeup.wakeup_journal()[0].kind == WakeupEventKind::RemoveQueuedEvent &&
              pm_wakeup.wakeup_journal()[0].event_id == 0U &&
              pm_wakeup.wakeup_journal()[1].kind == WakeupEventKind::PostDelayedEvent &&
              pm_wakeup.wakeup_journal()[1].event_id == 0U &&
              pm_wakeup.wakeup_journal()[1].delay_ms == 50U &&
              pm_wakeup.wakeup_journal()[2].kind == WakeupEventKind::RemoveQueuedEvent &&
              pm_wakeup.wakeup_journal()[2].event_id == 2U &&
              pm_wakeup.wakeup_journal()[3].delay_ms == 1200U &&
              pm_wakeup.wakeup_journal()[4].delay_ms == 1500U,
              "wakeup journals the recovered event cancellation and delayed posts in order");
        check(pm_wakeup.state().master_mute_reason_mask & (1U << 30U),
              "wakeup leaves reason 30 set for the delayed callback to clear");

        Emulator pm_wakeup_one;
        pm_wakeup_one.apply_recovered_shutdown();
        check(pm_wakeup_one.apply_recovered_pm_event(16U, 1U) == 0 &&
              !pm_wakeup_one.state().shutdown_started &&
              pm_wakeup_one.wakeup_journal().size() == 5U,
              "PM event 1 follows the same recovered wakeup branch");

        Emulator pm_ignored;
        pm_ignored.apply_recovered_shutdown();
        check(pm_ignored.apply_recovered_pm_event(15U, 2U) == 0 &&
              pm_ignored.state().shutdown_started &&
              pm_ignored.shutdown_journal().size() == 4U,
              "PM actions other than 16 are ignored");
        check(pm_ignored.apply_recovered_pm_event(16U, 5U) == -22 &&
              pm_ignored.state().shutdown_started,
              "unknown PM event codes return -EINVAL without state changes");

        Emulator pm_shutdown;
        check(pm_shutdown.apply_recovered_pm_event(16U, 4U) == 0 &&
              pm_shutdown.state().shutdown_started &&
              pm_shutdown.shutdown_journal().size() == 4U,
              "PM event 4 enters the recovered shutdown path");
        check(pm_shutdown.apply_recovered_pm_event(16U, 2U) == 0 &&
              pm_shutdown.shutdown_journal().size() == 8U,
              "PM events 2 and 3-class codes dispatch shutdown each time");

        Emulator effects_emulator;
        effects_emulator.apply_recovered_probe_setup();
        effects_emulator.apply_recovered_initialization_sequence();
        check(static_cast<bool>(effects_emulator.write(
                  make_two_word_command(0x55, 0x12345678U, 18U))),
              "initialized bass transition is accepted");
        check(effects_emulator.state().bass_boost_target == 10U &&
              effects_emulator.state().bass_register_value == 0x6CU &&
              effects_emulator.register_journal().size() == 21U &&
              effects_emulator.register_journal()[18].command_byte == 0x6E &&
              effects_emulator.register_journal()[19].command_byte == 0x6D &&
              effects_emulator.register_journal()[20].command_byte == 0x6C,
              "bass transition walks the exact CD3313 lookup values from index 7 to 10");
        check(static_cast<bool>(effects_emulator.write(
                  make_two_word_command(0x5C, 0x87654321U, 7U))),
              "initialized treble transition is accepted");
        check(effects_emulator.state().treble_target == 6U &&
              effects_emulator.state().treble_register_value == 0x76U &&
              effects_emulator.register_journal().size() == 22U &&
              effects_emulator.register_journal().back().command_byte == 0x76U,
              "treble transition writes the exact CD3313 lookup value for index 6");
        check(static_cast<bool>(effects_emulator.write(encode_driver_command(
                  DriverCommand{0x56, 1, {1, 0, 0, 0}}))),
              "loudness enable request is accepted");
        check(effects_emulator.state().loudness_enabled == true &&
              effects_emulator.state().control_register_value == 0x43U &&
              effects_emulator.register_journal().size() == 23U &&
              effects_emulator.register_journal().back().source ==
                  RegisterWriteSource::LoudnessControl,
              "loudness enable clears the active-low control bit");
        check(static_cast<bool>(effects_emulator.write(encode_driver_command(
                  DriverCommand{0x56, 1, {1, 0, 0, 0}}))) &&
              effects_emulator.register_journal().size() == 23U,
              "repeating the current loudness state does not emit a redundant write");
        check(static_cast<bool>(effects_emulator.write(encode_driver_command(
                  DriverCommand{0x56, 1, {0, 0, 0, 0}}))) &&
              effects_emulator.state().loudness_enabled == false &&
              effects_emulator.state().control_register_value == 0x47U &&
              effects_emulator.register_journal().size() == 24U &&
              effects_emulator.register_journal().back().command_byte == 0x47U,
              "loudness disable restores the recovered control byte");

        std::cout << "All CD3313 driver-command tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
