#include "pcm_dsp/audio_profile.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace pcm_dsp;
    try {
        AudioProfile profile;
        check(validate_audio_profile(profile, 44100U),
              "default profile validates at 44.1 kHz");
        check(validate_audio_profile(profile, 48000U),
              "default profile validates at 48 kHz");
        check(!validate_audio_profile(profile, 32000U),
              "unsupported sample rate is rejected");

        profile.quad.channels[0].bands[0] =
            {1000.0F, 3.0F, 0.707F, true, FilterType::Peaking, 1.0F};
        profile.quad.channels[1].bands[0] =
            {1000.0F, -4.0F, 1.2F, true, FilterType::LowShelf, 0.8F};
        profile.quad.channels[2].delay_ms = 12.5F;
        profile.routing.enabled = true;
        profile.routing.bass_management_enabled = true;
        profile.routing.crossover_frequency_hz = 90.0F;
        profile.dynamics.enabled = true;
        profile.dynamics.compressors[0][0].enabled = true;
        check(validate_audio_profile(profile, 48000U),
              "complete four-channel profile validates");

        AudioProfile invalid = profile;
        invalid.schema_version++;
        check(!validate_audio_profile(invalid, 48000U),
              "unknown schema version is rejected");

        invalid = profile;
        invalid.quad.channels[0].delay_ms = kMaxTimeAlignmentMs + 0.1F;
        check(!validate_audio_profile(invalid, 48000U),
              "delay beyond fixed buffer limit is rejected");

        invalid = profile;
        invalid.quad.channels[0].bands[0].frequency_hz =
            std::numeric_limits<float>::quiet_NaN();
        check(!validate_audio_profile(invalid, 48000U),
              "non-finite EQ frequency is rejected");

        invalid = profile;
        invalid.routing.bass_management_enabled = false;
        invalid.routing.enabled = false;
        invalid.routing.matrix[0] = 2.1F;
        check(!validate_audio_profile(invalid, 48000U),
              "out-of-range routing coefficient is rejected");

        invalid = profile;
        invalid.dynamics.compressors[3][2].ratio = 21.0F;
        check(!validate_audio_profile(invalid, 48000U),
              "invalid compressor ratio is rejected");

        invalid = profile;
        invalid.routing.bass_management_enabled = true;
        invalid.routing.enabled = false;
        check(!validate_audio_profile(invalid, 48000U),
              "bass management cannot be enabled with routing disabled");


        // Exercise every public profile-validation family with non-finite,
        // out-of-range, and invalid-enum values before parameters reach DSP.
        invalid = profile;
        invalid.quad.preamp_db = std::numeric_limits<float>::infinity();
        check(!validate_audio_profile(invalid, 48000U), "non-finite preamp is rejected");

        invalid = profile;
        invalid.quad.limiter_threshold = 1.01F;
        check(!validate_audio_profile(invalid, 48000U), "limiter threshold above unity is rejected");

        invalid = profile;
        invalid.quad.channels[3].gain_db = -24.1F;
        check(!validate_audio_profile(invalid, 48000U), "channel gain below range is rejected");

        invalid = profile;
        invalid.quad.channels[0].bands[0].type = static_cast<FilterType>(0xFF);
        check(!validate_audio_profile(invalid, 48000U), "unknown enabled EQ filter type is rejected");

        invalid = profile;
        invalid.quad.channels[0].bands[0].q = 10.1F;
        check(!validate_audio_profile(invalid, 48000U), "EQ Q above range is rejected");

        invalid = profile;
        invalid.quad.channels[0].bands[0].shelf_slope = 1.1F;
        check(!validate_audio_profile(invalid, 48000U), "EQ shelf slope above range is rejected");

        invalid = profile;
        invalid.routing.sub_gain_db = -24.1F;
        check(!validate_audio_profile(invalid, 48000U), "subwoofer gain below range is rejected");

        invalid = profile;
        invalid.routing.enabled = true;
        invalid.routing.bass_management_enabled = true;
        invalid.routing.crossover_order = static_cast<LinkwitzRileyOrder>(3);
        check(!validate_audio_profile(invalid, 48000U), "unsupported crossover order is rejected");

        invalid = profile;
        invalid.routing.enabled = true;
        invalid.routing.bass_management_enabled = true;
        invalid.routing.crossover_frequency_hz = 22000.0F;
        check(!validate_audio_profile(invalid, 48000U), "unsafe crossover frequency is rejected");

        invalid = profile;
        invalid.enhancements.bass_amount = std::numeric_limits<float>::quiet_NaN();
        check(!validate_audio_profile(invalid, 48000U), "non-finite bass amount is rejected");

        invalid = profile;
        invalid.enhancements.bass_frequency_hz = 39.0F;
        check(!validate_audio_profile(invalid, 48000U), "bass frequency below range is rejected");

        invalid = profile;
        invalid.enhancements.clarity_amount = 1.01F;
        check(!validate_audio_profile(invalid, 48000U), "clarity amount above unity is rejected");

        invalid = profile;
        invalid.enhancements.clarity_frequency_hz = 22000.0F;
        check(!validate_audio_profile(invalid, 48000U), "unsafe clarity frequency is rejected");

        invalid = profile;
        invalid.dynamics.low_crossover_hz = 0.0F;
        check(!validate_audio_profile(invalid, 48000U), "invalid dynamics low crossover is rejected");

        invalid = profile;
        invalid.dynamics.high_crossover_hz = invalid.dynamics.low_crossover_hz;
        check(!validate_audio_profile(invalid, 48000U), "overlapping dynamics crossovers are rejected");

        invalid = profile;
        invalid.dynamics.compressors[1][1].threshold_db = -61.0F;
        check(!validate_audio_profile(invalid, 48000U), "compressor threshold below range is rejected");

        invalid = profile;
        invalid.dynamics.compressors[1][1].attack_ms = 0.0F;
        check(!validate_audio_profile(invalid, 48000U), "zero compressor attack is rejected");

        invalid = profile;
        invalid.dynamics.compressors[1][1].release_ms = 2001.0F;
        check(!validate_audio_profile(invalid, 48000U), "compressor release above range is rejected");

        invalid = profile;
        invalid.dynamics.compressors[1][1].knee_db = 24.1F;
        check(!validate_audio_profile(invalid, 48000U), "compressor knee above range is rejected");

        invalid = profile;
        invalid.dynamics.compressors[1][1].makeup_gain_db = 12.1F;
        check(!validate_audio_profile(invalid, 48000U), "compressor makeup gain above range is rejected");

        // Documented lower endpoints must remain accepted.
        AudioProfile boundary;
        boundary.quad.preamp_db = -24.0F;
        boundary.quad.channels[0].gain_db = -24.0F;
        boundary.quad.channels[0].delay_ms = kMaxTimeAlignmentMs;
        boundary.quad.channels[0].bands[0] =
            {10.0F, -18.0F, 0.1F, true, FilterType::Peaking, 0.1F};
        boundary.quad.limiter_threshold = 1.0F;
        boundary.routing.sub_gain_db = -24.0F;
        boundary.enhancements.bass_amount = 1.0F;
        boundary.enhancements.bass_frequency_hz = 40.0F;
        boundary.enhancements.clarity_amount = 1.0F;
        boundary.enhancements.clarity_frequency_hz = 1500.0F;
        boundary.dynamics.low_crossover_hz = 30.0F;
        boundary.dynamics.high_crossover_hz = 45.001F;
        check(validate_audio_profile(boundary, 48000U),
              "documented lower boundary values remain valid");

        std::cout << "Audio profile validation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio profile test failed: " << error.what() << '\n';
        return 1;
    }
}
