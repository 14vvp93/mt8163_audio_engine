# AudioCore Session Token Contract

**Status:** P1 implementation in progress; SessionRegistry, AudioCore, PcmStreamBroker and PrimaryHalStreamAdapter now use generation-bearing tokens. Final verification and remaining audit gates are still open.

## Problem

The current public session identity is a caller-supplied `uint32_t`. `SessionRegistry`, `AudioCore`, `PrimaryHalStreamAdapter` and `PcmStreamBroker` compare only that value. Closing a session and opening another session with the same numeric handle makes a delayed operation from the old session indistinguishable from an operation for the replacement.

The PCM broker's private slot generation does not solve this public identity problem. It is an internal transfer-race guard and is not propagated through AudioCore or adapter calls.

## Required identity

Introduce a trivially copyable value token:

```cpp
struct SessionToken {
    std::uint32_t handle;
    std::uint64_t generation;
};
```

- `handle` remains the caller's correlation identifier; it is not sufficient authority to mutate a session.
- `generation` is assigned by AudioCore/SessionRegistry when a session is successfully opened.
- A token is valid only when both fields match the currently live session.
- The all-zero/default token is invalid.
- Generation values are monotonically increasing across the registry lifetime, not per slot. A released slot never resets or reuses a generation.
- If the generation counter reaches its maximum value, opening further sessions fails closed. It must never wrap to a previously issued value.
- Equality compares both fields. No implicit conversion from or to `uint32_t` is permitted.

## API propagation

The migration must be atomic at the source/API level; do not leave a raw-handle mutation overload available as an alternate path. A raw correlation handle is accepted only at the open boundary.

1. `SessionRegistry::open` accepts the external handle and returns the assigned `SessionToken` through an output parameter. `transition`, `close` and `find` accept only `SessionToken`.
2. `SessionRecord` and `AudioCoreSnapshot` publish the complete token, not a handle-only identity.
3. `AudioCore::open_session` returns the token; all subsequent session mutations require it.
4. `PrimaryHalStreamAdapter::open` returns the token to the owning HAL stream object. Every configure/start/read/write/stop/close operation must carry that token.
5. `PcmStreamBroker` stores and compares the complete token. The callback-safe transfer path must validate the token before entering the transfer gate and revalidate it after admission.
6. Any callback, deferred task, queue item or completion that can outlive the initiating call must capture the complete token, never only the numeric handle.
7. Tests and diagnostics must make stale-token rejection observable.

## Compatibility and ABI

The current AudioCore and adapter interfaces are internal project APIs, not yet the installed factory HAL ABI. The production HAL is still a bootstrap and is explicitly non-installable. Therefore the token change belongs before HAL integration, while preserving the recovered factory ABI separately.

Do not widen a factory C ABI handle, reinterpret a pointer, or change Android framework callback signatures to carry this token. The future HAL adapter must associate each factory stream object with its internal token and use that token for all asynchronous/internal operations.

## Required tests

- Open handle H, close its token, reopen the same H, and prove old-token transition/close/configure/start/read/write/stop all fail without affecting the replacement.
- Reuse a different registry/PCM slot and prove the old token remains invalid.
- Reject default tokens, mismatched handle/generation pairs and tokens from another registry instance.
- Verify failed opens do not consume a generation unless the contract deliberately reserves it; the preferred behavior is to consume only after all validation and capacity checks succeed.
- Verify generation exhaustion fails closed without wraparound.
- Exercise delayed transfer admission racing with stop/close/reopen; no old token may access the replacement backend stream.
- Verify snapshots contain the complete token and stale token operations cannot mutate published replacement state.


## Repository consumer audit (2026-11-14)

The generation-bearing token has been propagated through the internal control and PCM lifecycle APIs:

| Layer | Implemented contract | Regression coverage |
|---|---|---|
| `SessionRegistry` | Raw handle only at open; all lookup/transition/close operations require `SessionToken` | Same-handle reopen, foreign registry, mismatched/default token, slot reuse and sequence exhaustion |
| `AudioCore` | Open returns token through output parameter; mutations require token | Snapshot publishes full token; stale token cannot mutate/close replacement |
| `PrimaryHalStreamAdapter` | Open returns token through output parameter; all stream operations require token | Stale adapter token rejected after same-handle reopen |
| `PcmStreamBroker` | All control and transfer APIs require token; records retain full token | Stale token rejected after same-handle reopen; transfer gate remains 32-bit lock-free |
| `PcmByteStreamAdapter` | Stores and forwards complete token | Byte callbacks cannot resolve a replacement by raw handle |
| `AudioCoreSnapshot` / `SessionRecord` | Publish complete token | Coordinator snapshot assertions |
| Host tests | Production broker/adapter APIs are token-only; raw handle wrappers exist only in test harnesses to preserve lifecycle scenario readability | Stale-token regressions at registry, coordinator, broker and adapter layers |

The generation is composed of a process-unique registry epoch and a monotonic per-registry sequence. The callback path does not perform 64-bit atomic operations: it first acquires the lock-free 32-bit transfer gate, then reads the immutable session generation. Close cannot change that generation until admitted callbacks drain.

The production `primary-hal/src/primary_hal.cpp` bootstrap does not currently call AudioCore or the adapter. The Android effect/JNI sources also have no AudioCore session-lifecycle callsites. The internal token migration therefore remains isolated from recovered Android C callback signatures and the factory HAL ABI.

Direct `PcmStreamBroker` use outside `PrimaryHalStreamAdapter` is currently limited to host tests; production integration remains absent.

## Migration order

1. Add `SessionToken` and value semantics. **Implemented.**
2. Convert `SessionRegistry`, global registry epoch allocation and lifecycle tests. **Implemented.**
3. Convert `AudioCore`, snapshots and coordinator tests. **Implemented.**
4. Convert `PcmStreamBroker`, callback admission validation and broker tests. **Implemented; final TSAN confirmation pending.**
5. Convert `PrimaryHalStreamAdapter`, `PcmByteStreamAdapter` and adapter tests. **Implemented; final TSAN confirmation pending.**
6. Audit the complete repository for raw-handle mutation paths and remove every internal occurrence. **Final audit pending.**
7. Run full host tests, ThreadSanitizer, Android ARMv7 build and repository-integrity checks against the exact final commit. **In progress.**
8. Only then permit HAL integration to consume the token API. **Blocked on steps 6–7.**

## Automated P1 gate

The repository includes [AudioCore P1 Session Identity Gate](../.github/workflows/audiocore-p1-session-token.yml). It runs on relevant pushes and pull requests targeting `feature`, and can be dispatched manually from `feature`.

The workflow has five required checks:

1. Public API contract: verifies the generation-bearing token shape, propagation through all four public layers, absence of raw numeric-handle mutation overloads (the external correlation handle remains allowed only at open), and presence of stale-token/same-handle-reuse and generation-exhaustion regression coverage.
2. Full host build and CTest suite.
3. Full AudioCore/DSP ThreadSanitizer build and test suite.
4. Android ARMv7 / API 28 cross-build and ELF architecture checks.
5. Repository Markdown/workflow integrity.

The final `P1 COMPLETE` job succeeds only when every required job succeeds. The workflow is expected to remain red until the full regression suite and all verification jobs pass. It is a completion gate, not a substitute for implementing P1. A green result proves offline build/test gates only; it does not prove target-device behavior or authorize HAL installation.

No device validation is implied by this host/API contract.
