#ifndef MT8163_FACTORY_STREAM_CONTRACT_H
#define MT8163_FACTORY_STREAM_CONTRACT_H

#include <cstdint>

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

}  // namespace mt8163::factory_contract

#endif
