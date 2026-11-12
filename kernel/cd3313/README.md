# CD3313 Driver Rewrite — Architecture and Safety Plan

## Status

Planning baseline for a clean-room replacement of the supplied ARM32 vendor module. This document is not an implementation and does not authorize hardware writes.

Target recovered from the paired firmware:
- SoC: MediaTek MT8163
- Kernel ABI: vendor Linux 4.9.117 family
- Architecture: ARM32 / EABI5
- I2C address: 0x44
- Existing userspace node: /dev/audiomux
- Existing module identifier: cd3313

The replacement must be built and validated against the exact target vendor kernel tree/configuration before it is considered installable.

## Goals

1. Replace unsafe kernel-side request handling with bounded, explicit parsing.
2. Preserve the observed userspace ABI where it can be implemented safely.
3. Preserve source switching, GPIO behavior, mute reasons, volume, balance, bass, treble, loudness, suspend/resume, and shutdown semantics.
4. Make I2C failures observable and keep software state consistent with confirmed hardware writes.
5. Make device lifetime, work cancellation, locking, and teardown deterministic.
6. Keep the original cd3313.ko untouched as a recovery image and behavioral reference.
7. Provide host-side tests for pure protocol/state logic and target-kernel build tests before device testing.

## Non-goals

- Do not change the CD3313 silicon register values without datasheet or measured evidence.
- Do not guess undocumented register addresses, bits, or board wiring.
- Do not change the MTK Audio HAL or PCM DSP as part of the kernel-driver rewrite.
- Do not claim hardware readback where the chip/driver only exposes a software cache.
- Do not issue experimental I2C/GPIO writes during static analysis.

## Compatibility contract

The recovered vendor interface uses /dev/audiomux and ioctl request words with:
- direction in bits 30..31;
- encoded word count in bits 16..29;
- command ID in bits 8..15;
- low request-number byte not inspected by the recovered dispatcher;
- write payload represented as 32-bit words.

The new implementation should retain the observed request layout and command IDs for known clients. It must parse the encoded length before accessing any payload and require the full command-specific argument count. Extra trailing words may be ignored only where compatibility evidence supports that behavior.

Known legacy command IDs include:
- 0x01 input channel;
- 0x03 input gain;
- 0x04 input mute;
- 0x10 master volume;
- 0x11 master mute reason;
- 0x12 volume percentage;
- 0x15 four output levels;
- 0x20 mixing mode;
- 0x55 bass;
- 0x56 loudness;
- 0x5C treble;
- 0xA3 subwoofer GPIO;
- 0xF0 static chip identifier query;
- 0xF1 DSP shutdown/wakeup.

The exact required argument counts and value ranges must live in one command-descriptor table and be covered by tests. Do not copy the legacy driver's weaker minimum-length checks.

### JNI framing caveat

The recovered vendor JNI constructs the ioctl word using (java_array_length + 1) words while passing a pointer to an array containing java_array_length integers. The kernel then copies the encoded byte count. This is a statically confirmed userspace over-read discrepancy. The kernel rewrite cannot repair the JNI caller's out-of-bounds read. A compatible replacement userspace bridge must allocate and initialize a correctly sized frame, and the legacy JNI path must be treated as a separate compatibility risk until runtime behavior is understood.

## Static issues that must be closed

See [docs/CD3313_DRIVER_RISK_AUDIT.md](../../docs/CD3313_DRIVER_RISK_AUDIT.md) and the central [factory defect register](../../docs/FACTORY_DEFECTS.md). At minimum:
- ioctl object allocation/copy size mismatch;
- unchecked short payloads for 0x56 and 0xA3;
- 0x15 reads four words despite a weaker length threshold;
- 0x15 speaker-table indices are not fully range-checked;
- raw write parser may lack NUL termination;
- raw write file-position handling makes repeated writes unreliable;
- raw write ignores I2C transfer status and updates cache on failure;
- ioctl reports queue submission rather than command completion;
- status output reports cached state, not physical chip readback.

## Proposed internal architecture

Separate pure logic from kernel I/O:

1. ABI layer — decode ioctl direction/command/word count; validate exact frame bounds; implement compat ioctl if required by the 32-bit userspace/kernel combination.
2. Command layer — table-driven command descriptors, argument validation, and conversion to typed operations.
3. State layer — source, gain, mute-reason masks, volume targets, output attenuation, tone controls, suspend state.
4. Transport layer — one serialized I2C write path with explicit return checking and cache-validity tracking.
5. Sequencer — serialized multi-step transitions (input switch, volume ramp, startup, shutdown) using a worker appropriate to the target kernel.
6. GPIO layer — descriptor-based GPIO acquisition and explicit polarity documentation; preserve recovered pin assignments only after matching them to target DT/platform data.
7. Lifecycle layer — probe/remove, suspend/resume, open-file lifetime, cancellation and resource teardown.

Do not choose mutex/spinlock/workqueue placement until the exact kernel API and callback context are verified. I2C operations may sleep and must never execute under a spinlock or in atomic context.

## Error and state rules

- Invalid request: return -EINVAL before changing state or touching hardware.
- Missing/short payload: return -EINVAL; never consume stale slab memory.
- Unsupported command: return -ENOTTY or the legacy-compatible error established by caller analysis.
- I2C short write or negative result: return/record an error; do not mark the register cache as successfully updated.
- Cache entries need an explicit valid/unknown state.
- Multi-write operations must stop on first transport error and leave state marked uncertain unless a safe rollback is proven.
- Queue submission success must not be confused with hardware-command completion. Define an observable completion/error mechanism without breaking existing clients.
- Never expose a cached register image as physical readback.

## Loudness investigation

The recovered control byte has active-low loudness bit 2: 0x43 is the modeled enabled value and 0x47 is the modeled disabled startup value. This confirms the control bit, not the acoustic loudness curve.

Before altering loudness behavior:
1. correlate the exact CD3313 datasheet revision and all loudness-related controls;
2. trace every writer of the control byte and startup sequence;
3. inspect the paired module's gain, bass, and initialization state;
4. determine whether any userspace service rewrites tone settings after probe;
5. measure a repeatable baseline on hardware with loudness off/on;
6. change one variable at a time and preserve a rollback path.

Do not invent additional register writes or claim that the bloat/bass symptom is caused by the loudness bit alone.

## Test strategy

### Host tests
- ioctl header decoding and word-count overflow/bounds;
- exact command argument counts and value ranges;
- all known command IDs and unknown IDs;
- no state mutation after invalid input;
- speaker table indices 0..15 and rejection outside range;
- mute-reason mask transitions;
- source selector and GPIO request mapping;
- I2C success, short-write, and error injection through a fake transport;
- cache validity after failed writes;
- cancellation and ordering model for multi-step operations;
- concurrent command/state access where the chosen design permits it.

### Kernel build/static checks
- build against the exact vendor 4.9.117 kernel tree and config;
- ARM32 toolchain and vermagic/module ABI checks;
- sparse/smatch or equivalent checks where available;
- inspect exported symbols, module dependencies, device registration and ioctl table;
- compare command traces with the reference emulator.

### Device validation gate
- first boot with a recovery route and original module available;
- verify probe, device node, chip-ID query, and no regressions in source switching;
- test each command individually at conservative values;
- test suspend/resume and shutdown;
- test Loudness on/off with controlled volume and repeatable audio material;
- collect kernel logs and audio measurements;
- only then consider replacing the stock module in the normal installation package.

## Release and rollback

The eventual installer must:
- identify exact board/kernel/module compatibility;
- save the original module and related configuration with hashes;
- refuse installation on an unknown kernel/build;
- install the replacement atomically where possible;
- preserve a recovery path if module load or device probe fails;
- support explicit restore/uninstall;
- never overwrite the original backup.

## Open blockers

- Exact vendor kernel source tree and config are not yet in the repository context for this rewrite.
- Exact board device-tree/platform GPIO configuration must be confirmed.
- Event-queue helper implementation from the vendor kernel is not recovered.
- Legacy JNI's extra-word read needs a safe userspace replacement or further runtime evidence.
- Physical I2C behavior, GPIO polarity, and acoustic Loudness behavior require target-device validation.

## Current implementation status

No replacement kernel source, module, or hardware behavior is claimed yet. The next engineering step is to inventory the repository's recovered artifacts and establish a reproducible ARM32 kernel-module build environment before implementing the ABI and transport layers.
