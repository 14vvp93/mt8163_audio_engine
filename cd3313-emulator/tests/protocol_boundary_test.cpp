#include "cd3313/protocol.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> make_frame(std::uint8_t id,
                                     std::uint16_t words,
                                     std::size_t payload_bytes,
                                     std::uint8_t direction = 1U) {
    const std::uint32_t header =
        (static_cast<std::uint32_t>(direction & 0x03U) << 30U) |
        (static_cast<std::uint32_t>(id) << 8U) |
        ((static_cast<std::uint32_t>(words) & 0x3FFFU) << 16U);
    std::vector<std::uint8_t> frame{
        static_cast<std::uint8_t>(header),
        static_cast<std::uint8_t>(header >> 8U),
        static_cast<std::uint8_t>(header >> 16U),
        static_cast<std::uint8_t>(header >> 24U)
    };
    frame.resize(4U + payload_bytes, 0U);
    return frame;
}

bool encode_rejects(const cd3313::DriverCommand& command) {
    try {
        (void)cd3313::encode_driver_command(command);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

std::vector<std::uint8_t> make_legacy_jni_frame(
    std::uint8_t id, const std::vector<std::uint32_t>& arguments,
    std::uint32_t initialized_padding_word) {
    const auto size_words = static_cast<std::uint16_t>(arguments.size() + 1U);
    auto frame = make_frame(
        id, size_words, (arguments.size() + 1U) * 4U);
    std::size_t offset = 4U;
    const auto append_word = [&](std::uint32_t value,
                                 std::vector<std::uint8_t>& output,
                                 std::size_t& cursor) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            output[cursor++] = static_cast<std::uint8_t>(value >> shift);
        }
    };
    for (const auto value : arguments) {
        append_word(value, frame, offset);
    }
    append_word(initialized_padding_word, frame, offset);
    return frame;
}

std::uint32_t read_word(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}
}

int main() {
    using namespace cd3313;

    try {
        require(parse_driver_command({}).error == ParseError::TruncatedHeader,
                "empty frame must be rejected");
        require(parse_driver_command({0U, 0U, 0U}).error ==
                    ParseError::TruncatedHeader,
                "partial header must be rejected");

        const auto wrong_direction = make_frame(0x10U, 1U, 4U, 2U);
        require(parse_driver_command(wrong_direction).error ==
                    ParseError::UnsupportedDirection,
                "non-write direction must be rejected");

        const auto unknown_command = make_frame(0xFEU, 1U, 4U);
        require(parse_driver_command(unknown_command).error ==
                    ParseError::UnknownCommand,
                "unknown command ID must be rejected");

        const auto header_only = make_frame(0x10U, 1U, 0U);
        require(parse_driver_command(header_only).error ==
                    ParseError::PayloadSizeMismatch,
                "header-only frame with a nonzero size field must be rejected");

        auto ignored_low_request_byte = make_frame(0x10U, 1U, 4U);
        ignored_low_request_byte[0] = 0xA5U;
        require(static_cast<bool>(parse_driver_command(ignored_low_request_byte)),
                "parser must preserve the driver behavior of ignoring request bits 0..7");

        for (const auto& spec : known_write_commands()) {
            const std::size_t required_bytes =
                static_cast<std::size_t>(spec.required_size_words) * 4U;
            const auto valid = make_frame(
                spec.id, spec.required_size_words, required_bytes);
            const auto parsed = parse_driver_command(valid);
            require(static_cast<bool>(parsed),
                    "each recovered command must accept its required complete payload");

            const auto encoded = encode_driver_command(*parsed.command);
            require(encoded == valid,
                    "every recovered command must round-trip byte-for-byte");

            if (spec.minimum_size_words > 0U) {
                const auto below_minimum = make_frame(
                    spec.id,
                    static_cast<std::uint16_t>(spec.minimum_size_words - 1U),
                    static_cast<std::size_t>(spec.minimum_size_words - 1U) * 4U);
                require(parse_driver_command(below_minimum).error ==
                            ParseError::SizeFieldTooSmall,
                        "each command must reject a size below its recovered minimum");
            }

            for (std::uint16_t words = spec.minimum_size_words;
                 words < spec.required_size_words; ++words) {
                const auto below_required = make_frame(
                    spec.id, words, static_cast<std::size_t>(words) * 4U);
                require(parse_driver_command(below_required).error ==
                            ParseError::InsufficientArgumentData,
                        "each command must reject every size that omits handler arguments");
            }

            auto truncated = valid;
            truncated.pop_back();
            require(parse_driver_command(truncated).error ==
                        ParseError::PayloadSizeMismatch,
                    "each command must reject a truncated payload");

            auto oversized = valid;
            oversized.push_back(0U);
            require(parse_driver_command(oversized).error ==
                        ParseError::PayloadSizeMismatch,
                    "each command must reject trailing payload bytes");
        }

        for (const std::uint8_t id : {0x09U, 0x56U, 0xA3U}) {
            const auto valid = make_frame(id, 1U, 4U);
            require(static_cast<bool>(parse_driver_command(valid)),
                    "special dispatch commands must accept one complete word");

            const auto empty = make_frame(id, 0U, 0U);
            require(parse_driver_command(empty).error ==
                        ParseError::SizeFieldTooSmall,
                    "special dispatch commands must reject zero-word frames");

            auto truncated = valid;
            truncated.pop_back();
            require(parse_driver_command(truncated).error ==
                        ParseError::PayloadSizeMismatch,
                    "special dispatch commands must reject truncated payloads");
        }

        constexpr std::uint32_t kInitializedJniPadding = 0xA5C39E71U;
        const std::vector<std::pair<std::uint8_t, std::vector<std::uint32_t>>>
            legacy_jni_calls{
                {0x03U, {2U, 1U}},
                {0x04U, {1U, 0U, 0U}},
                {0x09U, {40U}},
                {0x10U, {20U}},
                {0x11U, {0U, 2U}},
                {0x12U, {75U}},
                {0x15U, {0U, 4U, 8U, 12U}},
                {0x20U, {1U, 32U, 0U}},
                {0x55U, {1U, 7U, 0U, 10U}},
                {0x56U, {1U, 40U, 0x7FFFFFFFU}},
                {0x5CU, {1U, 7U, 0U, 10U}},
                {0xA3U, {1U, 100U, 0U, 10U}}
            };
        for (const auto& call : legacy_jni_calls) {
            const auto frame = make_legacy_jni_frame(
                call.first, call.second, kInitializedJniPadding);
            const auto parsed = parse_driver_command(frame);
            require(static_cast<bool>(parsed),
                    "known Java/Binder command arrays must parse with initialized JNI padding");
            require(parsed.command->size_field == call.second.size() + 1U,
                    "legacy JNI size field must be array length plus one word");
            require(parsed.command->payload.size() ==
                        (call.second.size() + 1U) * 4U,
                    "legacy JNI payload must contain exactly one initialized extra word");
            for (std::size_t i = 0U; i < call.second.size(); ++i) {
                require(read_word(parsed.command->payload, i * 4U) == call.second[i],
                        "legacy JNI argument words must retain their original order");
            }
            require(read_word(parsed.command->payload, call.second.size() * 4U) ==
                        kInitializedJniPadding,
                    "legacy JNI compatibility word must be explicit and preserved");
            require(encode_driver_command(*parsed.command) == frame,
                    "legacy JNI frame must round-trip without synthesizing or dropping padding");
        }

        for (std::size_t word = 0U; word < 4U; ++word) {
            auto invalid_index = make_frame(0x15U, 4U, 16U);
            invalid_index[4U + word * 4U] = 16U;
            require(parse_driver_command(invalid_index).error ==
                        ParseError::InvalidArgumentValue,
                    "each of the four speaker indices must be range-checked");
        }

        const auto max_safe = make_frame(0x10U, 63U, 252U);
        require(static_cast<bool>(parse_driver_command(max_safe)),
                "63-word payload must fit the recovered 256-byte slab including header");

        const auto first_unsafe = make_frame(0x10U, 64U, 256U);
        require(parse_driver_command(first_unsafe).error ==
                    ParseError::UnsafePayloadSize,
                "64-word payload must be rejected before slab-boundary overflow");

        const auto largest_header_value = make_frame(0x10U, 0x3FFFU, 256U);
        require(parse_driver_command(largest_header_value).error ==
                    ParseError::UnsafePayloadSize,
                "maximum 14-bit size field must be rejected without arithmetic overflow");

        require(encode_rejects({0xFEU, 1U, {0U, 0U, 0U, 0U}}),
                "encoder must reject unknown command IDs");
        require(encode_rejects({0x10U, 1U, {0U, 0U, 0U}}),
                "encoder must reject payload-size mismatch");
        require(encode_rejects({0x15U, 4U, {0U, 0U, 0U, 0U}}),
                "encoder must reject incomplete four-channel payload");
        require(encode_rejects({0x15U, 3U, {
                    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                    0U, 0U, 0U, 0U}}),
                "encoder must reject size fields below the four-word handler requirement");
        require(encode_rejects({0x15U, 4U, {
                    16U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}}),
                "encoder must reject an out-of-range speaker index");
        require(encode_rejects({0x10U, 0x4000U, {0U}}),
                "encoder must reject size fields wider than 14 bits");
        require(encode_rejects({0x10U, 64U, std::vector<std::uint8_t>(256U, 0U)}),
                "encoder must reject payloads that exceed the slab-object boundary");

        std::cout << "CD3313 protocol boundary tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CD3313 protocol boundary test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
