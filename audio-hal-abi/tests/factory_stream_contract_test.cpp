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

    // This list records the factory predicate, not a normative Android mask allowlist.
    constexpr std::array<std::uint32_t, 6> factory_accepted_channels{
        0x0c, 0x10, 0x4000, 0x8000, 0xc000, 0x1c
    };

    for (const auto mask : factory_accepted_channels) {
        if (!input_channel_mask_supported(mask)) {
            return fail("factory input channel-mask predicate rejected a recovered factory-accepted mask");
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


    using mt8163::factory_contract::input_buffer_duration_us;
    using mt8163::factory_contract::input_buffer_size_20ms;
    using mt8163::factory_contract::input_format_width_bytes;
    using mt8163::factory_contract::kFactoryInputFlagFast;
    using mt8163::factory_contract::output_buffer_duration_us;

    if (output_buffer_duration_us(2U, 48000U, 2U) != 42666U ||
        output_buffer_duration_us(0U, 48000U, 2U) != 0U ||
        output_buffer_duration_us(2U, 0U, 2U) != 0U) {
        return fail("factory output buffer duration contract mismatch");
    }

    if (input_buffer_duration_us(kFactoryInputFlagFast, true, 0) != 5000U ||
        input_buffer_duration_us(0U, false, 0) != 20000U ||
        input_buffer_duration_us(0U, true, 0) != 60000U ||
        input_buffer_duration_us(0U, true, -1) != 20000U) {
        return fail("factory input buffer duration selection mismatch");
    }

    constexpr std::array<std::uint32_t, 5> format_widths{2U, 1U, 4U, 4U, 0U};
    for (std::uint32_t format = 1U; format <= 5U; ++format) {
        if (input_format_width_bytes(format) != format_widths[format - 1U]) {
            return fail("factory input format-width lookup mismatch");
        }
    }
    if (input_format_width_bytes(0U) != 2U ||
        input_format_width_bytes(6U) != 2U ||
        input_format_width_bytes(UINT32_MAX) != 2U) {
        return fail("factory input format-width fallback mismatch");
    }

    const auto input_stereo_pcm16 = input_buffer_size_20ms(44100U, 2U, 1U);
    if (!input_stereo_pcm16.valid || input_stereo_pcm16.bytes != 3528U) {
        return fail("factory 20 ms input buffer-size arithmetic mismatch");
    }
    const auto input_width_one = input_buffer_size_20ms(44100U, 2U, 2U);
    if (!input_width_one.valid || input_width_one.bytes != 1764U) {
        return fail("factory one-byte input format width mismatch");
    }
    const auto input_zero_width = input_buffer_size_20ms(44100U, 2U, 5U);
    if (!input_zero_width.valid || input_zero_width.bytes != 0U) {
        return fail("factory zero-width format quirk mismatch");
    }
    const auto input_fallback = input_buffer_size_20ms(44100U, 2U, 99U);
    if (!input_fallback.valid || input_fallback.bytes != 3528U) {
        return fail("factory input format fallback arithmetic mismatch");
    }
    if (input_buffer_size_20ms(UINT32_MAX, UINT32_MAX, 3U).valid) {
        return fail("overflowing input buffer-size request must be rejected");
    }

    return 0;
}
