#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cd3313 {
struct CommandSpec {
    std::uint8_t id;
    // Conservative minimum from static command-length evidence; vendor callback
    // acceptance is not fully reconciled for every command.
    std::uint16_t minimum_size_words;
    // Minimum needed to safely contain all arguments read by the handler.
    std::uint16_t required_size_words;
};

// Conservative host-side allowlist, not a complete vendor-build protocol spec.
const std::vector<CommandSpec>& known_write_commands();

// Static 8-byte payload returned by the driver's read-direction ioctl ID 0xF0.
std::array<std::uint8_t, 8> read_device_identifier_payload();
// Driver-visible names from the channel-name pointer table in .rodata.
std::string_view input_channel_name(std::uint8_t channel);
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
    PayloadSizeMismatch,
    UnsafePayloadSize
};

struct ParseResult {
    std::optional<DriverCommand> command;
    ParseError error{ParseError::None};
    explicit operator bool() const { return command.has_value(); }
};

// Parses the queued command: write-direction ioctl header (LE32), then
// size_field * 4 bytes of opaque user payload, capped by the driver's 256-byte copy.
// The host parser additionally rejects >252 bytes: the 4-byte header shares the
// driver's 256-byte slab object, and hardened usercopy may BUG on a 256-byte copy.
ParseResult parse_driver_command(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> encode_driver_command(const DriverCommand& command);

// Typed builders for commands whose argument layout and range are recovered
// from the driver. These build safe emulator frames; they do not perform I/O.
// They intentionally do NOT reproduce the vendor JNI bridge's request sizing:
// SendCmdAudioMux encodes (Java array length + 1) words and the kernel copies
// that extra word from GetIntArrayElements(). Its value is unknown, so these
// builders encode only explicit, validated payload words and never over-read.
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
