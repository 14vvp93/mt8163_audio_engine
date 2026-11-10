# CD3313 Reverse Engineering Notes

Status: offline analysis only. No head unit is currently available, so all hardware behavior remains unverified.

## Known baseline

The repository contains `cd3313.ko` (ELF32 ARM module, 183,368 bytes). Earlier static analysis in this project established the following baseline; this document records those findings rather than repeating the initial ELF inspection:

- I²C driver name: `dsp_i2c_driver`.
- Register writes use an I²C send path (`i2c_master_send`).
- A Linux misc device named `audiomux` exposes read/write/ioctl operations.
- Previously identified symbols include `cd3313_write_reg`, `cd3313_voltable_40`, `cd3313_speaker`, and `cd3313_equalizer`.
- Other data tables observed include `TAB.33841` and `CSWTCH.194`.
- Earlier analysis also reported volume, mute, channel, bass/treble/loudness/subwoofer and callback-related functions.

These are static-analysis findings, not confirmation of live hardware behavior. Exact ioctl numbers, argument layouts, register addresses, bit fields, table meanings and the volume-to-dB transfer curve are still open questions.

## Reverse-engineering work plan

### 1. Recover the userspace ABI

- Enumerate misc-device file operations and ioctl dispatch.
- Recover ioctl command values and direction/size fields.
- Recover argument structures, integer widths, packing and pointer semantics.
- Document read/write behavior and error returns.
- Identify whether the existing userspace client uses `/dev/audiomux` and which commands it sends.

### 2. Recover register access

- Find every call site of `cd3313_write_reg` and any register-read helper.
- Build a register-write map: address, value source, initialization/runtime context.
- Separate confirmed register addresses from inferred ones.
- Record masks, shifts, enumerations and reset/default values only when supported by code evidence.

### 3. Identify data tables

Investigate the roles and consumers of:

- `cd3313_voltable_40`
- `cd3313_speaker`
- `cd3313_equalizer`
- `TAB.33841`
- `CSWTCH.194`

Do not assign semantic meaning from a symbol name alone. Trace each table's references and transformations.

### 4. Reconstruct volume behavior

- Trace the input/output volume setters to the table lookup and register write.
- Determine index range, clamping, mute/zero behavior and channel handling.
- Distinguish raw register codes from dB values.
- Keep any estimated dB curve explicitly marked as an estimate until confirmed by datasheet or measurement.

## Offline test strategy

Create a host-side CD3313 emulator with:

- A 256-byte register bank initially marked unknown.
- A write log containing timestamp, register, value and caller/action label.
- A mock `audiomux` command dispatcher once the ABI is recovered.
- Unit tests for argument validation, volume mapping, mute and channel operations.
- Golden traces based only on sequences proven by static code evidence.

The emulator must not silently invent register defaults. Unknown values remain unknown.

## Evidence labels

Use these labels in future notes and code:

- **CONFIRMED-STATIC** — directly supported by disassembly, symbols or source.
- **INFERRED** — best explanation of observed code, but not uniquely proven.
- **DATASHEET** — supported by an authoritative CD3313 datasheet.
- **HARDWARE-VERIFIED** — confirmed on the actual head unit. Not available yet.

## Safety boundary

Until hardware is available, do not write guessed register values to a real device. Keep the emulator and hardware backend separate, and require an explicit hardware-enabled build/configuration before any real I²C or misc-device writes.
