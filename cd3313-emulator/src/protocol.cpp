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

bool is_dispatch_command(std::uint8_t id) {
    if (id == 0x56 || id == 0xA3) return true;
    const auto& commands = known_write_commands();
    return std::any_of(commands.begin(), commands.end(),
        [&](const CommandSpec& spec) { return spec.id == id; });
}

ParseResult parse_driver_command(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 4U) return {{}, ParseError::TruncatedHeader};

    const std::uint32_t header =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);

    const auto id = static_cast<std::uint8_t>((header >> 8U) & 0xFFU);
    const auto size_field = static_cast<std::uint16_t>((header >> 16U) & 0x3FFFU);
    if (!is_dispatch_command(id)) return {{}, ParseError::UnknownCommand};

    const auto& specs = known_write_commands();
    const auto it = std::find_if(specs.begin(), specs.end(),
        [&](const CommandSpec& spec) { return spec.id == id; });
    if (it != specs.end() && size_field < it->minimum_size_field)
        return {{}, ParseError::SizeFieldTooSmall};

    DriverCommand command;
    command.id = id;
    command.size_field = size_field;
    command.payload.assign(bytes.begin() + 4, bytes.end());
    return {command, ParseError::None};
}

std::vector<std::uint8_t> encode_driver_command(const DriverCommand& command) {
    if (!is_dispatch_command(command.id))
        throw std::invalid_argument("unknown CD3313 driver command");

    const auto& specs = known_write_commands();
    const auto it = std::find_if(specs.begin(), specs.end(),
        [&](const CommandSpec& spec) { return spec.id == command.id; });
    if (it != specs.end() && command.size_field < it->minimum_size_field)
        throw std::invalid_argument("CD3313 size field below recovered minimum");

    const std::uint32_t header =
        (static_cast<std::uint32_t>(command.id) << 8U) |
        ((static_cast<std::uint32_t>(command.size_field) & 0x3FFFU) << 16U);

    std::vector<std::uint8_t> bytes{
        static_cast<std::uint8_t>(header & 0xFFU),
        static_cast<std::uint8_t>((header >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((header >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((header >> 24U) & 0xFFU)
    };
    bytes.insert(bytes.end(), command.payload.begin(), command.payload.end());
    return bytes;
}
} // namespace cd3313
