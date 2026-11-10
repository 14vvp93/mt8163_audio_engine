#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

constexpr std::size_t kEqBandCount = 10;

enum class FilterType : std::uint8_t {
    Peaking,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass
};

struct EqBand {
    float frequency_hz;
    float gain_db;
    float q;
    bool enabled;
    FilterType type{FilterType::Peaking};
    // RBJ shelf slope S. Valid values are clamped to 0.1..1.0.
    float shelf_slope{1.0F};
};

struct Parameters {
    bool enabled{false};
    float preamp_db{0.0F};
    // -1 attenuates the right channel; +1 attenuates the left channel.
    float balance{0.0F};
    bool limiter_enabled{true};
    float limiter_threshold{0.98F};
    float limiter_attack_ms{5.0F};
    float limiter_release_ms{80.0F};
    std::array<EqBand, kEqBandCount> bands{{
        {31.0F, 0.0F, 1.0F, false},
        {62.0F, 0.0F, 1.0F, false},
        {125.0F, 0.0F, 1.0F, false},
        {250.0F, 0.0F, 1.0F, false},
        {500.0F, 0.0F, 1.0F, false},
        {1000.0F, 0.0F, 1.0F, false},
        {2000.0F, 0.0F, 1.0F, false},
        {4000.0F, 0.0F, 1.0F, false},
        {8000.0F, 0.0F, 1.0F, false},
        {16000.0F, 0.0F, 1.0F, false}
    }};
};

class Engine {
public:
    bool configure(std::uint32_t sample_rate_hz);
    void reset();

    // Processes interleaved signed PCM16 stereo. Parameters must remain
    // immutable for the duration of this call. No memory is allocated here.
    // Filter coefficients transition linearly across each processing block.
    bool process(const std::int16_t* input,
                 std::int16_t* output,
                 std::size_t frames,
                 const Parameters& parameters);

    std::uint32_t sample_rate() const { return sample_rate_hz_; }

private:
    struct BiquadCoefficients {
        float b0{1.0F};
        float b1{0.0F};
        float b2{0.0F};
        float a1{0.0F};
        float a2{0.0F};
    };

    struct BiquadState {
        float z1{0.0F};
        float z2{0.0F};
    };

    static BiquadCoefficients make_filter(float sample_rate,
                                          const EqBand& band);
    static BiquadCoefficients interpolate(const BiquadCoefficients& from,
                                          const BiquadCoefficients& to,
                                          float amount);
    static float run_biquad(float input,
                            const BiquadCoefficients& coefficients,
                            BiquadState& state);
    static float db_to_gain(float db);
    static float clamp(float value, float low, float high);
    static std::int16_t to_pcm16(float sample);

    std::uint32_t sample_rate_hz_{0};
    std::array<std::array<BiquadState, 2>, kEqBandCount> states_{};
    std::array<BiquadCoefficients, kEqBandCount> current_coefficients_{};
    float limiter_envelope_{0.0F};
};

} // namespace pcm_dsp
