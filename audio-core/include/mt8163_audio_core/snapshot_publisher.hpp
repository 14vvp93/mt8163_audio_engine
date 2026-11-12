#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace mt8163_audio_core {

// Bounded single-writer / multi-reader snapshot publication.
// Readers never take a mutex and retry a fixed number of times. The writer
// never waits: publish() returns false when every inactive slot is pinned.
// A failed reader acquisition must reuse its previous snapshot for that block.
// The publisher must outlive every ReadHandle acquired from it.
template <typename T, std::size_t SlotCount = 3U>
class SnapshotPublisher {
    static_assert(SlotCount >= 3U, "at least three slots are required");
    static_assert(SlotCount <= std::numeric_limits<std::uint32_t>::max(),
                  "slot index must fit in uint32_t");
    static_assert(std::is_default_constructible<T>::value,
                  "snapshot type must be default constructible");
    static_assert(std::is_trivially_copyable<T>::value,
                  "published snapshots must be trivially copyable");
    static_assert(std::is_nothrow_copy_assignable<T>::value,
                  "snapshot assignment must not throw");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "32-bit atomics must be lock-free on the target ABI");

public:
    class ReadHandle {
    public:
        ReadHandle() noexcept = default;
        ReadHandle(const ReadHandle&) = delete;
        ReadHandle& operator=(const ReadHandle&) = delete;

        ReadHandle(ReadHandle&& other) noexcept
            : owner_(other.owner_), index_(other.index_), value_(other.value_) {
            other.owner_ = nullptr;
            other.value_ = nullptr;
        }

        ReadHandle& operator=(ReadHandle&& other) noexcept {
            if (this != &other) {
                reset();
                owner_ = other.owner_;
                index_ = other.index_;
                value_ = other.value_;
                other.owner_ = nullptr;
                other.value_ = nullptr;
            }
            return *this;
        }

        ~ReadHandle() { reset(); }

        explicit operator bool() const noexcept { return value_ != nullptr; }
        const T& operator*() const noexcept { return *value_; }
        const T* operator->() const noexcept { return value_; }

        void reset() noexcept {
            if (owner_ != nullptr) owner_->release(index_);
            owner_ = nullptr;
            value_ = nullptr;
        }

    private:
        friend class SnapshotPublisher<T, SlotCount>;
        ReadHandle(SnapshotPublisher* owner, std::uint32_t index,
                   const T* value) noexcept
            : owner_(owner), index_(index), value_(value) {}

        SnapshotPublisher* owner_{nullptr};
        std::uint32_t index_{0U};
        const T* value_{nullptr};
    };

    explicit SnapshotPublisher(const T& initial) noexcept {
        for (auto& slot : slots_) slot = initial;
        for (auto& readers : readers_) {
            readers.store(0U, std::memory_order_relaxed);
        }
    }

    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;

    // Exactly one control-plane writer may call publish(). A concurrent
    // second writer fails immediately; no writer ever waits or allocates.
    bool publish(const T& value) noexcept {
        if (writer_busy_.test_and_set(std::memory_order_acquire)) return false;
        const std::uint32_t active = active_index_.load(std::memory_order_seq_cst);
        for (std::uint32_t offset = 1U; offset < SlotCount; ++offset) {
            const std::uint32_t candidate =
                (active + offset) % static_cast<std::uint32_t>(SlotCount);
            if (readers_[candidate].load(std::memory_order_seq_cst) != 0U) {
                continue;
            }
            slots_[candidate] = value;
            active_index_.store(candidate, std::memory_order_seq_cst);
            writer_busy_.clear(std::memory_order_release);
            return true;
        }
        writer_busy_.clear(std::memory_order_release);
        return false;
    }

    // Audio-thread acquisition has a fixed retry bound and no waiting loop.
    ReadHandle try_acquire() noexcept {
        for (std::uint32_t attempt = 0U; attempt < SlotCount; ++attempt) {
            const std::uint32_t index =
                active_index_.load(std::memory_order_seq_cst);
            readers_[index].fetch_add(1U, std::memory_order_seq_cst);
            if (active_index_.load(std::memory_order_seq_cst) == index) {
                return ReadHandle(this, index, &slots_[index]);
            }
            readers_[index].fetch_sub(1U, std::memory_order_seq_cst);
        }
        return ReadHandle{};
    }

private:
    void release(std::uint32_t index) noexcept {
        readers_[index].fetch_sub(1U, std::memory_order_seq_cst);
    }

    std::array<T, SlotCount> slots_{};
    std::array<std::atomic<std::uint32_t>, SlotCount> readers_{};
    std::atomic_flag writer_busy_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint32_t> active_index_{0U};
};

}  // namespace mt8163_audio_core
