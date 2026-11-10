#include "cd3313/emulator.hpp"

namespace cd3313 {
ParseResult Emulator::write(const std::vector<std::uint8_t>& bytes) {
    ParseResult result = parse_driver_command(bytes);
    if (result) {
        last_command_ = result.command;
        ++accepted_commands_;
    } else {
        ++rejected_commands_;
    }
    return result;
}

void Emulator::reset() {
    last_command_.reset();
    accepted_commands_ = 0;
    rejected_commands_ = 0;
}
} // namespace cd3313
