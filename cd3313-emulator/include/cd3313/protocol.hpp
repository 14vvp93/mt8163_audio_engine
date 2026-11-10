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
    InvalidArgumentValue,
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

// Typed builders for commands whose argument layout and range are recovered
// from the driver. These build emulator frames; they do not perform I/O.
DriverCommand make_input_channel_command(std::int32_t channel);
DriverCommand make_input_gain_command(std::uint32_t channel, std::uint32_t gain,
                                      std::uint32_t opaque_word = 0);
DriverCommand make_input_mute_command(std::int32_t channel, bool mute,
                                      std::uint32_t reason);
DriverCommand make_input_mixing_command(bool enabled,
                                        std::uint32_t opaque_word0 = 0,
                                        std::uint32_t opaque_word1 = 0);
DriverCommand make_master_volume_command(std::uint32_t step);
DriverCommand make_master_mute_command(bool mute, std::uint32_t reason);
DriverCommand make_four_channel_level_command(std::uint8_t front_left,
                                              std::uint8_t front_right,
                                              std::uint8_t rear_left,
                                              std::uint8_t rear_right);
} // namespace cd3313
