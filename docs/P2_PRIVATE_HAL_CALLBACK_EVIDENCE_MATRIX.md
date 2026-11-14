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

**Concurrency-fix verification status:** this successful run belongs to the pre-fix audit commit `41e52071...`, not the later concurrency-group commit `bd79bbda...`. A subsequent run on the latter or a descendant is still required to verify the unique-`run_id` concurrency configuration end-to-end.

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
