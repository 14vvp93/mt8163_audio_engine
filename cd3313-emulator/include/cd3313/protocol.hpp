#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
struct CommandSpec {
    std::uint8_t id;
    std::uint8_t payload_size;
};

// Recovered from cmd_len_list in the CD3313 driver's DWARF information.
// These lengths are payload bytes, excluding the command byte.
const std::vector<CommandSpec>& known_write_commands();

struct Packet {
    std::uint8_t command{};
    std::vector<std::uint8_t> payload;
};

enum class ParseError {
    None,
    EmptyPacket,
    UnknownCommand,
    InvalidLength
};

struct ParseResult {
    std::optional<Packet> packet;
    ParseError error{ParseError::None};
    explicit operator bool() const { return packet.has_value(); }
};

ParseResult parse_write_packet(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> encode_write_packet(const Packet& packet);
} // namespace cd3313
