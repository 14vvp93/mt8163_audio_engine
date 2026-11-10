#include "cd3313/channel_control.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace cd3313 {
namespace {
constexpr std::array<std::uint8_t, 16> kSpeakerCodes{
    31, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0
};
constexpr double kMaxDb = 38.75;
constexpr double kRearCompensationMaxDb = 12.0;

double attenuation(std::uint8_t code) {
    return static_cast<double>((code >> 3U) & 3U) * 10.0 +
           static_cast<double>(code & 7U) * 1.25;
}

double slider_db(int value) {
    return static_cast<double>(std::abs(std::clamp(value, -100, 100))) *
           kMaxDb / 100.0;
}

std::uint8_t nearest_index(double db) {
    db = std::clamp(db, 0.0, kMaxDb);
    std::uint8_t best = 15;
    double error = kMaxDb + 1.0;
    for (std::size_t i = 0; i < kSpeakerCodes.size(); ++i) {
        const double candidate = std::abs(attenuation(kSpeakerCodes[i]) - db);
        if (candidate < error) {
            error = candidate;
            best = static_cast<std::uint8_t>(i);
        }
    }
    return best;
}
} // namespace

ChannelLevels calculate_channel_levels(BalanceFader controls) {
    const int balance = std::clamp(controls.balance_percent, -100, 100);
    const int fader = std::clamp(controls.fader_percent, -100, 100);
    const int rear_compensation =
        std::clamp(controls.rear_compensation_percent, 0, 100);

    const double b = slider_db(balance);
    const double f = slider_db(fader);
    const double rear_compensation_db =
        static_cast<double>(rear_compensation) *
        kRearCompensationMaxDb / 100.0;

    double fl = 0.0;
    double fr = 0.0;
    double rl = 0.0;
    double rr = 0.0;

    if (balance > 0) {
        fl += b;
        rl += b;
    } else if (balance < 0) {
        fr += b;
        rr += b;
    }

    if (fader > 0) {
        rl += f;
        rr += f;
    } else if (fader < 0) {
        fl += f;
        fr += f;
    }

    // CD3313 is an attenuator: rear compensation is implemented by reducing
    // the front pair, not by applying gain beyond the rear channels' 0 dB.
    fl += rear_compensation_db;
    fr += rear_compensation_db;

    return {nearest_index(fl), nearest_index(fr),
            nearest_index(rl), nearest_index(rr)};
}

std::uint8_t speaker_register_byte(std::uint8_t channel_index,
                                   std::uint8_t level_index) {
    if (channel_index > 3U || level_index > 15U) return 0U;
    const auto prefix = static_cast<std::uint8_t>(0x80U | (channel_index << 5U));
    return static_cast<std::uint8_t>(prefix | kSpeakerCodes[level_index]);
}
} // namespace cd3313
