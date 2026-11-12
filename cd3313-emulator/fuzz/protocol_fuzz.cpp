#include "cd3313/protocol.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    constexpr std::size_t kMaximumInputBytes = 1024U;
    const std::size_t bounded_size = std::min(size, kMaximumInputBytes);
    std::vector<std::uint8_t> input;
    if (bounded_size != 0U) {
        if (data == nullptr) return 0;
        input.assign(data, data + bounded_size);
    }

    const auto parsed = cd3313::parse_driver_command(input);
    if (!parsed) return 0;

    const auto encoded = cd3313::encode_driver_command(*parsed.command);
    const auto reparsed = cd3313::parse_driver_command(encoded);
    if (!reparsed || reparsed.command->id != parsed.command->id ||
        reparsed.command->size_field != parsed.command->size_field ||
        reparsed.command->payload != parsed.command->payload) {
        std::abort();
    }
    return 0;
}
