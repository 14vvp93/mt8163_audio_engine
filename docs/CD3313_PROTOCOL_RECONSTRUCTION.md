# CD3313 protocol reconstruction — static analysis

Source: `cd3313.ko` from this repository.
Analysis artifact: GitHub Actions run [42799116357](https://github.com/14march93/mt8163_audio_engine/actions/runs/42799116357) (ARM disassembly completed; all report commands exited 0).
Module SHA-256: `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb`.

## Confidence and scope

This report is derived from the module's ELF symbols, DWARF debug information, strings, and read-only data. The module is ELF32 ARM EABI5, built for Linux 4.9.117+ and is not stripped. DWARF identifies the original compilation unit as `drivers/misc/cd3313/cd3313.c`.

The values below are confirmed as constants or names embedded in this exact module. They are not yet a complete electrical/register map of the CD3313 silicon. Runtime behavior and undocumented hardware semantics still require validation.

## Confirmed driver identity

- I²C address constant: decimal 68 = `0x44` (`CD3313_I2C_ADDR`).
- I²C write primitive: `cd3313_write_reg()`, using `i2c_master_send()`.
- User-facing misc device: `/dev/audiomux`.
- Driver exposes read/write/ioctl paths: `cmd_read`, `cmd_write`, `cmd_ioctl`.
- Main command dispatcher: `do_cmd_write()`.
- Register initialization: `init_all_regs()`.
- Channel mapping: `channel_to_cd3313()`.
- Input controls: `set_input_gain()`, `set_input_mute()`, `set_input_channel()`.
- Output controls: `set_master_volume()`, `set_master_mute()`, `set_balance()`, `set_speak_vol()`.
- Tone/effect controls: `set_equalizer_bassboost()`, `set_equalizer_treble()`, `set_equalizer_loudness()`, `set_equalizer_subwoofer()`.

## Protocol status constants recovered from DWARF

These are protocol status/command identifiers in the driver, not Linux ioctl request numbers.

### Read/status identifiers

| Identifier | Decimal | Hex |
|---|---:|---:|
| STATUS_READ_CMD_INPUT_STATUS | 1 | 0x01 |
| STATUS_READ_CMD_OUT_STATUS | 2 | 0x02 |
| STATUS_READ_CMD_MASTER_VOLUME_STATUS | 16 | 0x10 |
| STATUS_READ_MUTE_STATUS | 17 | 0x11 |
| STATUS_READ_BALANCE_STATUS | 21 | 0x15 |
| STATUS_READ_MIXING_STATUS | 32 | 0x20 |
| STATUS_READ_EQ_BAND_STATUS | 48 | 0x30 |
| STATUS_READ_EQ_BASSBOOST_STATUS | 85 | 0x55 |
| STATUS_READ_EQ_LOUDNESS_STATUS | 86 | 0x56 |
| STATUS_READ_EQ_SURROUND_STATUS | 87 | 0x57 |
| STATUS_READ_CMD_EQ_COMPRESSOR_STATUS | 88 | 0x58 |
| STATUS_READ_CMD_EQ_HFE_STATUS | 89 | 0x59 |
| STATUS_READ_VERSION_STATUS | 240 | 0xF0 |

### Write/status identifiers

| Identifier | Decimal | Hex |
|---|---:|---:|
| STATUS_WRITE_CMD_INPUT_STATUS | 1 | 0x01 |
| STATUS_WRITE_CMD_OUT_STATUS | 2 | 0x02 |
| STATUS_WRITE_CMD_INPUT_GAIN_STATUS | 3 | 0x03 |
| STATUS_WRITE_CMD_INPUT_MUTE_STATUS | 4 | 0x04 |
| STATUS_WRITE_CMD_MASTER_VOLUME_STATUS | 16 | 0x10 |
| STATUS_WRITE_CMD_MASTER_MUTE_STATUS | 17 | 0x11 |
| STATUS_WRITE_CMD_VOLUME_PERCENTAGE_STATUS | 18 | 0x12 |
| STATUS_WRITE_CMD_BALANCE_STATUS | 21 | 0x15 |
| STATUS_WRITE_CMD_MIXING_STATUS | 32 | 0x20 |
| STATUS_WRITE_CMD_EQ_BAND_STATUS | 80 | 0x50 |
| STATUS_WRITE_CMD_EQ_BASSBOOST_STATUS | 85 | 0x55 |
| STATUS_WRITE_CMD_EQ_LOUDNESS_STATUS | 86 | 0x56 |
| STATUS_WRITE_CMD_EQ_SURROUND_STATUS | 87 | 0x57 |
| STATUS_WRITE_CMD_EQ_COMPRESSOR_STATUS | 88 | 0x58 |
| STATUS_WRITE_CMD_EQ_HFE_STATUS | 89 | 0x59 |
| STATUS_WRITE_CMD_EQ_ALTO_STATUS | 91 | 0x5B |
| STATUS_WRITE_CMD_EQ_TREBLE_STATUS | 92 | 0x5C |
| STATUS_WRITE_CMD_DELAY_STATUS | 96 | 0x60 |
| STATUS_WRITE_CMD_EQ_BAND_REG | 160 | 0xA0 |
| STATUS_WRITE_CMD_XOVER_LOW_REG | 161 | 0xA1 |
| STATUS_WRITE_CMD_XOVER_HIGH_REG | 162 | 0xA2 |
| STATUS_WRITE_CMD_SUBWOOFER_LPF_REG | 163 | 0xA3 |
| STATUS_WRITE_CMD_RECOVRY_STATUS | 240 | 0xF0 |
| STATUS_WRITE_CMD_WAKE_SLEEP_CONTROL_STATUS | 241 | 0xF1 |

## Minimum size-field thresholds recovered from `cmd_len_list`

DWARF preserves the exact 96-byte constant array. It is a sequence of 12 pairs: command ID followed by the minimum accepted value of the command header's 14-bit size field. These values are **not proven payload byte lengths**. The outer ioctl size field is multiplied by four to obtain the number of bytes copied from userspace. The dispatcher compares that same word-count field against these minimum thresholds.

| Command | Minimum size-field value |
|---:|---:|
| 0x01 | 1 |
| 0x03 | 3 |
| 0x04 | 3 |
| 0x10 | 1 |
| 0x11 | 2 |
| 0x12 | 2 |
| 0x15 | 2 |
| 0x20 | 3 |
| 0x55 | 2 |
| 0x5C | 2 |
| 0xF0 | 1 |
| 0xF1 | 1 |

This table is useful for validating the ioctl command word queued to the driver. Do not assume that every identifier in the read/write enums is accepted by this particular table. The dispatcher also contains special branches for `0x56` and `0xA3`, which are not present in these 12 pairs.

## Register readback decoding documented by the driver

The driver's diagnostic strings explicitly describe the following index masks:

| Index mask | Meaning |
|---|---|
| `00xx xxxx` | Master volume |
| `110x xxxx` | RL volume |
| `111x xxxx` | RR volume |
| `100x xxxx` | FL volume |
| `101x xxxx` | FR volume |
| `010x xxxx` | Channel |
| `0110 xxxx` | Bass gain |
| `0111 xxxx` | Treble gain |

These are confirmed software-side readback index classifications. They should not yet be treated as a full datasheet register address map.

## Recovered data objects and exact bytes

DWARF and ELF section layout identify these constant arrays. The byte sequences below are read directly from `.rodata` at the symbol offsets:

- `cd3313_voltable_40`: 41 bytes, source line 124, offset `0x28`:
  `3F 2A 26 25 24 23 22 21 20 1F 1E 1D 1C 1B 1A 19 18 17 16 15 14 13 12 11 10 0F 0E 0D 0C 0B 0A 09 08 07 06 05 04 03 02 01 00`
- `cd3313_speaker`: 16 bytes, source line 176, offset `0x7C`:
  `1F 1C 1A 18 16 14 12 10 0E 0C 0A 08 06 04 02 00`
- `cd3313_equalizer`: 15 bytes, source line 195, offset `0x8C`:
  `00 01 02 03 04 05 06 07 0E 0D 0C 0B 0A 09 08`
- `reg_bufs`: 8-entry array, source line 62.
- `channel_gain`: 7-entry array, source line 77.
- `channel_mute`, `master_mute`, `master_volume`, `volume_percentage`, `loudness_gain`, `bassboost_gain`, `master_channel`, and `cur_channel` are present as driver state.

The raw bytes are preserved in the Actions artifact's `section-bytes.txt`. Array semantics and units still need to be derived from the relevant function logic before exposing them as user-facing values.

## Additional facts confirmed by ARM disassembly

The corrected analysis run successfully disassembled the module as ARM (`elf32-littlearm`). The following points are directly visible in the function bodies:

- `cmd_write()` parses the userspace write buffer as an unsigned decimal integer with `kstrtouint()`, truncates it to one byte, and sends that single byte with `i2c_master_send()`. It also updates a cached register byte selected using the high nibble. This path is a raw one-byte register write interface, not the framed multi-byte command protocol.
- `cd3313_write_reg()` sends exactly one byte per call using `i2c_master_send()`, then updates the driver's cached register image.
- `cmd_ioctl()` extracts the outer Linux ioctl direction from bits 30–31, request number from bits 8–15, and encoded size from bits 16–29. The write-direction path copies a user buffer into a kernel allocation and posts it to the driver's event queue. Encoded size is capped at 256 bytes before allocation/copy. `cmd_ioctl()` prepends the original ioctl request word to the queued event object. `event_callback()` reads that same word and extracts the command ID from bits 8–15 and size field from bits 16–29; it is not a second nested header.
- The read-direction path accepts request number `0xF0` and copies an 8-byte object from driver state back to userspace. Other read request numbers return an error.
- For write requests with command byte `0x55` or `0x5C`, the ioctl path waits 30 ms after posting the event. This is a driver timing behavior, not a payload semantic.
- `set_master_volume()` changes the hardware volume register incrementally, one step per I²C write, with a delay between steps. It uses a 0–63 internal attenuation scale and applies a separate percentage-derived adjustment. The exact user-facing mapping still needs correlation with the constant volume table and call sites.
- `set_master_mute()` manipulates a bit in a software mute mask and controls GPIO 393 (`0x189`) through `gpiod_set_raw_value()`. This indicates master mute includes an external GPIO/amplifier-enable path, not only an I²C register write.

The command buffer queued to the event worker starts with the original 32-bit ioctl request word, followed by the user payload copied by `cmd_ioctl()`. The worker reads the command ID and size field from that prepended word; arguments are 32-bit values at offsets +4, +8, +12, and +16. The precise meaning of every argument is documented with confidence labels in [CD3313_COMMAND_DISPATCH.md](CD3313_COMMAND_DISPATCH.md). The outer direction and size units are now established: direction 1 is write, direction 2 is read, and the size field counts 32-bit words (copied bytes = size × 4, capped at 256). The exact ioctl magic/type convention used by vendor callers remains to be correlated.

## Important implementation consequence

The driver is a command/control layer for the external CD3313 DSP over I²C. It is not itself the Android PCM audio-processing engine. Our architecture should keep these responsibilities separate:

1. **CD3313 hardware-control layer** — reproduce the confirmed command/status protocol and manage hardware state.
2. **Software DSP core** — process PCM audio independently (EQ, filters, dynamics, limiter, etc.).
3. **Android integration** — route system audio through the software DSP only after the AudioFlinger/HAL path has been mapped.

## Remaining reverse-engineering work

1. Recover the exact ioctl magic/type convention used by vendor callers. The request word layout, direction values, size units, and argument offsets are now established.
2. Decode the 41-byte volume curve and the speaker/equalizer arrays.
3. Map each status command to the register writes performed by its handler.
4. Reconstruct channel numbering and channel-to-chip mapping.
5. Determine which commands are used by the vendor userspace service and audio HAL.
6. Add a host-side emulator and protocol unit tests before any device-side validation.

## Disassembly note

The initial x86 objdump attempt failed. The workflow was corrected to install multiarch binutils and run `objdump -m arm -drwC`. The latest run [42799116357](https://github.com/14march93/mt8163_audio_engine/actions/runs/42799116357) completed successfully; `disassembly-relocations.txt` is a valid ARM disassembly report and its command exit status is 0.

### Potential bounds issue found in the original driver\n\nThe `0x15` balance handler reads four 32-bit values at offsets +4, +8, +12, and +16, but the recovered `cmd_len_list` minimum for `0x15` is only 2 words (8 copied payload bytes). If a caller supplies exactly the minimum, the handler appears to read beyond the copied payload. This is a static-analysis warning, not proof of an exploitable bug; callers may always pass a larger size. The emulator validates the actual copied payload length but does not emulate unsafe out-of-bounds reads.\n\n## Detailed command dispatcher map

A source-line-correlated reconstruction of the command handlers, observed 32-bit argument offsets, mute/volume behavior, and unresolved fields is maintained in [CD3313_COMMAND_DISPATCH.md](CD3313_COMMAND_DISPATCH.md). The corresponding analysis artifact is from workflow run [42799777270](https://github.com/14march93/mt8163_audio_engine/actions/runs/42799777270); all generated report commands completed with exit code 0.
