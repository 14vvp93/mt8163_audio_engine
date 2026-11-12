# MT8163 Audio Engine — project status

Updated: 2026-11-12

## Current milestone

**Phase 1 static ownership reconstruction is in progress.** The firmware analysis now covers the CD3313 JNI/kernel path, vehicle-state input, ACC mute call sites, FM direct/capture routes, A2DP and HFP/SCO separation, and the primary HAL's actual Normal processing boundary. The approved product target remains one unified engine replacing the factory vendor audio implementation.

Current working branch: `feature/profile-transition-benchmark`.
The live feature-branch HEAD is tracked in GitHub; this status document does not pin a stale revision.
`main` was not changed. PR #1 is closed and not merged.

## New architecture and audit documents

- [Phase 1 audit baseline](PHASE1_AUDIT_BASELINE.md) — consolidated firmware and repository findings.
- [Factory audio ownership matrix](FACTORY_AUDIO_OWNERSHIP_MATRIX.md) — control/data-plane owners, mute arbitration and replacement responsibilities.
- [Unified Audio Core architecture](UNIFIED_AUDIO_CORE_ARCHITECTURE.md) — one state authority, module contracts, source/route model, mute arbitration and migration gates.
- [Factory defect and risk register](FACTORY_DEFECTS.md) — static defects and unresolved compatibility risks.

## Latest continuation — factory identity and AudioCore coordinator

Current feature-branch HEAD: `ae76fd8b15eef6f6d2651e909a15cb589ee0ca35`.  
Factory replacement names/paths are now a hard build and packaging requirement. The current `main` baseline remains `d38c020763cb448fe0bceb00c6463cbb7d13c3a7`; it was not modified.

### Factory artifact map

The public extracted-firmware manifest was read directly and filtered against audio/framework/device-control components. The initial replacement map records exact factory paths, basenames, sizes and SHA-256 anchors for the primary HAL, framework audio libraries, car-services JNI, vendor audio HIDL service, ATCI tuning service, CD3313/carstatus/alternative DSP modules and policy/configuration files.

Confirmed primary replacement target:

- Factory path: `/vendor/lib/hw/audio.primary.mt8163.so`
- Factory basename: `audio.primary.mt8163.so`
- Baseline size: 1,671,852 bytes
- Baseline SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`

The AudioCore static library is an internal implementation library to be linked into this factory-named HAL. It must not be installed as a parallel primary HAL or project-named runtime library. Other factory control clients must be migrated or redirected so no second process continues to own CD3313 state. Kernel module replacement remains blocked on exact kernel source/config/toolchain and module ABI.

See [Factory artifact replacement map](FACTORY_ARTIFACT_REPLACEMENT_MAP.md).

### AudioCore coordinator

Implemented the first `AudioCore` coordinator in `audio-core/`. It composes SessionRegistry, MuteArbiter and AudioMode, exposes serialized control-plane mutations and publishes immutable snapshots to the PCM side through SnapshotPublisher. Snapshots contain generation, master/per-source mute masks, mode and session counts. If publication is blocked because all inactive slots are pinned, the control state remains committed and the dirty flag requires a later control-loop retry.

New host test target: `mt8163_audio_core_coordinator_tests`. It covers initial state, snapshot publication, generation changes, mute-owner isolation, session lifecycle and invalid mode/session inputs. No hardware I/O or route execution has been added.

### CI for coordinator and artifact-map changes

- [Test PCM DSP core — run 43259646080](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43259646080) — success at code commit `e4339c6`.
- [PCM DSP Stability Lab — run 43259646082](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43259646082) — success at `e4339c6`.
- [PCM DSP Performance Benchmark — run 43259646083](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43259646083) — success at `e4339c6`.
- [Android ARMv7 build and DSP benchmark — run 43259646049](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43259646049) — success at `e4339c6`, including Android NDK ARMv7 compilation and ELF architecture verification.
- [Repository integrity — run 43259740306](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43259740306) — success at documentation HEAD `ae76fd8`.

These results verify host tests, ARMv7 static-library compilation and repository integrity. They do not constitute a complete replacement HAL build or device validation.

## Confirmed static findings

### CD3313 transport and hardware control

- Kernel module: `vendor_lib/lib/modules/cd3313.ko`, ELF32 ARM with DWARF.
- Character device: `/dev/audiomux`.
- JNI provider: `lib/lib/libcarservices_jni.so`, ELF32 ARM, Android 28.
- Java/Binder path: `AudioEffect → ISourceService → SourceService → ExtAudioMuxer JNI`.
- `nativeInit()` opens `/dev/audiomux` read-only and issues `0x8080F000`; the driver returns the 8-byte `cd3313` identifier.
- `SendCmdAudioMux()` opens the same device write-only and issues ioctl.
- Native request formula: `0x40000000 | ((array_length + 1) << 16) | (command << 8)`.
- The native request encodes one more 32-bit word than the Java array declares. The extra word's value and intended origin are unknown.

Do not invent padding or reproduce this extra-word copy in a new hardware client. The host emulator intentionally builds safe frames from complete, validated payload words.

### Vehicle state and ACC mute

- `carstatus.ko` samples ACC from GPIO86/IRQ and exposes state through `/dev/carstatus`.
- CarStatusService consumes that state. On ACC OFF it calls `ExtAudioMuxer.setMasterMute(true, 2)` through `/dev/audiomux`, separately sends MCU text `mute=10001` through `/dev/carstatus`, and cancels pending delayed unmute.
- ACC ON clears CD3313 master reason 2 only after the 2,500 ms delayed transition and only if ACC is still ON; it separately sends MCU text `mute=10000`.
- The analyzed VDEX contains seven `setMasterMute()` call sites: ACC reason 2, SilenceMode reasons 1 and 24, and Bluetooth voice reason 3.
- CD3313 stores per-input and global master mute as independent bitmasks; reason arguments are bit indices, not reference counters.
- `carstatus.ko` probes `/dev/audiomux`; when available, CD3313 owns GPIO393, and carstatus drives it only as a fallback. This is not the same as the ACC-to-CD3313 master-mute command.

### FM, Bluetooth and source routing

- FM control through `/dev/fm`, direct `FM_I2S_Playback`, software `FM_I2S_Capture`, CD3313 RADIO source selection and Audio Policy FM Tuner In are distinct planes.
- The direct FM HAL path opens/starts an ALSA endpoint without a userspace PCM write loop. The capture provider is a separate PCM fanout path; no proof yet connects it to the direct speaker AudioPatch.
- A2DP is separate from HFP/SCO. Policy declares A2DP PCM16 stereo 44.1 kHz, while offload control also exists.
- HFP/SCO and Voice are separate HAL handlers, with 8/16 kHz profiles, microphone uplink and echo-reference components. They are not normal media streams.

### Primary HAL and PCM topology

- The primary HAL's direct `AudioMTKFilterManager::process()` call is only in `AudioALSAPlaybackHandlerNormal::write()`.
- The Normal pipeline performs DC removal, then speaker-dependent stereo-to-mono folding, then MTK post-processing, BLI SRC, bit conversion and ALSA write.
- The framework primary output and ordinary speaker ALSA route are stereo. The physical downstream distribution to FL/FR/RL/RR is not yet proven.
- CD3313 four-output attenuation controls are analog control-plane values, not proof of four independent PCM channels.
- AOSP Android 9 AudioFlinger protects both EffectModule process and command with the same mLock. The previously suspected effect-config race is not a confirmed issue under stock AOSP; the target vendor binary still needs comparison.

### DSP driver selection

- DTBO enables `cd3313@44`, `ak7604@1c` and `fy7604@50` nodes.
- Supplied init scripts attempt to load all three module variants.
- All three modules register the same misc-device name `audiomux`; runtime probe result and actual node owner remain unknown.
- Do not load alternative modules experimentally. Obtain read-only boot log, module state and I2C probe evidence.

## AudioCore foundation implementation

The new `audio-core/` library is now part of the `pcm-dsp` CMake build:

- Source/route/mode identifiers are separate from legacy CD3313 reason values.
- `MuteArbiter` tracks master and per-source mute owners independently, with idempotent owner set/clear operations.
- `MuteSnapshot` is a detached, read-only value.
- `SnapshotPublisher<T>` provides three-slot single-writer/multi-reader publication, a non-blocking writer gate, fixed reader retry bound and non-blocking backpressure. A failed reader acquisition must reuse its previous block snapshot.
- The actual MuteSnapshot publisher is instantiated by an Android compile probe; ARMv7 CI confirms 32-bit atomics are lock-free for the target ABI.
- `cd3313_compat` maps AudioCore owner masks to recovered CD3313 master/input reason bits. RadioScan and RadioPacketMute remain distinct internally while safely aggregating onto legacy radio reason bit 2.
- `SessionRegistry` provides 16 fixed-capacity session slots and a validated Requested/Active/Suspended/Stopping/Failed lifecycle.
- `AudioCore` now coordinates session lifecycle, mute ownership and audio mode, then publishes a generation-stamped fixed-size snapshot to the PCM plane, including each session handle, source, route and lifecycle state.
- `HardwareCommandBroker` is owned by AudioCore. It accepts semantic source selection and master/per-source mute commands, validates inputs, permits only one attached backend and propagates backend errors. It does not encode CD3313 ioctl numbers or wire framing.
- The broker has a host fake-backend test. No production backend is attached; no `/dev/audiomux` open, ioctl, I2C, GPIO or mixer operation is implemented or performed.
- The module remains control-plane only; it performs no HAL, Binder, ALSA PCM, ioctl, I2C or GPIO operations.

## Test and validation status

Latest CI for the AudioCore foundation:

- [Test PCM DSP core — run 43255493269](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255493269) — success; **14/14 CTest tests passed**, including all three AudioCore test executables.
- [PCM DSP Stability Lab — run 43255493328](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255493328) — success.
- [PCM DSP Performance Benchmark — run 43255493868](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255493868) — success.
- [Repository integrity — run 43255670188](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255670188) — success; includes the read-only collector syntax check.
- Local ThreadSanitizer: all 3 AudioCore test executables passed.
- Local AddressSanitizer + UndefinedBehaviorSanitizer: all 3 AudioCore test executables passed.

Android ARMv7 build and verification — [run 43255493234](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255493234) — success. The mute arbiter, CD3313 compatibility mapper, session registry and snapshot-publisher compile probe were verified as ELF32 ARM. The three-library Android artifact was uploaded: [mt8163-audio-engine-android-armv7](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255493234/artifacts/13960034320).

A read-only target inventory script is available at [collect_mt8163_audio_runtime.sh](../scripts/device/collect_mt8163_audio_runtime.sh). It records modules, ALSA devices, policy candidates, audio services and process FDs without opening the vendor device nodes or changing hardware state. Its Bash syntax check passed in [Repository integrity run 43255670188](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43255670188); it has not been run on the physical head unit.

Evidence classification:
- **HOST VERIFIED:** 14/14 CTest tests, including AudioCore mute/compatibility, session lifecycle and concurrent snapshot publication; stability lab and performance benchmark; local TSAN and ASAN/UBSAN.
- **ANDROID BUILD VERIFIED:** AudioCore compiles for Android ARMv7; the snapshot publisher's 32-bit lock-free requirement passes the target compile assertion; ELF32 ARM objects and artifact upload are confirmed.
- **DEVICE VERIFIED:** no complete replacement route, CD3313 write transaction, FM/A2DP/HFP integration or factory cutover has been validated on the physical head unit.

No live ioctl, I2C write, GPIO change, mixer write, audio-setting change or physical head-unit test has been performed in this phase.

## Open Phase 1 gates

1. Obtain read-only runtime evidence for loaded DSP modules and actual `/dev/audiomux` owner.
2. Obtain read-only process FD inventory for `/dev/audiomux`, `/dev/carstatus` and `/dev/fm`.
3. Confirm the selected Audio Policy XML and active FM AudioPatch on target.
4. Confirm HFP downlink/uplink, microphone and echo-reference lifecycle on target.
5. Confirm physical DSP population and FM/CD3313 board wiring.
6. Confirm exact kernel source/config/toolchain before any driver build.
7. Resolve the native extra-word ABI discrepancy without sending writes.
8. Compare vendor `libaudioflinger.so` process/command locking with the AOSP Android 9 EffectModule contract.

## Next implementation work

1. Add an evidence-based route model and session-to-route validation without conflating FM direct I2S with FM capture, A2DP with HFP/SCO, or analog CD3313 inputs with PCM streams.
2. Implement a fake-tested PCM backend contract and lifecycle adapter; do not guess ALSA device names or channel topology.
3. Recover and validate the exact CD3313 request ABI before writing a production hardware backend.
4. Build the factory-named `/vendor/lib/hw/audio.primary.mt8163.so` HAL shell against Android 9 headers and verify its required HAL symbols/SONAME before connecting the AudioCore.
5. Continue read-only target runtime discovery for actual DSP module ownership, process FDs, selected policy XML and active FM/HFP routes.

Preserve the existing PCM/DSP core. Do not integrate factory control writes or begin destructive cutover until ABI, runtime ownership and rollback gates are closed.