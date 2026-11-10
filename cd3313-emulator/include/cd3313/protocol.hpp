#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
struct CommandSpec {
    std::uint8_t id;
    std::uint16_t minimum_size_field;
};

// The second DWARF value is the minimum encoded size-field threshold,
// not a proven byte count for the command payload.
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
    UnknownCommand,
    SizeFieldTooSmall
};

struct ParseResult {
    std::optional<DriverCommand> command;
    ParseError error{ParseError::None};
    explicit operator bool() const { return command.has_value(); }
};

// Parses the driver's queued command buffer:
// LE32 header (id in bits 8..15, size field in bits 16..29), then opaque data.
ParseResult parse_driver_command(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> encode_driver_command(const DriverCommand& command);
} // namespace cd3313
