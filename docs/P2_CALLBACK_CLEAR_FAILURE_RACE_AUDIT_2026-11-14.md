# P2 callback clear lock-failure race surface — 2026-11-14

## Pinned artifacts

- Factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`
- Factory HAL SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- HIDL bridge: `vendor/lib/hw/android.hardware.audio@4.0-impl-mediatek.so`
- HIDL bridge SHA-256: `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`
- Evidence artifact: P2 workflow run `43600007985`, artifact `14116306091`
- Scope: static ARM32 ELF disassembly only; no target runtime execution.

## Factory clear behavior

The pinned factory symbol is:

`AudioALSAHardware::clearAudioParameterChangedCallback(void*)` at `0x000cecc0`, size 600 bytes.

The function uses the registry lock at `this+0x3ac` and the callback vector at `this+0x3b0`, with the vector size at `this+0x3b8`.

The lock-failure branch is explicit:

- `0x000ced08`: calls `alock_lock_ms@plt`.
- `0x000ced0c`: tests the return value.
- `0x000ced10`: branches to `0x000ced6c` when the lock call reports failure.
- The branch at `0x000ced6c` continues into callback registry access rather than returning or aborting.
- The function's ordinary non-exception exits set `r0 = 0` before returning.

The successful-lock path also accesses and mutates the same vector, then calls `alock_unlock`. Therefore, on the observed lock-failure path, clear continues without owning the registry lock and does not report the synchronization failure through its return value.

The dispatcher `AudioALSAHardware::onAudioParameterChangedCallback(char const*)` at `0x000cef18` iterates that vector and invokes each callback before reaching its unlock call at `0x000cf050`. On the successful-lock path, callback invocation is therefore inside the registry critical section.

## HIDL bridge behavior

The pinned bridge symbol is:

`Device::clearAudioParameterChangedCallback()` at `0x0001cdac`, size 116 bytes.

It loads the legacy clear slot at `audio_hw_device+0xd8` and invokes it indirectly. It preserves the `Device*` cookie in `r1`, matching the cookie passed by the setter.

After the legacy call, the bridge proceeds through its common callback-member cleanup path. The callback strong-reference member at `Device+0x28` is cleared even when the legacy call reports a nonzero result; the error branch only adds logging before joining that cleanup path. On the pinned factory implementation, the ordinary clear exits return zero, including the lock-failure fallthrough.

The callback thunk at `0x0001cc74` promotes the Device cookie, loads the callback member at `Device+0x28`, and immediately dereferences the resulting pointer to obtain its vtable. There is no explicit null branch between that member load and dereference in the observed instruction sequence.

## Safety consequence

The successful-lock design can serialize clear against dispatch because dispatch invokes callbacks while holding the same registry lock. That serialization is not guaranteed on the clear lock-failure path:

1. A dispatcher may be iterating the callback vector while holding the registry lock.
2. Clear may fail to acquire that lock and branch into vector access anyway.
3. Clear can remove or move vector entries concurrently with the dispatcher.
4. The HIDL bridge then clears its callback strong-reference member after the legacy clear returns.

This establishes a statically visible, unsafe-capable race surface involving registry mutation, callback dispatch, and HIDL callback ownership. It does **not** prove that a particular runtime interleaving occurs, nor does it establish a demonstrated crash, use-after-free, or deadlock.

The absence of a lock-failure abort/return is sufficient to reject the factory clear operation as a proven callback-drain barrier. Do not rely on the successful-lock serialization as a lifetime guarantee unless lock acquisition failure is made fail-closed and the caller retains the callback owner until a verified drain completes.

## Required remediation contract

A replacement callback registry must:

- reject registration, clear, and dispatch when synchronization cannot be acquired;
- never access registry storage after a failed lock operation;
- close callback admission before teardown;
- wait for all in-flight dispatches to finish before releasing callback owners or destroying registry storage;
- make clear return an explicit failure when it cannot establish removal and quiescence;
- keep HIDL callback ownership alive until clear/drain success is confirmed.

A callback invocation must not be performed while holding a lock that the callback can re-enter. If callbacks can re-enter registration or clear, use a snapshot/reference-counted dispatch design with an in-flight drain protocol rather than invoking under the registry mutex.

## Gate status

- Factory lock-failure fallthrough: **confirmed by static disassembly**.
- Dispatcher callback invocation before unlock: **confirmed by static disassembly**.
- HIDL clear resets callback member after legacy call: **confirmed by static disassembly**.
- Runtime race occurrence: **not tested / not claimed**.
- Callback registry drain on factory device close: **not proven**.
- P2: **OPEN**.
- Primary HAL: **fail-closed; installable=false**.
- Hardware access, flashing, and device writes: **not authorized**.
