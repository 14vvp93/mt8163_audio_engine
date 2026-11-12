# MT8163 Audio Engine — project status

Updated: 2026-11-12

## Current milestone

**Phase 1 static ownership reconstruction is in progress.** The firmware analysis now covers the CD3313 JNI/kernel path, vehicle-state input, ACC mute call sites, FM direct/capture routes, A2DP and HFP/SCO separation, and the primary HAL's actual Normal processing boundary. The approved product target remains one unified engine replacing the factory vendor audio implementation.

Current working branch: `feature/profile-transition-benchmark`.
Current branch HEAD at last check: `542a2ee44d74ed76744aa51e672ed2384f6b5af8`.
`main` was not changed. PR #1 is closed and not merged.

## New architecture and audit documents

- [Phase 1 audit baseline](PHASE1_AUDIT_BASELINE.md) — consolidated firmware and repository findings.
- [Factory audio ownership matrix](FACTORY_AUDIO_OWNERSHIP_MATRIX.md) — control/data-plane owners, mute arbitration and replacement responsibilities.
- [Unified Audio Core architecture](UNIFIED_AUDIO_CORE_ARCHITECTURE.md) — one state authority, module contracts, source/route model, mute arbitration and migration gates.
- [Factory defect and risk register](FACTORY_DEFECTS.md) — static defects and unresolved compatibility risks.

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

## Test and validation status

Latest previously successful feature-branch code CI was run against `aecc5f5f15a68f60d5e1639b9d7d43e46f6c8c8d` on 2026-11-12:

- PCM DSP core tests — success.
- PCM DSP Stability Lab — success.
- PCM DSP Performance Benchmark — success.
- Repository integrity — success.
- Android ARMv7 build and DSP benchmark — success.
- Android test APK build — success.

These runs predate the documentation-only commits on the current branch HEAD. No new CI run for the latest documentation commits was observed at the last check.

Evidence classification:
- **HOST VERIFIED:** existing PCM/DSP tests, stability lab, performance benchmark and CD3313 emulator tests covered by CI.
- **ANDROID BUILD VERIFIED:** Android ARMv7 build and diagnostic/test APK packaging covered by CI.
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

In parallel with the remaining read-only gates, begin host-testable AudioCore foundations from `UNIFIED_AUDIO_CORE_ARCHITECTURE.md`: typed source/session state, route model, owner-based mute arbitration and hardware-broker interfaces. Preserve the existing PCM/DSP core. Do not integrate factory control writes or begin destructive cutover until the ABI and rollback gates are closed.