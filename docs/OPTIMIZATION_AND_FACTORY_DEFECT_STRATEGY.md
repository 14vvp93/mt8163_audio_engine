# MT8163 Audio Engine — Optimization and Factory Defect Strategy

Date: 2026-11-12
Target: MT8163 / Android 9 / ARM32 / vendor Linux 4.9.117
Status: engineering strategy based on the supplied firmware snapshot and current host/Android CI evidence

## 1. Engineering decision

The product is one replacement audio implementation with one authoritative owner of stream, route, mute, volume, DSP and hardware-control state. The primary runtime artifact remains the factory-compatible `/vendor/lib/hw/audio.primary.mt8163.so`. AudioCore and DSP code are internal implementation units, not parallel runtime owners.

Factory binaries are evidence, not design authority. Preserve verified Android/framework/kernel/hardware contracts; correct unsafe memory handling, invalid state transitions, unreliable error reporting, and inefficient or duplicated ownership. Never copy a factory defect merely to imitate its behavior.

## 2. Priority order

### P0 — Close unsafe factory control ABI before any live hardware command

Static analysis of the supplied `cd3313.ko` and JNI bridge confirms:
- `SendCmdAudioMux(int, int[])` encodes one more 32-bit word than the Java array contains. The kernel copies the encoded size, creating a native over-read.
- The driver has insufficient payload-length checks for commands including `0x03`, `0x09`, `0x56`, and `0xA3`; command `0x15` reads four words but has a weaker length threshold.
- Speaker-level indices are not fully range-checked in the factory callback.
- ioctl success means asynchronous queue submission, not confirmed I2C completion.
- The read/status path reports cached software state, not physical CD3313 readback.
- The raw write path has file-position and maximum-length termination risks.

Actions:
1. Keep production CD3313 transport detached until the exact JNI/kernel frame contract is reconciled.
2. Retain the emulator's bounded parser and typed builders as the protocol reference; add boundary tests for every command and invalid frame.
3. Implement a replacement userspace bridge that allocates a complete initialized frame and encodes the exact byte/word count. Never reuse the factory extra-word read.
4. Implement a kernel replacement only after the exact vendor 4.9.117 source/config/toolchain and board DT are available. Validate lengths before allocation/copy or state mutation.
5. Separate requested state, last successfully transmitted state, and physical readback/unknown state. Never report queued work or cached state as hardware success.

No guessed ioctl, I2C, GPIO, or mixer writes during discovery.

### P0 — Establish actual board ownership and topology

The supplied firmware statically contains CD3313, AK7604 and FY7604 module variants that register the same `audiomux` misc-device name. This is a collision risk, not proof that all modules are active or that all three chips are populated.

Collect read-only target evidence before enabling endpoints or selecting a driver:
- loaded module list and boot/probe logs;
- actual owner of `/dev/audiomux`, device major/minor and process file descriptors;
- `/proc/asound/cards`, `/proc/asound/pcm`, PCM status and mixer controls;
- selected Audio Policy XML and active AudioPatch routes;
- FM direct-I2S versus FM capture ownership;
- HFP/SCO downlink, microphone uplink and echo-reference lifecycle;
- board/DT/DTBO evidence for DSP population, I2C and GPIO ownership.

### P1 — Correct the PCM/DSP topology before HAL integration

The recovered Normal playback path is:
`DC removal -> conditional stereo-to-mono fold -> MTK post-processing -> BLI SRC -> bit conversion -> ALSA write`.

For speaker-containing masks, the MTK callback receives duplicated mono samples, not independent stereo and not four-channel PCM. The callback is only directly reached from the Normal handler; it is not a global hook for FM, SCO, voice, HDMI or capture.

Therefore:
- Do not attach the current quad engine to this callback as a four-channel path.
- Recover an earlier stereo-preserving insertion point or redesign the first production DSP mode as a verified stereo path.
- Keep CD3313's four analog output attenuators separate from claims of four independent PCM/EQ channels.
- Preserve exact bit-transparent bypass for unsupported routes/formats and DSP failures.
- Keep the existing quad engine as a tested capability until a real four-channel PCM boundary is proven; do not delete working code based on an unverified topology assumption.

### P1 — Finish concurrency and lifecycle guarantees

The PCM backend uses fixed-capacity slots, atomic stream identity/state publication and non-blocking per-stream transfer admission. The broker owns lifecycle serialization and drains/rejects in-flight transfers before stop/close.

Required validation:
- stale callback after close/reopen;
- stop racing with playback write and capture read;
- close after XRUN/device loss;
- repeated open/configure/start/stop/close cycles;
- token wrap/reuse and slot exhaustion;
- TSAN plus deterministic barrier-controlled tests.

Keep lifecycle calls on one control context. The real-time callback must not allocate, block on a mutex, log, issue control ioctls, or perform I2C.

### P1 — Build a complete factory-compatible Android 9 HAL

Before connecting the engine:
- recover exact factory HAL entry points, structure sizes, symbol visibility, SONAME, ELF class/machine, dependencies and linker namespace assumptions;
- verify stream/device open-close, routing, standby, mode, volume, mute, input/capture and call paths;
- map every factory artifact and client that currently owns audio state;
- use exact factory output filename/path and preserve rollback images.

A successful ARMv7 static-library build is not a complete primary HAL build and is not target-device proof.

### P2 — Replace remaining factory ownership only after parity

Migrate source selection, mute arbitration, volume/tone controls, FM and vehicle events to the single AudioCore control owner. Keep FM direct I2S, FM capture, A2DP, HFP/SCO and microphone capture as distinct route classes. Keep vehicle MCU status commands separate from CD3313 audio mute commands.

Do not remove a factory service, library, module or policy file until its replacement owns all observed behavior, ABI compatibility is checked, and a tested rollback exists.

## 3. Performance rules

- Real-time processing: fixed-size preallocated buffers, bounded work per callback, no heap allocation, no blocking locks, no logging and no hardware-control calls.
- Control plane: serialize state transitions and hardware commands off the PCM callback; coalesce redundant state updates where semantics permit.
- PCM: use evidence-backed period/buffer sizes and format negotiation; do not hardcode guessed card/device numbers.
- DSP: benchmark latency, throughput, clipping/headroom, DC behavior, transition artifacts and long-run stability at actual target rates and block sizes.
- Prefer measurable end-to-end latency and glitch rate over synthetic throughput alone.
- Keep bypass fail-safe and bit-transparent when DSP is disabled or an unsupported condition is encountered.

## 4. Branch and CI discipline

- `integration/beta-0.1` is the sole integration destination.
- Promote reviewed work only; no blind sibling-branch merges.
- `main`, factory firmware binaries and recovery originals remain untouched during development.
- Use focused work branches only for isolated implementation tasks; virtual-audio branches remain test fixtures.
- Required CI tiers: focused unit tests; full host suite with ASAN/UBSAN and TSAN where applicable; Android ARM32 build; ELF/ABI checks; repository-integrity checks.
- Record host, Android-build and device evidence separately.

## 5. Beta 0.1 release gates

Beta is not build-ready until all are true:
1. Android 9 HAL ABI and factory artifact identity are verified.
2. A factory-named ARM32 primary HAL builds with correct SONAME, symbols and dependencies.
3. PCM lifecycle and concurrency tests pass, including deterministic race regressions.
4. The actual board PCM endpoints, formats, period sizes, mixer controls and route owners are documented from read-only device evidence.
5. The stereo/mono/multichannel topology is proven at the selected DSP boundary.
6. CD3313 transport framing is corrected and command validation is complete before any hardware backend is enabled.
7. Factory components remain recoverable and rollback is tested.
8. Physical-device validation covers normal media, route changes, mute/ACC, FM, Bluetooth A2DP, HFP/SCO, microphone and suspend/resume.

## 6. Current status and honest limits

- Host CI and Android ARMv7 static-library CI have passed for the PCM/DSP work described in the repository.
- The endpoint backend has no evidence-backed production card/device profile yet.
- The CD3313 production transport is intentionally not attached.
- The complete factory-named Android 9 primary HAL is not yet built.
- No target-device route, I2C write, GPIO change, mixer write or acoustic measurement is claimed.
- The current factory defect findings are static-analysis findings from the supplied firmware snapshot; their occurrence and physical impact on the live board remain to be measured.

This strategy deliberately puts memory safety, ownership correctness and route fidelity ahead of adding more DSP features.