#include "cd3313/protocol.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cd3313 {
namespace {
constexpr std::size_t kMaxCopiedPayloadBytes = 256U;

std::size_t copied_payload_size(std::uint16_t size_field) {
    const std::size_t requested = static_cast<std::size_t>(size_field) * 4U;
    return std::min(requested, kMaxCopiedPayloadBytes);
}

const CommandSpec* find_command(std::uint8_t id) {
    const auto& commands = known_write_commands();
    const auto it = std::find_if(commands.begin(), commands.end(),
        [&](const CommandSpec& spec) { return spec.id == id; });
    return it == commands.end() ? nullptr : &*it;
}

std::uint16_t minimum_size_words(std::uint8_t id) {
    if (id == 0x56 || id == 0xA3) return 1;
    const auto* spec = find_command(id);
    return spec == nullptr ? 0 : spec->minimum_size_words;
}

std::uint16_t required_size_words(std::uint8_t id) {
    if (id == 0x56 || id == 0xA3) return 1;
    const auto* spec = find_command(id);
    return spec == nullptr ? 0 : spec->required_size_words;
}

bool valid_channel_indices(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 16U) return false;
    for (std::size_t offset = 0; offset < 16U; offset += 4U) {
        const std::uint32_t value =
            static_cast<std::uint32_t>(payload[offset]) |
            (static_cast<std::uint32_t>(payload[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(payload[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(payload[offset + 3U]) << 24U);
        if (value >= 16U) return false;
    }
    return true;
}
} // namespace

const std::vector<CommandSpec>& known_write_commands() {
    static const std::vector<CommandSpec> commands{
        {0x01, 1, 1}, {0x03, 3, 3}, {0x04, 3, 3}, {0x10, 1, 1},
        {0x11, 2, 2}, {0x12, 2, 2}, {0x15, 2, 4}, {0x20, 3, 3},
        {0x55, 2, 2}, {0x5C, 2, 2}, {0xF0, 1, 1}, {0xF1, 1, 1}
    };
    return commands;
}

bool is_dispatch_command(std::uint8_t id) {
    if (id == 0x56 || id == 0xA3) return true;
    return find_command(id) != nullptr;
}

ParseResult parse_driver_command(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 4U) return {{}, ParseError::TruncatedHeader};

    const std::uint32_t header =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);

    const auto direction = static_cast<std::uint8_t>((header >> 30U) & 0x03U);
    if (direction != 1U) return {{}, ParseError::UnsupportedDirection};

    const auto id = static_cast<std::uint8_t>((header >> 8U) & 0xFFU);
    const auto size_field = static_cast<std::uint16_t>((header >> 16U) & 0x3FFFU);
    if (!is_dispatch_command(id)) return {{}, ParseError::UnknownCommand};

    if (size_field < minimum_size_words(id))
        return {{}, ParseError::SizeFieldTooSmall};
    if (size_field < required_size_words(id))
        return {{}, ParseError::InsufficientArgumentData};

    const std::size_t expected = copied_payload_size(size_field);
    if (bytes.size() - 4U != expected)
        return {{}, ParseError::PayloadSizeMismatch};

    if (id == 0x15 && !valid_channel_indices(
            std::vector<std::uint8_t>(bytes.begin() + 4, bytes.end())))
        return {{}, ParseError::InvalidArgumentValue};

    DriverCommand command;
    command.id = id;
    command.size_field = size_field;
    command.payload.assign(bytes.begin() + 4, bytes.end());
    return {command, ParseError::None};
}

std::vector<std::uint8_t> encode_driver_command(const DriverCommand& command) {
    if (!is_dispatch_command(command.id))
        throw std::invalid_argument("unknown CD3313 driver command");
    if (command.size_field > 0x3FFFU)
        throw std::invalid_argument("CD3313 size field exceeds 14 bits");
    if (command.size_field < minimum_size_words(command.id))
        throw std::invalid_argument("CD3313 size field below recovered minimum");
    if (command.size_field < required_size_words(command.id))
        throw std::invalid_argument("CD3313 payload is too short for handler arguments");

    const std::size_t expected = copied_payload_size(command.size_field);
    if (command.payload.size() != expected)
        throw std::invalid_argument("payload size does not match ioctl size field");
    if (command.id == 0x15 && !valid_channel_indices(command.payload))
        throw std::invalid_argument("CD3313 speaker channel index must be in range 0..15");

    const std::uint32_t header =
        (1U << 30U) |
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
namespace {
std::vector<std::uint8_t> word_bytes(std::uint32_t value) {
    return {
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU)
    };
}
void append_word(std::vector<std::uint8_t>& payload, std::uint32_t value) {
    const auto bytes = word_bytes(value);
    payload.insert(payload.end(), bytes.begin(), bytes.end());
}
}

DriverCommand make_input_channel_command(std::int32_t channel) {
    if (channel < -1 || channel > 6)
        throw std::invalid_argument("CD3313 input channel must be -1 or in range 0..6");
    return {0x01, 1, word_bytes(static_cast<std::uint32_t>(channel))};
}

DriverCommand make_input_gain_command(std::uint32_t channel, std::uint32_t gain,
                                      std::uint32_t opaque_word) {
    if (channel > 6U)
        throw std::invalid_argument("CD3313 input gain channel must be in range 0..6");
    if (gain > 3U)
        throw std::invalid_argument("CD3313 input gain must be in range 0..3");
    std::vector<std::uint8_t> payload;
    payload.reserve(12U);
    append_word(payload, channel);
    append_word(payload, gain);
    append_word(payload, opaque_word);
    return {0x03, 3, std::move(payload)};
}

DriverCommand make_input_mute_command(std::int32_t channel, bool mute,
                                      std::uint32_t reason) {
    if (channel < -1 || channel > 6)
        throw std::invalid_argument("CD3313 input mute channel must be -1 or in range 0..6");
    if (reason > 31U)
        throw std::invalid_argument("CD3313 input mute reason must be in range 0..31");
    std::vector<std::uint8_t> payload;
    payload.reserve(12U);
    append_word(payload, static_cast<std::uint32_t>(channel));
    append_word(payload, mute ? 1U : 0U);
    append_word(payload, reason);
    return {0x04, 3, std::move(payload)};
}

DriverCommand make_input_mixing_command(bool enabled,
                                        std::uint32_t opaque_word0,
                                        std::uint32_t opaque_word1) {
    std::vector<std::uint8_t> payload;
    payload.reserve(12U);
    append_word(payload, enabled ? 1U : 0U);
    append_word(payload, opaque_word0);
    append_word(payload, opaque_word1);
    return {0x20, 3, std::move(payload)};
}

DriverCommand make_master_volume_command(std::uint32_t step) {
    if (step > 40U)
        throw std::invalid_argument("CD3313 master volume step must be in range 0..40");
    return {0x10, 1, {
        static_cast<std::uint8_t>(step & 0xFFU),
        static_cast<std::uint8_t>((step >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((step >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((step >> 24U) & 0xFFU)
    }};
}

DriverCommand make_master_mute_command(bool mute, std::uint32_t reason) {
    if (reason > 31U)
        throw std::invalid_argument("CD3313 mute reason must be in range 0..31");
    return {0x11, 2, {
        static_cast<std::uint8_t>(mute ? 1U : 0U), 0, 0, 0,
        static_cast<std::uint8_t>(reason & 0xFFU),
        static_cast<std::uint8_t>((reason >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((reason >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((reason >> 24U) & 0xFFU)
    }};
}

DriverCommand make_four_channel_level_command(std::uint8_t front_left,
                                              std::uint8_t front_right,
                                              std::uint8_t rear_left,
                                              std::uint8_t rear_right) {
    const std::uint8_t indices[]{front_left, front_right, rear_left, rear_right};
    for (const auto index : indices) {
        if (index > 15U)
            throw std::invalid_argument("CD3313 speaker level index must be in range 0..15");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(16U);
    for (const auto index : indices) {
        payload.push_back(index);
        payload.insert(payload.end(), 3U, 0U);
    }
    return {0x15, 4, std::move(payload)};
}

} // namespace cd3313
