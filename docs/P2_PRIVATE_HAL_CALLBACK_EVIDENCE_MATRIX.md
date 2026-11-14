# P2 private HAL callback evidence matrix

Status: **OPEN — static evidence only; runtime contracts are not closed.**

Evidence baseline:
- Factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`
- Factory SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- Target: Android 9, ARM32, MT8163.
- Primary ABI and private-tail analysis: [Android 9 ARM32 Primary HAL ABI baseline](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md).
- Android framework close analysis: [Android 9 stream callback audit](ANDROID9_STREAM_CALLBACK_AUDIT.md).

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
6. `AudioVUnlockDL::startInput()` creates a detached pthread. `stopInput()` sets stop state and polls for worker exit; `freeInstance()` separately frees singleton-owned state and does not stop/join the worker. The inspected destructor does not provide shutdown.
7. Android 9 AOSP r3 ordinary playback/capture close waits for worker-side I/O quiescence before deleting stream wrappers. This does not establish private HIDL callback quiescence, callback-table serialization, or the lifetime of the detached voice-unlock worker.

## Unresolved hard gates

- [ ] Identify every external xWay caller, including numeric-offset and indirect dispatch from executables, APK/JNI payloads and runtime-loaded components.
- [ ] Recover and cross-check exact source-level prototypes and return types for all 16 slots against matching vendor headers/source or complete caller-side ABI evidence.
- [ ] Prove callback admission, in-flight drain and owner lifetime for every private callback, including HIDL parameter callback registration, replacement and clear races.
- [ ] Prove voice-unlock teardown ordering: stop request, worker-exit observation, drain all `ReadRefFromRing` callers, then free singleton and ring/SRC state. The detached worker cannot be treated as joined.
- [ ] Prove serialization of concurrent start/stop/read/write and callback registration/control calls; define lock ordering and reentrancy.
- [ ] Obtain the exact target Android 9 linker namespace configuration and verify all `DT_NEEDED` libraries and search paths.
- [ ] Pin matching ALPS/MediaTek vendor headers or source revision and reconcile private declarations with the recovered binary.
- [ ] Add deterministic host tests for callback admission/close races and teardown ordering, then run TSAN/ASAN/UBSAN.

## Automation scope and limits

The P2 GitHub Actions workflow:
- verifies the factory HAL SHA-256 and ARM32 ELF contract;
- runs the existing APS2 relocation/vtable audit;
- scans extracted system/vendor shared libraries and service executables for callback-related symbols and strings;
- preserves scan reports as workflow artifacts;
- verifies that all 16 offsets remain inventoried and unresolved evidence gaps remain visible;
- verifies that Primary HAL stream opens still clear output pointers and return `-ENODEV`, and that the module remains `installable: false`.

A clean symbol/string scan is not proof that stripped or indirect numeric-offset callers do not exist. Static CI cannot prove runtime callback quiescence, target linker namespace behavior, or hardware routing. P2 must remain open until the hard gates above are closed with source/binary evidence and deterministic tests.

## Enablement policy

Do not implement guessed private callback prototypes. Do not enable Primary HAL stream opens, set the Soong module installable, replace the factory HIDL bridge, or free voice-unlock state without the corresponding evidence gates. The retained factory path remains authoritative until a reviewed replacement is proven safe.
