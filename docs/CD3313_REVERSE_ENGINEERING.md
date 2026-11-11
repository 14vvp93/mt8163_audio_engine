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

These are static-analysis findings, not confirmation of live hardware behavior. Since the initial notes, the ioctl bit layout, command payload offsets, command-length thresholds, event scheduling calls, and the principal lookup tables have been reconstructed. The native JNI ioctl request formula and caller library are now recovered. The extra-word discrepancy, imported event-queue internals, complete physical register map, acoustic dB curves, and hardware behavior remain open. See CD3313_IOCTL_ABI.md, CD3313_COMMAND_DISPATCH.md, and CD3313_REGISTER_IMAGE.md.

## Reverse-engineering work plan

### 1. Recover the userspace ABI — mostly reconstructed statically

- Confirmed ioctl direction, command-ID, and size-field bit positions; the size field counts 32-bit words on writes.
- Confirmed write framing: original 32-bit request word followed by the copied payload.
- Recovered the 12 command-length thresholds and the observed argument layouts for implemented commands.
- Recovered read ID 0xF0 as a static 8-byte cd3313 identifier response.
- Recovered: `libcarservices_jni.so` implements `nativeInit()` and `SendCmdAudioMux()`; write request is `0x40000000 | ((array_length + 1) << 16) | (command << 8)`, with `_IOC_NR=0`.
- Remaining: explain the native one-word over-read discrepancy and validate the read-only Binder/API probe on the matching head unit.

### 2. Recover register access — partial software map reconstructed

- Recovered one-byte I²C write behavior, initialization sequence, volume ramp, speaker output bytes, input control byte, bass/treble transition bytes, and mute-related GPIO requests.
- Decoded TAB.33841 as the 16-page-to-8-slot reg_bufs cache mapping.
- Remaining: full silicon register-address semantics, complete register-read path, and datasheet correlation.

### 3. Identify data tables — principal consumers decoded

- cd3313_voltable_40: 41-entry master-volume attenuation lookup.
- cd3313_speaker: 16-entry four-output attenuation lookup.
- cd3313_equalizer: exact 15-byte bass/treble register-code lookup.
- TAB.33841: command-page to reg_bufs cache-slot mapping.
- CSWTCH.194: six-entry input selector lookup for channels 0..5; channel 6 uses selector 3.

The raw indices and register codes are known from static analysis; acoustic dB meaning and physical signal-source labels remain unverified.

### 4. Reconstruct volume behavior — raw transfer logic recovered

- Master-volume range 0..40 and 41-entry lookup are recovered.
- Percentage adjustment is floor((100 - percentage) / 3), added to the table code and capped at 0x3F.
- The driver ramps the attenuation register incrementally.
- Remaining: convert raw codes to acoustic dB only with datasheet evidence or hardware measurements.

## Offline test strategy

The repository now contains a host-side CD3313 emulator with:

- A safe ioctl-frame parser and typed command builders.
- State and write journals, including both a diagnostic command-page view and the recovered eight-slot reg_bufs cache.
- Tests for command validation, exact lookup tables, volume transitions, mute, input routing, event requests, and startup/shutdown sequences.
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
