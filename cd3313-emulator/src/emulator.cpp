#include "cd3313/emulator.hpp"

namespace cd3313 {
ParseResult Emulator::write(const std::vector<std::uint8_t>& bytes) {
    ParseResult result = parse_write_packet(bytes);
    if (result) {
        last_packet_ = result.packet;
        ++accepted_packets_;
    } else {
        ++rejected_packets_;
    }
    return result;
}

void Emulator::reset() {
    last_packet_.reset();
    accepted_packets_ = 0;
    rejected_packets_ = 0;
}
} // namespace cd3313
