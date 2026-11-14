# Factory private callback caller audit — 2026-11-14

Status: **OPEN — partial static call graph; no factory callback is approved for integration.**

## Pinned evidence

| Component | SHA-256 | Evidence |
|---|---|---|
| Factory HAL `audio.primary.mt8163.so` | `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4` | P2 target ELF disassembly, runs 43565487354 / 43565923614 / 43566157662 |
| HIDL bridge `android.hardware.audio@4.0-impl-mediatek.so` | `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc` | run 43401616001 |
| HIDL interface `vendor.mediatek.hardware.audio@4.1.so` | `553704e5285c4ab1e505bab569ac261f40ce7e6aa8c0c9173e038acc59dc3be5` | run 43567544355 |
| MTK client `libaudioprimarydevicehalifclient.so` | `b5e805b6cf20d995babb730f70b6287d4d4ff0c7168c189de326d05e8d58c22c` | run 43566518573 |

The firmware scan used immutable dataset commit `865b6efaeff0c4547dab1f4456f2fa664b3611d1`.

## Additional CI evidence — run 43603169612

The corrected callback xref auditor and dependent lifecycle checks passed against the pinned factory ELF. The reproducible evidence bundle is artifact `mt8163-primary-hal-p2-evidence` (14 files; artifact ID `14117480619`).

### Factory callback dispatch dataflow

- `onAudioParameterChangedCallback(char const*)` is at `0x000cef18`; the pinned ELF loads the callback entry from vector element offset `+0`, cookie from `+4`, moves the parameter pointer into `r0`, cookie into `r1`, then executes `BLX r2` at `0x000cf020`.
- The call occurs before `alock_unlock()` at `0x000cf050` on the lock-success path. The lock-failure branch still falls through to vector traversal and callback dispatch. Therefore neither successful-lock serialization nor the clear operation can be treated as a universal cookie-lifetime barrier.
- `setParametersByCallback(String8 const&)` at `0x000cf108` loads global callback and cookie cells, then invokes the callback indirectly with `BLX r3` at `0x000cf218`. This is a separate global callback path; its caller graph and teardown semantics remain unresolved. Its observed argument setup is not evidence of the vector-registry callback ABI.
- The whole-ELF scan inventories 1,575 register-indirect `BLX` sites. It resolves no direct `BL/BLX` callsite to the five audited callback methods. This does not establish absence of callers: indirect/vtable/function-pointer dispatch remains unresolved except where local dataflow is proven.

### Confirmed factory close-to-destructor chain

The dependent lifecycle audit correlates the factory module close wrapper's second vtable slot with `AudioALSAHardware`'s deleting destructor. The factory `audio_hw_device_mtk.close` slot at `+0x3c` enters HMI close at `0x0009ba48`; HMI close invokes the deleting destructor through the returned object's second vtable slot. The destructor then performs its resource-manager cleanup through the second virtual slot of the object stored at `AudioALSAHardware+0x04`. This confirms the close-to-destructor identity, but does not prove that the parameter callback vector is drained: no admission closure, callback quiescence, or in-flight wait is visible in the audited destructor path.

The separate `AudioALSAStreamManager` event callback path is not the parameter callback registry. CI resolved event type 0 to `callbackPhoneCallReopen(int, void*)`, and confirmed its worker unregister sequence clears the run flag, signals, joins the worker, then removes the worker entry. This worker join must not be generalized into a drain guarantee for the unrelated vector-backed parameter callback registry or global setup-parameters callback.

## Proven call paths

### Parameter-change callback: HIDL registration to factory registry

1. HIDL bridge `Device::setAudioParameterChangedCallback(sp<IAudioParameterChangedCallback> const&)` loads the legacy device slot at offset `0xd4` (212).
2. It calls that slot with the bridge's `Device::audioParameterChangedCallback(char const*, void*)` thunk and the `Device*` as the opaque cookie.
3. Factory `AudioALSAHardware::setAudioParameterChangedCallback(callback, cookie)` stores the function pointer and cookie in the per-device vector at `this+0x3b0), protected by the lock at `this+0x3ac` only when lock acquisition succeeds.
4. Factory `AudioALSAHardware::onAudioParameterChangedCallback(char const*)` iterates the vector and invokes each stored callback. On its lock-success path, invocation occurs while the same registry lock is held.
5. The bridge thunk obtains the HIDL callback strong reference and dispatches through the generated HIDL interface. The pinned Bp proxy uses transaction code 1 with Binder flags 0: this callback transaction is synchronous, not `FLAG_ONEWAY`.

This is a proven internal registration/dispatch chain. It is **not** proof of which framework process or application triggers the factory notification, how often it runs, or that all notification entry points have been found.

### Parameter-change callback: clear and owner lifetime

1. HIDL bridge `Device::clearAudioParameterChangedCallback()` loads legacy slot `0xd8` (216) and invokes it with the same `Device*` cookie used by registration.
2. Factory clear removes entries matching that cookie.
3. The bridge clears its HIDL callback strong-reference member after the legacy clear call, including the observed nonzero-status branch.
4. The pinned factory clear logs and continues into vector mutation when `alock_lock_ms` fails; it does not fail closed.
5. The complete HIDL `Device` destructor invokes legacy device `close` through offset `0x3c` before releasing its callback member. It has no direct call to private clear slot `0xd8`.
6. HMI close reaches the factory deleting destructor. The factory destructor destroys the parameter callback vector and frees its lock without a visible callback admission-close or in-flight drain.

The synchronous Binder transaction makes callback reentrancy possible at the architectural boundary, but this static evidence does not prove a particular reentrant call sequence. Because factory dispatch invokes the callback while holding its registry lock, reentrant registration/clear must be treated as unsafe. The lock-failure fallthrough also means successful-lock serialization cannot be used as an unconditional raw-cookie lifetime guarantee.

### Separate setup-parameters callback

The HIDL bridge `Device::setupParametersCallback()` loads legacy slot `0xd0` (208) and calls it with the bridge's `syncCallback` thunk and a `Device*` cookie. The target factory `AudioALSAHardware::setupParametersCallback(...)` stores callback/cookie state in global locations; its body has no visible lock acquisition. Factory `setParametersByCallback(String8 const&)` is a separate indirect-callback path and has no visible mutex/condition-variable calls in the audited body.

Do not merge this global callback path with the vector-backed `setAudioParameterChangedCallback` registry. Its exact registration replacement, invocation callers, and teardown/owner lifetime remain unresolved.

## Caller inventory and evidence level

| Private slot | Factory target / bridge mapping | External caller finding |
|---:|---|---|
| `0x9c–0xa8` | `xWayPlay_Start/Stop/Write/GetFreeBufferCount` → `Play2Way` | No external caller identified in the pinned scan; numeric-offset/indirect callers remain possible |
| `0xac–0xb4` | `xWayRec_Start/Stop/Read` → `Record2Way` | No external caller identified in the pinned scan; numeric-offset/indirect callers remain possible |
| `0xb8` | `ReadRefFromRing` → `AudioVUnlockDL::ReadRefFromRing` | Internal forwarding target identified; external entry and concurrent caller ordering unresolved |
| `0xbc–0xcc` | Voice Unlock HIDL 4.1 Device methods | Bridge-to-slot mapping identified; concrete framework/client call sites and stop/free ordering not proven |
| `0xd0` | `setupParametersCallback` | HIDL bridge invocation identified; upstream framework/client call sites and global callback lifetime unresolved |
| `0xd4` | `setAudioParameterChangedCallback` | HIDL bridge registration invocation and Device cookie identified |
| `0xd8` | `clearAudioParameterChangedCallback` | HIDL bridge clear invocation and matching Device cookie identified; not a proven drain |

### External client scan

The pinned firmware scan covered 1,294 ELF/bytecode/archive candidates. Five files had callback-related string/symbol matches; zero DEX/native-library archive members inside APK/JAR files matched. No separately identified xWay caller was found. This scan cannot prove absence of stripped symbols, numeric-offset dispatch, indirect function tables, runtime-loaded modules, dynamically downloaded code, or callers outside the extracted firmware dataset.

The ATCI executable has an undefined import and PLT relocation for `MTKPrimaryDevicesHalClient::getInstance()`, but the dedicated audit did not identify a direct call site. Treat ATCI as a linked/importing candidate, not a confirmed executing caller. The client method audit found generic parameter get/set operations and no direct callback registration/clear, xWay, or Voice Unlock calls.

### ABI compatibility boundary

The public vendor V3 header corroborates some internal xWay method names/signatures but its private `audio_hw_device_mtk` tail does not match the target's recovered 16-slot layout. It cannot be used to populate the target private table.

The new standalone AudioCore registry currently declares:

`void (*)(void* cookie, const char* parameters) noexcept`

The pinned HIDL bridge thunk symbol is:

`Device::audioParameterChangedCallback(char const*, void*)`

The argument order differs, and the binary does not prove a source-level `noexcept` contract. A dedicated typed adapter may be possible after exact ABI confirmation, but direct function-pointer substitution is not valid. The generic registry must not be connected to this slot by casting or guessed prototypes.

## Integration decision

**No private factory callback is currently integration-ready.**

- Keep the factory HAL and HIDL bridge retained.
- Keep Primary HAL fail-closed and `installable: false`.
- Do not cast vendor callback pointers into AudioCore callback types.
- Do not infer external xWay usage from the presence of the private slots.
- Do not treat HIDL strong references, synchronous Binder calls, or the factory lock-success path as a complete raw-cookie drain protocol.
- Keep setup-parameters callback separate from the vector-backed parameter-change registry.
- Continue read-only analysis only. No hardware writes, flashing, service control, or runtime device commands.

## Remaining evidence gates

- [ ] Identify every external xWay and Voice Unlock caller, including numeric-offset and indirect dispatch.
- [ ] Resolve all exact target prototypes, argument order, return types, ownership and exception contracts for all 16 slots.
- [ ] Identify every caller of `onAudioParameterChangedCallback` and `setParametersByCallback`, including indirect and framework entry paths.
- [ ] Prove callback registration replacement, clear, callback reentrancy and device teardown ordering under lock failure and concurrent execution.
- [ ] Establish the framework/service owner lifetime for the HIDL `Device*` cookie, not merely the HIDL callback interface object's strong reference.
- [ ] Resolve Voice Unlock stop, worker termination, `ReadRefFromRing` drain and free ordering.
- [ ] Pin matching target vendor headers/source and exact Android 9 linker namespace configuration.

Static evidence only. P2 remains OPEN.
