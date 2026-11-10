#include "cd3313/emulator.hpp"
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
              "expected 12 recovered command/minimum-size pairs");
        check(known_write_commands()[1].id == 0x03 &&
              known_write_commands()[1].minimum_size_words == 3,
              "0x03 minimum size field");

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
                  ParseError::SizeFieldTooSmall, "size field below minimum");
        check(parse_driver_command({0x00, 0x10, 0x01, 0x40, 0x28, 0, 0}).error ==
                  ParseError::PayloadSizeMismatch, "truncated payload");
        check(static_cast<bool>(parse_driver_command(
                  {0x00, 0x56, 0x01, 0x40, 0, 0, 0, 0})),
              "special 0x56 dispatcher branch");
        check(static_cast<bool>(parse_driver_command(
                  {0x00, 0xA3, 0x01, 0x40, 1, 0, 0, 0})),
              "special 0xA3 dispatcher branch");

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
