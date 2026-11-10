#include "pcm_dsp/eq_layout.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        std::array<pcm_dsp::EqBand, pcm_dsp::kQuadEqBandCount> bands{};
        check(pcm_dsp::make_logarithmic_eq_layout_32(bands),
              "default 32-band layout is created");
        check(std::fabs(bands.front().frequency_hz - 20.0F) < 0.001F,
              "first band is 20 Hz");
        check(std::fabs(bands.back().frequency_hz - 20000.0F) < 0.1F,
              "last band is 20 kHz");
        for (std::size_t i = 0; i < bands.size(); ++i) {
            check(!bands[i].enabled, "layout starts with filters disabled");
            check(bands[i].type == pcm_dsp::FilterType::Peaking,
                  "default layout uses peaking filters");
            check(std::isfinite(bands[i].frequency_hz),
                  "all center frequencies are finite");
            if (i > 0U) {
                check(bands[i].frequency_hz > bands[i - 1U].frequency_hz,
                      "center frequencies increase monotonically");
                const float ratio = bands[i].frequency_hz /
                                    bands[i - 1U].frequency_hz;
                check(std::fabs(ratio - bands[1].frequency_hz /
                                         bands[0].frequency_hz) < 0.0001F,
                      "layout has constant logarithmic spacing");
            }
        }

        check(!pcm_dsp::make_logarithmic_eq_layout_32(
                  bands, 20000.0F, 20.0F),
              "reversed frequency range is rejected");
        check(!pcm_dsp::make_logarithmic_eq_layout_32(
                  bands, 20.0F, 20000.0F, 20.0F),
              "out-of-range Q is rejected");
        std::cout << "All 32-band EQ layout tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "EQ layout test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
