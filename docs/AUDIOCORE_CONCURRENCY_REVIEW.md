# AudioCore concurrency review

Date: 2026-11-12
Baseline: d0547fa811050a248c1d9d4479d94cc8cf270680
Scope: AudioCore control state and bounded snapshot publication
Status: static source review; host CI has passed current snapshot tests; no device test

## 1. Ownership model

AudioCore has one control-plane writer. Session open/transition/close, mute-owner changes, audio-mode changes, hardware-broker attachment and dispatch, and snapshot publication must be serialized on one HAL control context. The PCM/audio thread must not call these mutation methods. It only acquires an immutable snapshot and consumes it for a processing block.

AudioCore documents this rule but does not enforce it internally with a mutex or thread-affinity assertion. The HAL integration layer must provide the single-writer executor.

## 2. SnapshotPublisher review

The publisher uses three fixed slots, an atomic active-slot index, per-slot atomic reader pins, and a writer exclusion flag.

1. One writer claims the writer flag; a concurrent writer fails immediately.
2. It reads the active index and selects a different slot whose reader count is zero.
3. It copies the trivially-copyable snapshot into that inactive slot.
4. It publishes the new index.
5. Readers load the active index, pin that slot, then re-check the active index. A stale reader releases the pin and retries within a fixed bound.
6. A successful reader keeps the pin for the lifetime of its move-only handle. The writer cannot reuse that slot until the handle releases it.

The sequentially consistent ordering on the active index and reader counters is intentional. If a reader pins a slot after a writer selected it, the reader's second active-index check cannot accept that slot until publication completes. If the slot is no longer active, the reader releases it without reading the payload.

No payload field is concurrently modified while a valid handle pins its slot. This avoids the C++ data race inherent in a naive seqlock over non-atomic payload fields.

## 3. Bounded behavior and backpressure

- Reader retries at most SlotCount times; it never waits for the writer.
- Writer scans at most SlotCount - 1 inactive slots; it never waits for readers.
- If every inactive slot is pinned, publication returns false.
- AudioCore retains committed control state and marks the snapshot dirty; the control loop must retry publication.
- The audio thread must retain its previous valid snapshot for the current block when acquisition fails.

Existing tests cover initial publication, pinned-slot backpressure, coherent multi-field snapshots with concurrent readers, concurrent writer rejection/retry, and mute snapshot transport. A dedicated GitHub Actions ThreadSanitizer workflow was added; its first run completed successfully on the snapshot stress suite.

## 4. Integration constraints

1. Keep each read handle alive only as long as the audio block needs its snapshot; release it promptly.
2. Never retain a handle across an unbounded wait, stream stop, or blocking device operation.
3. Retry dirty publication on the control thread; do not make the PCM callback publish or wait.
4. Route all HAL control mutations through one control executor.
5. Do not destroy AudioCore or SnapshotPublisher until all read handles are released and the audio callback has quiesced.
6. Preserve the 32-bit lock-free atomic assertion on ARM32. If the Android toolchain fails it, review the ABI and atomic implementation rather than silently removing the assertion.

## 5. Remaining verification

- Keep the ThreadSanitizer workflow enabled and rerun it after changes to the publisher or snapshot lifetime protocol.
- Add repeated acquire/publish/release stress with multiple readers and an intentionally slow pinned reader.
- Add a dirty-state retry test after all three slots are pinned.
- Confirm Android NDK r27c ARMv7 compilation of the atomic requirements.
- Establish callback quiescence and AudioCore lifetime ordering in the primary HAL.

## 6. Decision

The current three-slot publisher has a defensible bounded reader/writer protocol and no evident payload data race under its documented single-writer/multi-reader contract. The main remaining concurrency risk is at the future HAL integration boundary: enforcing single-writer ownership, callback quiescence, and object lifetime. Do not add a mutex to the realtime reader as a shortcut.