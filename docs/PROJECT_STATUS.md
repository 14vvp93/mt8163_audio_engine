# MT8163 Audio Engine — current status

Updated: 2026-11-13  
Active development branch: `feature`  
Stable branch: `main`

## Owner-approved architecture decisions (2026-11-13)

- **Replacement depth: DEEP** — replace selected DSP, processing and control layers; do not replace the complete factory vendor audio stack by default.
- **DSP: PROFILES** — apply validated, route-specific profiles only to explicitly supported and DSP-visible paths. Unintegrated routes remain on their verified factory path.
- **Factory components: RETAIN** — preserve factory binaries/services/drivers and keep compatible components active where appropriate. A route or hardware-control domain must have one runtime owner.
- Authoritative record: [Deep Replacement, Profile-Based DSP and Factory Retention](ARCHITECTURE_DECISIONS_DEEP_REPLACEMENT_PROFILE_DSP_FACTORY_RETENTION.md).


> **Latest verified code-bearing commit:** `5f5cf4547bd51cb6c7987e5e1616d8191e175e12`. It adds a versioned PCM16 stereo playback-profile contract, strict profile validation and fail-open integration through the NormalPlayback adapter. Host CMake/CTest passed **22/22 tests** on descendant `66b1782c554ba22f7a2f3029e1e199ee293cf5a1` ([run 43510826842](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43510826842)); Android ARMv7 static-library cross-build/ELF verification passed on the same descendant ([run 43510826727](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43510826727)). Repository Integrity passed after the architecture and documentation updates ([run 43510833457](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43510833457)). These results do not claim an Android HAL connection or physical-device validation.

> **Latest regression coverage (2026-11-13):** commit [2f0d4f7](https://github.com/14vvp93/mt8163_audio_engine/commit/2f0d4f77f873471b5eb0149c13fed320d08e35c5) adds endpoint-backend tests for impossible transfer counts on both playback and capture. The backend must return `BackendFailure`, reject subsequent transfers, and require explicit stop/close cleanup. These tests are added but are **not yet CI-verified**; the last verified code baseline remains `fb5a00e0a1d38aed71571cd3b2db41773d90e509`.

> **Execution authority:** [Project Roadmap](PROJECT_ROADMAP.md). This file is the current evidence-based snapshot. Do not infer completion from historical plans or old CI logs.

## Branch model

- Exactly two branches exist: `main` and `feature`.
- All development changes are made on `feature`.
- The current `main` tree is byte-for-byte tree-identical to its pre-cleanup commit `d38c020763cb448fe0bceb00c6463cbb7d13c3a7`.
- A helper file was accidentally created and then removed on `main`; two bookkeeping commits remain in its history, but the final tree is unchanged. The helper now exists only on `feature`.
- Repository Integrity, AudioCore TSAN and PCM Broker TSAN workflows target `main` and `feature`; obsolete branch filters were removed.

## Factory Primary HAL — recovered static ABI

Factory artifact:
- Path: `firmware/extracted/vendor/lib/hw/audio.primary.mt8163.so` in `14vvp93/public_calculations`
- Size: 1,671,852 bytes
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- ELF32 ARM EABI5, Android API 28
- Build ID: `f66eaf9e6ac950259f0c253be618d15a`

Recovered module/device contract:
- Exported `HMI`: `GLOBAL DEFAULT`, 128-byte object at `0x00195014`.
- `module_api_version=0x0001`, `hal_api_version=0x0100`.
- Module ID `audio`, name `MTK Audio HW HAL`, author `MTK`.
- `hw_module_methods_t` at `0x00195010`; `open` at `0x0009b70c`.
- Device API version `0x0300`; factory device wrapper size 224 bytes.
- Standard `audio_hw_device` callbacks, 68-byte vendor tail and the 57-entry `AudioALSAHardware` C++ vtable recovered from 2,431 Android APS2 relocations.
- Factory SONAME `audio.primary.mt8163.so`; all 32 `DT_NEEDED` entries, `BIND_NOW` flags and libc/libdl/libm symbol-version requirements recorded.
- All 32 dependency basenames exist somewhere in the extracted firmware tree. Actual linker namespace visibility is not proven because no `ld.config*.txt` or equivalent namespace configuration was present in the current extraction.

Recovered stream contract:
- `AudioALSAStreamOut` and `AudioALSAStreamIn` C++ vtables decoded.
- Output C wrapper: 124 bytes = 120-byte Android `audio_stream_out` + `legacy_out` at offset 120.
- Input C wrapper: 100 bytes = 96-byte Android `audio_stream_in` + `legacy_in` at offset 96.
- Output/input callback tables and null/unsupported slots recorded.
- Input `set_parameters` and `get_parameters` adapters verified to dispatch to the corresponding C++ vtable methods.
- Open/set/close forwarding paths through `AudioALSAHardware` and `AudioALSAStreamManager` recovered.
- Factory route-specific handler classes recovered: playback Normal/Voice/HFP Client/BTSCO/BTCVSD/FM Transmitter/HDMI; capture Normal/Voice/FM Radio/BT/AEC.
- Normal playback open resolves PCM/card indices through `AudioALSADeviceParser` using `/proc/asound/pcm`, opens/prepares TinyALSA PCM and initializes its DSP stages; Normal write order is confirmed as DC removal → conditional mono fold → MTK post-processing → BLI SRC → bit conversion → pending-data handling → PCM write → voice-unlock ring feed.
- Normal capture uses a shared `AudioALSACaptureDataProviderNormal` singleton with per-stream `AudioALSACaptureDataClient` fan-out; provider owns TinyALSA PCM and read thread.
- Close path delegates legacy C++ object teardown to the manager and frees only the C wrapper.
- Exact format/rate/channel negotiation and all route/error semantics still require method-level analysis.

ABI tooling:
- `tools/audit_primary_hal_binary.sh` verifies factory ELF identity, HMI export, SONAME, dependencies, versions and relocations.
- `tools/audit_android_packed_relocations.py` decodes APS2 relocations and recovers C++ vtables.
- `audio-hal-abi/include/mt8163_android9_abi_layout32.h` models ARM32 structure layouts and asserts sizes/offsets. It is explicitly layout-only; its 32-bit words are not callable native pointers.
- Standalone CMake/CTest ABI layout workflow passed.
- Detailed evidence: [Android 9 ARM32 Primary HAL ABI baseline](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md).

## Project status

**Engineering foundations and factory ABI recovery are progressing. Factory stream negotiation has been partially reconstructed from disassembly. A complete replacement Primary HAL has not been implemented, built or validated on the head unit.**

Final replacement target:

`/vendor/lib/hw/audio.primary.mt8163.so`

### P0 — target runtime inventory

- [x] Prepare read-only PowerShell collector for PCM endpoints/substream status, device-node metadata, open-FD owners and I2C driver symlinks.
- [x] Avoid persisting the ADB serial in the manifest.
- [ ] Run collector on the physical MT8163 unit.
- [ ] Confirm live ALSA card/PCM indices, active policy, module ownership and `/dev/audiomux` owner.

### P1 — AudioCore and PCM

- [x] AudioCore coordinator, session registry, route policy, mute arbitration and snapshot publication.
- [x] Explicit DSP route visibility classification, separate from transport-route validity: A2DP offload control, HFP/SCO PCM bridge, microphone echo-reference requirement, FM direct-I2S digital tap, FM capture PCM, and analog-only CD3313 inputs are distinguished.
- [x] Add `SystemSound` as a first-class AudioCore source using the primary PCM route.
- [x] PCM backend contract, endpoint adapter and stream broker.
- [x] Fix direction metadata race by reading it only after transfer admission.
- [x] Add repeated same-handle close/reopen stress test.
- [x] Add snapshot backpressure regression with all publication slots pinned.
- [x] Add a per-stream PCM16 HAL byte/frame adapter with immutable negotiated configuration.
- [x] Validate buffer alignment, frame divisibility, direction, unsupported formats, zero lengths and the 2048-frame transfer bound.
- [x] Add a lock-free callback admission gate so stop/close cannot race admitted byte-stream transfers.
- [x] Extend backend transfers to report actual frames transferred, preserving partial success and partial progress accompanied by an error.
- [x] Keep status-only legacy backend implementations source-compatible while rejecting hidden short transfers in status-only callers.
- [x] Validate endpoint callback availability by direction, so playback-only endpoint tables do not need capture callbacks and vice versa.
- [x] Add broker regression coverage proving an impossible backend transfer count fails closed, rejects subsequent callbacks, and still permits explicit stop/close recovery.
- [x] Add regression coverage for partial transfers and playback-only/capture-only endpoint operation.
- [x] Test stop/retry while a byte-stream write is in flight, plus reconfiguration after successful stop.
- [x] Include the byte adapter in both CMake and the Android Soong static-library source list.
- [x] Prior PCM Broker and AudioCore TSAN runs passed.
- [x] PCM DSP host tests and Android ARMv7 cross-build passed on code HEAD `fb5a00e0a1d38aed71571cd3b2db41773d90e509` (Actions runs `43474875981` and `43474876012`); the preceding source commit `5161fa5` passed both workflows as well (`43474868715`, `43474868710`).

### P2 — Android 9 integration adapter and factory compatibility

- [x] Pin AOSP reference to `android-9.0.0_r3` from firmware build `PPR1.180610.011`.
- [x] Recover HMI, module/device API versions, device callback table and vendor tail.
- [x] Recover stream C wrappers, callback tables, C++ stream vtables and manager ownership.
- [x] Recover SONAME, DT_NEEDED list and symbol-version requirements.
- [x] Add layout-only ABI mirror, compile-time offset assertions and host CI checks.
- [x] Add executable reference tests for factory output sample-rate allowlist, input PCM16 fallback and input channel-mask predicate.
- [ ] Obtain/pin matching ALPS/MediaTek vendor headers or source revision.
- [ ] Recover actual Android 9 linker namespace configuration and verify every dependency is visible there.
- [x] Recover factory output format/rate/channel predicates and input PCM16/channel/rate validators from ARM32 disassembly; record the raw input-mask predicate as factory behavior without assuming it is normative.
- [x] Recover output/input handler-open dispatch and output buffer-time formula.
- [x] Recover legacy output-open flags defaulting to zero, `openOutputStreamWithFlags()` forwarding, raw output flag storage offset and the 8,192-byte output buffer getter contract.
- [x] Recover input `AUDIO_INPUT_FLAG_FAST` handling, the 5/20/60 ms input stream-buffer duration branches, and the separate 20 ms `getInputBufferSize()` formula with its exact format-width lookup table.
- [ ] Finish other input/output flag bits, the third-argument channel-mask contract, all error branches, all channel-mask combinations and vendor-header reconciliation.
- [x] Map all 16 private device-tail slots to their internal forwarding targets: xWay playback/recording, reference-ring, voice-unlock and parameter callbacks.
- [x] Recover xWay adapter forwarding to `Play2Way`, `Record2Way` and `AudioVUnlockDL`; correct the offset-172 entry to `xWayRec_Start` (not a duplicate stop callback).
- [x] Recover all 16 device-tail adapter thunks: each accepts the public `audio_hw_device*`, loads the internal `AudioALSAHardware*` at offset 220 and dispatches through the recovered C++ vtable slot.
- [ ] Confirm source-level return types and every external call site for the xWay callbacks; the adapter instructions establish argument forwarding but vendor declarations are absent.
- [x] Recover `AudioVUnlockDL` detached worker lifecycle, condition-variable ring-buffer synchronization and mandatory stop-before-free ordering.
- [ ] Identify all external consumers of the private tail and validate callback-in-flight draining. The 1,052-library/service scan found no separate named xWay client; indirect offset callers remain possible. The factory voice-unlock singleton is freed separately from `stopInput()`, so a replacement must enforce stop/drain-before-free.
- [x] Add a non-installable Android 9 ARM32 Soong bootstrap with exported `HMI`, factory module metadata and compile-time assertions for the 128/64/56/120/96/156/224-byte layouts. The device shell opens, while `init_check` and stream opens fail closed with `-ENODEV`; no hardware access is exposed.
- [x] Add the internal PCM16 byte/frame bridge and per-stream lifecycle gate; it is not yet wired into Android `audio_stream_out` / `audio_stream_in` callbacks.
- [x] Cross-compile the bootstrap as ELF32 ARM, verify `audio.primary.mt8163.so` SONAME and exported `HMI` (`GLOBAL DEFAULT`, 128 bytes) with NDK r27c in [CI run 43410778686](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43410778686). This is bootstrap ABI evidence, not a complete HAL build.
- [ ] Select and implement the narrowest production integration adapter for the validated PrimaryMedia profile while retaining the factory primary HAL.
- [ ] Verify adapter ABI, effect/stream lifecycle, linker visibility and route gating on Android 9 ARM32.
- [ ] Do not replace the complete factory primary HAL unless later evidence demonstrates that deep integration cannot satisfy the required behavior and the owner approves that scope change.

### P3 — DSP integration

- [x] Recover factory Normal playback pipeline and identify mono fold before MTK post-processing callback.
- [x] Confirm stereo-preserving DSP insertion point after DC removal and before conditional stereo-to-mono fold; fold condition and PCM16/PCM32 averaging behavior documented.
- [x] Confirm the MTK callback does not cover every audio route.
- [x] Add versioned, validated PCM16 stereo profile contract for the first PrimaryMedia processing boundary; invalid/unknown profiles fail open to bit-transparent bypass (host tests and ARMv7 cross-build passed).
- [ ] Integrate the PrimaryMedia profile into the verified Android/HAL route adapter.
- [ ] Define and validate additional profiles only for routes with proven DSP-visible PCM boundaries.
- [ ] Add route-specific playback, capture, telephony and echo-reference processing graphs under one DSP/profile owner.
- [ ] Disable or redirect A2DP offload where it bypasses the DSP-visible PCM path.
- [ ] Recover a DSP-visible digital path for FM direct I2S and verified ADC/PCM capture for analog-only AUX/AUX2/DAB inputs.
- [ ] Validate stereo separation, latency, CPU and stability.

### P4 — source adapters and routes

- [ ] Media playback and route arbitration.
- [ ] FM direct-I2S and FM capture.
- [ ] Bluetooth A2DP and HFP/SCO.
- [ ] Voice call, microphone, echo-reference synchronization, AEC/NS/AGC/sidetone and analog-input digitization where supported.
- [ ] Navigation prompts and concurrent-session transitions.
- [ ] Route-by-route regression against factory behavior.

### P5 — CD3313EO hardware integration

- [x] Identify physical IC as CD3313EO and driver as CD3313KO (`cd3313.ko`).
- [x] Reconstruct JNI/ioctl framing and protocol bounds.
- [x] Add host boundary tests, ASAN/UBSAN and libFuzzer workflow.
- [ ] Confirm live module/device ownership and I2C topology from P0 evidence.
- [ ] Implement bounded hardware transport only after ownership and protocol gates.
- [ ] Verify physical readback/command completion semantics before enabling writes.

### P6 — release

- [ ] Complete host regression, TSAN/ASAN/UBSAN and ARMv7 build.
- [ ] Verify Android 9 linker/ABI compatibility.
- [ ] Test all routes on the physical unit.
- [ ] Measure latency, stability and long-run behavior.
- [ ] Prepare factory rollback artifact and hashes.
- [ ] Package and validate final `audio.primary.mt8163.so`.

## Current CI evidence

- Latest code-bearing feature commit `30a59a9539a7d36e41dd4c54eb6d5f576e2ceedf` (the current `feature` HEAD adds only status/documentation updates): all eight workflows PASS — [Android ARMv7 build](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850412), [PCM Broker TSAN](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850419), [AudioCore TSAN](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850584), [Android 9 Primary HAL ARM32 ABI](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850446), [PCM DSP Stability](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850550), [PCM DSP Performance](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850452), [Repository Integrity](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850507), and [PCM DSP Core Tests](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43418850418). This verifies host tests, TSAN, ARM32 compilation/ABI bootstrap and repository checks; it does not verify a complete installable HAL or target hardware.

- Current feature HEAD `61259c7b72c2fda0d3ec1c8038e5c336aade38ff`: AudioCore TSAN, PCM Stream Broker TSAN, PCM DSP Stability Lab, PCM DSP Performance Benchmark, PCM DSP Core Tests and Android ARMv7 build all PASS — [AudioCore TSAN](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43409132535), [PCM Broker TSAN](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43409132470), [Stability Lab](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43409132445), [Performance](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43409132810), [DSP tests](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43409132458), [ARMv7 build](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43409132882).
- Android 9 ARM32 ABI layout and factory stream-contract tests: PASS on code-equivalent feature commit `0321a586ace98f9bf1e9154be449e2e4675980a2` — [run 43406307144](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43406307144). Includes C/C++ layout checks, stream-predicate tests and compile-time assertions for all 16 private callback indices and exact offsets 156–216.
- AudioCore TSAN: PASS on code-equivalent feature commit — [run 43406307164](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43406307164).
- PCM Stream Broker TSAN: PASS on code-equivalent feature commit — [run 43406307107](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43406307107).
- Repository Integrity: PASS on feature commit `8286c29a1327680803adc21c50e3c62ed8f9dcd9`; subsequent changes are documentation-only — [run 43406677450](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43406677450).
- Primary HAL factory audit, stream contracts, xWay forwarding, voice-unlock worker/teardown, HIDL bridge, full 1,052-library scan and service executable audit: PASS on code-equivalent feature commit `29000c551ce36bf9602245c7b67f2ff6b1f37883` — [run 43405588048](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43405588048). Artifact [mt8163-factory-primary-hal-audit](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43405588048/artifacts/14026274892).
- The firmware extraction contains no MediaTek vendor headers and no `ld.config*.txt` linker namespace configuration. Audio policy XML/conf and audio-device XML are present.
- No complete Primary HAL implementation, exact ARM32 HAL build, or target-device validation is claimed.

## Safety state

- No live ioctl, I2C write, GPIO change, mixer write, module load/unload or firmware flash is claimed.
- Hardware transport remains disabled until P0 ownership and bounded command ABI gates are satisfied.
- Unsupported routes/formats must use bit-transparent bypass.
- Retain all factory components and immutable originals/rollback hashes; prevent parallel runtime ownership after cutover.


- **Latest execution caveat (2026-11-13):** workflows triggered on `07e8fae18edd39e958cc6deb78552c268ba3acf6` and `8092e1688e889b8d4750faa118dfdc444b014b23` were marked failed by GitHub, but their jobs had `runner_id=0`, an empty `runner_name`, and an empty `steps` array. Re-running the failed jobs reproduced the same pre-runner failure. Job-log retrieval returned storage `BlobNotFound`; therefore no compiler, test, TSAN, or benchmark step is known to have executed. Do not interpret these workflow conclusions as evidence of source-code test failures. The newest code changes remain unverified until a runner actually executes the workflow steps.

- **Latest broker regression (2026-11-13):** commit `263825dcc080ff54983718bfd95105514cbee258` adds a test for a backend reporting more frames than requested. The expected contract is `BackendFailure`, stream state `Failed`, closed callback admission, then explicit `stop()` and `close()`. CI was triggered but all seven jobs again failed before runner assignment (`runner_id=0`, no runner name, zero executed steps); the new test has not yet executed in CI.


### Latest lifecycle regression — 2026-11-13

- Audited the interaction between `PcmStreamBroker` and `PrimaryHalStreamAdapter` after XRUN/device-loss terminal errors.
- Found that `PrimaryHalStreamAdapter::stop()` performed backend cleanup for a failed stream but returned before moving the corresponding active AudioCore session to `Suspended`; this left published session snapshots stale until close.
- Updated stop handling to inspect the AudioCore session state, perform backend stop, and transition only sessions that were actually `Active`. This preserves the `Requested` state for failed-start cleanup.
- Added a regression test asserting `Active` before stop and `Suspended` after successful cleanup.
- Host PCM DSP tests and Android ARMv7 cross-build passed on the final code HEAD `fb5a00e0a1d38aed71571cd3b2db41773d90e509`.

### CI queue policy — 2026-11-13

- Removed workflow-level `concurrency` cancellation blocks from all 13 workflows. New pushes no longer cancel older runs through `cancel-in-progress: true`; runs for successive commits can execute independently or wait for runner capacity.
- Repository Integrity passed on workflow-policy commit `20ff7460c15f861461d224e422d345a983707b8c` (run `43474595132`).
- The PCM DSP and Android ARMv7 workflows both executed successfully for commits `5161fa5` and `fb5a00e`; both commits' runs completed successfully rather than canceling earlier runs.

## PCM lifecycle audit update — 2026-11-13

Latest code commit: [cd1d695](https://github.com/14vvp93/mt8163_audio_engine/commit/cd1d695e5742b3d44260ffa592b4b8689f6dcbed).

- Found a lifecycle defect shared by `PcmStreamBroker` and `EndpointPcmBackend`: after `start()` succeeded, a failed `stop()` changed the state to `Failed`, but `close()` still allowed native resource release.
- Added an explicit stop-required lifecycle flag. It is set before calling backend start (so partial start failures are covered), cleared only after successful stop, and checked before close. A failed start also requires a stop/cleanup attempt.
- Closed the endpoint backend's callback admission before attempting stop, including when stop returns `WouldBlock`; pending callbacks are rejected while stop is retried. The broker also advances its callback generation at start/stop boundaries so an admission attempt from an earlier run cannot enter after a restart.
- A configure failure does not set the stop-required flag; an unstarted stream can still be closed without an invalid stop call.
- Added regression tests for failed stop, retry-before-close, failed start cleanup and configure-failure close behavior in both the broker and endpoint backend test suites.
- The control plane remains single-writer/serialized as specified by the roadmap. The new lifecycle flag is intentionally accessed only by serialized lifecycle calls; PCM transfer callbacks do not access it.

### CI status for this update

Workflows for commits `19017c7`, `e0c499b` and `cd1d695` again failed before runner assignment. The latest job metadata reports `runner_id=0`, empty `runner_name`, `steps=[]`, and log retrieval returns `BlobNotFound`. No compile, unit-test, TSAN, benchmark or ARM32 step is known to have executed for these commits. Do not classify these runs as source-code failures or as successful validation.

A current GitHub Community report describes the same runner-allocation signature; this does not identify the cause for this repository. Private-repository Actions compute is subject to the account's included usage and billing configuration. The repository owner's **Settings → Billing and licensing → Usage** page is the next account-level check. See [GitHub Actions billing and usage](https://docs.github.com/en/actions/concepts/billing-and-usage).

