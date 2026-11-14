# AudioCore callback registry lifecycle contract

## Scope

`CallbackRegistry` is a fixed-capacity, host-testable primitive for callback ownership. It is not wired into Primary HAL, the factory HAL, HIDL, AudioPolicy, or any hardware route. Its existence does not close P2 or prove the pinned factory ABI.

## Synchronization contract

- Capacity is fixed at 16 registrations. Registration is the only operation that may fail for capacity; dispatch uses a fixed stack snapshot and performs no heap allocation.
- One mutex protects slot identity, callback admission, and in-flight counters.
- Dispatch reserves an in-flight reference for every admitted slot while holding the mutex, then invokes callbacks after releasing the mutex.
- Before each invocation, dispatch rechecks admission. A callback disabled by an earlier callback in the same batch is skipped.
- Clear first closes admission. It returns success only after every already-reserved invocation for that registration has completed.
- A slot cannot be reused while an invocation is in flight. Registration identity is monotonic and never intentionally reused.
- Callback functions are `noexcept`; the registry does not permit exceptions to cross the dispatch boundary.
- Shutdown is terminal: it closes admission for every slot and waits for all in-flight callbacks.

## Reentrancy and deadlock handling

A callback must not synchronously wait for any callback drain from a callback context. This rule applies across registry instances, preventing two callback threads from cross-clearing each other while each holds an in-flight reference.

If `clear_and_drain`, `clear_cookie_and_drain`, or `shutdown_and_drain` is called from a callback context while the target has in-flight work, it closes admission and returns `WouldDeadlock` without waiting. The owner must retry the drain from a non-callback control context before releasing the cookie or callback-owned state.

If no target invocation is in flight, a callback-context clear may complete immediately because it does not wait.

## Ownership requirements

- The cookie owner must outlive successful `clear_and_drain` or `clear_cookie_and_drain` completion.
- The registry must outlive all dispatch and drain operations.
- Callers must serialize destruction against new external calls. The destructor performs terminal shutdown and drain; if it cannot establish a completed drain, it terminates rather than silently destroying state still referenced by a callback.
- A `LockFailure` result is not proof of drain. Keep callback owners alive and retry or escalate shutdown from a safe control context.

## Validation

The host test target `mt8163_audio_core_callback_registry_tests` covers registration identity, capacity, concurrent dispatch/clear, in-flight drain blocking, self-clear, cross-clear within a dispatch batch, cross-registry deadlock avoidance, cookie drain, and terminal shutdown. The existing AudioCore workflow also runs the full host suite under ThreadSanitizer and ASan/UBSan and cross-builds the library for Android ARMv7.

Passing those tests validates this standalone implementation only. It does not establish factory callback call-site coverage, factory teardown ordering, HIDL callback ABI compatibility, or a safe migration path for the factory callback registry.
