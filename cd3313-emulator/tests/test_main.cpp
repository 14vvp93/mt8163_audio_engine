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
        check(state_emulator.state().master_volume_step == 23U,
              "master volume state is updated");
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
                  std::array<std::uint8_t, 4>{{0x80, 0xA2, 0xC6, 0xFF}}),
              "speaker register image uses recovered prefixes and table");

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
        check(state_emulator.state().bass_boost_target == 14U &&
              state_emulator.state().bass_boost_opaque_arg0 == 0x12345678U,
              "bass target is bounded and opaque first argument is preserved");
        check(static_cast<bool>(state_emulator.write(
                  make_two_word_command(0x5C, 0x87654321U, 7U))),
              "treble command is accepted");
        check(state_emulator.state().treble_target == 7U &&
              state_emulator.state().treble_opaque_arg0 == 0x87654321U,
              "treble target and opaque first argument are retained");
        check(static_cast<bool>(state_emulator.write(
                  encode_driver_command(DriverCommand{0x56, 1, {1, 0, 0, 0}}))),
              "loudness command is accepted");
        check(state_emulator.state().loudness_enabled == true,
              "loudness state is modeled");
        check(state_emulator.journal().size() == 9U &&
              state_emulator.journal().front().sequence == 1U &&
              state_emulator.journal().back().sequence == 9U,
              "journal sequence IDs are deterministic and monotonic");
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
              state_emulator.journal().size() == 9U,
              "rejected transition is atomic and is not journaled");

        Emulator unknown_command_emulator;
        check(static_cast<bool>(unknown_command_emulator.write(
                  encode_driver_command(DriverCommand{0x01, 1, {0, 0, 0, 0}}))),
              "recognized command with unresolved semantics is retained");
        check(unknown_command_emulator.journal().size() == 1U &&
              !unknown_command_emulator.journal().front().modeled &&
              unknown_command_emulator.journal().front().command.id == 0x01,
              "unresolved semantics remain visible as an unmodeled journal event");

        state_emulator.reset();
        check(state_emulator.journal().empty() &&
              state_emulator.accepted_commands() == 0U &&
              state_emulator.state().master_mute_reason_mask == 0U &&
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

        std::cout << "All CD3313 driver-command tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
