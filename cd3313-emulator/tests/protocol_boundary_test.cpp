#include "cd3313/protocol.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> make_frame(std::uint8_t id,
                                     std::uint16_t words,
                                     std::size_t payload_bytes) {
    const std::uint32_t header = (1U << 30U) |
        (static_cast<std::uint32_t>(id) << 8U) |
        (static_cast<std::uint32_t>(words) << 16U);
    std::vector<std::uint8_t> frame{
        static_cast<std::uint8_t>(header),
        static_cast<std::uint8_t>(header >> 8U),
        static_cast<std::uint8_t>(header >> 16U),
        static_cast<std::uint8_t>(header >> 24U)
    };
    frame.resize(4U + payload_bytes, 0U);
    return frame;
}
}

int main() {
    using namespace cd3313;

    try {
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
                        "each command must reject a size below its recovered driver minimum");
            }

            if (spec.required_size_words > spec.minimum_size_words) {
                const auto below_required = make_frame(
                    spec.id,
                    static_cast<std::uint16_t>(spec.required_size_words - 1U),
                    static_cast<std::size_t>(spec.required_size_words - 1U) * 4U);
                require(parse_driver_command(below_required).error ==
                            ParseError::InsufficientArgumentData,
                        "each command must reject a payload shorter than handler reads");
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


        const auto legacy_jni_balance = make_frame(0x15U, 5U, 20U);
        const auto parsed_legacy_jni_balance =
            parse_driver_command(legacy_jni_balance);
        require(static_cast<bool>(parsed_legacy_jni_balance),
                "legacy JNI framing with one initialized compatibility word must parse");
        require(parsed_legacy_jni_balance.command->payload.size() == 20U &&
                    encode_driver_command(*parsed_legacy_jni_balance.command) ==
                        legacy_jni_balance,
                "legacy JNI compatibility word must be preserved without an over-read");

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

        std::cout << "CD3313 protocol boundary tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CD3313 protocol boundary test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
