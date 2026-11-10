# CD3313 protocol reconstruction — static analysis

Source: `cd3313.ko` from this repository.
Analysis artifact: GitHub Actions run [42798271937](https://github.com/14march93/mt8163_audio_engine/actions/runs/42798271937).
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

## Command payload lengths recovered from `cmd_len_list`

DWARF preserves the exact 96-byte constant array. It is a sequence of 12 pairs: command/status byte followed by expected payload length.

| Command | Payload length |
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

This table is especially useful for building a safe protocol encoder/parser. Do not assume that every identifier in the read/write enums is accepted by this particular write-length table.

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

## Recovered data objects

DWARF and ELF section layout identify:

- `cd3313_voltable_40`: 41-byte constant array, source line 124, `.rodata` offset 0x28.
- `cd3313_speaker`: 16-byte constant array, source line 176, `.rodata` offset 0x7C.
- `cd3313_equalizer`: 15-byte constant array, source line 195, `.rodata` offset 0x8C.
- `reg_bufs`: 8-entry array, source line 62.
- `channel_gain`: 7-entry array, source line 77.
- `channel_mute`, `master_mute`, `master_volume`, `volume_percentage`, `loudness_gain`, `bassboost_gain`, `master_channel`, and `cur_channel` are present as driver state.

The raw bytes are preserved in the Actions artifact's `section-bytes.txt`. Array semantics and units still need to be derived from the relevant function logic before exposing them as user-facing values.

## Important implementation consequence

The driver is a command/control layer for the external CD3313 DSP over I²C. It is not itself the Android PCM audio-processing engine. Our architecture should keep these responsibilities separate:

1. **CD3313 hardware-control layer** — reproduce the confirmed command/status protocol and manage hardware state.
2. **Software DSP core** — process PCM audio independently (EQ, filters, dynamics, limiter, etc.).
3. **Android integration** — route system audio through the software DSP only after the AudioFlinger/HAL path has been mapped.

## Remaining reverse-engineering work

1. Recover exact command packet layout and ioctl argument semantics from `cmd_ioctl()`, `cmd_ioctl_read_process()`, `do_cmd_write()`, and `cmd_write()`.
2. Decode the 41-byte volume curve and the speaker/equalizer arrays.
3. Map each status command to the register writes performed by its handler.
4. Reconstruct channel numbering and channel-to-chip mapping.
5. Determine which commands are used by the vendor userspace service and audio HAL.
6. Add a host-side emulator and protocol unit tests before any device-side validation.

## Disassembly caveat

The workflow generated ELF, symbols, relocations, DWARF, strings, and section-byte reports successfully. The `disassembly-relocations.txt` command returned exit code 1, so this report does not claim to contain a complete disassembly. A compatible ARM-capable objdump invocation/toolchain is still needed for that part.
