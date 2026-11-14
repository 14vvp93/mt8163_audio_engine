# P2 private HAL callback evidence matrix

Status: **OPEN — static evidence only; runtime contracts are not closed.**

Evidence baseline:
- Factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`
- Factory SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- Target: Android 9, ARM32, MT8163.
- Primary ABI and private-tail analysis: [Android 9 ARM32 Primary HAL ABI baseline](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md).
- Android framework close analysis: [Android 9 stream callback audit](ANDROID9_STREAM_CALLBACK_AUDIT.md).
- Non-authoritative vendor source comparison: [P2 vendor source corroboration](P2_VENDOR_SOURCE_CORROBORATION.md).

## Callback inventory

| Device offset | Callback | Internal target / bridge | External caller evidence | Closure state |
|---:|---|---|---|---|
| 156 (0x9c) | `xWayPlay_Start` | `Play2Way::Start` | No external caller tied to this slot | OPEN |
| 160 (0xa0) | `xWayPlay_Stop` | `Play2Way::Stop` | No external caller tied to this slot | OPEN |
| 164 (0xa4) | `xWayPlay_Write` | `Play2Way::Write` | No external caller tied to this slot | OPEN |
| 168 (0xa8) | `xWayPlay_GetFreeBufferCount` | `Play2Way::GetFreeBufferCount` | No external caller tied to this slot | OPEN |
| 172 (0xac) | `xWayRec_Start` | `Record2Way::Start` | No external caller tied to this slot | OPEN |
| 176 (0xb0) | `xWayRec_Stop` | `Record2Way::Stop` | No external caller tied to this slot; distinct adapter ABI | OPEN |
| 180 (0xb4) | `xWayRec_Read` | `Record2Way::Read` | No external caller tied to this slot | OPEN |
| 184 (0xb8) | `ReadRefFromRing` | `AudioVUnlockDL::ReadRefFromRing` | HIDL/voice-unlock path identified internally; all call ordering unresolved | OPEN |
| 188 (0xbc) | `GetVoiceUnlockULTime` | MediaTek HIDL 4.1 Device bridge | HIDL method mapped to legacy slot | STATIC MAP |
| 192 (0xc0) | `SetVoiceUnlockSRC` | MediaTek HIDL 4.1 Device bridge | HIDL method mapped to legacy slot | STATIC MAP |
| 196 (0xc4) | `startVoiceUnlockDL` | MediaTek HIDL 4.1 Device bridge | HIDL method mapped to legacy slot | STATIC MAP |
| 200 (0xc8) | `stopVoiceUnlockDL` | MediaTek HIDL 4.1 Device bridge | HIDL method mapped to legacy slot | STATIC MAP |
| 204 (0xcc) | `getVoiceUnlockDLInstance` | MediaTek HIDL 4.1 Device bridge | HIDL method mapped to legacy slot | STATIC MAP |
| 208 (0xd0) | `setupParametersCallback` | MediaTek HIDL 4.1 Device bridge | Registration method mapped | STATIC MAP |
| 212 (0xd4) | `setAudioParameterChangedCallback` | MediaTek HIDL 4.1 Device bridge | Registration method mapped | STATIC MAP |
| 216 (0xd8) | `clearAudioParameterChangedCallback` | MediaTek HIDL 4.1 Device bridge | Clear method mapped | STATIC MAP |

The 16 slots are a private MediaTek extension, not part of the public AOSP `audio_hw_device` ABI. The two recording-stop entry forms at offsets 172/176 must not be collapsed or assigned a shared guessed prototype.

## Proven static paths

1. The factory constructor initializes all 16 private slots in the 224-byte device allocation. The public AOSP r3 device prefix is 156 bytes; the private extension occupies offsets 156..216, followed by the legacy C++ hardware pointer at 220.
2. Each private adapter recovers the legacy `AudioALSAHardware*` from offset 220 and dispatches through its vtable.
3. The MediaTek HIDL 4.1 `Device` adapter loads and calls offsets 188..216. The generated HIDL interface transports the parameter-change callback in both registration and notification directions.
4. The service init entry starts `android.hardware.audio@4.0-service-mediatek` as `vendor.audio-hal-4-0` / `audioserver`.
5. The ATCI process loads the MediaTek primary-device client. The recovered client bridge currently establishes the parameter `setParameters/getParameters` path only; it does not establish xWay callback use.
6. Target factory ELF `ReadRoutine` calls `pthread_self()` and `pthread_detach()`. `AudioALSAHardware::stopVoiceUnlockDL()` calls `getInstance()` then `stopInput()`; `freeVoiceUnlockDLInstance()` calls `freeInstance()` directly. The target `freeInstance()` body destroys SRC/buffer/singleton state and contains no `stopInput()`, `pthread_join()`, or worker shutdown call. The free wrapper therefore does not enforce stop-before-free. This establishes an unsafe-capable teardown surface, but not that an external caller actually frees an active instance.
7. Target `AudioVUnlockDL::getInstance()` at `0x000b3170` performs a global-pointer check, allocation, construction and global-pointer store without a visible mutex/atomic operation in its own body. This does not rule out serialization by callers. Target `AudioVUnlockDL::~AudioVUnlockDL()` at `0x000b3548` releases the SRC object and dynamic buffer but contains no `stopInput()`, worker-exit handshake or `pthread_join()`. Together with `freeInstance()`, no internally enforced worker-stop/drain-before-free sequence is visible. An actual concurrent free/use race remains unproven.
8. The target `stopInput()` has an initial polling loop followed by a repeated check/log branch; it must not be modeled as a simple bounded wait that always returns safely on timeout. The exact source-level flag semantics remain unresolved. Public V3 source has a different lifecycle implementation and is not proven to match the target ELF.
9. Android 9 AOSP r3 ordinary playback/capture close waits for worker-side I/O quiescence before deleting stream wrappers. This does not establish private HIDL callback quiescence, callback-table serialization, or the lifetime of the detached voice-unlock worker.

## Unresolved hard gates

- [ ] Identify every external xWay caller, including numeric-offset and indirect dispatch from executables, APK/JNI payloads and runtime-loaded components.
- [ ] Recover and cross-check exact source-level prototypes and return types for all 16 slots against matching vendor headers/source or complete caller-side ABI evidence.
- [ ] Prove callback admission, in-flight drain and owner lifetime for every private callback, including HIDL parameter callback registration, replacement and clear races.
- [ ] Identify and verify every external caller's ordering of `stopVoiceUnlockDL()` and `freeVoiceUnlockDLInstance()`; the target free wrapper itself does not stop the worker.
- [ ] Prove voice-unlock teardown ordering: stop request, worker-exit observation, drain all `ReadRefFromRing` callers, then free singleton and ring/SRC state. The detached worker cannot be treated as joined.
- [ ] Prove serialization of concurrent start/stop/read/write and callback registration/control calls; define lock ordering and reentrancy.
- [ ] Obtain the exact target Android 9 linker namespace configuration and verify all `DT_NEEDED` libraries and search paths.
- [ ] Pin matching ALPS/MediaTek vendor headers or source revision and reconcile private declarations with the recovered binary.
- [ ] Add deterministic host tests for callback admission/close races and teardown ordering, then run TSAN/ASAN/UBSAN.

## Automation scope and limits

The push-triggered and manually dispatched P2 GitHub Actions workflow pins public firmware dataset commit `865b6efaeff0c4547dab1f4456f2fa664b3611d1` and:
- verifies the factory HAL SHA-256 and ARM32 ELF contract;
- runs the existing APS2 relocation/vtable audit;
- scans ARM32 ELF libraries/executables, Android OAT/ODEX/VDEX bytecode, and DEX/native-library members inside APK/JAR archives for callback-related symbols and strings;
- preserves scan reports as workflow artifacts;
- verifies that all 16 offsets remain inventoried and unresolved evidence gaps remain visible;
- verifies that Primary HAL stream opens still clear output pointers and return `-ENODEV`, and that the module remains `installable: false`.

The current scan's five matches do not include a separately identified xWay caller. Target wrapper disassembly confirms that `freeVoiceUnlockDLInstance()` dispatches straight to `AudioVUnlockDL::freeInstance()` without a stop call; the target free body also has no worker shutdown call. A clean symbol/string scan is not proof that stripped or indirect numeric-offset callers do not exist. APK/JAR coverage is limited to DEX and native-library archive members in the pinned extracted dataset; it does not prove coverage of dynamically downloaded code or data-driven dispatch. Static CI cannot prove runtime callback quiescence, target linker namespace behavior, or hardware routing. P2 must remain open until the hard gates above are closed with source/binary evidence and deterministic tests.

## Enablement policy

Do not implement guessed private callback prototypes. Do not enable Primary HAL stream opens, set the Soong module installable, replace the factory HIDL bridge, or free voice-unlock state without the corresponding evidence gates. The retained factory path remains authoritative until a reviewed replacement is proven safe.

## 2026-11-14 expanded target-ELF dispatch audit

Workflow run: [43565487354](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43565487354)  
Audited commit: `41e52071ea6fd6bb5ba577d0de5da69f6f145fce`  
Factory HAL SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`  
Artifact: `mt8163-primary-hal-p2-evidence`, ID `14101694200`  
Artifact ZIP SHA-256: `affcfd7f5c04751dd014a79644af0db2a0e39e27d21fb6bf264bda2075fb4688`

The audit script resolved and disassembled all 23 required target symbols. In particular, exact ELF symbol names were confirmed for:

- `AudioALSAHardware::setupParametersCallback(...)` at `0x000ce8fc`
- `AudioALSAHardware::setAudioParameterChangedCallback(...)` at `0x000ce9dc`
- `AudioALSAHardware::clearAudioParameterChangedCallback(void*)` at `0x000cecc0`
- `xWayPlay_Start/Stop/Write/GetFreeBufferCount` at `0x000ce564`–`0x000ce628`
- `xWayRec_Start/Stop/Read` at `0x000ce664`–`0x000ce6dc`
- `AudioALSAHardware::ReadRefFromRing(...)` at `0x000ce728`

The targeted wrapper disassembly confirms the xWay adapters dispatch to `Play2Way` and `Record2Way`; the reference-ring adapter obtains `AudioVUnlockDL::getInstance()` and forwards to `AudioVUnlockDL::ReadRefFromRing(...)`. This establishes internal dispatch destinations, not external caller existence or callback lifetime safety.

The same run scanned 1,294 firmware candidates: five files contained callback-related string/symbol matches and zero APK/JAR archive members contained such matches. This is not proof that stripped, numeric-offset, indirect, dynamically loaded or runtime-downloaded callers do not exist.

All three workflow jobs completed successfully, including the aggregate offline-evidence gate. The artifact is reproducible evidence only; no runtime/device behavior is inferred. P2 remains OPEN. External caller coverage, exact target callback ABI/prototypes, callback admission/drain, registration/clear races, detached-worker teardown ordering and target linker namespace remain hard gates.

**Historical note:** this run predates the unique-`run_id` concurrency fix. The fix was subsequently verified on run [43565923614](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43565923614), recorded below.

## 2026-11-14 concurrency-fix verification

Workflow run: [43565923614](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43565923614)  
Commit: `dad35b118e2d5f8cf3e28c6e8c4fae526f55d7c5`  
Concurrency group configured in the checked-out workflow: `primary-hal-p2-${{ github.run_id }}`.

All three jobs completed successfully:
- P2 contract inventory and fail-closed gates
- Reproducible factory binary and external-client scan
- Aggregate P2 offline evidence gate

The factory audit again resolved and disassembled all 23 required symbols against the pinned factory SHA-256. It scanned 1,294 firmware candidates; five files had callback-related matches and zero APK/JAR archive members matched. Artifact ID: `14101829575`; ZIP SHA-256: `ba4e4a15d1acd1dca08402925b126e6fa34144dabef82a84df8292f650c0f6c7`.

This run completed without the earlier "higher priority waiting request" cancellation. The unique-`run_id` concurrency configuration is therefore verified for this push-triggered run. This does not prove behavior under every possible GitHub Actions event or repository setting, but it closes the previously observed pending-run replacement incident for the audited workflow path.

## HIDL 4.1 callback bridge: additional static lifetime evidence

Pinned bridge binary:
- `vendor/lib/hw/android.hardware.audio@4.0-impl-mediatek.so`
- SHA-256: `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`
- Evidence: workflow run `43401616001`, report `primary-hal-hidl-bridge.txt`.

The bridge disassembly confirms these device-tail dispatches:
- `Device::setupParametersCallback` loads the legacy callback at device offset `0xd0` (208) and invokes it with the bridge's `syncCallback` thunk and a `Device*` cookie.
- `Device::setAudioParameterChangedCallback` loads offset `0xd4` (212) and invokes it with the bridge's `audioParameterChangedCallback` thunk and a `Device*` cookie.
- `Device::clearAudioParameterChangedCallback` loads offset `0xd8` (216) and invokes that legacy clear callback.
- The bridge keeps HIDL callback objects in strong-reference members within the `Device` object (member locations observed at `this+0x24` and `this+0x28` in the relevant methods). The notification path obtains a local strong reference before invoking the HIDL callback and compares the current member after the call, consistent with protecting the callback object across reentrant notification.

This narrows the bridge-side object-lifetime question: the HIDL callback object has strong-reference ownership during the observed notification path. It does **not** establish that the legacy HAL's raw function pointer/cookie pair remains valid during concurrent clear, that the bridge's member updates are serialized, or that all in-flight legacy invocations are drained before owner destruction. No explicit mutex or callback-admission/drain protocol is visible in the targeted bridge methods. The exact helper semantics and cross-thread ordering remain open.

The HIDL bridge methods are not the source of the private HAL callback implementations; they call the target's legacy callback slots. Do not infer a complete callback lifetime contract from HIDL strong references alone.

## Target HAL callback registry: lock and storage recovered

Evidence: pinned factory ELF disassembly from workflow run `43565923614`, factory SHA-256 unchanged.

The target `AudioALSAHardware::setAudioParameterChangedCallback(callback, cookie)` body at `0x000ce9dc`:
- obtains the per-device lock at object offset `0x3ac` and calls `alock_lock_ms`;
- operates on the callback vector whose storage begins at object offset `0x3b0`, with vector size at `0x3b8`;
- compares existing entries by both callback function pointer and cookie;
- allocates an 8-byte pair containing the callback pointer and cookie for a new registration;
- inserts the pair into the vector and calls `alock_unlock`.

The target `clearAudioParameterChangedCallback(cookie)` body at `0x000cecc0` uses the same lock and vector, compares the stored cookie, removes matching entries with `VectorImpl::removeItemsAt`, and unlocks. Thus registration and removal of the parameter-change callback registry are serialized against one another by this per-device lock. The clear operation is cookie-scoped; callers must use the same cookie identity they registered.

The separate `setupParametersCallback(callback, cookie)` body at `0x000ce8fc` stores callback and cookie in global locations and contains no visible `alock_lock_ms`, `alock_unlock`, pthread mutex or condition-variable call in its body. Do not assume it shares the vector registry's locking protocol.

**Important boundary:** this proves locking of registration/removal mutations only. The subsequent dispatch audit is recorded below: notification iterates and invokes callbacks while holding the same lock on successful acquisition, but all three methods continue after lock failure, so serialization and in-flight draining are not guaranteed.

## 2026-11-14 callback dispatch-versus-clear audit

Workflow run: [43566157662](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43566157662)  
Audited code commit: `c077c52231db6f721ec3ddbf38b6a8757606a0b7`  
Factory SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`  
Artifact ID: `14102211955`  
Artifact ZIP SHA-256: `9e8c08c71dddcb83b6e9de1384b595d6ed89e16e183586c5340c6d637ed390ad`

All 25 required target symbols resolved and were disassembled; the new synchronization scan also recognized `alock_lock_ms` and `alock_unlock`.

### Dispatch behavior

`AudioALSAHardware::onAudioParameterChangedCallback(char const*)` at `0x000cef18`, size 496:
- obtains the same per-device lock at `this+0x3ac`;
- reads the callback vector at `this+0x3b0` and its size at `this+0x3b8`;
- iterates each 8-byte entry, loads the cookie from entry offset `+4`, loads the function pointer from entry offset `+0`, and invokes the function pointer with the changed-parameter string;
- unlocks after the callback loop.

Therefore, on the successful-lock path, callbacks are invoked while the registry lock is held. A concurrent clear using the same lock cannot remove an entry until the current dispatch releases the lock. This is a lock-based in-flight barrier only while lock acquisition succeeds.

### Critical lock-failure behavior

In all three target methods—`setAudioParameterChangedCallback`, `clearAudioParameterChangedCallback`, and `onAudioParameterChangedCallback`—the result of `alock_lock_ms` is checked. A nonzero result enters a warning/logging branch, then falls through into the vector operation rather than returning or aborting. The method can consequently mutate or traverse/invoke the callback vector without holding the lock when lock acquisition fails.

This invalidates any unconditional claim that the factory registry is race-free or that clear always drains in-flight callbacks. The successful-lock path serializes dispatch with clear; the lock-failure path does not. Static evidence does not establish whether a real target run encounters that failure, nor does it prove an actual use-after-free.

### Reentrancy and separate callback path

Because the notification function calls registered callbacks while holding the registry lock, a callback that synchronously re-enters registration or clear may contend for the same lock. The exact `alock_lock_ms` timeout/recursion semantics and HIDL callback transaction mode are not proven by this disassembly; treat reentrant unregister as unsafe until those contracts are established.

`setParametersByCallback(android::String8 const&)` at `0x000cf108`, size 396, is a separate path. It bounds the copied parameter string to less than 1,024 bytes and performs an indirect callback invocation, but contains no visible `alock`, pthread mutex or condition-variable call. Do not conflate it with the vector-backed callback registry.

### Replacement requirement

The replacement callback registry must not call client/HIDL callbacks while holding its mutation lock. It must:
1. publish an immutable or reference-counted callback entry under a control-plane lock;
2. acquire an in-flight reference before dispatch and invoke outside the lock;
3. make clear/unregister close admission and remove the entry;
4. wait for the entry's in-flight count to reach zero before releasing its cookie/owner;
5. fail closed if lock/admission state cannot be established—never continue unlocked;
6. define self-unregister/reentrant callback behavior explicitly.

P2 remains OPEN: external caller coverage, exact private ABI prototypes, lock implementation semantics, HIDL transaction mode, callback registration/clear races, detached voice-unlock worker teardown and target linker namespace remain unresolved.

### Raw HIDL bridge cookie lifetime

The HIDL bridge registration path passes its `Device*` as the opaque cookie alongside the static `audioParameterChangedCallback` thunk. The factory HAL stores that raw cookie in the callback-vector entry and passes it back to the thunk during dispatch. The bridge's local `sp<IAudioParameterChangedCallback>` protects the HIDL callback interface object, but it does not retain the `Device` owner referenced by the raw cookie.

Consequently, bridge destruction depends on successful removal/drain of the raw `Device*` cookie before the `Device` object is destroyed. The target's lock-held dispatch/clear ordering provides this only on the successful `alock_lock_ms` path; the observed lock-failure fallthrough leaves the raw-cookie lifetime unproven. This is a concrete unsafe-capable lifetime surface, not proof that a use-after-free occurred on the device.

## 2026-11-14 external MTK HAL client disassembly

Workflow run: [43566518573](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43566518573)  
Audited commit: `63ff6726093e6807ded739145762baf7f6aefe3e`  
Client ELF: `vendor/lib/libaudioprimarydevicehalifclient.so`  
Client SHA-256: `b5e805b6cf20d995babb730f70b6287d4d4ff0c7168c189de326d05e8d58c22c`  
Artifact ID: `14103230419`  
Artifact ZIP SHA-256: `bcdc0f36a0b11930b736d8b5cdf443b40bbb1a092e8523c438267d2fbd9e36ab`

The dedicated audit resolved and disassembled the client singleton, constructor/destructor and parameter methods:
- `MTKPrimaryDevicesHalClient::getInstance()` and `destroyInstance()` serialize singleton access with a pthread mutex.
- `MTKPrimaryDevicesHalClientImpl` constructs an `MTKDevicesFactoryHalInterface` through its `create()` factory and retains the returned strong interface reference.
- `setParameters(String8 const&)` and `getParameters(String8 const&)` serialize their calls with the client mutex, then dispatch through interface vtable slots at byte offsets 24 and 28 respectively.
- The audited methods contain no direct call to the private callback registration/clear methods, xWay adapters or voice-unlock methods. The client destructor releases its retained interface reference and destroys its mutex.

The firmware scanner also found `vendor/audiocmdservice_atci` referencing `MTKPrimaryDevicesHalClient::getInstance()`; the ATCI executable itself was not part of this dedicated method-level disassembly. Five firmware files had callback-related string/symbol matches overall, while zero APK/JAR archive members matched.

This identifies a concrete external user of the MTK primary-device HIDL client for generic parameter operations, but does not identify it as a caller of the private callback tail. It does not exclude other clients, stripped/indirect dispatch, runtime-loaded code or numeric-offset callers. The exact meaning of the interface vtable slots 24/28 must be confirmed against the matching HIDL interface contract before assigning source-level names.

### ATCI import and PLT evidence — caller not confirmed

The pinned `vendor/audiocmdservice_atci` executable (`302eee8562067a54d4b9e65ef982a100b94b36be6f73a993b9e01e059421a566`) has an undefined dynamic symbol for `android::MTKPrimaryDevicesHalClient::getInstance()`, an `R_ARM_JUMP_SLOT` relocation at `0x0000cf9c`, and a corresponding PLT stub at `0x000018a4`. The full executable scan did not identify a direct symbolic call site. Therefore the binary has a linked import/PLT entry, but execution of that entry by ATCI is not proven; do not list ATCI as a confirmed runtime caller. Indirect or table-based dispatch remains unresolved.

Evidence was emitted by the read-only ATCI audit in workflow run [43567387148](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43567387148). The overall run failed only in the separate HIDL Thumb-disassembly address validation; the ATCI import/relocation/PLT observations above were emitted before that failure and are independent of the HIDL disassembly.


## 2026-11-14 HIDL callback transaction mode — synchronous

Pinned HIDL interface binary:
- `vendor/lib/vendor.mediatek.hardware.audio@4.1.so`
- SHA-256: `553704e5285c4ab1e505bab569ac261f40ce7e6aa8c0c9173e038acc59dc3be5`
- Factory dataset: immutable public firmware commit `865b6efaeff0c4547dab1f4456f2fa664b3611d1`
- Evidence artifact from successful workflow run [43567544355](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43567544355), artifact ID `14102622747`, ZIP SHA-256 `0c5a641be4cc2f3c05b8978d616590432d66dbdd74b3b28d47003722bef651a4`.

The SHA-pinned disassembly of `BpHwAudioParameterChangedCallback::_hidl_audioParameterChangedCallback(...)` at `0x18128` shows:
- transaction code loaded as `1`;
- the fifth Binder transaction argument (flags) initialized to zero before the indirect `transact` call;
- the callback argument supplied separately on the stack.

For this proxy path, Binder flags are therefore `0`: the callback transaction is **synchronous**, not `FLAG_ONEWAY`. The public `audioParameterChangedCallback(hidl_string const&)` proxy method at `0x182f8` is also present. The `BsAudioParameterChangedCallback::addOnewayTask(...)` method is a separate passthrough helper and does not change the flags used by this Bp proxy transaction.

The audit script now asserts the pinned proxy's transaction-code/flags instruction signature and emits the synchronous classification; a binary/disassembly mismatch fails the audit rather than silently changing the conclusion. This assertion is static evidence for the pinned library, not a runtime Binder test.

### Lock interaction and risk

The factory HAL's `onAudioParameterChangedCallback(char const*)` invokes registered callbacks while holding its registry lock on the successful-lock path. If the registered callback is this HIDL proxy, the service-side dispatch waits synchronously for the remote callback transaction to return while that lock remains held.

A remote callback that re-enters the HAL and synchronously attempts `clearAudioParameterChangedCallback` can consequently contend for the same lock while the original dispatch is waiting for the remote transaction. The actual callback implementation and reentrant call path have not been established, so this is a concrete deadlock/timeout-capable design hazard, **not proof of an observed deadlock**. The previously identified lock-acquisition failure fallthrough further prevents treating the factory lock as a reliable drain barrier.

Replacement requirement remains unchanged: never invoke external/HIDL callbacks while holding the callback-registry mutation lock; close admission, retain an in-flight reference, invoke outside the lock, and drain before destroying the cookie owner. Keep the Primary HAL fail-closed and non-installable. P2 remains OPEN.
