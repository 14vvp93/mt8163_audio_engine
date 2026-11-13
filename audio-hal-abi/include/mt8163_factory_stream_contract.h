#ifndef MT8163_FACTORY_STREAM_CONTRACT_H
#define MT8163_FACTORY_STREAM_CONTRACT_H

#include <cstdint>
#include <limits>

namespace mt8163::factory_contract {

struct normalized_value {
    std::uint32_t value;
    bool accepted;
};

constexpr bool output_sample_rate_supported(std::uint32_t rate) noexcept {
    switch (rate) {
        case 8000:
        case 11025:
        case 12000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
        case 88200:
        case 96000:
        case 176400:
        case 192000:
            return true;
        default:
            return false;
    }
}

constexpr normalized_value normalize_input_format(std::uint32_t format) noexcept {
    constexpr std::uint32_t pcm_16_bit = 1;
    return {format == pcm_16_bit ? format : pcm_16_bit, format == pcm_16_bit};
}

constexpr bool input_channel_mask_supported(std::uint32_t mask) noexcept {
    constexpr std::uint32_t stereo = 0x0c;
    constexpr std::uint32_t mono_bit = 0x10;
    constexpr std::uint32_t voice_mask = 0xc000;

    if (mask == stereo || (mask & mono_bit) != 0) {
        return true;
    }

    return (mask & voice_mask) != 0 && (mask & ~voice_mask) == 0;
}

constexpr normalized_value normalize_input_channel_mask(std::uint32_t mask) noexcept {
    constexpr std::uint32_t stereo = 0x0c;
    const bool accepted = input_channel_mask_supported(mask);
    return {accepted ? mask : stereo, accepted};
}

constexpr std::uint32_t kFactoryOutputBufferBytes = 8192U;
constexpr std::uint32_t kFactoryInputFlagFast = 0x01U;

constexpr std::uint64_t output_buffer_duration_us(
    std::uint32_t channels, std::uint32_t sample_rate,
    std::uint32_t bytes_per_sample) noexcept {
    if (channels == 0U || sample_rate == 0U || bytes_per_sample == 0U) {
        return 0U;
    }
    const std::uint64_t channel_rate =
        static_cast<std::uint64_t>(channels) * sample_rate;
    if (channel_rate >
        std::numeric_limits<std::uint64_t>::max() / bytes_per_sample) {
        return 0U;
    }
    const std::uint64_t bytes_per_second = channel_rate * bytes_per_sample;
    return (1000000ULL * kFactoryOutputBufferBytes) / bytes_per_second;
}

constexpr std::uint32_t input_buffer_duration_us(
    std::uint32_t flags, bool wcn_controller_path,
    std::int32_t wcn_controller_query_result) noexcept {
    if ((flags & kFactoryInputFlagFast) != 0U) return 5000U;
    if (wcn_controller_path && wcn_controller_query_result == 0) {
        return 60000U;
    }
    return 20000U;
}

constexpr std::uint32_t input_format_width_bytes(
    std::uint32_t format) noexcept {
    switch (format) {
        case 1U: return 2U;
        case 2U: return 1U;
        case 3U: return 4U;
        case 4U: return 4U;
        case 5U: return 0U;
        default: return 2U;
    }
}

struct input_buffer_size_result {
    std::uint64_t bytes;
    bool valid;
};

// The second argument is deliberately kept raw: its vendor caller-side
// meaning has not been recovered. The factory helper uses a 20 ms quantity.
constexpr input_buffer_size_result input_buffer_size_20ms(
    std::uint32_t sample_rate, std::uint32_t raw_third_argument,
    std::uint32_t format) noexcept {
    const std::uint64_t width = input_format_width_bytes(format);
    if (width == 0U || sample_rate == 0U || raw_third_argument == 0U) {
        return {0U, true};
    }
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t rate = sample_rate;
    if (rate > max / raw_third_argument) return {0U, false};
    const std::uint64_t rate_argument = rate * raw_third_argument;
    if (rate_argument > max / width) return {0U, false};
    return {(rate_argument * width) / 50U, true};
}

}  // namespace mt8163::factory_contract

#endif
