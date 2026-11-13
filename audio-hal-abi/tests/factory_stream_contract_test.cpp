#include "mt8163_factory_stream_contract.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using mt8163::factory_contract::input_channel_mask_supported;
    using mt8163::factory_contract::normalize_input_channel_mask;
    using mt8163::factory_contract::normalize_input_format;
    using mt8163::factory_contract::output_sample_rate_supported;

    constexpr std::array<std::uint32_t, 12> supported_rates{
        8000, 11025, 12000, 22050, 24000, 32000,
        44100, 48000, 88200, 96000, 176400, 192000
    };

    for (const auto rate : supported_rates) {
        if (!output_sample_rate_supported(rate)) {
            return fail("factory output sample-rate allowlist rejected a recovered rate");
        }
    }

    constexpr std::array<std::uint32_t, 12> unsupported_rates{
        0, 4000, 16000, 22000, 38400, 64000,
        88201, 128000, 176399, 192001, 256000, UINT32_MAX
    };

    for (const auto rate : unsupported_rates) {
        if (output_sample_rate_supported(rate)) {
            return fail("factory output sample-rate allowlist accepted an unlisted rate");
        }
    }

    const auto pcm16 = normalize_input_format(1);
    if (!pcm16.accepted || pcm16.value != 1) {
        return fail("PCM 16-bit input format should pass unchanged");
    }

    for (const auto format : {0U, 2U, 3U, 4U, 0x100U, UINT32_MAX}) {
        const auto normalized = normalize_input_format(format);
        if (normalized.accepted || normalized.value != 1) {
            return fail("unsupported input format must normalize to PCM 16-bit");
        }
    }

    constexpr std::array<std::uint32_t, 6> supported_channels{
        0x0c, 0x10, 0x4000, 0x8000, 0xc000, 0x1c
    };

    for (const auto mask : supported_channels) {
        if (!input_channel_mask_supported(mask)) {
            return fail("factory input channel-mask predicate rejected a recovered mask");
        }
    }

    constexpr std::array<std::uint32_t, 7> unsupported_channels{
        0, 1, 2, 3, 0x08, 0x4001, 0x8004
    };

    for (const auto mask : unsupported_channels) {
        if (input_channel_mask_supported(mask)) {
            return fail("factory input channel-mask predicate accepted an invalid mask");
        }
        const auto normalized = normalize_input_channel_mask(mask);
        if (normalized.accepted || normalized.value != 0x0c) {
            return fail("unsupported input channel mask must normalize to stereo");
        }
    }

    return 0;
}
