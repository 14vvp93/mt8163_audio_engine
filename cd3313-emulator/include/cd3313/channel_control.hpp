#pragma once
#include <cstdint>

namespace cd3313 {
struct BalanceFader {
    int balance_percent{0};
    int fader_percent{0};
    // Relative rear-level compensation. Positive values attenuate the front
    // pair so the rear pair is louder in comparison; this chip cannot boost.
    int rear_compensation_percent{0};
};

struct ChannelLevels {
    std::uint8_t front_left{15};
    std::uint8_t front_right{15};
    std::uint8_t rear_left{15};
    std::uint8_t rear_right{15};
};

ChannelLevels calculate_channel_levels(BalanceFader controls);
std::uint8_t speaker_register_byte(std::uint8_t channel_index,
                                   std::uint8_t level_index);
} // namespace cd3313
