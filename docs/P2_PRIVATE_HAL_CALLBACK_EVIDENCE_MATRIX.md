# P2 private HAL callback evidence matrix

Status: **OPEN — static evidence only; runtime contracts are not closed.**

The expanded caller classification is maintained in [Factory Private Callback Caller Audit — 2026-11-14](P2_FACTORY_CALLBACK_CALLER_AUDIT_2026-11-14.md).

Evidence baseline:
- Factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`
- Factory SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- Target: Android 9, ARM32, MT8163.
- Primary ABI and private-tail analysis: [Android 9 ARM32 Primary HAL ABI baseline](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md).
- Android framework close analysis: [Android 9 stream callback audit](ANDROID9_STREAM_CALLBACK_AUDIT.md).
- Non-authoritative vendor source comparison: [P2 vendor source corroboration](P2_VENDOR_SOURCE_CORROBORATION.md).

## Latest reproducible callback/lifecycle evidence

- Workflow [43603169612](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43603169612): all jobs passed; evidence artifact [14117480619](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43603169612/artifacts/14117480619), containing 14 reports.
- The corrected whole-ELF callback xref audit parsed the pinned ARM32 factory ELF and enumerated 1,575 register-indirect `BLX` sites. It found no direct branch callsites to the five audited callback methods; indirect dispatch means this is not proof that no callers exist.
- `onAudioParameterChangedCallback()` dataflow is confirmed at `0x000cf020`: callback pointer at vector-entry `+0`, cookie at `+4`, parameters in `r0`, cookie in `r1`, indirect `BLX r2`. Invocation occurs before unlock on the lock-success path; lock-failure still falls through to traversal.
- `setParametersByCallback()` at `0x000cf108` uses a separate global callback/cookie pair and indirect `BLX r3` at `0x000cf218`. Caller graph and lifetime remain OPEN; do not conflate it with the vector registry.
- Factory HMI close-to-`AudioALSAHardware` deleting-destructor dispatch is now independently correlated by the dependent lifecycle audit. The destructor still shows no explicit parameter-callback admission closure or in-flight drain.
- The `AudioALSAStreamManager` event worker's verified join/unregister path is a separate synchronization domain and does not close the parameter callback lifetime gate.

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

P2 remains OPEN: external caller coverage, exact private ABI prototypes, lock implementation semantics, callback registration/clear races, detached voice-unlock worker teardown and target linker namespace remain unresolved. The pinned HIDL Bp callback proxy transaction mode is separately established as synchronous (Binder flags 0) in the evidence section below.

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


## 2026-11-14 HIDL bridge callback ownership and reentrancy audit

Pinned bridge binary:
- `vendor/lib/hw/android.hardware.audio@4.0-impl-mediatek.so`
- SHA-256: `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`
- Prior source artifact: workflow run [43401616001](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401616001), artifact ID `14024364994`, report `primary-hal-hidl-bridge.txt`.

The targeted disassembly establishes these bridge-side operations:

1. `Device::setAudioParameterChangedCallback(...)` reads the legacy HAL registration slot at `audio_hw_device+0xd4` and invokes it with the static `Device::audioParameterChangedCallback` thunk and the `Device*` cookie. It then assigns the supplied HIDL callback strong reference to the member at `Device+0x28`.
2. `Device::clearAudioParameterChangedCallback()` invokes the legacy HAL clear slot at `audio_hw_device+0xd8` before releasing/resetting the stored callback strong reference at `Device+0x28`. The exact argument identity passed through this wrapper is not promoted to a source-level prototype without matching headers.
3. The static callback thunk obtains a local strong reference from the callback member at `Device+0x28`, invokes the callback interface through vtable byte offset `0x34`, then compares the current member with the previously observed callback and performs the corresponding reference cleanup. This is consistent with protecting the HIDL callback object across a reentrant callback-member replacement.
4. No direct `alock`, pthread mutex or rwlock primitive call is visible in these three targeted bridge method bodies. This is not proof that every helper or caller is lock-free, and no Device destructor/owner teardown protocol was recovered from this report.

### Reentrancy boundary

The local `sp<IAudioParameterChangedCallback>` protects the callback interface object after it has been acquired. It does **not** keep the `Device` object alive while the thunk dereferences the raw `Device*` cookie to acquire that strong reference.

The bridge's clear ordering is correct in intent—ask the legacy HAL to remove the cookie before dropping the stored HIDL callback reference—but its completion is only as strong as the legacy clear operation. The pinned factory HAL can continue into registry operations after `alock_lock_ms` fails, so this ordering is not a proven callback drain barrier.

Combining the independently pinned transaction and dispatch evidence:
- the factory HAL invokes the registered callback while holding its registry lock on the successful-lock path;
- the HIDL Bp proxy sends a synchronous Binder transaction (`flags=0`);
- the remote callback therefore runs before the factory dispatch call returns;
- if remote callback code synchronously calls back into the same Device service to clear/re-register, a Binder worker can block on the factory registry lock while the original dispatch waits for the remote callback.

This is a **possible cross-process lock cycle**, not a proven runtime deadlock. The actual remote callback implementation and whether it re-enters Device were not found in the pinned evidence. A same-thread local member replacement is partially protected by the thunk's local strong reference; that does not resolve the cross-process factory-lock cycle or raw-cookie lifetime.

### Replacement contract

- Never invoke HIDL/client callbacks while holding the callback-registry mutation lock.
- Use a stable owner token rather than a raw `Device*` cookie, or provide an independently proven owner-lifetime pin.
- Close callback admission before unregistering.
- Retain an in-flight reference, invoke outside the lock, then drain before releasing the owner.
- Define self-unregister and cross-process reentrant unregister behavior.
- Treat lock acquisition failure as fatal to the operation; never continue unlocked.

A dedicated SHA-pinned bridge audit script is now part of the P2 workflow and checks the three method symbols and key callback/legacy-slot offsets. The Primary HAL remains fail-closed and non-installable. P2 remains OPEN.


### Reproducible bridge-audit result

The new SHA-pinned bridge audit was executed by P2 workflow [43569787813](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43569787813) on commit `88b22e2bd254741a028b1a1f6b68ead63e94c1f8`. All three jobs passed, including the bridge audit and aggregate offline-evidence gate.

Artifact:
- ID: `14103379010`
- ZIP SHA-256: `a33646a1fe7747699064d02cc040d48085a4208254bce41fae3b2e4a441bd8aa`
- Report: `p2-hidl-bridge-lifecycle.txt`

The report resolved all three required symbols and verified the callback-member access, callback vtable slot, and legacy registration/clear tail offsets against the pinned bridge ELF. The report contains no visible direct lock primitive calls in these three method bodies. It does not inspect all transitive helper implementations or prove runtime owner destruction ordering.


## 2026-11-14 non-matching vendor source corroboration: HIDL Device ownership

Public source examined:
- Interface definitions: [vendor.mediatek.hardware.audio@4.1 IAudioParameterChangedCallback.hal](https://github.com/dotOS-Devices/android_vendor_mediatek_interfaces/blob/5f9dde8fb39fe1ceb0a8284d93884b2490b1d7c4/audio/4.1/IAudioParameterChangedCallback.hal) and [IMTKPrimaryDevice.hal](https://github.com/dotOS-Devices/android_vendor_mediatek_interfaces/blob/5f9dde8fb39fe1ceb0a8284d93884b2490b1d7c4/audio/4.1/IMTKPrimaryDevice.hal).
- Service implementation comparison: [Device.h](https://github.com/dotOS-Devices/android_vendor_mediatek_opensource/blob/b8bed8bbc7ed61f09258a39ca553758f6ad076be/hardware/audio/common/service/5.0/Device.h) and [Device.cpp](https://github.com/dotOS-Devices/android_vendor_mediatek_opensource/blob/b8bed8bbc7ed61f09258a39ca553758f6ad076be/hardware/audio/common/service/5.0/Device.cpp), repository commit `b8bed8bbc7ed61f09258a39ca553758f6ad076be`.

The public 4.1 HIDL declaration defines `audioParameterChangedCallback(string audioTypeName)`; `IMTKPrimaryDevice` declares parameter-callback registration and a zero-argument `clearAudioParameterChangedCallback()`. This is interface-level corroboration for the HIDL callback method shape, not a declaration of the private C HAL tail prototypes.

The available service 5.0 implementation (not the target's 4.0 bridge source) adds useful ownership detail:
- `Device.h` declares `static int audioParameterChangedCallback(const char *param, void *cookie)`, and stores the callback in `sp<IAudioParameterChangedCallback>`.
- `Device.cpp` has a process-global `Vector<sp<Device>> gCallbackDeviceList` explicitly commented as keeping Device objects alive during callback processing.
- Registration passes `this` as the HAL cookie, then pushes `this` into that strong-reference vector and assigns the HIDL callback member. The push/member assignment occur even when the HAL registration result is nonzero; the method returns the analyzed error afterward.
- Clear passes `this` to the HAL clear callback, then removes matching Device references from the global vector regardless of the clear result. This is a potential ownership gap if HAL clear fails or does not drain callbacks; the target HAL's lock-acquisition failure fallthrough makes this comparison especially relevant.
- The static thunk reconstructs `wp<Device>` from the raw cookie and promotes it, then dereferences `self` and its callback member without visible null checks in this source. The global strong-reference list is intended to make promotion succeed while registered, but the clear/error path must be reliable for that assumption to hold.

**Evidence boundary:** this is non-matching-version source corroboration (service 5.0 / interface 4.1), not proof that the exact MT8163 Android 9 service binary uses this global list or identical error handling. Do not back-project these source details onto the pinned 4.0 bridge ELF. It does, however, provide a concrete candidate design explanation for why the bridge may retain Device owners outside the callback member itself, and identifies a specific behavior to verify against the exact target source or binary: whether failed clear still drops the owner pin.

Target clear-wrapper cookie identity has since been resolved by register-level analysis of the pinned 4.0 bridge ELF; see the following section. The later service 5.0 source remains non-matching-version corroboration only.

P2 remains OPEN. Keep the exact target ABI, owner-pin implementation, clear failure behavior, callback drain and Device destruction ordering as hard gates.


## 2026-11-14 target bridge register-level cookie identity

The pinned target bridge ELF (SHA-256 58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc) was re-read using the ARM32 AAPCS return convention for HIDL Return<void>. The non-trivial return uses a hidden sret pointer in r0; therefore r1 is the Device* this value in both member wrappers and r2 is the setter's explicit callback argument.

Setter at 0x1cd1c:
- Saves r1 (Device this) in r6.
- Loads the legacy audio_hw_device* member from [r6, #8] into r0.
- Loads the legacy set slot from [r0, #0xd4].
- Places the callback thunk in r1 and restores r2 = r6 immediately before blx r3.
- Thus the factory callback registry receives Device* as its cookie in the third argument.

Clear at 0x1cdac:
- Keeps the hidden sret pointer in r4; r1 remains the original Device* this.
- Loads the same legacy audio_hw_device* member from [r1, #8] into r0.
- Loads the legacy clear slot from [r0, #0xd8] into r2.
- Calls blx r2 without modifying r1; therefore the factory member function receives the same Device* cookie as its second argument.

The factory HAL ELF independently confirms the corresponding member signatures:
- setAudioParameterChangedCallback(int (*)(char const*, void*), void*) stores its third C++ argument as the cookie.
- clearAudioParameterChangedCallback(void*) compares its first explicit C++ argument against stored cookies.
- Both wrappers receive the factory object pointer as the implicit C++ this in r0; the cookie is the next register argument.

Conclusion: the target bridge set/clear wrappers pass the same Device* cookie. The suspected cookie mismatch is rejected. This does not establish callback drain: the factory lock-failure fallthrough remains, and the target bridge Device owner/destructor protocol is still not recovered.

The bridge audit script now asserts the setter cookie register and that clear preserves r1 through the indirect call. A compiler/ABI change that alters this register contract will fail the offline audit rather than silently changing callback identity.


## Next target-binary owner-lifetime audit — automation added 2026-11-14

A read-only audit was added at `scripts/repository/audit_primary_hal_p2_hidl_device_lifetime.py` for the SHA-pinned Android 4.0 bridge ELF (`58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`).

The audit inventories demangled `Device` constructor/destructor symbols and named OBJECT/NOTYPE symbols whose names suggest callback ownership, vectors, weak/strong references or Device lifetime. It disassembles every retained constructor/destructor with relocations and records direct call targets. The report is intentionally a discovery aid: symbol names and constructor/destructor bodies alone cannot prove that a global owner-pin list is populated, that clear drains callbacks, or that a destructor is reachable during service teardown.

The P2 workflow now includes this report as `p2-hidl-device-lifetime.txt`. **Execution against the target ELF is pending**; no target owner-pin conclusion is promoted until the resulting artifact is reviewed. Missing or stripped symbols must remain an explicit evidence gap, not be interpreted as absence of ownership.

## 2026-11-14 — target HIDL Device destructor audit

Workflow run: [43579336500](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43579336500)  
Audited commit: `80a1f8d3abf8170466425ec7ad41c4f5a84f9066`  
Bridge SHA-256: `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`  
Artifact ID: `14107960184`  
Artifact ZIP SHA-256: `7c5cd5d1b088f0142ca908835cf45aa3835bc015f2d6f858faae42dbba8b0da0`

All three workflow jobs passed: contract/fail-closed gates, pinned factory ELF and external-client scan, and the aggregate offline-evidence gate. The artifact contains the new `p2-hidl-device-lifetime.txt` report.

### Newly recovered destructor order

The target bridge ELF retains two `Device` constructors and seven destructor/thunk symbols. In the complete `Device::~Device()` body at `0x1b91d`:

1. It loads the legacy `audio_hw_device_mtk*` from `Device+0x08`.
2. It calls the legacy device `close` function pointer at offset `+0x3c`.
3. If `close` returns nonzero, it logs and branches back to retry the same close call. No bounded retry count or delay is visible in this destructor body.
4. Only after `close` returns zero does it begin clearing/releasing the Device's strong-reference fields, including the callback-related member at `Device+0x28`.

The destructor body does not visibly call the private `clearAudioParameterChangedCallback` slot at `audio_hw_device_mtk+0xd8` before invoking `close`. Therefore the teardown safety contract depends critically on the semantics of the legacy `close` implementation: whether it closes callback admission, synchronizes with every in-flight dispatch, and prevents later use of the raw `Device*` cookie before the HIDL callback strong reference is released.

The symbol inventory found no named global `Device` owner/list/vector symbol in the pinned bridge ELF. This is only a symbol-table observation; it does not exclude stripped, unnamed, indirect, or externally maintained ownership.

### Updated conclusion and next target

This evidence narrows the target owner-lifetime question but does not close it. It does **not** prove that `close` is a callback-drain barrier, that the destructor is reached on every teardown path, or that the retry loop terminates. The factory callback registry's lock-failure fallthrough remains a known reason not to assume clear/dispatch serialization.

Next audit must resolve the exact legacy device `close` callback implementation and the `AudioALSAHardware` destruction path, including callback-registry shutdown and any lock/drain behavior. Until then:
- callback owner lifetime and in-flight drain remain OPEN;
- Primary HAL remains fail-closed and non-installable;
- no hardware operations or device writes are authorized.


## 2026-11-14 — HIDL close callback correlated with factory destruction

Evidence sources:
- Pinned factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`, SHA-256 `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`.
- Pinned HIDL bridge: `vendor/lib/hw/android.hardware.audio@4.0-impl-mediatek.so`, SHA-256 `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`.
- Factory artifact: workflow run [43586351304](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43586351304), artifact ID `14109858471`; reviewed reports `p2-factory-elf-audit.txt`, `p2-factory-device-close.txt`, and `p2-hidl-device-lifetime.txt`.
- Target HIDL destructor audit: workflow run [43579336500](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43579336500), artifact ID `14107960184`.

### Cross-ELF call chain

The HIDL bridge's complete `Device::~Device()` at `0x0001b91c` loads the legacy `audio_hw_device_mtk*` from `Device+0x08`, reads its close function pointer at byte offset `+0x3c`, and calls it indirectly. It retries while the callback returns nonzero; no bounded retry count or delay is visible.

The factory module-open routine at `0x0009b70c` allocates the 0xe0-byte legacy device structure and stores the callback at offset `+0x3c`. The PC-relative sequence at `0x0009b780`, `0x0009b79c`, and `0x0009b7ac`, with literal `0x000002a4` at `0x0009b998`, resolves the stored callback target to:

`0x0009b79c + 8 + 0x000002a4 = 0x0009ba48`.

Address `0x0009ba48` is the factory `HMI.close` wrapper. Its last-reference path loads the legacy C++ object from wrapper offset `+0xdc`, dispatches through the object's vtable slot `+0x00c`, and reaches the deleting destructor `AudioALSAHardware::~AudioALSAHardware()` at `0x000c5874`. The vtable relocation report pins the non-deleting destructor at `0x000c56e4` and deleting thunk at `0x000c5874`.

Therefore the cross-binary teardown chain is statically established:

`HIDL Device::~Device() -> audio_hw_device_mtk.close(+0x3c) -> HMI.close(0x9ba48) -> AudioALSAHardware deleting destructor(0xc5874)`.

### Safety interpretation

This correlation resolves which factory function the HIDL destructor invokes; it does **not** establish callback quiescence:

- The factory `AudioALSAHardware` destructor destroys the callback vector and its `alock` storage. Its audited body has no visible callback-admission closure, registry clear loop, or in-flight dispatch drain.
- The factory callback dispatcher invokes the registered callback while holding the registry lock on the successful-lock path. Registration, clear, and dispatch all fall through to registry access after `alock_lock_ms` failure.
- The HIDL `Device` destructor does not visibly call private clear slot `+0xd8` before invoking legacy close. It releases the callback-related strong-reference member at `Device+0x28` only after close returns zero.
- The raw `Device*` cookie is therefore kept allocated by the destructor's active stack frame during the close call, but no evidence proves that factory callback dispatch has stopped before the factory callback registry is destroyed or before the HIDL destructor releases its callback member.
- A nonzero close result causes an unbounded retry loop in the HIDL destructor body. Do not assume this path always terminates.

These are static control-flow and object-layout observations. They establish an unsafe-capable teardown surface, not a runtime UAF, race, or deadlock.

### Regression gate added

`scripts/repository/audit_primary_hal_p2_device_close_contract.py` now verifies the module-open instruction/literal sequence that initializes `audio_hw_device_mtk.close` and computes that its target equals the audited `HMI.close` entry at `0x0009ba48`. The assertion runs in the P2 private-callback workflow against the SHA-pinned factory ELF.

Remaining blocker: obtain a callback-drain guarantee from the exact target close path or implement a replacement lifetime protocol that closes admission and drains dispatch before destroying registry storage. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.


### Close return contract refinement

The pinned `HMI.close` body converges its null-wrapper and normal teardown branches on the final `mov r0, #0` at `0x0009badc` and returns at `0x0009bae0`. On the audited target path, the HIDL destructor's nonzero-close retry loop therefore does not repeat after a normal `HMI.close` invocation. The loop remains structurally unbounded if a different callback implementation or corrupted state returns nonzero; the target wrapper's observed success return does not prove callback drain.

The close-contract regression now also checks this zero-return epilogue. Its CI result is pending.


## 2026-11-14 — exact HIDL destruction order and factory cleanup owner

Pinned bridge ELF: SHA-256 `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`.

The complete `Device::~Device()` body at `0x0001b91c` now has a regression assertion for this order:
1. Load legacy `audio_hw_device_mtk*` from `Device+0x08`.
2. Load and invoke its `close` slot at byte offset `+0x3c`.
3. Only after close returns, release the callback strong-reference member at `Device+0x28`.

No direct load/invocation of the legacy clear slot `+0xd8` is visible in this destructor body. The callback thunk independently reads the same member at `Device+0x28` before invoking the callback interface. This proves the destructor's local instruction order, not that an already-admitted factory callback has drained before close returns.

### Resolve factory cleanup object at AudioALSAHardware+0x04

The pinned factory constructor calls `AudioALSAStreamManager::getInstance()` and stores its pointer at `AudioALSAHardware+0x04`. The destructor invokes that object's vtable slot `+0x04`. The `AudioALSAStreamManager` vtable relocation report resolves that slot to deleting destructor `0x00077e20` (complete destructor `0x00077b58`).

The StreamManager destructor clears its singleton pointer and calls `AudioEventThreadManager::getInstance()->unregisterCallback(AudioEventType(0))`, then destroys its own vectors. It is distinct from the `AudioALSAHardware` parameter-change callback registry at `this+0x3ac` / `this+0x3b0`. The unregister call may affect an event-thread callback path, but no direct call to `AudioALSAHardware::clearAudioParameterChangedCallback` is visible. The internal event-thread unregister/drain semantics are now under separate binary audit; do not treat this cleanup as proven quiescence for the parameter-change registry.

Regression gates:
- `audit_primary_hal_p2_hidl_bridge.py`: pins close-before-`Device+0x28` release and absence of direct clear-slot invocation in the complete Device destructor.
- `audit_primary_hal_p2_device_close_contract.py`: pins `AudioALSAHardware+0x04` construction, deleting-destructor dispatch, and StreamManager vtable slot.
- `audit_primary_hal_p2_factory_resource_manager.py`: captures StreamManager and AudioEventThreadManager lifecycle functions, including unregister, worker loop, worker destructor, signal, and notify paths.

CI status for the newest additions must be checked against the exact latest `feature` SHA before marking these regression gates verified. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.


## 2026-11-14 AudioEventThread unregister/join contract — static ELF evidence

Evidence: workflow run [43588457542](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43588457542), audited code commit `e8721fc04e5489de1b4c224359e44d98a89b5e17`, artifact ID `14111186560`, artifact ZIP SHA-256 `bed55851b8da6a7d57e02ffc2f760412b22211931d7392e62497b6695a30f88e`. Reviewed report: `p2-factory-resource-manager-lifecycle.txt`. Factory ELF SHA-256 remains `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`.

The target `AudioALSAStreamManager::AudioALSAStreamManager()` at `0x00076d94` calls `AudioEventThreadManager::getInstance()` and registers an event callback with event type `0`, a callback pointer loaded from the target's relocation-backed data, and `this` as cookie (`0x7721c..0x77230`). Its destructor at `0x00077b58` calls `unregisterCallback(AudioEventType(0))` before destroying its own vectors and synchronization objects.

The target `AudioEventThreadManager::unregisterCallback(AudioEventType)` at `0x0013612c`:
- finds the worker entry whose stored event type equals the requested type;
- clears the worker's run byte at object offset `+0x1c`;
- calls `AudioEventThread::signal()`;
- loads the worker pthread ID and calls `pthread_join()`;
- removes the worker entry from the manager vector only after the join returns.

The worker loop at `0x00135034` checks the run byte, waits on its event lock/condition, invokes its registered callback entries, and exits through `pthread_exit()` after the run byte is cleared. The `AudioEventThread` destructor at `0x001353b4` repeats the stop/signal/join sequence before destroying its synchronization state. Thus the event-type unregister path is a real worker-stop-and-join operation, not merely removal of a callback record. The worker's callback loop is executed on this worker thread; `pthread_join()` waits for that thread to terminate.

**Scope boundary:** this establishes quiescence of the matching `AudioEventThread` worker for event type `0` on the observed successful control path. It does not establish that the event callback is the HIDL `setAudioParameterChangedCallback` registry callback, nor that it drains `AudioALSAHardware`'s separate vector at `this+0x3b0` protected by `this+0x3ac`. The StreamManager registration's cookie is `AudioALSAStreamManager*`, while the parameter-change registry is owned by `AudioALSAHardware`; no direct cross-link from this event-thread callback to that registry's dispatch/clear functions is established by the inspected disassembly. The parameter-change registry's lock-failure fallthrough and callback-under-lock behavior therefore remain unresolved safety blockers.

CI run [43588457542](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43588457542) completed successfully: both evidence jobs and the aggregate offline gate passed. This is static binary evidence only. P2 remains OPEN; Primary HAL remains fail-closed and `installable: false`.

## 2026-11-14 — callback dataflow regression assertions

The pinned factory callback audit now fails CI if the reviewed instruction-level dataflow changes:

- The vector-backed dispatcher must load the registration vector and indexed entry, load the cookie from entry `+4`, load the callback from entry `+0`, move the parameter pointer into `r0`, and invoke through `BLX r2`.
- The dispatcher must retain the observed lock-call/branch structure and invoke the callback before the `alock_unlock` call in the pinned function.
- The separate `setParametersByCallback()` path must load its global callback and cookie cells independently, pass the stack parameter structure in `r1`, event value in `r0`, cookie in `r2`, and invoke through `BLX r3`.

The assertions parse normalized ARM disassembly instruction records and are scoped to the immutable factory HAL SHA-256 already pinned by the audit. ARM objdump comments are removed before operand comparison.

CI run [43604214721](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43604214721) completed successfully on code commit `7a39afefa91040b62c110a0a191538da73bf06b7`. The artifact [14117686286](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43604214721/artifacts/14117686286) contains the reproducible report, including `CALLBACK_DATAFLOW_ASSERTIONS: PASS`.

**Scope:** this regression protects the observed static dataflow against binary/auditor drift. It does not identify all indirect callers, establish callback owner lifetime, or prove in-flight drain. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.

## 2026-11-14 — exact-address callback registration regression CI PASS

The parser fix and expanded registration-store assertions are verified on feature commit `162e48a3dc127a8c1e25ae9a8890abaab3881671` by [workflow run 43607609463](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43607609463). All three workflow jobs passed: P2 contract inventory/fail-closed gates, reproducible factory binary/external-client scan, and aggregate offline evidence gate.

The pinned factory audit emitted `CALLBACK_DATAFLOW_ASSERTIONS: PASS` for factory HAL SHA-256 `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`. The assertions include:
- Exact-address parsing of `setupParametersCallback()` at `0x000ce8fc`, avoiding same-name/alias body ambiguity.
- Callback and cookie argument capture at `0x000ce904` and `0x000ce908`.
- Callback global pointer store at `0x000ce934` and cookie global pointer store at `0x000ce940`, including the ordered PC-relative loads.
- Existing vector-registry dispatch ABI/lock ordering and the separate `setParametersByCallback()` global callback dispatch path.

Artifact [14119346339](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43607609463/artifacts/14119346339) preserves the evidence reports; ZIP SHA-256 is `4b78d162765e4e30ae4766168d766397a8736455061a21da9ecd81e18a91626d`.

**Interpretation:** the exact-address parser and static instruction regression are now CI-verified. This does not prove the global callback's owner lifetime, registration replacement semantics, in-flight drain, complete external caller inventory, or safe destruction ordering. The vector-backed parameter-change registry and global setup-parameters callback remain distinct synchronization/lifetime domains. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.

## 2026-11-14 — global setup-parameters callback/cookie path: body-level evidence

CI [run 43607609463](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43607609463) passed on the exact-address parser commit. The pinned factory disassembly provides the following additional facts for the separate `setupParametersCallback()` mechanism:

| Item | Evidence |
|---|---|
| Registration function | `0x000ce8fc`, 224 bytes |
| Callback argument | `r1 → r5` at `0x000ce904` |
| Cookie argument | `r2 → r4` at `0x000ce908` |
| Callback global GOT displacement | `0x000c4248` → effective slot `0x00192b80`, store at `0x000ce934` |
| Cookie global GOT displacement | `0x000c4238` → effective slot `0x00192b7c`, store at `0x000ce940` |
| Dispatch function | `0x000cf108`, 396 bytes |
| Callback load / indirect call | `0x000cf1f8..0x000cf218`, `BLX r3` |
| Cookie load | `0x000cf208..0x000cf210` |
| Dispatch arguments | event `0` in `r0`, stack-local parameter structure in `r1`, cookie in `r2` |

Neither audited function body shows a lock, reference-count pin, or in-flight drain around the global callback/cookie pair. This is a bounded static finding about these bodies, not proof that no external caller serialization exists.

Keep this path separate from the vector-backed registry. The bridge's `Device::setupParametersCallback` thunk/cookie relationship is known at symbol/slot level, but complete upstream call graph, registration replacement policy, owner lifetime, and teardown quiescence are still unproven. No integration is permitted. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.


## 2026-11-14 — HIDL setup slot 0xd0 forwarding confirmed

The HIDL bridge setup registration body is now preserved in CI artifact [14120126885](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43611282299/artifacts/14120126885), generated by workflow [43611282299](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43611282299) (all three jobs PASS).

For the pinned bridge SHA-256 `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`:
- `Device::setupParametersCallback` at `0x0001cadc`, 112 bytes, loads the legacy function pointer at `audio_hw_device_mtk+0xd0` and calls it indirectly.
- It passes the `Device::syncCallback` thunk and `Device*` cookie.
- On successful registration it stores the supplied HIDL callback strong reference at `Device+0x24`.
- The 10-byte `MTKPrimaryDevice::setupParametersCallback` wrapper forwards to the base Device method.

The slot mapping and registration arguments are established. The `syncCallback` body is now preserved and inspected in artifact [14121078085](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43613705447/artifacts/14121078085). Its event-zero path dispatches through the callback interface at `Device+0x24`; nonzero events take a log/error path. This setup callback channel is distinct from the `0xd4` parameter-changed callback channel at `Device+0x28`. The effective setup callback/cookie GOT slots are `0x00192b80` and `0x00192b7c`; prior `0xc4248/0xc4238` values are PC-relative displacements, not absolute addresses. Upstream callers and lifetime/quiescence remain open. Do not treat the HIDL strong reference as proof that the raw `Device*` cookie remains valid during concurrent factory dispatch. P2 remains OPEN; Primary HAL remains fail-closed and `installable: false`.


## 2026-11-14 — syncCallback and two-channel lifecycle evidence

CI [run 43613705447](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43613705447) passed all 3 jobs on commit `6143502ebce5a3d96a0cce0bb086e816049f4999`. Artifact [14121078085](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43613705447/artifacts/14121078085), SHA-256 `afb24415097f0949380509e0088bf7fd3e6934119299b58f1e77230de4bb99ce`.

| Callback channel | Private slot | HIDL thunk | HIDL strong-ref member | Status |
|---|---:|---|---:|---|
| Setup parameters | `0xd0` | `Device::syncCallback` | `Device+0x24` | Body inspected; lifetime/quiescence open |
| Parameter changed | `0xd4` | `Device::audioParameterChangedCallback` | `Device+0x28` | Separate path; lifetime/quiescence open |

For `syncCallback` at `0x0001cb4c` (240 bytes), event zero converts the parameter string and invokes the HIDL callback interface through vtable offset `0x34`; nonzero events take a logging/error branch. No explicit mutex/admission gate/in-flight drain is visible in this body.

The setup callback and cookie are stored independently in factory GOT slots `0x00192b80` and `0x00192b7c`. The literals `0x000c4248`, `0x000c4238`, `0x000c3978`, and `0x000c3968` are PC-relative displacements used to resolve those slots, not absolute addresses. The factory auditor now asserts the literal words and effective PC-relative slot calculations.

HIDL Device destruction closes the legacy HAL device before dropping callback strong refs at `+0x24` and `+0x28`. The audited Device destructor does not call private setup clear slot `0xd0`; the factory AudioALSAHardware destructor clears the setup cookie GOT slot `0x00192b7c` and callback GOT slot `0x00192b80`, in that order, without a visible in-flight drain. A dispatch that loaded the old pair before those stores may still be executing; this is a static race surface, not a proven runtime race. Do not integrate either callback channel into AudioCore until caller serialization, owner lifetime, replacement and in-flight drain are proven. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.


### Factory destructor clear regression

The factory callback auditor now verifies the pinned destructor at `0x000c56e4`:
- `0x000c576c` clears cookie slot `0x00192b7c`.
- `0x000c5778` clears callback slot `0x00192b80`.
- The order is cookie first, callback second; no wait/drain is visible between them.
- Synthetic unit tests cover GOT displacement resolution, the shared effective registration/dispatch slots, changed literal rejection, and destructor clear ordering.

The global callback pair is therefore cleared by factory destruction, but callback quiescence is still unproven. Registration and dispatch use separate loads/stores; a callback already admitted with the old pair can outlive the global clear. P2 remains **OPEN**.


CI regression [43617157323](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43617157323) passed all 3 jobs. Artifact [14121669562](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43617157323/artifacts/14121669562), ZIP SHA-256 `5dfe84cc04a98647adca89770eb7f73a5e324b9effedbf3ccf67a8ecbefb4319`. The factory audit verified both effective GOT slots and the destructor clear order (cookie `0x00192b7c` before callback `0x00192b80`). Four synthetic unit tests passed. No in-flight drain was established; P2 remains **OPEN**.


## 2026-11-14 — factory callback regression rerun and HIDL helper audit queued

Workflow run [43617327107](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43617327107) completed with all three jobs successful on audited commit `efc40cba29af57f3c093a7d6b37ba6409a52c189`:
- P2 contract inventory and fail-closed gates: PASS.
- Reproducible factory binary and external-client scan: PASS.
- Aggregate offline evidence gate: PASS.

The factory audit preserved 16 reports in artifact [14122478954](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43617327107/artifacts/14122478954), ZIP SHA-256 `eac369dc8cacf1af624cf0ede3e906c04ef0a5b2f6317209160c0f783763f780`. It reconfirmed the pinned factory/HIDL binary identities, callback xref/dataflow assertions, setup-callback GOT clear ordering, and the external firmware scan limitations. This run is a successful static/offline audit; it does not close callback owner lifetime, registration replacement, or in-flight drain.

After that run, feature advanced to `77c3741c4d3afc29298417b832b53af295bd3a3b` with the new read-only `audit_primary_hal_p2_hidl_sync_helpers.py` and its workflow integration. The helper auditor disassembles the direct helper targets reached from `Device::syncCallback` so reference ownership and lifetime behavior can be assessed from instructions/dataflow rather than helper names. **Run 43617327107 predates those additions and does not validate this helper auditor.** The current feature update is intentionally triggering a fresh P2 workflow; its artifact must be inspected before assigning semantics to those helpers.

Current branch check at this update: `feature=77c3741c4d3afc29298417b832b53af295bd3a3b`; `main=cc2fd72ed118c8695ccbe9556f5b5841efc0af59` (unchanged). P2 remains OPEN; Primary HAL remains fail-closed and `installable: false`.
