#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

enum class LinkwitzRileyOrder : std::uint8_t {
    Db12 = 2,
    Db24 = 4,
    Db36 = 6,
    Db48 = 8
};

// One mono crossover instance. Use one instance per output channel.
// LR12/LR36 high branch polarity is inverted to obtain the intended phase sum.
class LinkwitzRileyCrossover {
public:
    bool configure(std::uint32_t sample_rate_hz, float frequency_hz,
                   LinkwitzRileyOrder order);
    void reset();
    void process(float input, float& low_output, float& high_output);

    float frequency() const { return frequency_hz_; }
    LinkwitzRileyOrder order() const { return order_; }

private:
    struct Coefficients {
        float b0{1.0F};
        float b1{0.0F};
        float b2{0.0F};
        float a1{0.0F};
        float a2{0.0F};
    };
    struct State { float z1{0.0F}; float z2{0.0F}; };
    struct Section { Coefficients low; Coefficients high; State low_state; State high_state; };

    static float run(float input, const Coefficients& c, State& state);
    static Coefficients first_order(float sample_rate, float frequency, bool high);
    static Coefficients second_order(float sample_rate, float frequency,
                                     float q, bool high);

    std::array<Section, 4> sections_{};
    std::size_t section_count_{0};
    std::uint32_t sample_rate_hz_{0};
    float frequency_hz_{0.0F};
    LinkwitzRileyOrder order_{LinkwitzRileyOrder::Db24};
    bool invert_high_{false};
};

} // namespace pcm_dsp
