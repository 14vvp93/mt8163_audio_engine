# Android 9 primary HAL contract checklist

Date: 2026-11-12
Target: MT8163 / Android 9 / ARM32
Factory replacement identity: /vendor/lib/hw/audio.primary.mt8163.so
Status: integration checklist; not an implemented HAL

## 1. Product boundary

The product is one replacement primary HAL under the factory basename and path. AudioCore, PCM transport, DSP, routing and hardware-control adapters are internal components linked into that HAL. Do not ship a second project-named primary HAL, an always-on parallel daemon, or an INSERT effect as the final architecture.

The factory artifact map records additional framework/vendor control clients. Those remain migration dependencies: the new HAL must become the single owner of audio state and device commands, while existing framework and Java/Binder contracts are preserved or redirected.

## 2. Android 9 HAL surfaces to recover

Before implementing entry points, capture exact interfaces from the supplied Android 9 vendor image and matching platform headers:

- audio_hw_device methods and versioned structure size;
- audio_stream_out and audio_stream_in method tables;
- audio_hw_device_open/close and module symbol exports;
- stream configuration negotiation and supported channel masks, formats and rates;
- output/input device open and close ordering;
- standby, pause/resume, flush, drain and presentation-position semantics;
- routing and device connection notifications;
- audio mode, mic mute, master mute and per-stream volume contracts;
- audio patches, FM direct-I2S and capture-route behavior;
- Bluetooth A2DP and HFP/SCO ownership and call transitions;
- effect attachment callbacks and vendor extension interfaces;
- HIDL 4.0 service and HAL client dependencies, if retained by this image.

Do not infer structure layout from generic AOSP alone. Compare vendor extensions and the exact Android 9 branch against extracted binaries and matching headers.

## 3. HAL lifecycle and ownership gates

The replacement HAL must define one explicit owner for device-wide mode and mute state; input/output stream lifecycle; PCM endpoint open/configure/start/stop/close; source-to-route arbitration; CD3313 and vehicle-state commands; Bluetooth call transitions and SCO; and FM control versus FM PCM/I2S transport.

No second component may independently mutate the same hardware state after cutover. Framework components may retain policy responsibilities but must not issue duplicate low-level hardware commands.

## 4. Realtime callback requirements

The output write path must be bounded and allocation-free after stream start. It must not acquire a blocking mutex, perform ALSA card/PCM discovery, open or close device nodes, issue CD3313 ioctl/I2C transactions, log synchronously, wait for a control thread, or allocate/free memory.

Control changes are handed to the audio thread through preallocated immutable state. Unsupported formats and routes must preserve the original buffer and fail open or bypass according to the verified Android contract.

## 5. Artifact and ABI requirements

Final shared object: /vendor/lib/hw/audio.primary.mt8163.so

Required evidence before packaging:
- ELF32, ARM EABI and correct machine flags;
- exact factory SONAME and exported HAL module symbol;
- complete symbol and dependency audit;
- Android 9 linker namespace compatibility;
- no unresolved imports or accidental host-library dependency;
- matching file path, mode and ownership;
- policy/effect XML consistency if those files are changed;
- factory rollback image and hashes.

A successful ARMv7 static-library build is not a HAL build and does not satisfy these gates.

## 6. Implementation order

1. Freeze exact Android 9 HAL ABI from target firmware and matching source headers.
2. Implement a minimal factory-named module shell with no hardware writes.
3. Add device/stream lifecycle and fake PCM backend tests.
4. Integrate verified PCM endpoints and negotiation.
5. Port each factory route individually, preserving HFP, A2DP, FM and capture behavior.
6. Redirect framework/vendor control paths into the one AudioCore owner.
7. Package only exact factory artifact identities and run ABI/symbol checks.
8. Perform route-by-route device validation with rollback available.

## 7. Explicit blockers

- Matching Android 9 vendor HAL headers/source revision not yet pinned in this workstream.
- Exact target ALSA runtime card and PCM indices are not established.
- Physical CD3313 wiring and live /dev/audiomux owner are not confirmed.
- Full factory HAL ABI replacement has not been implemented.
- No head-unit runtime test is claimed.