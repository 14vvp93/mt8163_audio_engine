# Project Roadmap

**Project:** MT8163 Audio Engine  
**Baseline:** 2026-11-13  
**Canonical repository:** `14vvp93/mt8163_audio_engine`  
**Working branch:** `feature`  
**Stable branch:** `main`  
**Latest verified AudioCore/PCM code baseline:** `346dc854204f4f1a907c580eb2b2e6c4bf42ad63` (versioned stereo profile API; 22/22 host tests and Android ARMv7 cross-build PASS)  
**Active development branch:** `feature` (use the live branch pointer for exact HEAD)  
**Platform:** MediaTek MT8163 · Android 9 / API 28 · ARM32 / EABI5 · vendor Linux 4.9.117  
**Product status:** engineering foundations exist; selected profile-driven DSP integration and device validation are not complete.

This is the single authoritative execution plan. Update it when evidence, implementation or test status changes. Binding anti-drift and red-warning requirements are defined in [Project Governance](PROJECT_GOVERNANCE.md); they apply even when a later conversational request conflicts with the approved course. Physical-access limitations may reorder hardware-dependent work but do not change the product destination. Do not create parallel roadmaps or restart completed investigations without new evidence. The previous Beta 0.1 execution log is preserved at [docs/archive/PROJECT_ROADMAP_BETA_0.1.md](archive/PROJECT_ROADMAP_BETA_0.1.md) for historical reference only.

Latest CI execution diagnostics: [docs/CI_EXECUTION_DIAGNOSTICS_2026-11-13.md](CI_EXECUTION_DIAGNOSTICS_2026-11-13.md). Historical runner-allocation failures are superseded by successful PCM host-test and ARMv7 runs on code HEAD `fb5a00e0a1d38aed71571cd3b2db41773d90e509`.

---

## 1. Product definition

**Owner-approved architecture decisions (2026-11-13):** deep replacement of selected factory audio processing/control layers; profile-driven DSP applied only to verified and explicitly supported routes; retain compatible factory components and immutable rollback artifacts without allowing competing runtime ownership. The detailed decision record is [Architecture Decisions — Deep Replacement, Profile-Based DSP and Factory Retention](ARCHITECTURE_DECISIONS_DEEP_REPLACEMENT_PROFILE_DSP_FACTORY_RETENTION.md).

Build one integrated **MT8163 Audio Engine** that deeply replaces selected factory audio-processing and control layers while retaining compatible factory HAL, policy, services and kernel integrations wherever they remain correct and verified.

Final primary HAL artifact:

`/vendor/lib/hw/audio.primary.mt8163.so`

Any artifact that intentionally replaces a factory component must retain its required factory basename, path, ELF/SONAME, symbols, dependencies and Android 9 ABI contract. A wholesale primary-HAL replacement is not the default target.

Internal modules may be separate source libraries, but they must not create competing runtime owners. AudioCore is the authoritative owner of audio state, sessions, routes, mute arbitration, volume/profile state and hardware-command scheduling.

### Non-negotiable rules

- One owner for each audio state, route, stream lifecycle and hardware control.
- Preserve the original firmware artifacts and hashes as immutable rollback references.
- Keep unsupported routes/formats and DSP failures bit-transparent during migration; temporary bypass is never a permanent source-class exclusion.
- Retain factory components and original artifacts; do not delete or overwrite them. After cutover, only one runtime owner may mutate route, mute, gain, DSP or CD3313 state.
- The real-time PCM path must not allocate, block on mutexes, perform Binder/IPC, open devices, log synchronously, issue ioctls or access I2C/GPIO.
- Do not infer hardware behavior from filenames, DTBO declarations, module load requests or host tests.
- Never send guessed I2C/ioctl/GPIO/mixer commands or load modules experimentally.
- Do not replace or disable factory components until their behavior has a tested replacement and rollback.
- Distinguish HOST VERIFIED, ANDROID BUILD VERIFIED, ABI VERIFIED and DEVICE VERIFIED.

## 2. Hardware and terminology baseline

Use these names consistently:

- **CD3313EO** — physical audio IC; use the manufacturer datasheet values as hard design constraints for the analog-control layer.
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

At the current `feature` HEAD `fb5a00e0a1d38aed71571cd3b2db41773d90e509`, the recursive Git tree contains **238 files** and **13 GitHub Actions workflows**.

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
- Non-installable Android 9 / ARM32 Primary HAL bootstrap with exported `HMI`, factory wrapper layout assertions and a fail-closed module-open entry point.
- NDK r27c cross-build of the bootstrap verified as ELF32 ARM with factory SONAME and a 128-byte `GLOBAL DEFAULT` `HMI` symbol; see [CI run 43410778686](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43410778686). This verifies the factory ABI reference only; the factory HAL remains the retained runtime component.
- Read-only device diagnostics collectors.
- Static FM direct-I2S/capture, A2DP and HFP/SCO route investigations.

### Incomplete or not proven

- No complete replacement `audio.primary.mt8163.so` implementation or exact factory-ABI build; the current bootstrap deliberately returns `-ENOSYS` and is not installable.
- No callable factory `audio_hw_device`/stream callback implementation or validated private-tail callback implementation.
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
2. Finish deterministic lifecycle tests for open/configure/start/write/read/stop/close, stale handles, generation reuse, slot exhaustion, backend-token exhaustion, XRUN, device loss and repeated recovery. **Implemented and TSAN-verified:** direction metadata access in PCM callbacks was moved behind transfer admission; a repeated same-handle close/reopen race test was added. [PCM broker TSAN run 43375546258](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546258) passed.
3. Review transfer-gate shutdown, callback quiescence and object lifetime under TSAN/ASAN/UBSAN. **PASS:** the new race regression passed under [PCM broker TSAN run 43375546258](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546258).
4. Verify AudioCore snapshot dirty-state recovery when all three slots are pinned. **PASS:** regression test committed in `7b1dbc3`, verified by [AudioCore TSAN run 43375546004](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546004).
5. Migrate session identity to generation-bearing `SessionToken` through SessionRegistry, AudioCore/snapshots, PcmStreamBroker, PrimaryHalStreamAdapter and PcmByteStreamAdapter. **IMPLEMENTED.** Same-handle reuse, stale operations, foreign registry tokens and generation exhaustion have regression coverage. Contract: [AudioCore Session Token Contract](AUDIOCORE_SESSION_TOKEN_CONTRACT.md).
6. Complete repository-wide raw-handle mutation audit and pass the [AudioCore P1 Session Identity Gate](../.github/workflows/audiocore-p1-session-token.yml): full host suite, full AudioCore/DSP TSAN and ASan/UBSan suites, Android ARMv7/API 28 build, ELF checks and repository integrity. **PASS at exact HEAD `ea48695c21e8ff5900c58c6bb62602d6a46bab90` (workflow run `43521731773`; all six required jobs and final completion job succeeded).**
7. Implement the concrete TinyALSA/ALSA operation table only after P0 provides verified endpoint mappings.
8. Validate period size, buffer size, channels, sample format, rate, interleaving and blocking behavior against the real endpoint.
9. Preserve the existing DSP and AudioCore regression suites.

**Exit gate:** lifecycle, concurrency and error recovery are sanitizer-clean; backend endpoints are based on measured target evidence, not guessed indexes. Session identity sub-gate is complete at `ea48695c21e8ff5900c58c6bb62602d6a46bab90`; target endpoint mapping remains separately gated by P0 evidence.

### P2 — Preserve the factory HAL and build a narrow Android 9 integration adapter

- [x] Add CI workflow to fetch the immutable factory HAL artifact from `public_calculations` and verify its expected size/SHA-256 before audit.
- [x] Verify factory artifact identity, ELF32 ARM EABI5, exported `HMI`, SONAME and dynamic dependencies in GitHub Actions.
- [x] Recover `HMI` object address/size, module API 0.1, HAL API 1.0, module ID/name/author and `methods->open` pointer.
- [x] Recover factory SONAME, Android API 28 ELF note, symbol-version requirements and exact 32-entry `DT_NEEDED` list.
- [x] Disassemble `open()` far enough to confirm `hw_device_t.version = 0x0300` (API 3.0) and a 224-byte vendor-extended device allocation.
- [x] Recover the complete 224-byte device layout and map standard callbacks plus the 68-byte vendor tail through the `AudioALSAHardware` vtable.
- [x] Add an APS2 packed-relocation decoder so vtable mappings are reproducible from the factory ELF.
- [x] Recover concrete `AudioALSAStreamOut` and `AudioALSAStreamIn` C++ vtables from APS2 relocations.
- [x] Recover Android C `audio_stream_out` / `audio_stream_in` wrapper sizes, private legacy-object offsets and callback table population.
- [x] Recover the AudioALSAStreamManager open/set/close entry points and forwarding chain for input/output streams.
- [x] Recover close ownership: manager closes legacy C++ stream; HAL adapter frees only the C wrapper.
- [x] Add a layout-only ARM32 ABI mirror with compile-time structure size and offset assertions; host CMake/CTest passes.
- [x] Add a host-only reference model and CTest coverage for the recovered output sample-rate allowlist, input PCM16 fallback and exact input channel-mask predicate.
- [x] Resolve input `set_parameters` / `get_parameters` callback addresses; both valid adapters dispatch to the expected `AudioALSAStreamIn` methods.
- [x] Identify the MediaTek HIDL 4.1 bridge and map the seven legacy callback entries at device offsets 188–216 to their HIDL methods.
- [x] Map all 16 private device-tail slots to internal targets, including xWay adapters forwarding to `Play2Way`/`Record2Way` and reference-ring forwarding to `AudioVUnlockDL`; correct offset 172 to `xWayRec_Start` and offset 176 to `xWayRec_Stop`.
- [x] Recover all 16 public device-tail adapter thunks and prove the `audio_hw_device*` first-argument adapter loads the internal `AudioALSAHardware*` from offset 220.
- [x] Recover the voice-unlock detached worker, condition-variable ring path and stop-before-free requirement.
- [ ] Identify external consumers of the private tail, confirm source-level callback return types and validate callback-in-flight draining. The current closure state is tracked in [P2 private HAL callback evidence matrix](P2_PRIVATE_HAL_CALLBACK_EVIDENCE_MATRIX.md), enforced by the scheduled/manual [P2 GitHub Actions workflow](../.github/workflows/primary-hal-p2-private-callbacks.yml). The full shared-library/service scan found no separate named xWay client, but indirect offset callers remain possible. The voice-unlock singleton is freed separately from `stopInput()`; a replacement must enforce stop/drain-before-free.

**Priority: critical product milestone**

1. Reconcile the recovered `DT_NEEDED` list with the actual Android 9 linker namespace and verify the library search paths; the namespace config file is not present in the current extracted tree.
2. Obtain/pin the matching ALPS/MediaTek vendor headers or source revision and compare private declarations with AOSP r3.
3. Recover stream negotiation from the factory methods. **Partially complete:** output formats/rates/channel predicates, input PCM16 normalization, input channel predicate, route-dependent input rate branches, handler-open dispatch, output flag forwarding/default behavior, 8,192-byte output buffer getter, input FAST flag and 5/20/60 ms input buffer sizing are recorded in [the ABI baseline](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md). The input format-width table is recovered as `[2, 1, 4, 4, 0]` for enum values 1–5. **Remaining:** other flag bits, the input size callback's third-argument semantics and format-5 zero-width behavior against the framework caller, full error branches, every channel-mask combination and vendor-header reconciliation. The host reference model now tests the 8,192-byte output-duration formula, 5/20/60 ms input-duration selection, and 20 ms input-size arithmetic with checked 64-bit intermediates; the third argument remains raw until its caller semantics are recovered.
4. Trace external callers and runtime semantics for all 16 private device-tail callbacks. **Internal targets recovered:** the eight entries at offsets 156–184 forward to `Play2Way`, `Record2Way` and `AudioVUnlockDL`; offsets 172/176 are `xWayRec_Start`/`xWayRec_Stop`. The eight entries at offsets 188–216 are mapped through the MediaTek HIDL 4.1 bridge to voice-unlock and parameter-callback methods. The service init registration path and ATCI parameter-client entry are confirmed. **Remaining:** external consumers of the xWay tail, indirect callers and callback lifetime/concurrency semantics. The Primary HAL bootstrap is now protected by `scripts/repository/check_primary_hal_fail_closed.py` in the ARM32 ABI workflow: stream-open callbacks must clear their output pointer and return `-ENODEV`, and the Soong module must remain non-installable until dispatch lifetime and endpoint gates close.
5. Keep the factory primary HAL installed and active. Implement only the narrowest verified integration adapter (for example, a per-stream effect or a reviewed HAL-side insertion) required by the selected profile.
6. Link the stereo DSP core into that adapter; do not produce a replacement primary HAL or an always-on stateful daemon as the default architecture.
7. Verify the adapter's exact Android 9 / ARM32 ABI, linker visibility, lifecycle and packaging; preserve all factory originals and rollback hashes.

**Exit gate:** the selected adapter builds for Android 9 ARM32 and passes its ABI/contract checks while the untouched factory primary HAL remains the active system HAL. This is build verification, not device verification.

### P3 — Select and implement the correct stereo DSP insertion point

**Priority: critical audio-quality milestone**

1. Preserve the recovered fact that the Normal speaker path performs stereo-to-mono folding before the MTK filter callback.
2. Do not use that post-fold callback as a stereo-preserving insertion point.
3. Compare the upstream HAL insertion candidate with the Android 9 per-stream AudioFlinger effect candidate.
4. Verify target vendor AudioFlinger locking, effect-chain classification, session lifecycle, output-device changes, offload transitions and effect registration schema.
5. Choose one supported production insertion point and document why it covers the intended media streams.
6. Initially enable only verified PCM16 stereo media formats/rates.
7. DSP is profile-driven, not universally applied. Enable only validated profiles on explicitly supported and DSP-visible routes. Preserve verified factory handling for routes without an approved profile; expand coverage incrementally as route contracts are recovered.
8. Measure CPU budget, callback duration, scheduling jitter, latency, clipping/headroom, DC behavior and profile-transition artifacts on the target.

Host-side PCM16 pre-fold adapter hardening: input and output pointer alignment is validated before sample access; misaligned buffers return `InvalidBuffer`. This is host-verified on code HEAD `dd11947d6cbf9aadd4402e5667e045dfa6879176` (22/22 tests). It does not mean the adapter is connected to the factory HAL.

**Exit gate:** independent L/R processing is proven at the selected boundary, and target runtime behavior is validated without regressions.

### P4 — Integrate source and route adapters one at a time

**Priority: after P0–P3 gates**

| Route | Existing evidence | Required implementation/validation |
|---|---|---|
| Local Music / Navigation / Notification | Primary Normal playback and Android policy statically mapped | First production route; PCM ownership, stream/session lifecycle, DSP and bypass |
| Bluetooth A2DP | PCM16 stereo 44.1 kHz policy; offload path exists | Keep factory offload/PCM handling unless a verified DSP profile and DSP-visible PCM insertion are available; do not claim DSP coverage while offloaded |
| FM direct I2S | HAL opens a dedicated FM endpoint; no ordinary userspace PCM write loop | Preserve or deliberately redesign the direct route; do not pretend FM capture is the same path |
| FM capture PCM | Dedicated AudioRecord capture handler/provider identified; 32 kHz adapter scaffold exists | Confirm actual consumer and audible route before enabling DSP |
| HFP/SCO / Voice | Separate call handlers, downlink/uplink, microphone and echo-reference components | DSP-visible downlink/uplink and capture paths; SCO transport, clock/format/latency, echo-reference synchronization, AEC, noise suppression, AGC, sidetone, gain/mute arbitration, call lifecycle and regression tests |
| AUX / AUX2 / DAB | Factory policy and external-input control references recovered | Determine whether each input is analog-only or has a verified ADC/PCM capture route. CD3313EO analog input selection alone cannot feed software DSP; do not claim coverage without digitization evidence |
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

1. Build and package the selected DSP adapter, validated profiles and only the narrowly required integration artifacts; keep the factory HAL and untouched vendor components intact.
2. Run host tests, sanitizers, Android ABI checks and repository-integrity checks.
3. Validate every supported route on the actual MT8163 head unit.
4. Test simultaneous media/navigation/notification, source changes, mute reasons, ACC transitions, suspend/resume, reboot, XRUN, device loss and recovery.
5. Measure latency, CPU usage, glitch rate, clipping/headroom, channel separation and long-run stability.
6. Verify factory-component cutover without double ownership.
7. Test rollback to the untouched factory image.
8. Publish a release manifest with exact hashes, ABI evidence, test reports and known limitations.

**Release gate:** selected DSP profiles work on their validated routes, untouched factory paths retain their behavior, hardware ownership is unambiguous, target validation and rollback are demonstrated. No host-only result can satisfy a device gate.

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
- PCM stream broker ThreadSanitizer workflow (runs on `main` and `feature`).
- Android ARMv7 library/JNI build and benchmark.
- Android 9 effect-library ABI build and host smoke tests.
- Stability lab and performance benchmark.
- Android test APK build.
- Repository integrity and public firmware analysis entry points.

### CI gaps to close

1. **DONE:** all active branch filters use only `main` and `feature`.
2. **DONE:** factory Primary HAL ELF/HMI/APS2 audit runs against the SHA-256-pinned firmware artifact.
3. **DONE:** ARM32 ABI layout mirror is compiled and tested by CMake/CTest.
4. Ensure the Android ARMv7 and full host-test workflows run on every relevant code change.
5. Add packaging checks that reject project-named parallel runtime HALs and unexpected extra owners.
6. Report workflow results against the exact commit being evaluated.

### Current verified baseline

- Repository Integrity: [43382385597](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43382385597), PASS on feature commit `1148555f1753`.
- AudioCore TSAN: [43382385578](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43382385578), PASS on feature commit `1148555f1753`.
- PCM Stream Broker TSAN: [43382385583](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43382385583), PASS on feature commit `1148555f1753`.
- Documentation-only commits after `1148555` have separate validation runs; no source-code behavior changed in those commits.
- Factory ELF/HMI/APS2 audit: [43381061217](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43381061217), PASS.
- Android 9 ARM32 ABI layout CMake/CTest: [43381498046](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43381498046), PASS.
- Earlier PCM DSP and Android ARMv7 runs are recorded in validation documents. Re-run them when relevant code changes; do not infer current code status from old run IDs.
- No complete HAL implementation, factory ABI build or device validation is claimed.

## 8. Immediate execution queue

1. Finish remaining output/input flag-bit semantics, full error branches and capture buffer-size caller contract; recovered predicates now have executable host tests.
2. Resolve Android 9 vendor linker namespace configuration and pin the matching ALPS/MediaTek headers.
3. Trace external callers and semantics of private device-tail callbacks.
4. Run the read-only target diagnostic collector and record runtime ownership/topology.
5. Implement the narrow selected-profile integration adapter and compile it for ARM32 without hardware writes; do not replace the retained factory primary HAL.
6. Keep the current C11/C++ ABI layout and AudioCore/PCM TSAN gates green on feature.
7. [x] Add a DSP-visibility policy separate from source/transport route validity; verify system sounds, A2DP offload, HFP/SCO bridge, microphone echo-reference, FM direct/capture and analog-only input classifications. Next: integrate the first verified media route at the stereo-preserving boundary.
8. Add every source adapter incrementally, including A2DP with DSP-visible PCM, FM direct/capture, analog-input digitization where hardware supports it, complete HFP/SCO, microphone/uplink and echo-reference paths. No source class is permanently excluded from final DSP scope.
9. Integrate CD3313EO hardware controls only after runtime ownership and safe transport gates are closed.

## 9. Definition of done

The MT8163 Audio Engine is complete only when:

- The factory primary HAL is replaced at the exact factory path and ABI.
- One coherent AudioCore owns audio state and control.
- Every required route—including A2DP, HFP/SCO, voice-call uplink/downlink, microphone capture, FM, AUX/DAB and system/navigation overlays—has verified PCM/control ownership and documented lifecycle.
- One DSP engine covers every required source and direction through separate playback/capture/telephony/echo-reference graphs, preserving channel topology and safe bypass/error behavior.
- Every analog input claimed as DSP-covered has a verified ADC/PCM path; analog CD3313EO selection alone is not software DSP coverage.
- All factory components and original artifacts are retained; factory clients cannot compete with the replacement runtime owner.
- Hardware commands use a bounded, validated ABI with truthful completion state.
- Factory clients cannot compete with the new owner.
- All known defects are fixed or explicitly tracked with evidence.
- Host, Android build, ABI and physical-device validation are reported separately.
- Rollback to the original factory behavior is tested.

---

## 10. Change log

### 2026-11-13 — superseded architecture proposal

- The earlier full-replacement/all-source-DSP proposal was superseded by the owner-approved DEEP / PROFILES / RETAIN decision recorded in [the current architecture ADR](ARCHITECTURE_DECISIONS_DEEP_REPLACEMENT_PROFILE_DSP_FACTORY_RETENTION.md).
- Historical route investigations remain useful evidence, but they do not establish a requirement to replace every factory path or apply DSP to every source.
- Accepted retention of factory components and immutable rollback artifacts; explicitly prohibited competing runtime state/hardware owners.
- Added `SystemSound` as an AudioCore source and introduced a DSP route-visibility classifier separate from transport-route validity. Analog CD3313 inputs are not treated as software-DSP-visible without a verified ADC/PCM path.
- Verified the code-bearing commit `f50f25a`: host tests 22/22, Android ARMv7 build and PCM/HAL adapter TSAN passed. This is host/build evidence only, not device validation.

### 2026-11-13 — factory route handler recovery

- Confirmed output and input stream `open()` paths create route-specific handlers through `AudioALSAStreamManager`.
- Identified playback handler classes for Normal, Voice, HFP Client, BTSCO, BTCVSD, FM Transmitter and HDMI.
- Identified capture handler classes for Normal, Voice, FM Radio, BT and AEC.
- Recorded that exact stream-attribute selection predicates and ALSA endpoint mappings still require method-level analysis and target runtime evidence.

### 2026-11-13 — factory Primary HAL ABI recovery

- Audited the exact factory `audio.primary.mt8163.so`: HMI/module/device versions, 224-byte device wrapper, standard callbacks and 68-byte vendor tail.
- Decoded APS2 relocations to recover AudioALSAHardware, AudioALSAStreamOut and AudioALSAStreamIn vtables.
- Recovered the 124-byte output and 100-byte input C stream wrappers, callback population, private legacy pointers and manager-owned close lifecycle.
- Recorded factory SONAME, 32 dependencies, symbol versions and dependency presence in the extracted firmware tree; linker namespace configuration remains unresolved.
- Added reproducible ELF/APS2 audit tooling and an ARM32 layout mirror with passing CMake/CTest.
- Consolidated current status and removed stale integration-branch and pre-audit statements.

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


## 11. Latest execution update — 2026-11-13

### CI scheduling

- Removed workflow-level `concurrency` cancellation blocks from all 13 GitHub Actions workflows. Pushes no longer cancel older runs via `cancel-in-progress: true`; successive commits may run concurrently or wait for available runners.
- Repository Integrity passed on policy commit [20ff746](https://github.com/14vvp93/mt8163_audio_engine/commit/20ff7460c15f861461d224e422d345a983707b8c), Actions run [43474595132](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43474595132).
- This policy was exercised by two consecutive code commits: runs for both commits `5161fa5` and `fb5a00e` executed and completed instead of canceling earlier runs.

### PCM lifecycle correction

- `PrimaryHalStreamAdapter::stop()` previously returned immediately after backend cleanup when the broker stream was `Failed`. For a stream that had been active before an XRUN/device-loss error, this left the AudioCore session marked `Active` even after backend stop succeeded.
- The adapter now snapshots whether the AudioCore session is `Active`, performs broker stop, and transitions only that active session to `Suspended`. A failed-start session remains `Requested` during cleanup.
- Added a regression assertion for the `Active` → `Suspended` transition around terminal XRUN cleanup.
- PCM DSP host tests passed on [commit 5161fa5](https://github.com/14vvp93/mt8163_audio_engine/commit/5161fa519c65d84c73bc26446e4748d63a0c8802) (run [43474868715](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43474868715)) and final code HEAD [fb5a00e](https://github.com/14vvp93/mt8163_audio_engine/commit/fb5a00e0a1d38aed71571cd3b2db41773d90e509) (run [43474875981](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43474875981)).
- Android ARMv7 cross-build and ELF verification passed on both commits: [run 43474868710](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43474868710) and [run 43474876012](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43474876012).
- TSAN workflows are manual-dispatch workflows and were not triggered by these code commits. No hardware tests or hardware writes were performed.

### 2026-11-14 — AudioCore session identity audit

- Added regression coverage proving that a closed session's old handle cannot mutate or close a different session after its registry slot is reused with a different handle. Commit: `b794a08e66032df4ce3da44a0c533e7692014e99`.
- The generation-bearing `SessionToken { handle, generation }` migration was completed through SessionRegistry, AudioCore/snapshots, PcmStreamBroker, PrimaryHalStreamAdapter and PcmByteStreamAdapter.
- Same-value handle reuse, stale operations, foreign registry tokens, sequence exhaustion, backend-token exhaustion and transfer/close lifecycle regressions are covered by host tests.
- The P1 gate passed all six required jobs on exact HEAD `ea48695c21e8ff5900c58c6bb62602d6a46bab90`: API contract, full host suite, TSAN, ASan/UBSan, Android ARMv7/API 28 and repository integrity. Workflow run: [43521731773](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43521731773).
- The factory Android C ABI remains unchanged. HAL stream-object ownership of internal tokens is a future integration responsibility; it is not part of this completed internal identity migration.
- P1 Session Identity offline exit gate: **PASS**. Device verification and concrete target ALSA endpoint mapping remain separate gates and are not implied by this result.
