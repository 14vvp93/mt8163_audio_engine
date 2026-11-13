#include "audio_core/audio_state.hpp"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace {

void test_initial_state() {
    audio_core::AudioState state;
    const auto snapshot = state.snapshot();

    assert(snapshot.generation == 0U);
    assert(snapshot.source == audio_core::AudioSource::None);
    assert(snapshot.route == audio_core::OutputRoute::None);
    assert(snapshot.mute_reasons == 0U);
    assert(snapshot.master_volume == 1.0F);
    assert(!snapshot.dsp_enabled);
    assert(!snapshot.muted());
}

void test_updates_and_generation() {
    audio_core::AudioState state;

    assert(state.set_source(audio_core::AudioSource::Music));
    assert(state.set_route(audio_core::OutputRoute::Speaker));
    assert(state.set_master_volume(0.75F));
    assert(state.set_dsp_enabled(true));
    assert(state.set_mute(audio_core::MuteReason::User, true));

    auto snapshot = state.snapshot();
    assert(snapshot.generation == 5U);
    assert(snapshot.source == audio_core::AudioSource::Music);
    assert(snapshot.route == audio_core::OutputRoute::Speaker);
    assert(snapshot.master_volume == 0.75F);
    assert(snapshot.dsp_enabled);
    assert(snapshot.muted());

    assert(state.set_mute(audio_core::MuteReason::Call, true));
    assert(state.set_mute(audio_core::MuteReason::User, false));
    snapshot = state.snapshot();
    assert(snapshot.muted());
    assert(snapshot.mute_reasons ==
           static_cast<std::uint32_t>(audio_core::MuteReason::Call));
}

void test_idempotent_updates() {
    audio_core::AudioState state;

    assert(state.set_source(audio_core::AudioSource::None));
    assert(state.set_route(audio_core::OutputRoute::None));
    assert(state.set_master_volume(1.0F));
    assert(state.set_dsp_enabled(false));
    assert(state.set_mute(audio_core::MuteReason::User, false));
    assert(state.snapshot().generation == 0U);
}

void test_rejects_invalid_values() {
    audio_core::AudioState state;

    assert(!state.set_source(static_cast<audio_core::AudioSource>(255U)));
    assert(!state.set_route(static_cast<audio_core::OutputRoute>(255U)));
    assert(!state.set_mute(static_cast<audio_core::MuteReason>(0U), true));
    assert(!state.set_mute(static_cast<audio_core::MuteReason>(3U), true));
    assert(!state.set_master_volume(-0.01F));
    assert(!state.set_master_volume(1.01F));
    assert(!state.set_master_volume(std::numeric_limits<float>::quiet_NaN()));
    assert(!state.set_master_volume(std::numeric_limits<float>::infinity()));
    assert(state.snapshot().generation == 0U);
}

void test_concurrent_control_updates() {
    constexpr std::uint32_t kThreads = 4U;
    constexpr std::uint32_t kUpdatesPerThread = 1000U;
    audio_core::AudioState state;
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (std::uint32_t thread_index = 0; thread_index < kThreads; ++thread_index) {
        workers.emplace_back([&state, &failed, thread_index]() {
            for (std::uint32_t i = 0; i < kUpdatesPerThread; ++i) {
                const float volume = ((i + thread_index) % 100U) / 100.0F;
                if (!state.set_master_volume(volume)) {
                    failed.store(true, std::memory_order_relaxed);
                }
                const bool enabled = ((i + thread_index) & 1U) != 0U;
                if (!state.set_dsp_enabled(enabled)) {
                    failed.store(true, std::memory_order_relaxed);
                }
                (void)state.snapshot();
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    const auto snapshot = state.snapshot();
    assert(!failed.load(std::memory_order_relaxed));
    assert(snapshot.generation <=
           static_cast<std::uint64_t>(kThreads * kUpdatesPerThread * 2U));
    assert(std::isfinite(snapshot.master_volume));
    assert(snapshot.master_volume >= 0.0F && snapshot.master_volume <= 1.0F);
}

} // namespace

int main() {
    test_initial_state();
    test_updates_and_generation();
    test_idempotent_updates();
    test_rejects_invalid_values();
    test_concurrent_control_updates();
    return 0;
}
