#pragma once
#include "cd3313/protocol.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
class Emulator {
public:
    ParseResult write(const std::vector<std::uint8_t>& bytes);
    const std::optional<Packet>& last_packet() const { return last_packet_; }
    std::uint64_t accepted_packets() const { return accepted_packets_; }
    std::uint64_t rejected_packets() const { return rejected_packets_; }
    void reset();

private:
    std::optional<Packet> last_packet_;
    std::uint64_t accepted_packets_{0};
    std::uint64_t rejected_packets_{0};
};
} // namespace cd3313
