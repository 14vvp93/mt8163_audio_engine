> **HISTORICAL AUDIT BASELINE. Findings remain evidence inputs, but the execution order is superseded by [Project Roadmap](PROJECT_ROADMAP.md). Hardware terminology follows the user's latest correction: CD3313EO is the physical IC; CD3313KO is the Linux driver (`cd3313.ko`).**

---

# MT8163 Audio Engine — Phase 1 audit baseline

Date: 2026-11-12  
Repository baseline: `14vvp93/mt8163_audio_engine`  
Audited development ref: `feature/profile-transition-benchmark`  
Target: MT8163, Android 9 / API 28, ARM32

## 1. Audit status and evidence boundary

This document consolidates the current repository and firmware findings into the Phase 1 baseline for the approved full factory-audio replacement. It is an engineering map, not a claim that replacement code or target-device validation is complete.

Evidence labels used here:
- **Static verified** — recovered from firmware files, ELF/DWARF, symbols, relocations, disassembly, XML, init scripts or device-tree data.
- **Host verified** — exercised by host-side tests or emulators.
- **Android build verified** — compiled for the Android/ARM target or packaged as an APK.
- **Device verified** — observed on the actual target head unit. No full replacement behavior currently has this status.

The current project tree and firmware manifests are separated by repository:
- Private product repository: `14vvp93/mt8163_audio_engine`.
- Public firmware/compute repository: `14vvp93/public_calculations`.
- Public extracted firmware manifest: `firmware/manifests/mt8163-extracted.json` (1,386 files).
- Public raw-dump provenance manifest: `firmware/manifests/mt8163-source-import.json` (22 files).
- HEQ APK chunks and full/chunk hashes: `firmware/manifests/heq-apk.json`.

The canonical extracted set is 578,175,653 bytes; raw dumps total 44,050,240 bytes. The original split ZIP volumes were removed from current branch trees only after size/SHA-256 copy verification. Git history was not rewritten, so historical blobs remain reachable.

A fresh hash grouping of the 1,386-entry extracted manifest finds 10 exact-content duplicate groups covering 69 paths. These include paired AudioInputSource APK/ODEX/VDEX locations, two 42-file and 10-file groups of tiny framework JAR placeholders (200 and 183 bytes respectively), System/Vendor copies of `libpixelflinger.so` and policy XML, and the two AudioParamOptions locations. These are **not approved for deletion**: identical bytes do not establish interchangeable runtime paths, and framework placeholder artifacts must be interpreted with their matching OAT/VDEX and boot-classpath configuration. Preserve every canonical path unless a path-level dependency audit proves it redundant.

## 2. Product boundary

MT8163 Audio Engine is one integrated replacement for the factory vendor audio implementation. Its internal modules may be separate libraries, services, HAL components and kernel code, but there must be one authoritative owner for each stream, route, state variable and hardware control.

The replacement boundary includes the vendor-specific behavior recovered in:
- primary Audio HAL and vendor audio-policy extensions;
- factory audio services and JNI/Binder bridges;
- vendor-specific AudioFlinger and AudioPolicyManager hooks that directly control vehicle audio devices;
- source selection, routing, volume/mute and DSP/effect control;
- CD3313 hardware control and any board-confirmed alternative DSP backend;
- FM, A2DP, HFP, navigation, notifications and other required sources.

Preserve Android 9 framework contracts and kernel/ALSA facilities where possible. Do not rewrite AudioFlinger or public Android APIs wholesale without a demonstrated compatibility requirement. Do not make a permanent INSERT effect the product architecture; it may remain a diagnostic or temporary integration experiment only.

## 3. Recovered factory architecture

### 3.1 Android/framework and service control plane

Static firmware analysis found direct references to both `/dev/audiomux` and `/dev/carstatus` in:
- `system/lib/libcarservices_jni.so`;
- `system/lib/libaudiopolicymanagerdefault.so`;
- `system/lib/libaudioflinger.so`.

The JNI library contains the `ExtAudioMuxer` transport and `sendCmdCarStatus` path. The recovered Java/Binder sequence includes `AudioEffect → ISourceService → SourceService → ExtAudioMuxer JNI`. The native CD3313 initializer opens `/dev/audiomux` read-only and issues the chip-identification ioctl; command writes use a separate write-only open.

**Implication:** replacing only the primary HAL or only the car-services JNI bridge would leave vendor framework-level control paths active. The exact call sites and ownership responsibilities in AudioFlinger and AudioPolicyManager must be mapped before redirection or retirement.

### 3.2 Primary HAL / PCM path

The Android 9 primary HAL is an ARM32 Android 28 binary. Its exported symbols and disassembly show:
- stream/device lifecycle and volume control;
- MTK filter manager and `AudioMTKFilter::process` path;
- ACF/HCF parameter and preview paths;
- capture and microphone gain/speech routing;
- normal playback processing call site in `AudioALSAPlaybackHandlerNormal::write()`.

The current evidence describes a byte-oriented processing wrapper with source format/channel metadata derived from stream attributes. The exact buffer layout, active filter configuration, per-route channels, and end-to-end data path are not yet fully proven.

The Android policy describes primary PCM16 stereo at 44.1/48 kHz, while the existing software DSP core is a four-channel Float32 FL/FR/RL/RR engine. The current HAL/effect scaffold does not prove that four independent channels exist at the candidate processing boundary. Do not duplicate stereo into four channels or fold four-channel output back to stereo as an unverified adapter.

### 3.3 DSP and hardware control are separate paths

The CD3313 interface is a control ABI exposed through `/dev/audiomux`; it is not itself proof of a PCM audio transport. Keep register/control transactions out of the real-time PCM callback. The new Audio Core should serialize hardware commands and own their state, while PCM/DSP processing remains bounded and allocation-free on the audio thread.

### 3.4 CD3313 native ABI and safety findings

Static analysis recovered the JNI-to-ioctl path and identified an unresolved extra-word size discrepancy in the vendor request framing. Do not guess the extra word or reproduce the driver's over-read.

The static driver audit identified high-confidence risks:
- ioctl payload-size/copy bounds mismatch around a 256-byte slab object and 4-byte header;
- commands `0x56` and `0xA3` read a payload word without a size check;
- command `0x15` reads four speaker indices despite weaker minimum-length validation and lacks full index bounds checks;
- raw write parsing may use an unterminated 12-byte stack buffer;
- repeated raw writes on one descriptor have a file-position issue;
- raw I2C write status is ignored while cache state is updated;
- ioctl success means queued, not necessarily executed;
- status reads expose cached state rather than confirmed hardware readback.

The host emulator intentionally validates complete payloads and avoids reproducing unsafe kernel behavior. Do not send malformed, oversized or speculative requests to a physical head unit.

### 3.5 DSP driver selection and device tree

Static DTBO/init analysis shows enabled I2C2 nodes for:
- `cd3313@44`;
- `ak7604@1c`;
- `fy7604@50`.

The supplied init scripts attempt to load `carstatus.ko`, `cd3313.ko`, `ak7604.ko`, and `fy7604.ko`. All three DSP modules register a misc device named `audiomux`; this creates a registration collision if more than one probe succeeds. AK7604/FY7604 also request overlapping GPIO resources.

This does **not** prove that all three chips are physically populated or that all three drivers bind. The runtime owner of `/dev/audiomux` is unresolved until boot logs or read-only device diagnostics confirm probe outcomes. Do not load all variants experimentally.

### 3.6 FM

The factory FM architecture has distinct tuner-control and audio-transport paths:
- tuner control uses the FM JNI/driver interfaces;
- policy exposes an FM tuner input/capture route;
- the primary HAL also has a dedicated `FM_I2S_Playback` path controlled through an AudioPatch;
- a separate `FM_I2S_Capture` provider can expose PCM to capture clients.

The capture route is not proven to feed the direct speaker AudioPatch. A software FM PCM adapter alone will not replace the direct FM route until the actual buffer boundary or a deliberate route redesign is established.

A deeper paired-firmware trace also recovered three distinct FM planes:
1. `/dev/fm` carries tuner/chip metadata and I2S configuration ioctls.
2. ALSA `FM_I2S_Playback` / `FM_I2S_Capture` carries the HAL's non-merge direct I2S route for the paired MT6627 class-1 configuration.
3. `/dev/audiomux` and the CD3313 logical `RADIO` channel provide a separate source-selection/mute control path. RadioService issues CD3313 FM mute operations through inputPath 0.

The static evidence confirms that these software paths coexist; it does not prove whether the board feeds the tuner to both paths, whether one is dormant, or which path is audible on the target. The paired FM driver also reports a 32 kHz configured rate while the HAL rate-set ioctl callback appears unpopulated and its error is masked. Runtime ALSA enumeration, tuner state, physical wiring and actual route selection remain device-side questions.

### 3.7 Bluetooth and calls

A2DP is a media route and must be mapped independently from SCO/HFP. The supplied A2DP policy declares PCM16 stereo at 44.1 kHz, but the binary contains offload-control behavior; runtime offload must therefore be detected and handled explicitly.

The primary HAL contains HFP downlink/uplink, echo-reference and PCM-merge components, an HFP socket path, and call-specific lifecycle handling. HFP controls and NVRAM parameters are also present. A full replacement must account for downlink, microphone uplink, echo reference, SCO controls, gain and NVRAM compatibility. HFP cannot be treated as an optional music-only bypass.

### 3.8 Vehicle state and mute

`carstatus.ko` maintains vehicle mute state and probes `/dev/audiomux` to decide whether CD3313 owns a GPIO mute path or a fallback is needed. The precise ACC-to-CD3313 mute-reason path remains unresolved. Preserve the distinction between car-status state, framework mute requests and chip-level mute reasons; consolidate them under one Audio Core owner only after mapping their semantics.

## 4. Existing project assets

The current repository contains:
- PCM/DSP core: PCM engine, quad engine, profile transitions, crossover, multiband dynamics, enhancement and time alignment;
- CD3313 protocol emulator and host tests;
- Android effect/JNI bring-up scaffold and diagnostic APK;
- Android ARMv7 build workflow;
- firmware-analysis reports and reusable public worker entry points;
- kernel/CD3313 design and ABI documentation.

These assets are useful foundations, not evidence of a finished unified engine. The JNI bridge and effect scaffold are not a production real-time HAL callback or complete replacement integration.

## 5. Validation snapshot

At the time of this audit:
- private feature branch `feature/profile-transition-benchmark` is at `aecc5f5f15a68f60d5e1639b9d7d43e46f6c8c8d`;
- private `main` is at `d38c020763cb448fe0bceb00c6463cbb7d13c3a7`;
- PR #1 is open and not merged;
- the latest feature-branch host DSP, stability, performance, repository-integrity, Android ARMv7 and test-APK workflows reported success;
- public firmware-worker validation and public compute/infrastructure checks reported success.

Classification:
- **HOST VERIFIED:** existing PCM/DSP tests, stability lab, performance benchmark and CD3313 emulator tests as covered by their workflows.
- **ANDROID BUILD VERIFIED:** Android ARMv7 build and diagnostic/test APK packaging as covered by CI.
- **DEVICE VERIFIED:** no complete Audio Engine route, CD3313 hardware transaction, FM/A2DP/HFP integration or factory replacement is currently claimed.

## 6. Phase 1 gaps to close

1. Reconcile the complete firmware inventory against the manifests and retain source provenance for every audio-relevant binary/configuration.
2. Map all direct and indirect `/dev/audiomux` and `/dev/carstatus` clients, including framework call sites and their state ownership.
3. Recover the actual active HAL/policy configuration and exact normal playback buffer contract.
4. Establish the physical DSP-chip/module selection from read-only runtime evidence; resolve the shared misc-device collision.
5. Complete FM direct/capture route ownership and determine where PCM can be intercepted or routed.
6. Map the complete A2DP offload and HFP call/microphone/echo paths.
7. Recover carstatus mute reasons, GPIO ownership and ACC/reverse/telephone transitions.
8. Confirm exact kernel source/config/build toolchain before any driver replacement build.
9. Maintain a factory-defect register with evidence, impact, root cause, fix and regression test.

## 7. Next implementation gate

Do not begin full HAL replacement or remove factory components yet. First close the source-of-truth/ownership map and the real PCM topology. In parallel, preserve and extend host-tested DSP/emulator foundations. Once the contracts are proven, implement the unified Audio Core and hardware command broker, then integrate the Android 9 HAL/policy adapter and progressively retire factory ownership with a tested rollback path.

## References

- `PROJECT_STATUS.md`
- `MT8163_AUDIO_ENGINE_GOAL.md`
- `MT8163_AUDIO_HAL_ANALYSIS.md`
- `ANDROID_AUDIO_INTEGRATION_GATE.md`
- `MT8163_AUDIO_SOURCE_COVERAGE.md`
- `CD3313_DRIVER_RISK_AUDIT.md`
- `CD3313_IOCTL_ABI.md`
- `analysis-reports/DSP_DRIVER_OWNERSHIP_ANALYSIS.md`
- `repository-audit/FIRMWARE_ANALYSIS_SUMMARY.md`
- `repository-audit/REPOSITORY_CLEANUP_REPORT.md`
