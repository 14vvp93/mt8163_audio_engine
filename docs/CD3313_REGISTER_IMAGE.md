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
| 6 | `0x47` | Control byte; full bitfield semantics are not all resolved |
| 7 | `0x6F` | Bass-control byte; full scale semantics are not all resolved |
| 8 | `0x7F` | Treble-control byte; full scale semantics are not all resolved |

`event_callback(event=0)` sends these eight bytes twice in order, then sends `0x47` once more. That is 17 writes in the event, plus the earlier probe guard write. The host model exposes separate methods for the probe phase and the event-0 sequence.

The four output bytes `0x80`, `0xA0`, `0xC0`, and `0xE0` correspond to FL, FR, RL, and RR at speaker-table index 15 (0 dB / neutral attenuation).

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
