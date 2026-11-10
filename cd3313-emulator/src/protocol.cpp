#include "cd3313/protocol.hpp"
#include <algorithm>
#include <stdexcept>

namespace cd3313 {
const std::vector<CommandSpec>& known_write_commands() {
    static const std::vector<CommandSpec> commands{
        {0x01, 1}, {0x03, 3}, {0x04, 3}, {0x10, 1},
        {0x11, 2}, {0x12, 2}, {0x15, 2}, {0x20, 3},
        {0x55, 2}, {0x5C, 2}, {0xF0, 1}, {0xF1, 1}
    };
    return commands;
}

ParseResult parse_write_packet(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return {{}, ParseError::EmptyPacket};

    const auto& commands = known_write_commands();
    const auto it = std::find_if(commands.begin(), commands.end(),
        [&](const CommandSpec& spec) { return spec.id == bytes.front(); });
    if (it == commands.end()) return {{}, ParseError::UnknownCommand};

    if (bytes.size() != static_cast<std::size_t>(it->payload_size) + 1U)
        return {{}, ParseError::InvalidLength};

    Packet packet;
    packet.command = bytes.front();
    packet.payload.assign(bytes.begin() + 1, bytes.end());
    return {packet, ParseError::None};
}

std::vector<std::uint8_t> encode_write_packet(const Packet& packet) {
    const auto& commands = known_write_commands();
    const auto it = std::find_if(commands.begin(), commands.end(),
        [&](const CommandSpec& spec) { return spec.id == packet.command; });
    if (it == commands.end()) throw std::invalid_argument("unknown CD3313 command");
    if (packet.payload.size() != it->payload_size)
        throw std::invalid_argument("invalid CD3313 payload length");

    std::vector<std::uint8_t> bytes;
    bytes.reserve(packet.payload.size() + 1U);
    bytes.push_back(packet.command);
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return bytes;
}
} // namespace cd3313
