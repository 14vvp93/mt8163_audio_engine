#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
struct CommandSpec {
    std::uint8_t id;
    // Minimum accepted by the recovered driver command-length table.
    std::uint16_t minimum_size_words;
    // Minimum needed to safely contain all arguments read by the handler.
    std::uint16_t required_size_words;
};

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
    InsufficientArgumentData,
    PayloadSizeMismatch
};

struct ParseResult {
    std::optional<DriverCommand> command;
    ParseError error{ParseError::None};
    explicit operator bool() const { return command.has_value(); }
};

// Parses the queued command: write-direction ioctl header (LE32), then
// size_field * 4 bytes of opaque user payload, capped by the driver's 256-byte copy.
// Unsafe short payloads are rejected even if the driver's own minimum-size table accepts them.
ParseResult parse_driver_command(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> encode_driver_command(const DriverCommand& command);
} // namespace cd3313
