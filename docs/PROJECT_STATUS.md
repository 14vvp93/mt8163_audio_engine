# MT8163 Audio Engine — current status

Updated: 2026-11-13  
Reviewed historical audit baseline: `59af8ea0000c1db9e1010f6b51b0377f0c4fbd66`  
Current implementation HEAD at status update: `e33c75234b629a1efb7f225d8a606b557e6ddcb8`

> **Execution authority:** [Project Roadmap](PROJECT_ROADMAP.md). This status file is a concise snapshot only. Do not use older execution logs to select priorities.

## Product status

**Engineering foundations implemented; complete factory Android 9 primary HAL replacement is not implemented or device-validated.**

Final replacement target remains:

`/vendor/lib/hw/audio.primary.mt8163.so`

Terminology: **CD3313EO** is the physical audio IC; **CD3313KO** is the Linux kernel driver (`cd3313.ko`). Historical documents using CD3310EO are superseded by the user's latest explicit correction and must be reconciled without rewriting their forensic history.

## Implemented foundations

- Firmware inventory, provenance, boot/kernel/DTB/DTBO and audio-stack static analysis.
- CD3313KO driver/JNI/ioctl static reconstruction, command model, host emulator, boundary tests, ASAN/UBSAN and libFuzzer workflow.
- AudioCore coordinator, session registry, route policy, mute arbitration, snapshot publication and semantic hardware broker.
- PCM backend contracts, endpoint adapter and stream broker with bounded lifecycle/concurrency behavior and host tests.
- PCM DSP foundations: stereo PCM processing, four-channel Float32 test engine, EQ, crossover, dynamics, enhancement, time alignment and profile transitions.
- Android ARMv7 library/JNI build and Android 9 effect-library ABI scaffold.
- Read-only target diagnostic collectors and route/ownership matrices.

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

- PCM stream-broker ThreadSanitizer: [run 43375007298](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375007298) — PASS, including the new repeated handle-reuse test.
- AudioCore ThreadSanitizer: [run 43375007312](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375007312) — PASS.
- PCM DSP host tests: [run 43375007291](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375007291) — PASS.
- Android ARMv7 build and DSP benchmark: [run 43375007287](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43375007287) — PASS.
- PCM DSP stability/performance: [runs 43375007331 and 43375007550](https://github.com/14vvp93/mt8163_audio_engine/actions) — PASS.
- These results apply to code commit `9ee683c`; subsequent commits are documentation/collector changes. The latest collector-triggered TSAN runs are tracked separately.
- No complete primary HAL build or device validation is claimed.

## Safety state

- No live ioctl, I2C write, GPIO change, mixer write, module load/unload or firmware flash is claimed.
- Do not enable hardware transport until target ownership and the complete bounded command ABI are verified.
- Keep unsupported routes/formats on bit-transparent bypass.
- Preserve factory originals and rollback hashes.

## Latest P1 change

The PCM broker no longer reads non-atomic stream direction metadata before transfer admission. This closes a lifecycle race window where a callback could inspect direction while close/reopen reset the same slot. A repeated same-handle close/reopen stress test passed under TSAN.

## P0 collector update

The PowerShell collector now records PCM substream status, device-node metadata, open-FD owners and I2C driver symlinks. It no longer stores the ADB serial in the manifest. The collector remains unrun on the physical unit.

## Next action

Run the read-only target inventory from [tools/README.md](../tools/README.md), then share the generated ZIP after reviewing private log/property data. Continue host-side HAL ABI recovery without speculative hardware operations.
