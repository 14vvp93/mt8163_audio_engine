#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>

namespace mt8163_audio_core {

constexpr std::size_t kMaxParameterCallbacks = 16U;

enum class CallbackRegistryResult : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    CapacityExceeded,
    AlreadyRegistered,
    NotFound,
    WouldDeadlock,
    LockFailure,
    ShuttingDown
};

class CallbackRegistry final {
public:
    using Callback = void (*)(void* cookie, const char* parameters) noexcept;
    using RegistrationId = std::uint64_t;

    CallbackRegistry() noexcept = default;
    CallbackRegistry(const CallbackRegistry&) = delete;
    CallbackRegistry& operator=(const CallbackRegistry&) = delete;
    CallbackRegistry(CallbackRegistry&&) = delete;
    CallbackRegistry& operator=(CallbackRegistry&&) = delete;

    ~CallbackRegistry() {
        if (shutdown_and_drain() != CallbackRegistryResult::Ok) {
            std::terminate();
        }
    }

    CallbackRegistryResult register_callback(
        Callback callback, void* cookie, RegistrationId* out_id) noexcept {
        if (out_id == nullptr) return CallbackRegistryResult::InvalidArgument;
        *out_id = 0U;
        if (callback == nullptr || cookie == nullptr) {
            return CallbackRegistryResult::InvalidArgument;
        }

        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_) return CallbackRegistryResult::ShuttingDown;

            for (const Slot& slot : slots_) {
                if (slot.active && slot.callback == callback &&
                    slot.cookie == cookie) {
                    return CallbackRegistryResult::AlreadyRegistered;
                }
            }

            if (next_id_ == 0U ||
                next_id_ == std::numeric_limits<RegistrationId>::max()) {
                return CallbackRegistryResult::CapacityExceeded;
            }

            for (Slot& slot : slots_) {
                if (!slot.active && slot.in_flight == 0U) {
                    slot.callback = callback;
                    slot.cookie = cookie;
                    slot.id = next_id_++;
                    slot.active = true;
                    *out_id = slot.id;
                    return CallbackRegistryResult::Ok;
                }
            }
            return CallbackRegistryResult::CapacityExceeded;
        } catch (...) {
            return CallbackRegistryResult::LockFailure;
        }
    }

    CallbackRegistryResult clear_and_drain(RegistrationId id) noexcept {
        if (id == 0U) return CallbackRegistryResult::InvalidArgument;

        try {
            std::unique_lock<std::mutex> lock(mutex_);
            Slot* target = find_slot_locked(id);
            if (target == nullptr) return CallbackRegistryResult::NotFound;

            target->active = false;
            if (target->in_flight != 0U && inside_callback_) {
                return CallbackRegistryResult::WouldDeadlock;
            }

            drained_.wait(lock, [this, id] {
                const Slot* slot = find_slot_locked(id);
                return slot == nullptr || slot->in_flight == 0U;
            });
            return CallbackRegistryResult::Ok;
        } catch (...) {
            return CallbackRegistryResult::LockFailure;
        }
    }

    CallbackRegistryResult clear_cookie_and_drain(void* cookie) noexcept {
        if (cookie == nullptr) return CallbackRegistryResult::InvalidArgument;

        std::array<RegistrationId, kMaxParameterCallbacks> ids{};
        std::size_t count = 0U;

        try {
            std::unique_lock<std::mutex> lock(mutex_);
            for (Slot& slot : slots_) {
                if (slot.id != 0U && slot.cookie == cookie) {
                    slot.active = false;
                    ids[count++] = slot.id;
                }
            }
            if (count == 0U) return CallbackRegistryResult::NotFound;
            bool has_in_flight = false;
            for (std::size_t i = 0U; i < count; ++i) {
                const Slot* slot = find_slot_locked(ids[i]);
                has_in_flight = has_in_flight ||
                    (slot != nullptr && slot->in_flight != 0U);
            }
            if (has_in_flight && inside_callback_) {
                return CallbackRegistryResult::WouldDeadlock;
            }

            drained_.wait(lock, [this, &ids, count] {
                for (std::size_t i = 0U; i < count; ++i) {
                    const Slot* slot = find_slot_locked(ids[i]);
                    if (slot != nullptr && slot->in_flight != 0U) return false;
                }
                return true;
            });
            return CallbackRegistryResult::Ok;
        } catch (...) {
            return CallbackRegistryResult::LockFailure;
        }
    }

    CallbackRegistryResult shutdown_and_drain() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            shutting_down_ = true;
            for (Slot& slot : slots_) slot.active = false;

            bool has_in_flight = false;
            for (const Slot& slot : slots_) {
                has_in_flight = has_in_flight || slot.in_flight != 0U;
            }
            if (has_in_flight && inside_callback_) {
                return CallbackRegistryResult::WouldDeadlock;
            }

            drained_.wait(lock, [this] {
                for (const Slot& slot : slots_) {
                    if (slot.in_flight != 0U) return false;
                }
                return true;
            });
            return CallbackRegistryResult::Ok;
        } catch (...) {
            return CallbackRegistryResult::LockFailure;
        }
    }

    std::size_t dispatch(const char* parameters) noexcept {
        std::array<DispatchItem, kMaxParameterCallbacks> pending{};
        std::size_t count = 0U;

        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_) return 0U;

            for (std::size_t i = 0U; i < slots_.size(); ++i) {
                Slot& slot = slots_[i];
                if (!slot.active) continue;
                ++slot.in_flight;
                pending[count++] = DispatchItem{i, slot.id, slot.callback,
                                                slot.cookie};
            }
        } catch (...) {
            return 0U;
        }

        std::size_t invoked = 0U;
        for (std::size_t i = 0U; i < count; ++i) {
            const DispatchItem& item = pending[i];
            bool should_invoke = false;
            try {
                std::lock_guard<std::mutex> lock(mutex_);
                const Slot& slot = slots_[item.slot_index];
                should_invoke = slot.id == item.id && slot.active;
            } catch (...) {
                release_dispatch(item);
                continue;
            }

            if (should_invoke) {
                const bool previous = inside_callback_;
                inside_callback_ = true;
                item.callback(item.cookie, parameters);
                inside_callback_ = previous;
                ++invoked;
            }
            release_dispatch(item);
        }
        return invoked;
    }

    std::size_t size() const noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            std::size_t count = 0U;
            for (const Slot& slot : slots_) {
                if (slot.active) ++count;
            }
            return count;
        } catch (...) {
            return 0U;
        }
    }

private:
    struct Slot {
        Callback callback{nullptr};
        void* cookie{nullptr};
        RegistrationId id{0U};
        std::uint32_t in_flight{0U};
        bool active{false};
    };

    struct DispatchItem {
        std::size_t slot_index{0U};
        RegistrationId id{0U};
        Callback callback{nullptr};
        void* cookie{nullptr};
    };

    Slot* find_slot_locked(RegistrationId id) noexcept {
        for (Slot& slot : slots_) {
            if (slot.id == id) return &slot;
        }
        return nullptr;
    }

    const Slot* find_slot_locked(RegistrationId id) const noexcept {
        for (const Slot& slot : slots_) {
            if (slot.id == id) return &slot;
        }
        return nullptr;
    }

    void release_dispatch(const DispatchItem& item) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            Slot& slot = slots_[item.slot_index];
            if (slot.id == item.id && slot.in_flight != 0U) {
                --slot.in_flight;
            }
            drained_.notify_all();
        } catch (...) {
            std::terminate();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable drained_;
    std::array<Slot, kMaxParameterCallbacks> slots_{};
    RegistrationId next_id_{1U};
    bool shutting_down_{false};

    inline static thread_local bool inside_callback_{false};
};

}  // namespace mt8163_audio_core
