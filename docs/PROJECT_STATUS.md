# MT8163 Audio Engine — current status

Updated: 2026-11-13  
Reviewed historical audit baseline: `59af8ea0000c1db9e1010f6b51b0377f0c4fbd66`  
Latest code/test commit: `7b1dbc347913c16c52460d350dcfd87913f1f5e7`

> **Execution authority:** [Project Roadmap](PROJECT_ROADMAP.md). This status file is a concise snapshot only. Do not use older execution logs to select priorities.


## Primary HAL artifact automation

- Added `.github/workflows/primary-hal-binary-audit.yml` on `feature`.
- The workflow sparsely checks out the factory `audio.primary.mt8163.so` from `14vvp93/public_calculations`, verifies its expected size and SHA-256, then runs the ELF/HMI audit script.
- First factory audit completed successfully: exact factory size/SHA-256, ELF32 ARM EABI5, Android API 28 note, exported `HMI`, SONAME, 32 `DT_NEEDED` entries, dynamic symbol versions and ARM relocations were inspected.
- Confirmed `HMI` is a 128-byte `OBJECT GLOBAL DEFAULT` in `.data` at `0x00195014`; module API `0.1`, HAL API `1.0`, module ID `audio`, name `MTK Audio HW HAL`, author `MTK`.
- `hw_module_methods_t.open` pointer `0x0009b70c`; disassembly confirms device API 3.0, 224-byte allocation and 68-byte vendor tail.
- Decoded 2,431 Android APS2 relocations and recovered the 57-entry `AudioALSAHardware` vtable, all standard device callback targets and the vendor extension callback map.
- Recovered `AudioALSAStreamOut` and `AudioALSAStreamIn` C++ vtables plus the Android C stream wrappers: output wrapper 124 bytes (`audio_stream_out` 120 + legacy pointer), input wrapper 100 bytes (`audio_stream_in` 96 + legacy pointer).
- Input wrapper callback slots `set_parameters` and `get_parameters` have suspicious mid-function targets; this is documented as an ABI anomaly requiring source/call-context reconciliation before implementation.
- Added a layout-only ARM32 ABI mirror with compile-time size/offset assertions for module, device, stream and vendor wrapper structures. The standalone CMake/CTest layout workflow passed.
- Factory SONAME and exact `DT_NEEDED` list are recorded in `ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md`.
- Main tree contents were restored after an accidental helper-file creation on the default branch; the two bookkeeping commits remain in `main` history. The helper now exists only on `feature`.

## Branch model

- `main` is the stable branch; no project changes were pushed to it.
- `feature` is the sole active development branch.
- Repository integrity, AudioCore TSAN and PCM broker TSAN workflows now target `main` and `feature`; legacy branch filters were removed.
- Latest observed validation on `feature` commit `31cfeb11a618`: Repository Integrity PASS, AudioCore TSAN PASS and PCM Stream Broker TSAN PASS. The factory HAL audit also passed on `50196bc0b946`.

## Product status

**Engineering foundations implemented; complete factory Android 9 primary HAL replacement is not implemented or device-validated.**

Final replacement target remains:

`/vendor/lib/hw/audio.primary.mt8163.so`

Terminology: **CD3313EO** is the physical audio IC; **CD3313KO** is the Linux kernel driver (`cd3313.ko`). Historical hardware labels are superseded by the canonical terminology above and must be reconciled without rewriting unrelated forensic findings.

## Implemented foundations

- Firmware inventory, provenance, boot/kernel/DTB/DTBO and audio-stack static analysis.
- CD3313KO driver/JNI/ioctl static reconstruction, command model, host emulator, boundary tests, ASAN/UBSAN and libFuzzer workflow.
- AudioCore coordinator, session registry, route policy, mute arbitration, snapshot publication and semantic hardware broker.
- PCM backend contracts, endpoint adapter and stream broker with bounded lifecycle/concurrency behavior and host tests.
- PCM DSP foundations: stereo PCM processing, four-channel Float32 test engine, EQ, crossover, dynamics, enhancement, time alignment and profile transitions.
- Android ARMv7 library/JNI build and Android 9 effect-library ABI scaffold.
- Read-only target diagnostic collectors and route/ownership matrices.

## HAL ABI recovery update

The firmware build fingerprint identifies Android 9 base build `PPR1.180610.011`, which maps to AOSP `android-9.0.0_r3`. The ARM32 AOSP HAL structure layouts and offsets are now pinned in [ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md). This is the platform reference only: the factory module API version, exported `HMI`, method table, vendor extensions, SONAME and dependencies still require binary-level confirmation. The primary HAL audit script now checks ET_DYN, ARM32 EABI5, factory filename and `HMI` export; CI syntax validation is enabled.

## Blocking gaps

1. No complete factory-compatible Android 9 primary HAL implementation or exact ABI-verified build.
2. No target runtime inventory proving module/device-node ownership, ALSA endpoint indexes, active policy, mixer controls or route state.
3. No concrete TinyALSA endpoint implementation configured from target evidence.
4. No production CD3313EO/CD3313KO hardware transport backend; transport remains disabled.
5. No proven stereo-preserving system-audio insertion point or end-to-end media route.
6. FM direct I2S, FM capture, A2DP offload, HFP/SCO, microphone/echo and analog input routes are not integrated end-to-end.
7. Factory control clients have not yet been migrated to one authoritative owner.
8. No physical-device audio, acoustic, latency, long-run or rollback validation.

## Current CI evidence

- PCM stream-broker ThreadSanitizer: [run 43375546258](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546258) — PASS, including repeated same-handle close/reopen stress.
- AudioCore ThreadSanitizer: [run 43375546004](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546004) — PASS, including dirty snapshot retry with all slots pinned.
- PCM DSP performance benchmark: [run 43375546124](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375546124) — PASS.
- PCM DSP host tests, stability lab and Android ARMv7 build were running at the last check and are not yet marked as passed.
- Earlier collector-only changes passed PCM broker TSAN [run 43375203331](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375203331) and repository integrity [run 43375253089](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375253089).
- No complete primary HAL build or device validation is claimed.

## Safety state

- No live ioctl, I2C write, GPIO change, mixer write, module load/unload or firmware flash is claimed.
- Do not enable hardware transport until target ownership and the complete bounded command ABI are verified.
- Keep unsupported routes/formats on bit-transparent bypass.
- Preserve factory originals and rollback hashes.

## Latest P1 changes

- PCM broker direction metadata is read only after transfer admission. The repeated same-handle close/reopen stress test passed under TSAN.
- AudioCore snapshot backpressure now has a dedicated regression: all three slots are pinned, a mutation commits while publication is blocked, then retry after a slot release publishes the latest complete state. This passed AudioCore TSAN.

## P0 collector update

The PowerShell collector now records PCM substream status, device-node metadata, open-FD owners and I2C driver symlinks. It no longer stores the ADB serial in the manifest. The collector remains unrun on the physical unit.

## Next action

Run the read-only target inventory from [tools/README.md](../tools/README.md), then share the generated ZIP after reviewing private log/property data. Continue factory-binary ABI recovery and pin the exact ALPS/MediaTek vendor header revision. The AOSP r3 baseline is pinned; the vendor ABI and binary audit remain open. No speculative hardware operations. The current AudioCore and PCM broker race regressions are verified; target inventory remains the P0 external evidence gate.
