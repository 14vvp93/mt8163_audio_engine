#pragma once
#include "cd3313/protocol.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace cd3313 {
class Emulator {
public:
    ParseResult write(const std::vector<std::uint8_t>& bytes);
    const std::optional<DriverCommand>& last_command() const { return last_command_; }
    std::uint64_t accepted_commands() const { return accepted_commands_; }
    std::uint64_t rejected_commands() const { return rejected_commands_; }
    void reset();

private:
    std::optional<DriverCommand> last_command_;
    std::uint64_t accepted_commands_{0};
    std::uint64_t rejected_commands_{0};
};
} // namespace cd3313
