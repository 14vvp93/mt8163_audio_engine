# Project Roadmap

**Project:** MT8163 Audio Engine  
**Baseline:** 2026-11-13  
**Canonical repository:** `14vvp93/mt8163_audio_engine`  
**Working branch:** `feature`  
**Stable branch:** `main`  
**Last audited baseline HEAD:** `59af8ea0000c1db9e1010f6b51b0377f0c4fbd66`  
**Latest code/test commit:** `7b1dbc347913c16c52460d350dcfd87913f1f5e7`  
**Platform:** MediaTek MT8163 · Android 9 / API 28 · ARM32 / EABI5 · vendor Linux 4.9.117  
**Product status:** engineering foundations exist; complete factory HAL replacement and device validation are not complete.

This is the single authoritative execution plan. Update it when evidence, implementation or test status changes. Do not create parallel roadmaps or restart completed investigations without new evidence. The previous Beta 0.1 execution log is preserved at [docs/archive/PROJECT_ROADMAP_BETA_0.1.md](archive/PROJECT_ROADMAP_BETA_0.1.md) for historical reference only.

---

## 1. Product definition

Build one integrated **MT8163 Audio Engine** that replaces the factory vendor audio implementation while preserving the required Android 9 framework, HAL, kernel and application contracts.

Final primary HAL artifact:

`/vendor/lib/hw/audio.primary.mt8163.so`

The final artifact must retain the factory basename, path, ELF/SONAME, required symbols, dependencies and Android 9 HAL contract.

Internal modules may be separate source libraries, but they must not create competing runtime owners. AudioCore is the authoritative owner of audio state, sessions, routes, mute arbitration, volume/profile state and hardware-command scheduling.

### Non-negotiable rules

- One owner for each audio state, route, stream lifecycle and hardware control.
- Preserve the original firmware artifacts and hashes as immutable rollback references.
- Keep unsupported routes/formats and DSP failures bit-transparent.
- The real-time PCM path must not allocate, block on mutexes, perform Binder/IPC, open devices, log synchronously, issue ioctls or access I2C/GPIO.
- Do not infer hardware behavior from filenames, DTBO declarations, module load requests or host tests.
- Never send guessed I2C/ioctl/GPIO/mixer commands or load modules experimentally.
- Do not replace or disable factory components until their behavior has a tested replacement and rollback.
- Distinguish HOST VERIFIED, ANDROID BUILD VERIFIED, ABI VERIFIED and DEVICE VERIFIED.

## 2. Hardware and terminology baseline

Use these names consistently:

- **CD3313EO** — physical audio IC.
- **CD3313KO** — Linux kernel driver; firmware file is `cd3313.ko`.
- **`/dev/audiomux`** — device-node interface exposed by the firmware driver.

This terminology follows the user's latest explicit correction. Some historical documents contain inconsistent hardware naming. Reconcile them to the canonical terms above; do not silently mix physical-IC and driver names or reinterpret protocol findings.

The firmware dump was confirmed by the user to come from the same head unit. Firmware evidence includes the CD3313-named module, a `cd3313@44` DTBO node, alternative DSP module declarations and a shared `audiomux` registration name. Static firmware evidence does not, by itself, prove the selected overlay, successful runtime probe, active module owner or physical I2C transaction.

### Hardware safety state

- CD3313KO static driver analysis and host emulator work: substantially completed.
- Manufacturer datasheet cross-check for CD3313EO: recorded.
- Runtime `/dev/audiomux` owner: UNKNOWN.
- Runtime I2C binding/probe result: UNKNOWN.
- Physical board wiring, GPIO polarity and all speaker connections: UNKNOWN until supported by board/runtime evidence.
- Production hardware transport: DISABLED.
- No physical write, module load, GPIO toggle, mixer write or flashing is authorized by this roadmap.

## 3. Repository audit — baseline inventory

At baseline HEAD the recursive Git tree contains **209 files** and **12 GitHub Actions workflows**.

### Product code

| Area | Existing implementation | Actual boundary |
|---|---|---|
| `audio-core/` | SessionRegistry, route policy, AudioCore coordinator, mute arbitration, snapshot publisher, semantic hardware broker | Host-testable state/control foundation; no production HAL or hardware backend |
| `pcm-dsp/` | PCM16 stereo DSP, Float32 four-channel foundation, EQ, crossover, dynamics, enhancement, time alignment, profiles and transitions | DSP library; not connected to the target system audio path |
| `audio-core/pcm_stream_broker` | Fixed-capacity stream lifecycle, generation/admission gates, playback/capture API | Host-tested broker; no target ALSA endpoint mapping |
| `audio-core/endpoint_pcm_backend` | Route/direction endpoint adapter and injected device operations | Adapter contract; no concrete target TinyALSA operations |
| `cd3313-emulator/` | Protocol parser/builders, state/register model, boundary tests and fuzz target | Offline firmware-driver model; no device I/O |
| `pcm-dsp/android/` | Android effect library, JNI and Java bridge | Build/bring-up scaffold; not the factory primary HAL |
| `android-test/` | Probe and profile test application | Diagnostic/test bench, not the finished end-user app |
| `kernel/cd3313/` | Driver rewrite plan/documentation | No replacement kernel-driver source implementation yet |
| `tools/`, `scripts/device/` | Read-only diagnostics and analysis helpers | Collector syntax is checked; target collection has not been completed |

### Documentation groups reviewed

- Product goal, roadmap, status and architecture.
- Factory ownership, artifact replacement and defect registers.
- CD3313 protocol, ioctl, JNI, register image, emulator and vendor-module revalidation.
- Firmware inventory, boot image, kernel configuration, DTB/DTBO and driver ownership.
- Primary HAL, AudioFlinger, MTK filter lifecycle, PCM topology and output-device dispatch.
- FM, MT6627, A2DP, HFP/SCO, vehicle mute and carstatus flows.
- ACF/HCF, BES processing, ATCI service and HEQ application analysis.
- AudioCore concurrency, PCM endpoint backend, stream broker, DSP stability and performance.
- Repository migration, archive inventory, duplicate analysis, workflow audit and public compute workers.

Firmware archives and extracted firmware are maintained in `14vvp93/public_calculations`; the private project repository contains product code, tests and reviewed findings.

## 4. Current implementation status

### Implemented or substantially reconstructed

- Firmware extraction, provenance and SHA-256 inventory migration to the public firmware repository.
- Static reconstruction of the primary MTK HAL and the major framework/service control paths.
- CD3313KO firmware-driver protocol, command dispatch, JNI/ioctl framing and risk analysis.
- CD3313 host emulator, strict parser/builders, boundary tests, ASAN/UBSAN and libFuzzer workflow.
- AudioCore primitives: session lifecycle, route validation, mute-owner arbitration, snapshots and semantic hardware broker.
- PCM backend contracts, endpoint adapter and stream broker with fixed-capacity lifecycle and concurrency tests.
- PCM DSP foundations: stereo core, four-channel Float32 test foundation, EQ, crossover, dynamics, enhancement, time alignment and profile transitions.
- Android ARMv7 static-library/JNI build workflow.
- Android 9 effect-library ABI scaffold and host smoke tests.
- Read-only device diagnostics collectors.
- Static FM direct-I2S/capture, A2DP and HFP/SCO route investigations.

### Incomplete or not proven

- No complete replacement `audio.primary.mt8163.so` implementation or exact factory-ABI build.
- No concrete TinyALSA/ALSA endpoint backend connected to target card/device values.
- No live target inventory of modules, device-node ownership, ALSA cards/PCMs, mixer controls, active policy or process file descriptors.
- No production CD3313KO hardware transport backend.
- No confirmed physical I2C/GPIO wiring or hardware readback.
- No complete route graph or end-to-end integration of Music, navigation, notification, FM, A2DP, HFP/SCO, microphone and AUX/DAB.
- No validated stereo-preserving production DSP insertion point.
- No target AudioFlinger effect registration/attachment validation; effect registration is intentionally not done.
- No complete migration of framework, car-services, policy and factory control clients to a single owner.
- No full device audio, acoustic, latency, suspend/resume or rollback validation.
- No flashable release artifact.

## 5. Prioritized execution plan

A phase may proceed in parallel only when it does not depend on an unresolved earlier safety gate. Do not skip exit criteria.

### P0 — Freeze evidence and prove the target runtime topology

**Priority: blocking**

1. Reconcile the physical-chip terminology across legacy documents using CD3313EO for the IC and CD3313KO for the driver.
2. Pin the exact firmware manifest, binary hashes, kernel build identity, board DTB/DTBO and selected overlay evidence.
3. The read-only PowerShell collector was expanded with PCM substream status, `/dev` node metadata, open-FD owners and I2C driver symlinks; removed the ADB serial from the saved manifest. **Pending:** run it on the target and preserve its unmodified output.
4. Capture loaded modules, module probe logs, `/dev/audiomux` major/minor and owner, relevant process FDs, I2C device/driver bindings and kernel messages.
5. Capture `/proc/asound/cards`, `/proc/asound/pcm`, PCM status, mixer control inventory and relevant sysfs/pinctrl metadata.
6. Determine the live Audio Policy XML, AudioPatch state, FM route state, Bluetooth offload state and HFP/SCO lifecycle.
7. Separate facts from hypotheses in the ownership and route matrices.

**Exit gate:** actual runtime owners and endpoint inventory are documented with read-only evidence. No speculative hardware operation is used to fill gaps.

### P1 — Close AudioCore and PCM backend lifecycle contracts

**Priority: high; host-testable work can continue while P0 evidence is collected**

1. Audit AudioCore's single-writer contract and enforce it at the future HAL control-executor boundary. **Static review complete:** current classes are deliberately single-control-thread and do not self-serialize; HAL must provide the single-writer executor.
2. Finish deterministic lifecycle tests for open/configure/start/write/read/stop/close, stale handles, generation reuse, slot exhaustion, XRUN, device loss and repeated recovery. **Implemented and TSAN-verified:** direction metadata access in PCM callbacks was moved behind transfer admission; a repeated same-handle close/reopen race test was added. [PCM broker TSAN run 43375546258](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546258) passed.
3. Review transfer-gate shutdown, callback quiescence and object lifetime under TSAN/ASAN/UBSAN. **PASS:** the new race regression passed under [PCM broker TSAN run 43375546258](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546258).
4. Verify AudioCore snapshot dirty-state recovery when all three slots are pinned. **PASS:** regression test committed in `7b1dbc3`, verified by [AudioCore TSAN run 43375546004](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546004).
5. Implement the concrete TinyALSA/ALSA operation table only after P0 provides verified endpoint mappings.
6. Validate period size, buffer size, channels, sample format, rate, interleaving and blocking behavior against the real endpoint.
7. Preserve the existing passing DSP and AudioCore tests.

**Exit gate:** lifecycle, concurrency and error recovery are sanitizer-clean; backend endpoints are based on measured target evidence, not guessed indexes.

### P2 — Recover and build the factory-compatible Android 9 primary HAL

- [x] Add CI workflow to fetch the immutable factory HAL artifact from `public_calculations` and verify its expected size/SHA-256 before audit.
- [x] Verify factory artifact identity, ELF32 ARM EABI5, exported `HMI`, SONAME and dynamic dependencies in GitHub Actions.
- [x] Recover `HMI` object address/size, module API 0.1, HAL API 1.0, module ID/name/author and `methods->open` pointer.
- [x] Disassemble `open()` far enough to confirm `hw_device_t.version = 0x0300` (API 3.0) and a 224-byte vendor-extended device allocation.
- [ ] Recover the complete 224-byte device layout and map all method callbacks, including the vendor-specific tail.

**Priority: critical product milestone**

1. Complete the factory binary audit: ELF32 ARM identity, SONAME, exported symbols, symbol versions, dependencies, relocations and linker namespace. **Tool improved:** `tools/audit_primary_hal_binary.sh` now checks ET_DYN, ARM32 EABI5, factory basename and exported `HMI`; it still needs to be run against the immutable firmware binary.
2. Recover the exact Android 9 `audio_hw_device`, output/input stream, device-open, callback, structure-layout and error contracts.
3. **AOSP baseline pinned:** the firmware fingerprint reports `PPR1.180610.011`, matching Android 9 tag `android-9.0.0_r3`; ARM32 AOSP structure layouts and offsets are documented in [ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md). Exact ALPS/MediaTek vendor headers remain unpinned.
4. Create the replacement HAL skeleton with the factory module entry point and required symbols.
5. Link AudioCore and the PCM/DSP libraries internally; do not ship a parallel primary HAL or always-on stateful daemon.
6. Add ELF, symbol, dependency and packaging checks against the factory baseline.
7. Preserve factory originals and a verified rollback manifest.

**Exit gate:** a factory-named `/vendor/lib/hw/audio.primary.mt8163.so` builds for the exact Android 9 ARM32 ABI and passes symbol/dependency/contract checks. This is ABI/build verification, not device verification.

### P3 — Select and implement the correct stereo DSP insertion point

**Priority: critical audio-quality milestone**

1. Preserve the recovered fact that the Normal speaker path performs stereo-to-mono folding before the MTK filter callback.
2. Do not use that post-fold callback as a stereo-preserving insertion point.
3. Compare the upstream HAL insertion candidate with the Android 9 per-stream AudioFlinger effect candidate.
4. Verify target vendor AudioFlinger locking, effect-chain classification, session lifecycle, output-device changes, offload transitions and effect registration schema.
5. Choose one supported production insertion point and document why it covers the intended media streams.
6. Initially enable only verified PCM16 stereo media formats/rates.
7. Keep unsupported formats, calls, SCO, capture and unverified routes bit-transparent bypasses.
8. Measure CPU budget, callback duration, scheduling jitter, latency, clipping/headroom, DC behavior and profile-transition artifacts on the target.

**Exit gate:** independent L/R processing is proven at the selected boundary, and target runtime behavior is validated without regressions.

### P4 — Integrate source and route adapters one at a time

**Priority: after P0–P3 gates**

| Route | Existing evidence | Required implementation/validation |
|---|---|---|
| Local Music / Navigation / Notification | Primary Normal playback and Android policy statically mapped | First production route; PCM ownership, stream/session lifecycle, DSP and bypass |
| Bluetooth A2DP | PCM16 stereo 44.1 kHz policy; offload path exists | Runtime offload detection, 44.1 kHz route, exact bypass when offloaded/unsupported |
| FM direct I2S | HAL opens a dedicated FM endpoint; no ordinary userspace PCM write loop | Preserve or deliberately redesign the direct route; do not pretend FM capture is the same path |
| FM capture PCM | Dedicated AudioRecord capture handler/provider identified; 32 kHz adapter scaffold exists | Confirm actual consumer and audible route before enabling DSP |
| HFP/SCO / Voice | Separate call handlers, downlink/uplink, microphone and echo-reference components | Dedicated telephony adapter, clock/gain/mute transitions and call regression tests |
| AUX / AUX2 / DAB | Factory policy and external-input control references recovered | Confirm physical input route and whether audio is analog or PCM before choosing adapter |
| HDMI / USB / other | Policy/handler declarations exist | Keep bypassed until exact active route and PCM contract are proven |

**Exit gate:** each enabled route has a documented owner, endpoint, format, lifecycle, error behavior, tests and target-device evidence.

### P5 — Complete hardware controls and migrate factory ownership

**Priority: after P0 runtime ownership and safe ABI gates**

1. Implement a typed hardware capability model for CD3313EO controlled through the verified CD3313KO path.
2. Keep source selection, master volume, output attenuation, mute, tone and other capabilities distinct; do not equate analog attenuators with independent PCM channels.
3. Replace the legacy JNI over-read with a fully initialized, explicitly bounded bridge. Never reproduce the out-of-bounds read.
4. Separate requested, queued, transmitted, acknowledged and physically read-back states. Use UNKNOWN when no physical readback exists.
5. Serialize all hardware commands on the control plane; never issue them from PCM callbacks or UI code.
6. Migrate framework, car-services JNI, policy extensions, ATCI and other factory clients so they cannot compete with AudioCore.
7. Keep vehicle MCU commands and audio-chip mute/control as separate control planes.
8. Retain factory services and drivers until each responsibility has a replacement and rollback test.

**Exit gate:** one authoritative owner controls every migrated state and hardware command; failures are observable and no factory client can issue duplicate writes.

### P6 — End-to-end validation and release

**Priority: final**

1. Build and package the complete factory-compatible HAL and all required replacement components.
2. Run host tests, sanitizers, Android ABI checks and repository-integrity checks.
3. Validate every supported route on the actual MT8163 head unit.
4. Test simultaneous media/navigation/notification, source changes, mute reasons, ACC transitions, suspend/resume, reboot, XRUN, device loss and recovery.
5. Measure latency, CPU usage, glitch rate, clipping/headroom, channel separation and long-run stability.
6. Verify factory-component cutover without double ownership.
7. Test rollback to the untouched factory image.
8. Publish a release manifest with exact hashes, ABI evidence, test reports and known limitations.

**Release gate:** complete factory-named HAL, supported route parity, hardware safety, target validation and rollback are all demonstrated. No host-only result can satisfy a device gate.

## 6. Route and subsystem ownership rules

- AudioCore owns logical sessions, source/route decisions, mute arbitration, volume/profile state and command scheduling.
- The HAL owns Android stream/device callbacks and binds verified platform endpoints.
- PCM backend owns TinyALSA/ALSA stream handles and lifecycle; it does not decide product routing.
- DSP processes only buffers delivered by a verified route adapter.
- CD3313KO transport is a control-plane backend; it never runs in the PCM callback.
- Framework and car-services clients become thin compatibility proxies after cutover; they must not keep shadow hardware state.
- Vehicle MCU state is not the same thing as audio-chip state.
- FM direct I2S, FM capture PCM, A2DP and HFP/SCO remain separate route types.

## 7. CI and verification policy

### Existing workflows

- PCM DSP host tests.
- CD3313 emulator host tests, ASAN/UBSAN and bounded libFuzzer.
- AudioCore ThreadSanitizer.
- PCM stream broker ThreadSanitizer workflow (feature branch (legacy workflow triggers may still reference deleted branches)).
- Android ARMv7 library/JNI build and benchmark.
- Android 9 effect-library ABI build and host smoke tests.
- Stability lab and performance benchmark.
- Android test APK build.
- Repository integrity and public firmware analysis entry points.

### CI gaps to close

1. **DONE (2026-11-13):** Repository-integrity push trigger now includes `integration/beta-0.1`.
2. **DONE (2026-11-13):** PCM stream-broker TSAN now runs on `integration/beta-0.1` as well as its work branch.
3. Ensure the Android ARMv7 and full host-test workflows run on every relevant integration change.
4. Add the factory HAL ABI/symbol/dependency audit as a required CI gate.
5. Add packaging checks that reject project-named parallel runtime HALs and unexpected extra owners.
6. Report workflow results against the exact commit being evaluated.

### Current verified baseline

- AudioCore TSAN: [43373877343](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43373877343) is running on the current documentation/feature commit; last completed success on this series: [43373791297](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43373791297).
- PCM stream-broker TSAN: [43373877328](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43373877328), success on commit `090f4d1`.
- Repository integrity: [43373877371](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43373877371), success on commit `090f4d1`; validates local Markdown links and read-only collector syntax.
- CD3313 protocol/safety: [43337144746](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43337144746), success on code commit `cfc20a4`; it is not a run on the later documentation-only commits.
- Earlier PCM DSP and Android ARMv7 runs are recorded in validation documents. Re-run them when relevant code changes; do not infer current code status from old run IDs.
- No complete HAL ABI build or device validation is claimed.

## 8. Immediate execution queue

1. Run the read-only target diagnostic collector and record runtime ownership/topology; this remains the only blocker requiring target-side evidence.
2. Finish PCM lifecycle race/recovery tests and close sanitizer findings.
3. Complete AudioCore single-writer and snapshot publication review.
4. Finish exact factory primary HAL ABI recovery and build a minimal factory-compatible HAL shell.
5. Select the stereo-preserving DSP insertion point and integrate the first verified media route.
6. Add source adapters incrementally; keep all unverified routes bypassed.
7. Integrate CD3313EO hardware controls only after runtime ownership and safe transport gates are closed.
8. Migrate factory control owners and validate rollback.
9. Complete end-to-end route, quality and stability validation.

## 9. Definition of done

The MT8163 Audio Engine is complete only when:

- The factory primary HAL is replaced at the exact factory path and ABI.
- One coherent AudioCore owns audio state and control.
- Every enabled route has verified PCM/control ownership and documented lifecycle.
- DSP preserves stereo/channel topology and has safe bypass/error behavior.
- Hardware commands use a bounded, validated ABI with truthful completion state.
- Factory clients cannot compete with the new owner.
- All known defects are fixed or explicitly tracked with evidence.
- Host, Android build, ABI and physical-device validation are reported separately.
- Rollback to the original factory behavior is tested.

---

## 10. Change log

### 2026-11-13 — initial consolidated roadmap

- Replaced the Beta 0.1-only roadmap with this single project roadmap.
- Consolidated the repository inventory, firmware/driver analysis, AudioCore, PCM/DSP, route, Android integration and CI evidence.
- Reordered work around runtime ownership, PCM lifecycle, factory HAL ABI and a proven stereo insertion point.
- Archived the old Beta 0.1 roadmap, refreshed the status/index/README, and marked legacy hardware-identity records as superseded where necessary.
- Enabled repository-integrity and PCM stream-broker TSAN workflows on the feature branch.
- Fixed a PCM broker race by moving non-atomic direction metadata reads behind the transfer-admission gate; added a repeated handle-reuse stress test and passed PCM broker TSAN.
- Added an AudioCore dirty-snapshot recovery test covering all three slots pinned, mutation commit, release and retry; passed AudioCore TSAN.
- Pinned the AOSP Android 9 r3 ARM32 HAL header baseline to the firmware's `PPR1.180610.011` build fingerprint; documented structure layouts and offsets while keeping vendor ABI facts explicitly unresolved.
- Hardened the primary HAL ELF audit to check the required `HMI` dynamic export, ET_DYN, ARM32 EABI5 and factory basename; added CI shell-syntax validation.
- Expanded the read-only PowerShell collector with runtime PCM status, device-node metadata, open-FD ownership and I2C driver bindings; stopped storing the ADB serial in its manifest.
- Preserved CD3313KO static analysis and emulator work as completed firmware research; production hardware transport remains gated.
