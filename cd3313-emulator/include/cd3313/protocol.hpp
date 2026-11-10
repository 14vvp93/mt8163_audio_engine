#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
struct CommandSpec {
    std::uint8_t id;
    std::uint16_t minimum_size_words;
};

// The second DWARF value is the minimum accepted size-field value.
// cmd_ioctl copies size_field * 4 bytes, so the field counts 32-bit words.
const std::vector<CommandSpec>& known_write_commands();
bool is_dispatch_command(std::uint8_t id);

struct DriverCommand {
    std::uint8_t id{};
    std::uint16_t size_field{};
    std::vector<std::uint8_t> payload;
};

enum class ParseError {
    None,
    TruncatedHeader,
    UnsupportedDirection,
    UnknownCommand,
    SizeFieldTooSmall,
    PayloadSizeMismatch
};

struct ParseResult {
    std::optional<DriverCommand> command;
    ParseError error{ParseError::None};
    explicit operator bool() const { return command.has_value(); }
};

// Parses the queued command: write-direction ioctl header (LE32), then
// size_field * 4 bytes of opaque user payload, capped by the driver's 256-byte copy.
ParseResult parse_driver_command(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> encode_driver_command(const DriverCommand& command);
} // namespace cd3313
