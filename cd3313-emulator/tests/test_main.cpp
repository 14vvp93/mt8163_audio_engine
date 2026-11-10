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
        check(known_write_commands().size() == 12, "expected 12 recovered command-length pairs");

        const auto ok = parse_write_packet({0x10, 0x28});
        check(static_cast<bool>(ok), "0x10 packet should parse");
        check(ok.packet->payload.size() == 1 && ok.packet->payload[0] == 0x28,
              "payload must be preserved");

        check(parse_write_packet({}).error == ParseError::EmptyPacket, "empty packet");
        check(parse_write_packet({0x7F, 0x00}).error == ParseError::UnknownCommand,
              "unknown command");
        check(parse_write_packet({0x10}).error == ParseError::InvalidLength,
              "short payload");
        check(parse_write_packet({0x10, 0x01, 0x02}).error == ParseError::InvalidLength,
              "long payload");

        const Packet packet{0x55, {0x02, 0x7F}};
        check(encode_write_packet(packet) == std::vector<std::uint8_t>({0x55, 0x02, 0x7F}),
              "packet encode round trip");

        bool threw = false;
        try { (void)encode_write_packet(Packet{0x55, {0x01}}); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "encoder must reject malformed packet");

        Emulator emulator;
        check(static_cast<bool>(emulator.write({0x03, 1, 2, 3})), "emulator accepts valid packet");
        check(!emulator.write({0x03, 1}), "emulator rejects malformed packet");
        check(emulator.accepted_packets() == 1, "accepted counter");
        check(emulator.rejected_packets() == 1, "rejected counter");
        check(emulator.last_packet()->command == 0x03, "last valid packet retained");
        emulator.reset();
        check(!emulator.last_packet() && emulator.accepted_packets() == 0,
              "reset clears emulator state");

        std::cout << "All CD3313 protocol tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
