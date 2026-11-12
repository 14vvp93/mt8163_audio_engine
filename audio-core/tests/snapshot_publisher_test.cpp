#include "mt8163_audio_core/mute_arbiter.hpp"
#include "mt8163_audio_core/snapshot_publisher.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <type_traits>

using mt8163_audio_core::MuteArbiter;
using mt8163_audio_core::MuteOwner;
using mt8163_audio_core::MuteSnapshot;
using mt8163_audio_core::SnapshotPublisher;
using mt8163_audio_core::SourceId;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct PairSnapshot {
    std::uint64_t sequence{0U};
    std::uint64_t inverse{~std::uint64_t{0U}};
};

static_assert(std::is_trivially_copyable<PairSnapshot>::value,
              "test snapshot must be trivially copyable");

void test_initial_and_published_values() {
    const PairSnapshot initial{0U, ~std::uint64_t{0U}};
    SnapshotPublisher<PairSnapshot> publisher(initial);
    auto first = publisher.try_acquire();
    require(static_cast<bool>(first), "initial snapshot is readable");
    require(first->sequence == 0U, "initial sequence is preserved");
    require(first->inverse == ~first->sequence, "initial fields are coherent");
    first.reset();

    const PairSnapshot next{42U, ~std::uint64_t{42U}};
    require(publisher.publish(next), "new snapshot publishes");
    auto current = publisher.try_acquire();
    require(static_cast<bool>(current), "published snapshot is readable");
    require(current->sequence == 42U && current->inverse == ~42ULL,
            "reader observes complete new snapshot");
}

void test_pinned_slots_apply_backpressure() {
    SnapshotPublisher<PairSnapshot> publisher(PairSnapshot{});
    auto slot0 = publisher.try_acquire();
    require(static_cast<bool>(slot0), "initial slot acquired");
    require(publisher.publish(PairSnapshot{1U, ~std::uint64_t{1U}}),
            "second slot published");
    auto slot1 = publisher.try_acquire();
    require(static_cast<bool>(slot1), "second slot acquired");
    require(publisher.publish(PairSnapshot{2U, ~std::uint64_t{2U}}),
            "third slot published");
    auto slot2 = publisher.try_acquire();
    require(static_cast<bool>(slot2), "third slot acquired");
    require(!publisher.publish(PairSnapshot{3U, ~std::uint64_t{3U}}),
            "writer never overwrites pinned inactive slots");
    slot0.reset();
    require(publisher.publish(PairSnapshot{3U, ~std::uint64_t{3U}}),
            "writer reuses a released slot without waiting");
}

void test_mute_snapshot_publication() {
    MuteArbiter mute;
    SnapshotPublisher<MuteSnapshot> publisher(mute.snapshot());
    mute.set_master(MuteOwner::AccPowerState, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioRetune, true);
    require(publisher.publish(mute.snapshot()), "mute state snapshot publishes");
    auto published = publisher.try_acquire();
    require(static_cast<bool>(published), "mute snapshot can be acquired");
    require(published->master_muted(), "published master mute is visible");
    require(published->source_muted(SourceId::FmRadio),
            "published source mute is visible");
}

void test_concurrent_readers_never_observe_torn_snapshot() {
    constexpr std::uint64_t kUpdates = 100000U;
    SnapshotPublisher<PairSnapshot> publisher(PairSnapshot{});
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};

    std::thread writer([&]() {
        for (std::uint64_t i = 1U; i <= kUpdates; ++i) {
            const PairSnapshot next{i, ~i};
            while (!publisher.publish(next)) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });

    auto reader_body = [&]() {
        while (!done.load(std::memory_order_acquire)) {
            auto snapshot = publisher.try_acquire();
            if (snapshot && snapshot->inverse != ~snapshot->sequence) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
        for (unsigned i = 0U; i < 1000U; ++i) {
            auto snapshot = publisher.try_acquire();
            if (snapshot && snapshot->inverse != ~snapshot->sequence) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    };

    std::thread reader1(reader_body);
    std::thread reader2(reader_body);
    writer.join();
    reader1.join();
    reader2.join();
    require(!failed.load(std::memory_order_relaxed),
            "concurrent readers observe only coherent snapshots");
}


struct MultiWriterSnapshot {
    std::uint64_t sequence{0U};
    std::uint64_t inverse{~std::uint64_t{0U}};
    std::uint64_t writer{0U};
    std::uint64_t check{0U};
};

static_assert(std::is_trivially_copyable<MultiWriterSnapshot>::value,
              "multi-writer snapshot must be trivially copyable");

void test_concurrent_writers_are_serialized_or_rejected() {
    constexpr std::uint64_t kUpdatesPerWriter = 30000U;
    SnapshotPublisher<MultiWriterSnapshot> publisher(MultiWriterSnapshot{});
    std::atomic<unsigned> completed{0U};
    std::atomic<bool> failed{false};

    auto writer_body = [&](std::uint64_t writer_id) {
        for (std::uint64_t i = 1U; i <= kUpdatesPerWriter; ++i) {
            const std::uint64_t sequence = (writer_id << 48U) | i;
            const MultiWriterSnapshot next{
                sequence, ~sequence, writer_id, sequence ^ writer_id};
            while (!publisher.publish(next)) std::this_thread::yield();
        }
        completed.fetch_add(1U, std::memory_order_release);
    };

    auto reader_body = [&]() {
        while (completed.load(std::memory_order_acquire) != 2U) {
            auto snapshot = publisher.try_acquire();
            if (!snapshot) continue;
            if (snapshot->inverse != ~snapshot->sequence ||
                snapshot->check != (snapshot->sequence ^ snapshot->writer)) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            // Keep the slot pinned briefly to exercise writer backpressure.
            std::this_thread::yield();
        }
    };

    std::thread writer1(writer_body, 1U);
    std::thread writer2(writer_body, 2U);
    std::thread reader1(reader_body);
    std::thread reader2(reader_body);
    writer1.join();
    writer2.join();
    reader1.join();
    reader2.join();

    require(!failed.load(std::memory_order_relaxed),
            "concurrent writers never expose a partially written snapshot");
    auto final_snapshot = publisher.try_acquire();
    require(static_cast<bool>(final_snapshot), "final snapshot remains readable");
    require(final_snapshot->inverse == ~final_snapshot->sequence &&
            final_snapshot->check == (final_snapshot->sequence ^ final_snapshot->writer),
            "final snapshot fields remain coherent after concurrent writer stress");
}

}  // namespace

int main() {
    test_initial_and_published_values();
    test_pinned_slots_apply_backpressure();
    test_mute_snapshot_publication();
    test_concurrent_readers_never_observe_torn_snapshot();
    test_concurrent_writers_are_serialized_or_rejected();
    std::cout << "AudioCore snapshot publisher tests passed\n";
    return 0;
}
