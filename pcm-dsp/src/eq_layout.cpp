#include "pcm_dsp/eq_layout.hpp"

#include <cmath>

namespace pcm_dsp {

bool make_logarithmic_eq_layout_32(
        std::array<EqBand, kQuadEqBandCount>& bands,
        float minimum_frequency_hz,
        float maximum_frequency_hz,
        float q,
        FilterType type) {
    if (!std::isfinite(minimum_frequency_hz) ||
        !std::isfinite(maximum_frequency_hz) ||
        !std::isfinite(q) ||
        minimum_frequency_hz < 10.0F ||
        maximum_frequency_hz <= minimum_frequency_hz ||
        maximum_frequency_hz >= 22000.0F ||
        q < 0.1F || q > 10.0F) {
        return false;
    }

    const float log_min = std::log(minimum_frequency_hz);
    const float log_span = std::log(maximum_frequency_hz) - log_min;
    for (std::size_t i = 0; i < kQuadEqBandCount; ++i) {
        const float position = static_cast<float>(i) /
            static_cast<float>(kQuadEqBandCount - 1U);
        bands[i] = {
            std::exp(log_min + log_span * position),
            0.0F,
            q,
            false,
            type,
            1.0F
        };
    }
    return true;
}

} // namespace pcm_dsp
