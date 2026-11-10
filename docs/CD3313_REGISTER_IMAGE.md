# CD3313 Register Image Emulator

## Scope

This document describes the host-side, evidence-bounded register-image model. It reconstructs byte writes visible in the supplied ARM32 `cd3313.ko` disassembly. It is not a complete CD3313 datasheet model, a hardware readback, or a userspace transport implementation.

The driver sends one-byte I2C command values through `cd3313_write_reg()`. Its cached image is selected by the command byte's upper nibble. The emulator mirrors this as a 16-entry command-page cache and leaves unobserved pages unset.

## Recovered probe and startup sequence

`dsp_probe()` performs a guard write of `0x3F`; the code retries up to three times if the I2C write fails. It then calls `set_master_mute(1, 31)`, activating startup mute reason bit 31.

The probe initializes eight cached command bytes:

| Order | Byte | Recovered interpretation |
|---:|---:|---|
| 1 | `0x3F` | Master-volume attenuation code at initial step 0 |
| 2 | `0xC0` | Rear-left output at neutral attenuation |
| 3 | `0xE0` | Rear-right output at neutral attenuation |
| 4 | `0x80` | Front-left output at neutral attenuation |
| 5 | `0xA0` | Front-right output at neutral attenuation |
| 6 | `0x47` | Control byte; bit 2 is the active-low loudness control (set = disabled, clear = enabled); other bits remain unresolved |
| 7 | `0x6F` | Bass-control byte; full scale semantics are not all resolved |
| 8 | `0x7F` | Treble-control byte; full scale semantics are not all resolved |

`event_callback(event=0)` sends these eight bytes twice in order, then sends `0x47` once more. That is 17 writes in the event, plus the earlier probe guard write. The host model exposes separate methods for the probe phase and the event-0 sequence.

The four output bytes `0x80`, `0xA0`, `0xC0`, and `0xE0` correspond to FL, FR, RL, and RR at speaker-table index 15 (0 dB / neutral attenuation).

## Input-control state recovered from the driver

The ARM32 DWARF records confirm seven-element `channel_gain` and `channel_mute` arrays, plus `is_mixing`, `hfp_active`, and the current-channel state. The initial current-channel value in the module's data image is 6.

- `0x01`: stores current input channel; argument `0xFFFFFFFF` (signed `-1`) maps to channel 6. Outside mixing mode, it applies the requested channel; during mixing it only remembers it.
- `0x03`: stores a two-bit gain value for channel 0..6. The second word's low two bits are the gain. The handler updates bits 3..4 of the cached control byte at offset +17. The command-length table requires three words, although the optimized handler reads only the first two.
- `0x04`: maintains per-channel mute-reason bitmasks. Arguments are channel, mute, reason. Channel `-1` maps to 6. For the active channel, a nonempty input-mute mask asserts master-mute reason 31; clearing the final reason clears it. Channel 1 / reason 16 also updates `hfp_active`.
- `0x20`: toggles `is_mixing`. Nonzero selects the channel-1 active path; zero restores the remembered current channel. The dispatcher minimum is three words, but only the first word is read by the optimized handler.

The host emulator models these state transitions and per-channel masks. The `set_input_channel()` disassembly and `.rodata` mapping recover the two-bit selector field in control-byte bits 0..1: channel IDs 0..6 map to codes `2, 1, 0, 3, 0, 2, 3`. Bits 3..4 carry the active channel's two-bit gain; bits 2 and 5..7 are preserved by this selector update. The emulator applies and journals the control-byte update when the startup control byte is known. The same routine sets raw GPIO output levels: GPIO 396 is low only for channel 2, and GPIO 398 is low only for channel 0. These are requested logic levels, not physical readback or source labels. The driver's attenuation/mute timing ramps are not simulated. Numeric channel IDs remain unlabeled; no AUX/radio/Bluetooth/phone identity is inferred.

## Master-volume table and transition

The ELF `.rodata` dump identifies `cd3313_voltable_40` at offset `0x28`, containing 41 bytes for steps 0..40:

```
3F 2A 26 25 24 23 22 21 20 1F
1E 1D 1C 1B 1A 19 18 17 16 15
14 13 12 11 10 0F 0E 0D 0C 0B
0A 09 08 07 06 05 04 03 02 01
00
```

The disassembly shows that the percentage control is clamped to 0..100 for the internal calculation, inverted, divided by three, and added to the table code. The final 6-bit target is capped at 63:

`target = min(63, table[step] + floor((100 - percentage) / 3))`

The public command parser accepts master-volume steps 0..40 and percentage values 1..100. The driver transitions from its current 6-bit attenuation code to the target one unit at a time, issuing one I2C byte per step with a delay between writes. The emulator reproduces the byte sequence and records each transition in the register journal; it does not emulate the driver's sleeps, GPIO timing, or transient hardware mute behavior.

The module's initialized `volume_percentage` data value is 100. The driver's zero-initialized master-volume step is 0; the probe guard byte is `0x3F`.

## Bass and treble transition table

The ELF `.rodata` symbol `cd3313_equalizer` begins at offset `0x8C` and contains these 15 bytes:

```
04 05 06 07 0E 0D 0C 0B 0A 09 08 00 00 00 00
```

For commands `0x55` (bass) and `0x5C` (treble), the second argument is decremented, clamped to 0..14, divided by two, and offset by three:

`target_index = floor(min(max(argument - 1, 0), 14) / 2) + 3`

The resulting transition index is 3..10. The first argument remains opaque because its meaning is not established. The current index is recovered from the current register byte's low nibble: values 0..7 map directly, while values 8..15 map to `22 - nibble`. Each transition step writes `0x60 | cd3313_equalizer[index]` for bass or `0x70 | cd3313_equalizer[index]` for treble, with a 20 ms delay in the driver. The emulator records the byte ramp when the previous register byte is known; otherwise it records only the final target byte.

The startup bytes `0x6F` and `0x7F` are preserved as the initial bass and treble register values. The table and transition mapping are reconstructed from the disassembly; the first command argument and higher-level UI scale remain unresolved.

## Loudness control bit

Command `0x56` updates bit 2 of the cached control byte at offset +17. A nonzero argument enables loudness by clearing bit 2; zero disables it by setting bit 2. From the recovered startup byte `0x47`, enabling produces `0x43` and disabling restores `0x47`. The handler emits no redundant I2C write when the requested state is already active. The emulator models this bit while leaving the remaining control-byte bits uninterpreted.

## Command-page cache and write journal

The cache has 16 optional entries. For each observed command byte, page = `byte >> 4`; the corresponding cache entry stores the most recent byte. It is deliberately called a command-page cache, not a physical register-address map.

Each register-journal entry contains:
- monotonic sequence number;
- source: probe guard, event-0 initialization, master-volume transition, four-channel command, or raw command byte;
- high-nibble page;
- command byte;
- previous cached byte, if any.

The separate raw `cmd_write()` interface is represented by `Emulator::write_raw_command_byte()`; it is not parsed as an ioctl frame. The four-channel `0x15` command updates the four speaker command bytes using the recovered prefixes and 16-entry attenuation table.

## Known limitations

- The full physical register-address map and all control-byte bit meanings are not reconstructed.
- Bass and treble lookup/transition behavior is only partially recovered; their first command arguments remain opaque in the state model.
- GPIO outputs are represented only where explicitly modeled as requested state; no physical GPIO is driven.
- I2C error handling, electrical timing, and actual chip readback are not simulated.
- No method in this emulator opens `/dev/audiomux` or performs hardware I/O.

## Evidence

- ARM32 module SHA-256: `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb`.
- Focused disassembly includes `dsp_probe`, `event_callback`, `set_master_volume.part.0`, and `cd3313_write_reg.constprop.4`.
- [Static analysis artifact workflow](https://github.com/14march93/mt8163_audio_engine/actions/runs/42852859151).
- [Host emulator tests](https://github.com/14march93/mt8163_audio_engine/actions/runs/42853276716).


## Input selector control field

`set_input_channel()` loads a six-entry 32-bit lookup table for channel IDs 0..5 and uses selector code `3` for channel 6. The recovered values are:

| Driver channel ID | Control-byte bits 1..0 |
|---:|---:|
| 0 | `2` (`10b`) |
| 1 | `1` (`01b`) |
| 2 | `0` (`00b`) |
| 3 | `3` (`11b`) |
| 4 | `0` (`00b`) |
| 5 | `2` (`10b`) |
| 6 / sentinel `-1` | `3` (`11b`) |

The routine constructs the control byte from the existing byte masked with `0xE4`, the active channel's gain shifted into bits 3..4, and the selector code in bits 0..1. Thus the update preserves loudness bit 2 and upper bits 5..7. The host model records the selected selector code and journals a changed control byte. This is a static driver reconstruction, not proof of the board-level source names or GPIO electrical polarity.


The input-routing helper also drives two GPIO outputs after writing the selector: GPIO 396 is set to `0` for channel 2 and `1` for all other channels; GPIO 398 is set to `0` for channel 0 and `1` for all other channels. The emulator records these raw output levels only. The signal names, board wiring, and active electrical polarity are not established by this disassembly.
