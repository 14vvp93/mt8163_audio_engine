#include "mt8163_audio_core/callback_registry.hpp"
#include "mt8163_audio_core/mute_arbiter.hpp"
#include "mt8163_audio_core/snapshot_publisher.hpp"

namespace mt8163_audio_core {
namespace {

// This compile-only probe instantiates the actual MuteSnapshot publisher in
// both host and Android ARM builds, including the 32-bit lock-free assertion.
[[maybe_unused]] bool snapshot_publisher_compile_check(
    const MuteSnapshot& initial, const MuteSnapshot& update) noexcept {
    SnapshotPublisher<MuteSnapshot> publisher(initial);
    auto reader = publisher.try_acquire();
    return static_cast<bool>(reader) && publisher.publish(update);
}

[[maybe_unused]] bool callback_registry_compile_check() noexcept {
    CallbackRegistry registry;
    CallbackRegistry::RegistrationId id = 0U;
    const auto callback = +[](void*, const char*) noexcept {};
    if (registry.register_callback(callback, &registry, &id) !=
        CallbackRegistryResult::Ok) {
        return false;
    }
    return registry.clear_and_drain(id) == CallbackRegistryResult::Ok;
}

}  // namespace
}  // namespace mt8163_audio_core
