#pragma once

#include "pcm_dsp/engine.hpp"
#include "pcm_dsp/time_alignment.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcm_dsp {

constexpr std::size_t kOutputChannelCount = 4;
constexpr std::size_t kQuadEqBandCount = 32;

enum class OutputChannel : std::uint8_t {
    FrontLeft = 0,
    FrontRight = 1,
    RearLeft = 2,
    RearRight = 3
};

struct QuadChannelParameters {
    float gain_db{0.0F};
    float delay_ms{0.0F};
    std::array<EqBand, kQuadEqBandCount> bands{};
};

struct QuadParameters {
    bool enabled{false};
    float preamp_db{0.0F};
    bool limiter_enabled{true};
    float limiter_threshold{0.98F};
    float limiter_attack_ms{5.0F};
    float limiter_release_ms{80.0F};
    std::array<QuadChannelParameters, kOutputChannelCount> channels{};
};

// Four-channel, interleaved Float32 DSP core.
// Samples are normalized audio values; input/output may point to the same buffer.
// Configure before process. Parameters must remain immutable for each call.
// The processing path performs no heap allocation and takes no locks.
class QuadEngine {
public:
    bool configure(std::uint32_t sample_rate_hz);
    void reset();

    bool process(const float* input,
                 float* output,
                 std::size_t frames,
                 const QuadParameters& parameters);

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

    struct Bank {
        std::array<std::array<BiquadCoefficients, kQuadEqBandCount>,
                   kOutputChannelCount> coefficients{};
        std::array<std::array<BiquadState, kQuadEqBandCount>,
                   kOutputChannelCount> states{};
        std::array<std::array<bool, kQuadEqBandCount>,
                   kOutputChannelCount> filter_enabled{};
        std::array<float, kOutputChannelCount> gains{{1.0F, 1.0F, 1.0F, 1.0F}};
    };

    static BiquadCoefficients make_filter(float sample_rate,
                                          const EqBand& band);
    static float run_biquad(float input,
                            const BiquadCoefficients& coefficients,
                            BiquadState& state);
    static float db_to_gain(float db);
    static float clamp(float value, float low, float high);
    static bool same_coefficients(const BiquadCoefficients& a,
                                  const BiquadCoefficients& b);

    std::uint32_t sample_rate_hz_{0};
    std::array<Bank, 2> banks_{};
    std::size_t active_bank_{0};
    std::array<float, kOutputChannelCount> limiter_envelope_{};
    QuadTimeAlignment time_alignment_{};
};

} // namespace pcm_dsp
