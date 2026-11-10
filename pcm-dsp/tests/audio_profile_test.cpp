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

        std::cout << "Audio profile validation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio profile test failed: " << error.what() << '\n';
        return 1;
    }
}
