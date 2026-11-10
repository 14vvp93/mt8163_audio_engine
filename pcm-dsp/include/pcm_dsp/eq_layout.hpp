#pragma once

#include "pcm_dsp/quad_engine.hpp"

#include <array>

namespace pcm_dsp {

// Creates 32 logarithmically spaced PEQ center frequencies. Bands are disabled
// by default; callers can copy the returned layout to each output channel and
// set gain_db/enabled independently.
bool make_logarithmic_eq_layout_32(
    std::array<EqBand, kQuadEqBandCount>& bands,
    float minimum_frequency_hz = 20.0F,
    float maximum_frequency_hz = 20000.0F,
    float q = 1.0F,
    FilterType type = FilterType::Peaking);

} // namespace pcm_dsp
