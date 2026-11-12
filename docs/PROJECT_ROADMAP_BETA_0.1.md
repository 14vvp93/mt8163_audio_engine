# MT8163 Audio Engine — Canonical Project Roadmap (Beta 0.1)

**Baseline date:** 2026-11-12  
**Target:** MediaTek MT8163, Android 9 / API 28, ARM32 / EABI5, vendor Linux 4.9.117  
**Integration branch:** `integration/beta-0.1`  
**Last known integration commit before this roadmap:** `36f159e9c29dad537123315838b3c3cea3c7d4fa`  
**Status:** engineering platform in development; not a flashable or device-validated replacement.

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
- The physical DSP/module population and ownership of `/dev/audiomux` are not confirmed.
- CD3313 production transport is intentionally detached.
- No target-device route, I2C write, GPIO change, mixer write, acoustic measurement or end-to-end audio validation is claimed.
- No evidence currently justifies flashing a replacement.

## 3. Confirmed factory findings and risks

### CD3313 control path

Static analysis of the supplied factory firmware found:

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

### P0-A — CD3313 memory-safety and protocol audit

- [ ] Map every factory command ID to exact payload length, field offsets, signedness, valid ranges and response semantics.
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

Start with **P0-A: CD3313 memory-safety and protocol audit**. In parallel, continue read-only preparation for P0-B and inspect PCM lifecycle only where it supports the P1-A gate. Do not add new DSP features, enable hardware transport, modify factory binaries, or attempt flashing while the P0 gates remain open.

---

**Decision:** This roadmap is the agreed project route for MT8163 Audio Engine Beta 0.1. Any proposed deviation must be evaluated against safety, ABI compatibility, route correctness and measurable performance, and must not silently change the agreed priority order.
