#include "mt8163_audio_core/callback_registry.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

using mt8163_audio_core::CallbackRegistry;
using mt8163_audio_core::CallbackRegistryResult;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Counter {
    std::atomic<unsigned> calls{0U};
};

void count_callback(void* cookie, const char*) noexcept {
    static_cast<Counter*>(cookie)->calls.fetch_add(1U, std::memory_order_relaxed);
}

struct BlockingCallback {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{false};
    bool release{false};
};

void blocking_callback(void* cookie, const char*) noexcept {
    auto& state = *static_cast<BlockingCallback*>(cookie);
    std::unique_lock<std::mutex> lock(state.mutex);
    state.entered = true;
    state.condition.notify_all();
    state.condition.wait(lock, [&state] { return state.release; });
}

struct SelfClearContext {
    CallbackRegistry* registry{nullptr};
    CallbackRegistry::RegistrationId id{0U};
    CallbackRegistryResult result{CallbackRegistryResult::LockFailure};
    unsigned calls{0U};
};

void self_clear_callback(void* cookie, const char*) noexcept {
    auto& state = *static_cast<SelfClearContext*>(cookie);
    ++state.calls;
    state.result = state.registry->clear_and_drain(state.id);
}

struct CrossClearContext {
    CallbackRegistry* registry{nullptr};
    CallbackRegistry::RegistrationId target{0U};
    CallbackRegistryResult result{CallbackRegistryResult::LockFailure};
    unsigned calls{0U};
};

void cross_clear_callback(void* cookie, const char*) noexcept {
    auto& state = *static_cast<CrossClearContext*>(cookie);
    ++state.calls;
    state.result = state.registry->clear_and_drain(state.target);
}

void test_register_dispatch_and_clear() {
    CallbackRegistry registry;
    Counter counter;
    CallbackRegistry::RegistrationId id = 0U;
    require(registry.register_callback(count_callback, &counter, &id) ==
                CallbackRegistryResult::Ok && id != 0U,
            "callback registers with a nonzero identity");
    CallbackRegistry::RegistrationId duplicate_id = 0U;
    require(registry.register_callback(count_callback, &counter, &duplicate_id) ==
                CallbackRegistryResult::AlreadyRegistered && duplicate_id == 0U,
            "duplicate callback and cookie are rejected");
    require(registry.dispatch("mode=normal") == 1U,
            "active callback is dispatched");
    require(counter.calls.load(std::memory_order_relaxed) == 1U,
            "callback receives one invocation");
    require(registry.clear_and_drain(id) == CallbackRegistryResult::Ok,
            "callback clear drains successfully");
    require(registry.dispatch("mode=call") == 0U,
            "cleared callback is not dispatched");
    require(registry.clear_and_drain(id) == CallbackRegistryResult::NotFound,
            "cleared registration cannot be cleared twice");
}

void test_clear_waits_for_in_flight_callback() {
    CallbackRegistry registry;
    BlockingCallback state;
    CallbackRegistry::RegistrationId id = 0U;
    require(registry.register_callback(blocking_callback, &state, &id) ==
                CallbackRegistryResult::Ok,
            "blocking callback registers");

    std::thread dispatcher([&registry] {
        require(registry.dispatch("blocked") == 1U,
                "blocking callback dispatch completes");
    });

    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.condition.wait(lock, [&state] { return state.entered; });
    }

    std::promise<void> clear_started;
    auto clear_started_future = clear_started.get_future();
    auto clearer = std::async(std::launch::async, [&registry, id, &clear_started] {
        clear_started.set_value();
        return registry.clear_and_drain(id);
    });
    clear_started_future.wait();
    require(clearer.wait_for(std::chrono::milliseconds(30)) ==
                std::future_status::timeout,
            "clear does not return while callback is in flight");

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.release = true;
    }
    state.condition.notify_all();

    dispatcher.join();
    require(clearer.get() == CallbackRegistryResult::Ok,
            "clear returns only after callback exits");
    require(registry.size() == 0U,
            "drained callback is no longer registered");
}

void test_self_clear_is_nonblocking_and_retryable() {
    CallbackRegistry registry;
    SelfClearContext state;
    state.registry = &registry;
    require(registry.register_callback(self_clear_callback, &state, &state.id) ==
                CallbackRegistryResult::Ok,
            "self-clearing callback registers");
    require(registry.dispatch(nullptr) == 1U,
            "self-clearing callback executes");
    require(state.calls == 1U &&
            state.result == CallbackRegistryResult::WouldDeadlock,
            "self-clear closes admission without waiting on itself");
    require(registry.clear_cookie_and_drain(&state) == CallbackRegistryResult::Ok,
            "external cookie retry completes the drain");
}

void test_callback_can_disable_later_callback_without_deadlock() {
    CallbackRegistry registry;
    CrossClearContext first;
    Counter second;
    CallbackRegistry::RegistrationId first_id = 0U;
    CallbackRegistry::RegistrationId second_id = 0U;
    first.registry = &registry;

    require(registry.register_callback(cross_clear_callback, &first, &first_id) ==
                CallbackRegistryResult::Ok,
            "first callback registers");
    require(registry.register_callback(count_callback, &second, &second_id) ==
                CallbackRegistryResult::Ok,
            "second callback registers");
    first.target = second_id;

    require(registry.dispatch("change") == 1U,
            "callback disabled during dispatch is skipped");
    require(first.calls == 1U &&
            first.result == CallbackRegistryResult::WouldDeadlock,
            "cross-clear never waits from inside callback context");
    require(second.calls.load(std::memory_order_relaxed) == 0U,
            "disabled callback is not invoked from captured dispatch batch");
    require(registry.clear_and_drain(second_id) == CallbackRegistryResult::Ok,
            "disabled callback drain completes externally");
    require(registry.clear_and_drain(first_id) == CallbackRegistryResult::Ok,
            "first callback drains");
}

struct CrossRegistryContext {
    CallbackRegistry* target_registry{nullptr};
    CallbackRegistry::RegistrationId target_id{0U};
    CallbackRegistryResult result{CallbackRegistryResult::LockFailure};
};

void cross_registry_clear_callback(void* cookie, const char*) noexcept {
    auto& state = *static_cast<CrossRegistryContext*>(cookie);
    state.result = state.target_registry->clear_and_drain(state.target_id);
}

void test_cross_registry_clear_never_waits_from_callback() {
    CallbackRegistry first_registry;
    CallbackRegistry second_registry;
    BlockingCallback blocked;
    CrossRegistryContext first;
    CallbackRegistry::RegistrationId first_id = 0U;
    CallbackRegistry::RegistrationId second_id = 0U;
    first.target_registry = &second_registry;

    require(second_registry.register_callback(blocking_callback, &blocked,
                                               &second_id) ==
                CallbackRegistryResult::Ok,
            "second registry blocking callback registers");
    first.target_id = second_id;
    require(first_registry.register_callback(cross_registry_clear_callback,
                                              &first, &first_id) ==
                CallbackRegistryResult::Ok,
            "first registry cross-clear callback registers");

    std::thread dispatcher([&second_registry] {
        require(second_registry.dispatch("blocked") == 1U,
                "second registry callback completes");
    });
    {
        std::unique_lock<std::mutex> lock(blocked.mutex);
        blocked.condition.wait(lock, [&blocked] { return blocked.entered; });
    }

    require(first_registry.dispatch("cross-clear") == 1U,
            "cross-registry callback executes");
    require(first.result == CallbackRegistryResult::WouldDeadlock,
            "callback context never waits for another registry's in-flight callback");

    {
        std::lock_guard<std::mutex> lock(blocked.mutex);
        blocked.release = true;
    }
    blocked.condition.notify_all();
    dispatcher.join();
    require(second_registry.clear_and_drain(second_id) ==
                CallbackRegistryResult::Ok,
            "external caller completes the second registry drain");
    require(first_registry.clear_and_drain(first_id) ==
                CallbackRegistryResult::Ok,
            "first registry callback drains");
}

void test_cookie_clear_and_capacity() {
    CallbackRegistry registry;
    Counter shared;
    std::array<CallbackRegistry::RegistrationId, 2U> ids{};
    require(registry.register_callback(count_callback, &shared, &ids[0]) ==
                CallbackRegistryResult::Ok,
            "first cookie registration succeeds");
    require(registry.register_callback(
                +[](void* cookie, const char*) noexcept {
                    static_cast<Counter*>(cookie)->calls.fetch_add(
                        1U, std::memory_order_relaxed);
                }, &shared, &ids[1]) == CallbackRegistryResult::Ok,
            "second callback for same cookie succeeds");
    require(registry.clear_cookie_and_drain(&shared) == CallbackRegistryResult::Ok,
            "cookie clear removes and drains all matching callbacks");
    require(registry.size() == 0U,
            "cookie clear removes every matching registration");

    std::array<Counter, mt8163_audio_core::kMaxParameterCallbacks> counters{};
    for (auto& counter : counters) {
        CallbackRegistry::RegistrationId id = 0U;
        require(registry.register_callback(count_callback, &counter, &id) ==
                    CallbackRegistryResult::Ok,
                "registry accepts every fixed-capacity slot");
    }
    Counter overflow;
    CallbackRegistry::RegistrationId overflow_id = 0U;
    require(registry.register_callback(count_callback, &overflow, &overflow_id) ==
                CallbackRegistryResult::CapacityExceeded,
            "registry rejects registration beyond fixed capacity");
}

void test_shutdown_is_terminal_and_drained() {
    CallbackRegistry registry;
    Counter counter;
    CallbackRegistry::RegistrationId id = 0U;
    require(registry.register_callback(count_callback, &counter, &id) ==
                CallbackRegistryResult::Ok,
            "callback registers before shutdown");
    require(registry.shutdown_and_drain() == CallbackRegistryResult::Ok,
            "shutdown closes admission and drains callbacks");
    require(registry.register_callback(count_callback, &counter, &id) ==
                CallbackRegistryResult::ShuttingDown,
            "shutdown permanently rejects new callbacks");
    require(registry.dispatch("ignored") == 0U,
            "shutdown registry never dispatches callbacks");
    require(registry.clear_and_drain(id) == CallbackRegistryResult::NotFound,
            "shutdown retires every registration after draining");
}

}  // namespace

int main() {
    test_register_dispatch_and_clear();
    test_clear_waits_for_in_flight_callback();
    test_self_clear_is_nonblocking_and_retryable();
    test_callback_can_disable_later_callback_without_deadlock();
    test_cross_registry_clear_never_waits_from_callback();
    test_cookie_clear_and_capacity();
    test_shutdown_is_terminal_and_drained();
    std::cout << "AudioCore callback registry tests passed\n";
    return 0;
}
