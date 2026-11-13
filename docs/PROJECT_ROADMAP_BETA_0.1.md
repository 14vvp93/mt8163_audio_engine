# MT8163 Audio Engine — Canonical Project Roadmap (Beta 0.1)

**Baseline date:** 2026-11-12  
**Target:** MediaTek MT8163, Android 9 / API 28, ARM32 / EABI5, vendor Linux 4.9.117  
**Integration branch:** `integration/beta-0.1`  
**Last known integration commit before this roadmap:** `36f159e9c29dad537123315838b3c3cea3c7d4fa`  
**Status:** engineering platform in development; not a flashable or device-validated replacement.

> **Hardware identity correction (2026-11-13): the user's physical head unit contains CD3310EO only. CD3313/AK7604/FY7604 firmware artifacts are not evidence of populated hardware on this unit. All earlier CD3313 work is retained as firmware/emulator research only and is not approved for physical transport or protocol reuse. The active CD3310EO control path is currently unidentified. This correction supersedes any earlier inference from DTBO names or init-script module lists.**

This document is the single authoritative execution route for Beta 0.1. New work must follow the priority order and gates below. Update this document when evidence changes; do not create competing roadmaps.

## 1. Product definition and non-negotiable architecture

Build one complete replacement for the factory audio implementation. The final primary runtime artifact must retain the factory path and filename:

`/vendor/lib/hw/audio.primary.mt8163.so`

AudioCore, PCM backend, DSP and hardware-control code are internal implementation units, not independent runtime owners. There must be exactly one authoritative owner for audio stream state, routing, mute/volume arbitration, DSP profile state and hardware commands.

Factory firmware is evidence, not design authority. Preserve verified Android/framework/kernel/hardware contracts, but correct unsafe memory handling, invalid state transitions, unreliable status reporting and duplicated ownership. Do not copy factory defects.

Keep a bit-transparent bypass for unsupported routes/formats and DSP failures. The real-time PCM callback must not allocate, block on mutexes, log, issue control ioctls or perform I2C.

## 2. Current state

### Implemented or evidenced in repository/CI

- PCM DSP core, endpoint backend and stream broker have been developed.
- Stability Lab and performance benchmark exist.
- Android ARMv7 build and DSP benchmark CI exist.
- Deterministic concurrency coverage exists for a PCM write racing with stop/close.
- AudioCore ThreadSanitizer workflow was expanded to cover the full AudioCore test suite.
- Android 9 HAL contract checklist, factory ABI recovery procedure and static ELF inventory/audit tooling exist.
- Hardware route evidence workstream includes read-only diagnostics and a route cutover matrix.
- CD3313 emulator/protocol parser exists; parser validation was optimized to avoid a temporary vector allocation in command 0x15 validation.
- Optimization/factory-defect strategy and development acceleration plan are documented.

CI evidence is host/build evidence only. Previous reports recorded passing PCM/DSP tests, TSAN, stability, performance and ARMv7 build checks. Before relying on a result for a new change, re-check the Actions status for the current integration HEAD. Never label CI as device validation.

### Not complete / not proven

- The complete factory-named Android 9 primary HAL has not yet been proven built with the exact factory ABI.
- The target board's actual ALSA card/device, PCM formats, period sizes and mixer controls are not yet confirmed by device evidence.
- Physical chip population is user-confirmed as CD3310EO only; the kernel/userspace control owner, `/dev/audiomux` ownership and actual I2C path remain unconfirmed.
- CD3313 production transport is intentionally detached.
- No target-device route, I2C write, GPIO change, mixer write, acoustic measurement or end-to-end audio validation is claimed.
- No evidence currently justifies flashing a replacement.

## 3. Confirmed factory findings and risks

### CD3313-named factory firmware path (not yet matched to physical CD3310EO)

Static analysis of the supplied factory firmware found the following defects in its CD3313-named software path. These findings apply to that firmware implementation only; they do not establish that this driver controls the physically populated CD3310EO:

- JNI `SendCmdAudioMux(int, int[])` encodes one more 32-bit word than the Java array contains, creating a native over-read when the kernel copies the encoded size.
- The driver lacks sufficient payload-length checks for commands including `0x03`, `0x09`, `0x56` and `0xA3`; command `0x15` reads four words with a weaker length threshold.
- Speaker-level channel indices are not fully range-checked in the factory callback.
- Successful ioctl submission means asynchronous queue admission, not confirmed I2C completion.
- Read/status reports cached software state, not guaranteed physical chip readback.
- Raw-write path has file-position and maximum-length termination risks.

Required behavior: validate complete frames before access; distinguish requested, queued, transmitted, acknowledged and physically observed state; report unknown when physical state is not observable. Do not enable production transport until the exact Java/JNI/kernel framing contract is reconciled.

### Board ownership collision risk

The supplied firmware contains CD3313, AK7604 and FY7604 module variants that register the same `audiomux` misc-device name. This proves a potential registration collision, not that all modules are active or all chips are populated. Actual board ownership must be established read-only.

### PCM/DSP topology

Recovered Normal playback path:

`DC removal -> conditional stereo-to-mono fold -> MTK post-processing -> BLI SRC -> bit conversion -> ALSA write`

For speaker-containing masks, the MTK callback receives duplicated mono samples, not independent stereo or four-channel PCM. It is reached from the Normal handler, not as a global hook for FM, SCO, voice, HDMI or capture.

Therefore:
- Do not claim four-channel PCM processing through this callback.
- Find a verified stereo-preserving insertion point or ship an initially verified stereo DSP path.
- Do not confuse CD3313's four analog output attenuators with four independent PCM/EQ channels.
- Keep the quad engine as a tested capability until a real four-channel PCM boundary is proven.

## 4. The only execution route

Work strictly in this order. A later phase cannot bypass an earlier safety gate.

### P0-A — identify the physical CD3310EO control path; preserve CD3313 audit as firmware-only

- [ ] Identify the exact driver/module and userspace/HAL/JNI path that controls the physically populated CD3310EO before defining any command protocol.
- [ ] Determine whether any CD3313-named firmware module is actually a compatible transport for CD3310EO; require direct source/disassembly evidence, not filename or DTBO inference.
- [ ] Map every command/register operation of the proven CD3310EO path to exact payload length, field offsets, signedness, valid ranges and response semantics.
- [ ] Reconcile Java array length, JNI framing, ioctl request and kernel copy length byte-for-byte.
- [ ] Remove every out-of-bounds read/write, integer overflow, unchecked index and unterminated raw-write path in the replacement implementation.
- [ ] Add positive and negative boundary tests for every command: empty, truncated, exact, oversized, invalid enum/index, null/invalid buffer where applicable.
- [ ] Add fuzzing plus ASAN/UBSAN coverage for parser/builders.
- [ ] Separate requested, queued, transmitted, acknowledged and physically read-back state.
- [ ] Keep hardware transport disabled until framing and ownership are proven.

**Exit gate:** complete command specification, bounded parser/builders, sanitizer/fuzz tests passing, no ambiguous frame-size interpretation.

### P0-B — Read-only board topology and ownership evidence

- [ ] Obtain exact target kernel source/config, board DT/DTBO and toolchain where available.
- [ ] Record loaded modules and boot/probe logs.
- [ ] Identify actual owner of `/dev/audiomux`, major/minor, module and open file descriptors.
- [ ] Record `/proc/asound/cards`, `/proc/asound/pcm`, PCM status and mixer controls.
- [ ] Identify active Audio Policy XML and AudioPatch/route ownership.
- [ ] Establish FM direct-I2S versus FM capture path.
- [ ] Establish A2DP, HFP/SCO downlink, microphone uplink and echo-reference lifecycle.
- [ ] Establish DSP population and I2C/GPIO ownership from board evidence.

Only read-only collection is allowed during discovery. Do not guess card/device numbers or issue guessed I2C/GPIO/mixer writes.

**Exit gate:** board topology and all relevant hardware owners documented with actual device evidence.

### P1-A — PCM backend and lifecycle hardening

- [ ] Audit all shared state and synchronization in endpoint backend and stream broker.
- [ ] Test stale callbacks after close/reopen and slot reuse.
- [ ] Test stop racing with playback write and capture read.
- [ ] Test close after XRUN/device loss and repeated lifecycle cycles.
- [ ] Test token wrap/reuse, slot exhaustion and invalid/stale tokens.
- [ ] Keep deterministic barrier-controlled race tests.
- [ ] Run full host suite with TSAN, ASAN and UBSAN where applicable.
- [ ] Keep lifecycle operations serialized on the control plane.

**Exit gate:** no unresolved races, lifetime errors, invalid state transitions or sanitizer failures.

### P1-B — Android 9 factory HAL ABI recovery and implementation

- [ ] Inspect exact factory `audio.primary.mt8163.so`: ELF class/machine, SONAME, exported symbols, symbol versions, dependencies, relocations and linker namespace assumptions.
- [ ] Recover exact device/stream interface contracts, structure sizes/layouts, callbacks and error semantics.
- [ ] Inventory all factory libraries, services, modules and policy components that currently own audio state.
- [ ] Implement the complete Android 9 primary HAL, not only a static library or isolated DSP module.
- [ ] Integrate AudioCore and PCM backend behind the factory-compatible ABI.
- [ ] Produce the factory-named ARM32 artifact at the exact path.
- [ ] Add ELF/ABI checks and preserve a verified rollback image.

**Exit gate:** complete factory-compatible HAL builds with verified ABI, SONAME, symbols and dependencies.

### P1-C — DSP insertion point and route-correct integration

- [ ] Prove the PCM topology at the selected insertion point.
- [ ] Preserve independent L/R samples on the initial production DSP path.
- [ ] Define supported sample formats, rates, channel layouts and block sizes from evidence.
- [ ] Implement exact bypass for unsupported conditions and DSP faults.
- [ ] Measure end-to-end latency, CPU cost, clipping/headroom, DC behavior, transition artifacts and long-run glitch rate.
- [ ] Keep FM, A2DP, HFP/SCO, voice, HDMI and capture as distinct route classes unless evidence proves shared processing is safe.

**Exit gate:** documented route/format matrix and measured, bounded DSP behavior without channel corruption or regressions.

### P2 — Single-owner migration and Beta 0.1 acceptance

- [ ] Move source selection, mute arbitration, volume/tone, DSP profiles and vehicle events under one AudioCore control owner.
- [ ] Keep vehicle MCU status controls separate from CD3313 audio mute controls.
- [ ] Replace remaining factory ownership only after behavior parity and ABI compatibility are proven.
- [ ] Do not remove or overwrite factory services, libraries, modules or policy files before replacement coverage and rollback are verified.
- [ ] Test normal media, route changes, mute/ACC, FM, A2DP, HFP/SCO, microphone, suspend/resume and recovery from device errors on the physical unit.
- [ ] Verify rollback on the target device.

**Beta 0.1 release gate:** all P0/P1 gates closed, factory-named HAL built, integration CI green, hardware topology proven, route behavior validated on the physical unit, rollback tested.

## 5. Branch and change-control policy

- Sole integration destination: `integration/beta-0.1`.
- `main` remains untouched unless the user explicitly authorizes a separate promotion.
- No force-push, history rewrite, branch deletion or automatic merge.
- No modifications to original factory firmware/recovery files.
- Existing work branches are evidence/source branches; promote only reviewed, focused commits.
- Do not create more parallel architecture experiments. Existing `work/virtual-audio-*` branches are test fixtures, not production runtime paths.
- Before each promotion: inspect exact diff, run focused tests, then integration CI.
- Keep CI, Android build, static ABI audit and device evidence as separate proof categories.

## 6. Performance and quality rules

- Optimize end-to-end latency and glitch rate, not synthetic throughput alone.
- Real-time callback: bounded work, preallocated buffers, no heap allocation, blocking locks, logging or hardware-control operations.
- Control plane: serialize lifecycle and hardware state transitions; coalesce redundant commands only when semantics permit.
- Never hardcode unverified ALSA card/device numbers, mixer controls, GPIOs or I2C addresses.
- Fail safely to bit-transparent bypass when DSP is unavailable or the route/format is unsupported.
- Fix memory-safety and factory correctness defects before adding more DSP features.

## 7. Evidence labels

Every status report must distinguish:

- **HOST VERIFIED** — host tests/sanitizers/benchmarks passed.
- **ANDROID BUILD VERIFIED** — Android ARM32 artifact compiled and static checks passed.
- **ABI VERIFIED** — exact factory ABI and ELF compatibility confirmed.
- **DEVICE VERIFIED** — behavior measured on the actual target head unit.

A lower level never implies a higher one. Never claim a build, test, flash or device result that was not actually observed.

## 8. Immediate next action

Start with **P0-A: identify the CD3310EO control path from the exact firmware**. Continue read-only P0-B preparation and inspect PCM lifecycle only where it supports the P1-A gate. Existing CD3313 parser/emulator remains isolated firmware research. Do not send CD3313 commands to the physical unit, enable any hardware transport, modify factory binaries, or attempt flashing while the P0 gates remain open.

---

**Decision:** This roadmap is the agreed project route for MT8163 Audio Engine Beta 0.1. Any proposed deviation must be evaluated against safety, ABI compatibility, route correctness and measurable performance, and must not silently change the agreed priority order.

## 9. Execution log — 2026-11-12

### P0-A work started

- Added `cd3313-emulator/tests/protocol_boundary_test.cpp` to systematically exercise every recovered normal command and both special dispatch commands at their minimum/required payload sizes, with truncated and oversized frames, round-trip encoding, and the 63/64-word slab boundary.
- Added the `cd3313_protocol_boundary_tests` CTest target.
- Added `.github/workflows/cd3313-protocol-safety.yml` with normal protocol tests and ASAN/UBSAN execution.
- Added bounded libFuzzer harness `cd3313-emulator/fuzz/protocol_fuzz.cpp`. The harness caps each input at 1024 bytes and round-trips every accepted frame through the encoder/parser.
- Added optional Clang-only `CD3313_BUILD_FUZZER` CMake target and a 45-second libFuzzer CI job.
- CI run [43300447361](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43300447361) completed successfully: `protocol-tests` PASS and `protocol-asan-ubsan` PASS. This run predates the newly added libFuzzer job.
- CI run [43300480240](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43300480240) is checking the expanded workflow including libFuzzer. At the time of this log, the normal protocol test passed; ASAN/UBSAN and libFuzzer were still running.
- Latest known integration commit at log creation: `46aa65ad8b691b127d4d42a88b6391f4b3111185`.

### Remaining P0-A gaps

- The exact factory JNI/kernel frame-length contract is not yet reconciled byte-for-byte.
- Command-specific field ranges and response semantics still need a complete source-backed specification; opaque words must not be guessed.
- Fuzzing has not yet completed successfully on the expanded workflow.
- Invalid/null pointer behavior at the JNI/kernel boundary requires source-level analysis; the userspace vector parser itself does not accept raw pointers.
- Production transport remains disabled.

### P0-A follow-up — evidence reconciliation and CI results

- Added [CD3313_EVIDENCE_RECONCILIATION.md](CD3313_EVIDENCE_RECONCILIATION.md). It identifies the vendor archive module (Build ID e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56) as the stronger installed-module candidate and explicitly separates it from the older standalone module (Build ID dae15c9eb019e9388d15cb88088c202d017ebcf0).
- Recorded the static JNI framing formula: array_length + 1 words. The extra word is a confirmed over-read in the factory JNI/kernel pair; a future compatibility bridge must allocate and initialize its own extra word or deliberately revise the ABI only after caller inventory.
- The current vendor-module ABI audit reports a special 0x09 branch. The parser now safely recognizes 0x09 as a one-word opaque command; no operation semantics or hardware behavior are inferred.
- Recorded unresolved evidence conflicts around 0x03 minimum-length validation and the exact vendor-module command map. The older standalone dispatch map is not treated as authoritative for the installed candidate.
- Consolidated CD3313 checks into the existing .github/workflows/test-cd3313-emulator.yml rather than retaining duplicate workflows. It now covers integration/beta-0.1, normal host tests, ASAN/UBSAN and a bounded Clang libFuzzer run.
- CI run [43301144576](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43301144576) passed all three jobs: host-tests, sanitizer-tests and libfuzzer. This run includes the 0x09 parser and boundary coverage.
- CI run [43301304060](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43301304060) is queued for the additional legacy JNI initialized-padding regression test. Do not mark that exact latest test as passed until this run completes.
- Latest known integration commit at this update: ae0f081bb40ce707f314b28915fa407736818e93.

### Updated immediate P0-A focus

Continue with the exact vendor archive module's callback disassembly and reconcile every command ID, length check and argument read. Specifically resolve the 0x03 discrepancy, determine the 0x09 operation, inventory all Java/Binder command-array callers, and define the safe legacy JNI compatibility buffer. Do not enable hardware transport.


### P0-A follow-up — paired vendor callback revalidation and parser boundaries

- Retrieved the exact `cd3313-current-module-analysis` artifact from public firmware worker run [43238133570](https://github.com/14vvp93/public_calculations/actions/runs/43238133570). Verified SHA-256 `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594` and Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`.
- Re-read the paired vendor `cmd_ioctl` and `event_callback` disassembly directly. Added [CD3313_VENDOR_CALLBACK_REVALIDATION.md](analysis-reports/CD3313_VENDOR_CALLBACK_REVALIDATION.md) with the exact command guard/read map and evidence identity.
- Corrected a prior risk-audit error: the paired vendor callback range-checks all four `0x15` indices against 0..15. The actual remaining issue is that its size check accepts two/three words before it reads four words, leaving missing values sourced from stale slab data.
- Confirmed the paired vendor callback has no visible size-field guard before its payload reads for `0x03`, `0x09`, `0x56` and `0xA3`. The host parser continues to require complete words. The three-word `0x03` threshold remains explicitly labelled as conservative host policy, not proven vendor acceptance behavior.
- Corrected variant-specific documentation so standalone-module defects are not incorrectly attributed to the paired vendor build. The paired vendor I2C write helpers retry and update cache only after exact one-byte transfer success.
- Expanded protocol tests for empty/partial headers, unsupported direction, unknown IDs, ignored low request byte, exact and malformed payload sizes, all four invalid `0x15` index positions, encoder validation, and the 63/64-word slab boundary.
- Seeded libFuzzer with valid `0x10`, `0x15` and `0x09` frames so mutation begins from accepted protocol paths.
- CI run [43303015809](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43303015809) completed successfully: host tests, ASAN/UBSAN and seeded libFuzzer all PASS. AudioCore TSAN run [43303015831](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43303015831) also completed successfully.
- Latest boundary extension checks both 2-word and 3-word `0x15` frames; run [43303482890](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43303482890) is queued and must pass before this exact extension is marked verified.
- Latest integration commit at this update: `ae10667037b5417460d284168a82bfc317874f07`.

### Remaining P0-A closure items

- [ ] Reconcile the complete vendor callback's field semantics/ranges and every branch against the exact paired module, including `0x03` channel/gain handling and `0x09` semantics.
- [ ] Inventory all Java/Binder callers and exact array lengths/values; determine the safe legacy JNI compatibility buffer.
- [ ] Confirm the vendor kernel's `__check_object_size` / hardened-usercopy implementation and configuration.
- [ ] Reconcile every command ID and ensure the host allowlist is complete without importing standalone-only behavior.
- [ ] Keep production transport detached; no physical-device or hardware-write validation has been performed.


### P0-A follow-up — command semantics and write allowlist hardening

- Recovered `0x09` semantics from the paired vendor module's DWARF symbol/dataflow: it sets `volume_max`, default 40; `set_master_volume()` rejects requested steps above this value.
- Added `make_master_volume_max_command()`, parser/encoder validation for 0..40, and `EmulatorState::master_volume_max` with default 40. The emulator now rejects a volume step above the configured maximum. This prevents a replacement-side out-of-bounds volume-table index that the factory callback does not visibly guard against when `volume_max` is set above 40.
- Added parser/encoder checks for `0x03`: channel 0..5 and gain 0..3. Direct vendor disassembly shows channel 6 is rejected, but values 7+ are not bounded before indexing the seven-byte `channel_gain` array; the replacement blocks this overflow.
- Removed write-direction `0xF0` from the write allowlist. Its read-direction operation is the separate fixed 8-byte chip-ID query; the vendor write branch performs no valid write operation. The host allowlist now has 11 write commands plus separately handled `0x09`, `0x56` and `0xA3`.
- Added regression tests for `0x09` maximum bounds, volume steps above the configured maximum, invalid `0x03` channel/gain, and rejection of `0xF0` in the write parser/encoder/emulator.
- CI run [43304787779](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43304787779) at code commit `8fee63d1` reports host tests PASS and ASAN/UBSAN PASS. Seeded libFuzzer is still running at this update; do not mark the complete safety workflow green until its result is available.
- The previous test failures were traced to outdated test expectations (first, an emulator fixture reused state after testing the new volume limit; then, a test still attempted to encode an invalid input-gain command and a write-direction `0xF0`). Both fixtures were corrected to exercise the parser directly and isolate state.
- Current documentation head at this update: `32de3e98a6099edd5c2a693d97fbfd95898d251f`.

### Remaining P0-A closure items

- [x] Decode the `0x03` optimized callback channel/gain branch: channel -1 and 6 are rejected, but values 7+ are not bounded before indexing the seven-byte `channel_gain` array; replacement accepts only 0..5 and gain 0..3.
- [ ] Correlate every `0x03` Java/Binder caller value against the safe replacement contract.
- [ ] Complete the Java/Binder caller inventory with exact method-to-command mapping and argument values, including the legacy extra JNI word.
- [ ] Decide whether the replacement JNI bridge keeps a separately allocated, initialized compatibility word or adopts a new bounded ABI.
- [ ] Confirm vendor `__check_object_size` and hardened-usercopy behavior from exact kernel source/config.
- [ ] Reconcile all command IDs and branch-local side effects against the paired vendor build; keep production transport detached.
- [ ] No target ABI, board topology, hardware I/O or device validation is complete.


### CI closure for the current P0-A code set

- CD3313 Protocol Safety run [43304787779](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43304787779) completed SUCCESS: host tests, ASAN/UBSAN and seeded libFuzzer all passed.
- AudioCore TSAN run [43304787782](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43304787782) completed SUCCESS.
- These runs cover the code set through commit `8fee63d1`; subsequent commits through the current roadmap update are documentation-only. Status: HOST VERIFIED for this code set. No Android ARM ABI, target-kernel build, or DEVICE VERIFIED claim is made.


### Final P0-A host verification after input-gain bounds

- CD3313 Protocol Safety run [43305452037](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43305452037) completed SUCCESS after the channel 0..5 correction: host tests, ASAN/UBSAN and seeded libFuzzer all PASS.
- AudioCore TSAN run [43305452073](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43305452073) completed SUCCESS.
- These runs cover code commit `f664d0ea`; current branch HEAD `7d893a72` includes that commit and subsequent documentation-only updates. HOST VERIFIED. Android ABI, target kernel, board topology and physical device remain UNVERIFIED.


### P0-A follow-up — application caller values and remaining evidence gaps

- Added [CD3313_HEQ_CALLER_AUDIT.md](analysis-reports/CD3313_HEQ_CALLER_AUDIT.md) from the recovered HEQ APK (`a7c6e116…`). It records concrete ASP caller values for treble, bass, loudness, subwoofer, four-channel balance and master-volume normalization, including the boot replay path.
- The recovered HEQ callsites use four balance values scaled to 0..15, loudness/subwoofer enable values normalized to 0/1, and nonuniform second arguments for treble/bass/subwoofer. Do not collapse those second arguments into one assumed range.
- `setInputGain()` and `setMasterVolumeMax()` were not found in the inspected `AspSettings` class. This is scoped to this APK slice, not proof of no other firmware callers.
- The `0x03` callback range analysis is now documented: vendor rejects channel -1 and 6, fails to bound values 7+, and writes through a seven-byte `channel_gain` array. The replacement restricts channels to 0..5 and gains to 0..3; tests cover channel 6 and 7 rejection.
- Remaining P0-A blockers are the full VDEX caller/callsite inventory, the exact policy for the legacy JNI extra word (compatibility adapter versus new bounded ABI), and the exact vendor kernel source/config for `__check_object_size`/hardened-usercopy. The connected Dropbox root and searches currently return no firmware/kernel source files; do not infer that the user's other Dropbox folders are empty.
- Keep the native transport detached. No direct `/dev/audiomux` client, ioctl, I2C write, GPIO change, module load or physical-device test has been implemented or performed.


### P0-B read-only evidence update — boot kernel, DTBO and FM routing

- Added [MT8163_BOOT_KERNEL_AND_DTBO_AUDIT.md](analysis-reports/MT8163_BOOT_KERNEL_AND_DTBO_AUDIT.md), based on firmware analysis run [43238133570](https://github.com/14vvp93/public_calculations/actions/runs/43238133570).
- Recovered the exact embedded boot-kernel config from `IKCFG_ST` / `IKCFG_ED`; boot and recovery configs are byte-identical. The kernel identifies as Linux `4.9.117+`, built by `publisher28@ubuntu128` with GCC 4.9.x on 2026-03-26.
- Confirmed `CONFIG_MACH_MT8163=y`, `CONFIG_HARDENED_USERCOPY=y`, `CONFIG_MODULES=y`, `CONFIG_MODVERSIONS=y`, `CONFIG_SND_SOC_MT8163=y` and `CONFIG_IKCONFIG=y`. The decompressed image contains `__check_object_size` and the hardened-usercopy diagnostic string.
- Clarified that hardened usercopy does not validate the logical length of the Java `int[]` passed to JNI. The extra-word over-read remains a real P0-A defect.
- The supplied DTBO declares `compatible = "hcn,cd3313"` in node `cd3313@44` with mute/sub GPIO properties. GPIO phandles and runtime overlay selection remain unverified.
- The recovered init script references `cd3313.ko`, `ak7604.ko` and `fy7604.ko` in that order, despite the shared `audiomux` name collision. Only runtime logs and device-node ownership can establish which driver actually registers.
- Factory policy/HAL evidence identifies FM I2S, FM capture and direct-control code paths, but does not identify the active route at runtime.
- P0-B remains open: no loaded-module list, boot/probe logs, ALSA runtime inventory, active DTBO confirmation or physical board inspection is available.
- This update is static/read-only firmware evidence only. No hardware I/O or device validation was performed.


### P0-A follow-up — firmware-wide VDEX caller leads

- Extended [CD3313_HEQ_CALLER_AUDIT.md](analysis-reports/CD3313_HEQ_CALLER_AUDIT.md) with evidence from `audiomux-userspace-client-scan` artifact [13954011424](https://github.com/14vvp93/public_calculations/actions/runs/43238133570).
- `system/framework/boot-carservices.vdex` contains `ExtAudioMuxer`, `DoAudioMux` and `SendCmdAudioMux` string-table entries. `system/AudioInputSource.vdex` contains the `ExtAudioMuxer` class descriptor.
- These are leads only. No method-level VDEX callsites, command IDs or actual argument arrays have been recovered from this artifact. The full Java/Binder caller inventory remains OPEN.
- Next action remains raw VDEX extraction/decompilation and caller-by-caller mapping. Do not implement the legacy JNI compatibility contract from string matches alone.


### P0-A code hygiene — input-gain range diagnostic

- Corrected the stale encoder exception text in `cd3313-emulator/src/protocol.cpp`: it previously said channel range `0..6`, while parser and typed builder correctly enforce `0..5`.
- Code commit: [cfc20a4](https://github.com/14vvp93/mt8163_audio_engine/commit/cfc20a4f495eeb38b5780bd89899e4ac56b47db9).
- The updated file was re-fetched from `integration/beta-0.1` and the corrected diagnostic was confirmed present.
- GitHub returned no combined status contexts for this commit at verification time; no test/CI pass is claimed for this one-line diagnostic change.


## 10. Hardware identity correction and firmware inventory — 2026-11-13

- The user explicitly confirmed that the physical head unit contains **CD3310EO only**. This is the authoritative physical-population fact for this device.
- The public firmware dump at `14vvp93/public_calculations/firmware/dumps/mt8163` contains a `kernel/cd3313.ko` binary (183,368 bytes; Git blob `5c63e6ef16f0b0e46a33eabc75fb6cd66f67afe7`). Binary content was not retrievable as UTF-8 through the GitHub connector, so no new disassembly or chip compatibility claim is made here.
- `init.mt8163.rc` lines 563–566 request `cd3313.ko`, `ak7604.ko`, `fy7604.ko` and `si475x.ko` during boot. `factory_init.rc` lines 530–537 also request CD3313/AK7604/FY7604 variants. These are init-script requests, not proof that all modules load successfully or that any of those ICs is physically populated.
- `ueventd.rc` line 139 assigns permissions to `/dev/audiomux`; this proves a configured device-node permission rule, not the live device-node owner, driver, or physical chip.
- The extracted `kernel/` directory currently exposes only `cd3313.ko`. The inspected `build.prop` has no CD3310/CD3310EO/audio-processor string match. These are scoped negative findings for the inspected files, not proof that no matching code exists elsewhere in the complete firmware.
- Initial textual searches in the indexed repositories returned no CD3310/CD3310EO hits. Binary modules, DTBs/DTBOs, proprietary libraries and compressed archives still require content-level inspection.
- **Revised immediate action:** inventory the complete firmware tree and extract/search all vendor modules and audio libraries; inspect CD3313 module strings, symbols, I2C IDs and probe path to determine whether its name is merely legacy or whether it actually supports CD3310EO. In parallel inspect all other codec/DSP modules and device-tree compatible strings. Do not infer compatibility from module filename.
- CD3313 protocol parser/emulator and its passing host CI remain software-only work. They are not CD3310EO protocol validation and must not be connected to the physical unit.
- No hardware writes, module loads, GPIO/mixer changes, flashing or physical-device validation were performed.


### P0 follow-up — CD3313 probe and false-positive module classification (2026-11-13)

- Downloaded and inspected the existing `firmware-library-analysis` workflow artifact from `14vvp93/public_calculations` (run `43238133570`; artifact SHA-256 `b5724b927f675fd32dc6f38339b0075bfb6e09d583a71ad46f7aef863e006942`).
- Corrected the initial `hxchipset-i2c.ko` lead: it is the Himax touchscreen driver (`Himax_common driver`, source under `drivers/input/touchscreen/HXCHIPSET-I2C`, HX83102 strings), not an audio processor. Exclude it from CD3310EO candidate drivers.
- The CD3313 vendor module is now statically identified more precisely: Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`, SHA-256 `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`, source path `drivers/misc/cd3313/cd3313.c`, I2C ID `cd3313`, OF compatible `hcn,cd3313`, and `CD3313_I2C_ADDR = 68` decimal (`0x44`).
- Recovered `dsp_probe` sends a one-byte `0x3f` I2C write and retries up to three times; the write helper calls `i2c_master_send(..., 1)`. A one-byte transfer success is sufficient for the visible probe path to continue registering `/dev/audiomux`. No explicit silicon-ID read is visible in that probe function. This does **not** prove that CD3310EO is compatible; it means the module's presence and probe success are not a reliable physical-chip identity check.
- The downloaded firmware-library artifact contains no literal `CD3310` or `CD3310EO` text. This is a scoped negative string-search result only.
- Updated detailed evidence in [CD3310EO_FIRMWARE_INVENTORY.md](analysis-reports/CD3310EO_FIRMWARE_INVENTORY.md). CD3313 protocol transport remains detached. Next work: recover CD3310EO datasheet/board-specific protocol evidence, then compare address `0x44` and probe sequence without issuing writes.
- No hardware I/O, module load, GPIO/mixer write or device validation performed.
