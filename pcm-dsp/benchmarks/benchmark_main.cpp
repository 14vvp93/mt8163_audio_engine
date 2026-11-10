#include "pcm_dsp/quad_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Result {
    double average_us{0.0};
    double p95_us{0.0};
    double max_us{0.0};
    double realtime_percent{0.0};
};

Result run_case(std::uint32_t sample_rate, std::size_t frames) {
    constexpr std::size_t channels = pcm_dsp::kOutputChannelCount;
    constexpr std::size_t warmup_blocks = 200U;
    constexpr std::size_t measured_blocks = 2000U;

    pcm_dsp::QuadEngine engine;
    if (!engine.configure(sample_rate)) return {};

    pcm_dsp::QuadParameters parameters;
    parameters.enabled = true;
    parameters.preamp_db = -3.0F;
    parameters.limiter_enabled = true;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        parameters.channels[channel].gain_db =
            channel == 0U ? 1.0F : -1.0F;
        parameters.channels[channel].delay_ms =
            static_cast<float>(channel) * 1.5F;
        for (std::size_t band = 0; band < pcm_dsp::kQuadEqBandCount; ++band) {
            auto& eq = parameters.channels[channel].bands[band];
            eq.enabled = true;
            eq.type = pcm_dsp::FilterType::Peaking;
            eq.frequency_hz = 25.0F *
                std::pow(800.0F, static_cast<float>(band) /
                    static_cast<float>(pcm_dsp::kQuadEqBandCount - 1U));
            eq.gain_db = (band % 2U == 0U) ? 1.5F : -1.0F;
            eq.q = 1.0F;
        }
    }

    pcm_dsp::QuadOutputRoutingParameters routing;
    routing.enabled = true;
    routing.bass_management_enabled = true;
    routing.crossover_frequency_hz = 80.0F;
    routing.crossover_order = pcm_dsp::LinkwitzRileyOrder::Db24;
    if (!engine.configure_routing(routing)) return {};

    std::vector<float> input(frames * channels);
    std::vector<float> output(frames * channels);
    std::vector<float> sub(frames);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = 0.2F * std::sin(
            2.0F * 3.14159265358979323846F * 997.0F *
            static_cast<float>(i / channels) /
            static_cast<float>(sample_rate));
    }

    for (std::size_t i = 0; i < warmup_blocks; ++i) {
        if (!engine.process(input.data(), output.data(), frames,
                            parameters, sub.data())) return {};
    }

    std::vector<double> timings;
    timings.reserve(measured_blocks);
    for (std::size_t i = 0; i < measured_blocks; ++i) {
        const auto start = Clock::now();
        if (!engine.process(input.data(), output.data(), frames,
                            parameters, sub.data())) return {};
        const auto end = Clock::now();
        timings.push_back(
            std::chrono::duration<double, std::micro>(end - start).count());
    }

    std::sort(timings.begin(), timings.end());
    const double average = std::accumulate(
        timings.begin(), timings.end(), 0.0) /
        static_cast<double>(timings.size());
    const double p95 = timings[
        static_cast<std::size_t>(0.95 * static_cast<double>(timings.size() - 1U))];
    const double max = timings.back();
    const double block_budget_us =
        1'000'000.0 * static_cast<double>(frames) /
        static_cast<double>(sample_rate);
    return {average, p95, max, average * 100.0 / block_budget_us};
}
} // namespace

int main() {
    bool passed = true;
    for (const std::uint32_t rate : {44100U, 48000U}) {
        for (const std::size_t frames : {128U, 256U, 512U}) {
            const Result result = run_case(rate, frames);
            const double budget_us =
                1'000'000.0 * static_cast<double>(frames) /
                static_cast<double>(rate);
            std::cout << rate << " Hz, " << frames << " frames: avg "
                      << result.average_us << " us, p95 " << result.p95_us
                      << " us, max " << result.max_us << " us, average CPU "
                      << result.realtime_percent << "% of one core, block "
                      << budget_us << " us\n";
            if (!(result.average_us > 0.0) || !std::isfinite(result.max_us)) {
                passed = false;
            }
        }
    }
    return passed ? 0 : 1;
}
