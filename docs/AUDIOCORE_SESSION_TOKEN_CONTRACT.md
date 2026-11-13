# AudioCore Session Token Contract

**Status:** proposed implementation contract for P1; raw-handle API remains in place until the migration is complete.

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

The migration must be atomic at the source/API level; do not leave a raw-handle mutation overload available as an alternate path.

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

The current repository-wide implementation surface is limited to these layers:

| Layer | Current raw-handle use | Required migration |
|---|---|---|
| `SessionRegistry` | `open/find/transition/close(uint32_t)` | Assign and validate complete token |
| `AudioCore` | `open_session/transition_session/close_session(uint32_t)` | Return token from open; require token for mutations |
| `PrimaryHalStreamAdapter` | `open/configure/start/read/write/stop/close(uint32_t)` | Return token to stream owner and require it for every operation |
| `PcmStreamBroker` | Slot lookup and callback transfer keyed by `uint32_t` | Store/compare complete token at control and transfer boundaries |
| `AudioCoreSnapshot` / `SessionRecord` | Snapshot publishes only `handle` | Publish complete token |
| Host tests | Direct raw-handle calls across registry, coordinator, broker and adapter tests | Add same-handle-reopen stale-token regression at every layer |

The production `primary-hal/src/primary_hal.cpp` bootstrap does not currently call AudioCore or the adapter. The Android effect/JNI sources also have no AudioCore session-lifecycle callsites. This is useful boundary evidence: the internal token migration can be completed before wiring the factory HAL ABI, without changing recovered Android C callback signatures.

Direct `PcmStreamBroker` use outside `PrimaryHalStreamAdapter` is currently in host tests; production integration remains absent. The migration must nevertheless convert the broker API itself rather than relying on the adapter as the only guard.

## Migration order

1. Add `SessionToken` and token equality/value validation.
2. Convert `SessionRegistry` and its tests.
3. Convert `AudioCore`, snapshots and coordinator tests.
4. Convert `PcmStreamBroker` and broker tests.
5. Convert `PrimaryHalStreamAdapter` and adapter tests.
6. Search the complete repository for raw session-handle lifecycle calls; remove every internal raw-handle mutation path.
7. Run host tests, AudioCore/PCM ThreadSanitizer, Android ARMv7 build and repository-integrity checks against the exact resulting commit.
8. Only then permit the HAL integration workstream to consume the token API.

No device validation is implied by this host/API contract.
