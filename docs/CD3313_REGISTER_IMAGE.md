# CD3313 Register Image Emulator

## Scope

This document describes the host-side, evidence-bounded register-image model. It reconstructs byte writes visible in the supplied ARM32 `cd3313.ko` disassembly. It is not a complete CD3313 datasheet model, a hardware readback, or a userspace transport implementation.

The driver sends one-byte I2C command values through `cd3313_write_reg()`. It selects one of eight reg_bufs cache slots using the command byte's upper nibble and the recovered TAB.33841 lookup. The emulator keeps a separate 16-entry diagnostic command-page view plus an exact 8-slot reg_bufs_cache mirror.

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

The four output bytes `0x80`, `0xA0`, `0xC0`, and `0xE0` correspond to FL, FR, RL, and RR at speaker-table index 15 (lookup code `0x00`, 0 dB per the datasheet). The full 16-index attenuation range is -38.75 dB to 0 dB, in the table order: -38.75, -35, -32.5, -30, -27.5, -25, -22.5, -20, -17.5, -15, -12.5, -10, -7.5, -5, -2.5, 0 dB.

## Input-control state recovered from the driver

The ARM32 DWARF records confirm seven-element `channel_gain` and `channel_mute` arrays, plus `is_mixing`, `hfp_active`, and the current-channel state. The initial current-channel value in the module's data image is 6.

- `0x01`: stores current input channel; argument `0xFFFFFFFF` (signed `-1`) maps to channel 6. Outside mixing mode, it applies the requested channel; during mixing it only remembers it.
- `0x03`: stores a two-bit gain value for channel 0..6. The second word's low two bits are the gain. The handler updates bits 3..4 of the cached control byte at offset +17. The command-length table requires three words, although the optimized handler reads only the first two.
- `0x04`: maintains per-channel mute-reason bitmasks. Arguments are channel, mute, reason. Channel `-1` maps to 6. For the active channel, a nonempty input-mute mask asserts master-mute reason 31; clearing the final reason clears it. Channel 1 / reason 16 also updates `hfp_active`.
- `0x20`: toggles `is_mixing`. Nonzero selects the channel-1 active path; zero restores the remembered current channel. The dispatcher minimum is three words, but only the first word is read by the optimized handler.

The CD3313-AX-BJ-24 datasheet identifies the control byte as 010 G1 G0 S2 S1 S0: G1G0 (bits 4..3) select input gain 0, 3.75, 7.5, or 11.25 dB; S2 (bit 2) enables loudness when 0 and disables it when 1; S1S0 (bits 1..0) select chip input 1..4. The datasheet specifies three external stereo inputs; source 4 has no external pins. Therefore the driver's selector map resolves to chip source numbers, without assigning application labels:

| Driver channel | Selector code | CD3313 input |
|---:|---:|---|
| 0 | 2 | Source 3 |
| 1 | 1 | Source 2 |
| 2 | 0 | Source 1 |
| 3 | 3 | Source 4 (no external pins) |
| 4 | 0 | Source 1 |
| 5 | 2 | Source 3 |
| 6 | 3 | Source 4 (no external pins) |

The startup control byte 0x47 therefore selects source 4, input gain 0 dB, and loudness disabled. set_input_channel() changes S1S0 and G1G0 while preserving S2; command 0x56 controls S2. This is chip-level source numbering only: the actual AUX/radio/Bluetooth/phone wiring is not established.

The host emulator models these state transitions and per-channel masks. The `set_input_channel()` disassembly and `.rodata` mapping recover the two-bit selector field in control-byte bits 0..1: channel IDs 0..6 map to codes `2, 1, 0, 3, 0, 2, 3`. Bits 3..4 carry the active channel's two-bit gain; bits 2 and 5..7 are preserved by this selector update. The emulator applies and journals the control-byte update when the startup control byte is known. The same routine sets raw GPIO output levels: GPIO 396 is low only for channel 2, and GPIO 398 is low only for channel 0. These are requested logic levels, not physical readback or source labels. The attenuation/mute sequence is modeled in the Input-switch transition timeline below, with nominal delays rather than measured hardware timing. Numeric driver channels are now mapped to CD3313 chip input numbers 1..4, but application-level source names such as AUX/radio/Bluetooth/phone remain unassigned.

## Master-volume table and transition

The ELF `.rodata` dump identifies `cd3313_voltable_40` at offset `0x28`, containing 41 bytes for steps 0..40:

```
3F 2A 26 25 24 23 22 21 20 1F
1E 1D 1C 1B 1A 19 18 17 16 15
14 13 12 11 10 0F 0E 0D 0C 0B
0A 09 08 07 06 05 04 03 02 01
00
```

The disassembly shows that the percentage control is clamped to 0..100 for the internal calculation, inverted, divided by three, and added to the table code. The final 6-bit target is capped at 63. The manufacturer datasheet decodes the base volume byte as B2..B0 = 10 dB steps and A2..A0 = 1.25 dB steps of attenuation; therefore code 0x3F is -78.75 dB and code 0x00 is 0 dB. The 41 base table entries correspond to these attenuation values in dB (steps 0..40):

`-78.75, -52.5, -47.5, -46.25, -45, -43.75, -42.5, -41.25, -40, -38.75,`
`-37.5, -36.25, -35, -33.75, -32.5, -31.25, -30, -28.75, -27.5, -26.25,`
`-25, -23.75, -22.5, -21.25, -20, -18.75, -17.5, -16.25, -15, -13.75,`
`-12.5, -11.25, -10, -8.75, -7.5, -6.25, -5, -3.75, -2.5, -1.25, 0 dB.`

`target = min(63, table[step] + floor((100 - percentage) / 3))`

The public command parser accepts master-volume steps 0..40 and percentage values 1..100. The driver transitions from its current 6-bit attenuation code to the target one unit at a time, issuing one I2C byte per step with a delay between writes. The emulator reproduces the byte sequence and records each transition in the register journal; it does not emulate the driver's sleeps, GPIO timing, or transient hardware mute behavior.

The module's initialized `volume_percentage` data value is 100. The driver's zero-initialized master-volume step is 0; the probe guard byte is `0x3F` (-78.75 dB per the datasheet encoding). The percentage overlay is added in raw control-code space, so its exact acoustic dB effect is not a simple linear percentage.

## Bass and treble transition table

The ELF `.rodata` symbol `cd3313_equalizer` begins at offset `0x8C` and contains these 15 bytes:

```
00 01 02 03 04 05 06 07 0E 0D 0C 0B 0A 09 08
```

For commands `0x55` (bass) and `0x5C` (treble), the second argument is decremented, clamped to 0..14, divided by two, and offset by three:

`target_index = floor(min(max(argument - 1, 0), 14) / 2) + 3`

The resulting transition index is 3..10. The first argument remains opaque because its meaning is not established. The manufacturer datasheet specifies 2 dB per bass/treble code step; the exact driver-used lookup entries at indices 3..10 decode to -8, -6, -4, -2, 0, +2, +4, and +6 dB respectively. The current index is recovered from the current register byte's low nibble: values 0..7 map directly, while values 8..15 map to `22 - nibble`. Each transition step writes `0x60 | cd3313_equalizer[index]` for bass or `0x70 | cd3313_equalizer[index]` for treble, with a 20 ms delay in the driver. The emulator records the byte ramp when the previous register byte is known; otherwise it records only the final target byte.

The startup bytes `0x6F` and `0x7F` are preserved as the initial bass and treble register values. The table and transition mapping are reconstructed from the disassembly; the first command argument and higher-level UI scale remain unresolved.

## Loudness control bit

Command `0x56` updates bit 2 of the cached control byte at offset +17. A nonzero argument enables loudness by clearing bit 2; zero disables it by setting bit 2. From the recovered startup byte `0x47`, enabling produces `0x43` and disabling restores `0x47`. The handler emits no redundant I2C write when the requested state is already active. The emulator models this bit while leaving the remaining control-byte bits uninterpreted.

## Command-page cache and write journal

The emulator exposes two views of each write. command_page_cache has 16 optional entries indexed by byte >> 4 for diagnostics; it is not the driver's internal cache. reg_bufs_cache has eight optional entries and mirrors the driver's actual reg_bufs array using TAB.33841. The write journal records both the command page and the selected cache slot, including the previous value in each view.

Each register-journal entry contains:
- monotonic sequence number;
- source: probe guard, event-0 initialization, master-volume transition, four-channel command, or raw command byte;
- high-nibble page;
- TAB.33841-selected reg_bufs cache slot;
- command byte;
- previous page-view byte and previous reg_bufs slot byte, when present.

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

## Input-switch transition timeline

The host emulator now records the ordered operations performed by set_input_channel():

- master attenuation ramp to 0x3F, using three-code increments;
- terminal 0x3F write and caller-specified delay;
- input selector/control-byte write;
- raw GPIO 396 and GPIO 398 output requests;
- master mute reason-31 update;
- post-mute delay;
- either a muted hold at 0x3F, or a three-code restoration ramp to the calculated master-volume target.

The normal selector path uses an 8 ms delay; the mixing-enable path selects channel 1 with delay 0. The model stores current_volume_register_value independently from the desired master-volume target. input_transition_journal() exposes the ordered operations, including explicit delay markers. These values are a static control-flow reconstruction and nominal requested delays, not hardware measurements.

## Master mute and GPIO 393 reconstruction

The ARM32 `set_master_mute(mute, reason)` function first updates the 32-bit master mute reason mask and then writes GPIO 393 using this recovered raw-level expression:

`(master_mute_reason_mask != 0 && !hfp_active) || reason24_is_set || !acc_on`

Any active mute reason asserts GPIO 393 while HFP is inactive. HFP suppresses ordinary reasons, but reason 24 is explicitly ORed back in and therefore still forces GPIO 393 high during HFP. ACC-off also forces the raw GPIO high. This is distinct from `master_muted()`, which reports whether any reason bit is set without applying HFP/ACC output gating.

The event callback's `0x11` handler special-cases reason 2 by assigning `acc_on = (mute == 0)` before calling `set_master_mute()`: mute=0 means ACC active; mute=1 means ACC off. Channel 1 / input-mute reason 16 similarly sets `hfp_active = (mute == 0)`. The probe path initializes `acc_on` true before asserting startup reason 31. System notifier event 2 clears reason 30; event 3 clears reason 24. The host emulator models these state transitions and records GPIO 393 raw-level requests as `Gpio393Write` journal entries.

GPIO 393's physical signal name and board-level active polarity remain unknown. The emulator records raw HIGH/LOW requests only; it does not drive hardware.


## Recovered software lookup tables

The focused .rodata analysis also resolves two previously unnamed/compiler-generated lookup tables:

### TAB.33841 — command-page cache slot mapping

The 16-byte table at .rodata + 0x54 is:

00 00 00 00 05 05 06 07 03 03 04 04 01 01 02 02

cd3313_write_reg() selects an entry using the command byte's upper nibble, then uses the table value as the index into the driver's eight-entry reg_bufs cache. This is a software cache-slot mapping, not a physical CD3313 register address table.

| Command byte high nibble | reg_bufs slot |
|---:|---:|
| 0x0 | 0 |
| 0x1 | 0 |
| 0x2 | 0 |
| 0x3 | 0 |
| 0x4 | 5 |
| 0x5 | 5 |
| 0x6 | 6 |
| 0x7 | 7 |
| 0x8 | 3 |
| 0x9 | 3 |
| 0xA | 4 |
| 0xB | 4 |
| 0xC | 1 |
| 0xD | 1 |
| 0xE | 2 |
| 0xF | 2 |

### CSWTCH.194 — input selector mapping

The six 32-bit entries at .rodata + 0x64 are:

2, 1, 0, 3, 0, 2

set_input_channel() indexes this table for channel IDs 0..5 and masks the result to two bits. Channel 6 bypasses the table and uses selector code 3. This yields the complete mapping:

| Driver channel | Driver label | Selector bits 1..0 | CD3313 input |
|---:|---|---:|---|
| 0 | RADIO | 2 | Source 3 |
| 1 | ARM | 1 | Source 2 |
| 2 | AUX | 0 | Source 1 |
| 3 | EXBT | 3 | Source 4 (no external pins) |
| 4 | AUX2 | 0 | Source 1 |
| 5 | DAB | 2 | Source 3 |
| 6 / sentinel -1 | NONE | 3 | Source 4 (no external pins) |

The driver labels are recovered from the channel-name pointer table used by cmd_read(). They identify software channel names, but do not prove the exact board wiring. In particular, EXBT maps to the datasheet's source-4 code, which has no external pins; its actual route may be outside the CD3313 analog input path.


## Datasheet correlation

The silicon-level bit definitions and dB values above are cross-checked against the manufacturer document **CD3313-AX-BJ-24, revision 2012-01-B1**: [CD3313 datasheet (PDF)](https://datasheet4u.com/pdf/831243/CD3313.pdf). The PDF identifies Wuxi I-CORE Electronics Co., Ltd. as the manufacturer. This datasheet confirms the chip's input selector/gain/loudness fields, four-channel output attenuation encoding, and 2 dB bass/treble steps. It does not prove the physical wiring of this particular head unit.
