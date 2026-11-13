# MT8163 Audio Engine — current status

Updated: 2026-11-13  
Active development branch: `feature`  
Stable branch: `main`

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
- [x] PCM backend contract, endpoint adapter and stream broker.
- [x] Fix direction metadata race by reading it only after transfer admission.
- [x] Add repeated same-handle close/reopen stress test.
- [x] Add snapshot backpressure regression with all publication slots pinned.
- [x] Prior PCM Broker and AudioCore TSAN runs passed.
- [ ] Confirm the latest TSAN runs on current HEAD.

### P2 — Android 9 Primary HAL

- [x] Pin AOSP reference to `android-9.0.0_r3` from firmware build `PPR1.180610.011`.
- [x] Recover HMI, module/device API versions, device callback table and vendor tail.
- [x] Recover stream C wrappers, callback tables, C++ stream vtables and manager ownership.
- [x] Recover SONAME, DT_NEEDED list and symbol-version requirements.
- [x] Add layout-only ABI mirror and CI checks.
- [ ] Obtain/pin matching ALPS/MediaTek vendor headers or source revision.
- [ ] Recover actual Android 9 linker namespace configuration and verify every dependency is visible there.
- [x] Recover factory output format/rate/channel predicates and input PCM16/channel/rate validators from ARM32 disassembly.
- [x] Recover output/input handler-open dispatch and output buffer-time formula.
- [x] Recover legacy output-open flags defaulting to zero, `openOutputStreamWithFlags()` forwarding, raw output flag storage offset and the 8,192-byte output buffer getter contract.
- [ ] Finish individual input/output flag-bit semantics, all error branches, all channel-mask combinations and vendor-header reconciliation.
- [x] Identify the MediaTek HIDL 4.1 bridge and recover exact legacy callback offsets for seven voice-unlock/parameter-callback methods plus the notification callback path.
- [ ] Trace all remaining private tail entries at offsets 156–184 (`xWayPlay`, `xWayRec`, `ReadRefFromRing`) and validate callback lifetime/concurrency semantics.
- [ ] Implement typed factory-compatible HAL shell with no hardware writes.
- [ ] Build exact ARM32 HAL and verify ABI/symbol/dependency contract.

### P3 — DSP integration

- [x] Recover factory Normal playback pipeline and identify mono fold before MTK post-processing callback.
- [x] Confirm stereo-preserving DSP insertion point after DC removal and before conditional stereo-to-mono fold; fold condition and PCM16/PCM32 averaging behavior documented.
- [x] Confirm the MTK callback does not cover every audio route.
- [x] Identify stereo-preserving insertion point after DC removal and before conditional stereo-to-mono fold.
- [ ] Integrate DSP with bit-transparent bypass on unsupported route/format or DSP failure.
- [ ] Validate stereo separation, latency, CPU and stability.

### P4 — source adapters and routes

- [ ] Media playback and route arbitration.
- [ ] FM direct-I2S and FM capture.
- [ ] Bluetooth A2DP and HFP/SCO.
- [ ] Voice call, microphone, echo and analog input.
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

- Factory ELF/HMI/APS2 audit, stream/manager disassembly, 1,052-library symbol/string scan, MediaTek HIDL 4.1 bridge, HIDL service executable, ATCI executable and init registration: PASS — [run 43401616001](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401616001). Artifact [mt8163-factory-primary-hal-audit](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401616001/artifacts/14024364994).
- AudioCore TSAN: PASS on feature commit `f430d39c126551d882f4d810b250e091c202ae10` — [run 43401615897](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401615897).
- PCM Stream Broker TSAN: PASS on the same feature commit — [run 43401615795](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401615795).
- Android 9 ARM32 ABI layout: C++ and C11 compile-time layout tests PASS on feature commit `09b42d6893a36874961784f57817c299d9838779` — [run 43401220635](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401220635).
- Repository Integrity: PASS on feature commit `7921fd8432c12226ad5eda4952a111160e92618a` — [run 43401448779](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401448779). Later changes are workflow/docs only.
- Linker namespace files `system/etc/ld.config*.txt` and `vendor/etc/ld.config*.txt` were absent from the extracted tree. Audio policy XML/conf and audio-device XML were present.
- No complete Primary HAL implementation, exact ARM32 HAL build, or target-device validation is claimed.

## Safety state

- No live ioctl, I2C write, GPIO change, mixer write, module load/unload or firmware flash is claimed.
- Hardware transport remains disabled until P0 ownership and bounded command ABI gates are satisfied.
- Unsupported routes/formats must use bit-transparent bypass.
- Preserve factory originals and rollback hashes.
