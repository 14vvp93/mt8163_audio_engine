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
