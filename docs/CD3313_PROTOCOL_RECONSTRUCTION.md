# CD3313 protocol reconstruction — static analysis

Source: `cd3313.ko` from this repository.
Latest detailed analysis artifact: GitHub Actions run [42849777588](https://github.com/14march93/mt8163_audio_engine/actions/runs/42849777588) (ARM disassembly, DWARF decoded lines, and function-to-source mapping completed successfully).
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

The raw bytes are preserved in the Actions artifact's `section-bytes.txt`. The volume, speaker, and equalizer consumers are now traced in [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md); the remaining compiler-generated lookup tables are decoded below. Their numeric values are driver indices, not verified acoustic dB values.



## Compiler-generated lookup tables decoded from call sites

### TAB.33841 — command-page cache slot map

The 16-byte table at .rodata + 0x54 is:

`00 00 00 00 05 05 06 07 03 03 04 04 01 01 02 02`

cd3313_write_reg() takes the high nibble of the outgoing command byte, indexes this table, then uses the result to select one of the eight reg_bufs cache entries. It is a software cache-slot map, not a physical silicon register-address map.

### CSWTCH.194 — input selector map

The table at .rodata + 0x64 consists of six 32-bit entries:

`2, 1, 0, 3, 0, 2`

set_input_channel() indexes it for channel IDs 0..5 and masks the result to two bits. Channel 6 bypasses the table and uses selector code 3. The resulting channel-to-selector map is therefore 0→2, 1→1, 2→0, 3→3, 4→0, 5→2, 6→3. This is a selector code only; it does not establish the human-facing source identity of any channel.

### cd3313_equalizer — corrected complete lookup

The exact 15-byte table at .rodata + 0x8C is:

`00 01 02 03 04 05 06 07 0E 0D 0C 0B 0A 09 08`

The bass/treble transition routine uses this array with an internal index range 3..10. The previously published emulator constant omitted the first four bytes and was incorrect; the emulator and transition tests have now been corrected to use the exact 15-byte array. The resulting internal indices 3..10 map to register nibbles 3, 4, 5, 6, 7, 14, 13, and 12 respectively.

## Additional facts confirmed by ARM disassembly

The latest corrected analysis run [42849777588](https://github.com/14march93/mt8163_audio_engine/actions/runs/42849777588) successfully disassembled the module as ARM (`elf32-littlearm`) and generated DWARF line tables plus function-to-source mappings. The following points are directly visible in the function bodies:

- `cmd_write()` parses the userspace write buffer as an unsigned decimal integer with `kstrtouint()`, truncates it to one byte, and sends that single byte with `i2c_master_send()`. It also updates a cached register byte selected using the high nibble. This path is a raw one-byte register write interface, not the framed multi-byte command protocol.
- `cd3313_write_reg()` sends exactly one byte per call using `i2c_master_send()`, then updates the driver's cached register image.
- `cmd_ioctl()` extracts the outer Linux ioctl direction from bits 30–31, request number from bits 8–15, and encoded size from bits 16–29. The write-direction path copies a user buffer into a kernel allocation and posts it to the driver's event queue. Encoded size is capped at 256 bytes before allocation/copy. `cmd_ioctl()` prepends the original ioctl request word to the queued event object. `event_callback()` reads that same word and extracts the command ID from bits 8–15 and size field from bits 16–29; it is not a second nested header.
- The read-direction path accepts ID `0xF0` and copies exactly 8 bytes from the start of `.rodata`: `63 64 33 33 31 33 00 00` (`cd3313\\0\\0`). This is a static identifier string, not a live DSP status structure or hardware version readback. Other read IDs return an error. The request ID is extracted from bits 8..15; the low `_IOC_NR` byte is not inspected by the recovered dispatcher.
- For write requests with command byte `0x55` or `0x5C`, the ioctl path waits 30 ms after posting the event. This is a driver timing behavior, not a payload semantic.
- `set_master_volume()` changes the hardware volume register incrementally, one step per I²C write, with a delay between steps. It uses the recovered 41-entry `cd3313_voltable_40` lookup and a separate percentage-derived adjustment on a 0–63 attenuation scale. The raw register mapping is reconstructed; its acoustic dB transfer function remains unverified.
- `set_master_mute()` manipulates a bit in a software mute mask and controls GPIO 393 (`0x189`) through `gpiod_set_raw_value()`. This indicates master mute includes an external GPIO/amplifier-enable path, not only an I²C register write.

The command buffer queued to the event worker starts with the original 32-bit ioctl request word, followed by the user payload copied by `cmd_ioctl()`. The worker reads the command ID and size field from that prepended word; arguments are 32-bit values at offsets +4, +8, +12, and +16. The precise meaning of every argument is documented with confidence labels in [CD3313_COMMAND_DISPATCH.md](CD3313_COMMAND_DISPATCH.md). The outer direction and size units are now established: direction 1 is write, direction 2 is read, and the size field counts 32-bit words (copied bytes = size × 4, capped at 256). The exact ioctl magic/type convention used by vendor callers remains to be correlated.

## Important implementation consequence

The driver is a command/control layer for the external CD3313 DSP over I²C. It is not itself the Android PCM audio-processing engine. Our architecture should keep these responsibilities separate:

1. **CD3313 hardware-control layer** — reproduce the confirmed command/status protocol and manage hardware state.
2. **Software DSP core** — process PCM audio independently (EQ, filters, dynamics, limiter, etc.).
3. **Android integration** — route system audio through the software DSP only after the AudioFlinger/HAL path has been mapped.

## Remaining reverse-engineering work

1. Recover the exact ioctl magic/type convention used by vendor callers. The request word layout, direction values, size units, and argument offsets are now established.
2. Confirm the acoustic/dB meaning of the recovered volume, speaker, and equalizer tables using an authoritative datasheet or hardware measurements.
3. Map each status command to the register writes performed by its handler.
4. Reconstruct channel numbering and channel-to-chip mapping.
5. Determine which commands are used by the vendor userspace service and audio HAL.
6. Add a host-side emulator and protocol unit tests before any device-side validation.

## Disassembly note

The initial x86 objdump attempt failed. The workflow now uses ARM cross-binutils and emits both instruction disassembly and DWARF source-line correlation. The latest run [42849777588](https://github.com/14march93/mt8163_audio_engine/actions/runs/42849777588) completed successfully.

### Potential bounds issue found in the original driver\n\nThe `0x15` balance handler reads four 32-bit values at offsets +4, +8, +12, and +16, but the recovered `cmd_len_list` minimum for `0x15` is only 2 words (8 copied payload bytes). If a caller supplies exactly the minimum, the handler appears to read beyond the copied payload. This is a static-analysis warning, not proof of an exploitable bug; callers may always pass a larger size. The emulator validates the actual copied payload length but does not emulate unsafe out-of-bounds reads.\n\n## Detailed command dispatcher map

A source-line-correlated reconstruction of the command handlers, observed 32-bit argument offsets, mute/volume behavior, and unresolved fields is maintained in [CD3313_COMMAND_DISPATCH.md](CD3313_COMMAND_DISPATCH.md). The corresponding analysis artifact is from workflow run [42799777270](https://github.com/14march93/mt8163_audio_engine/actions/runs/42799777270); all generated report commands completed with exit code 0.


## Follow-up: ioctl and event-queue ABI details

The focused analysis artifact from run [42878596077](https://github.com/14march93/mt8163_audio_engine/actions/runs/42878596077) provides a more precise ABI reconstruction and corrects the earlier description of the read path. Read ID 0xF0 returns the static eight-byte identifier "cd3313\\0\\0" from .rodata, not a dynamic status object. The request ID is read from bits 8..15; the low eight bits are not inspected by the recovered handler.

The imported event-queue declarations are present in DWARF, but their implementations are not linked into cd3313.ko. The driver calls post_event_delayed(handler, event_id, data, delay_ms) and remove_event(handler, event_id). Their prototypes and all observed scheduling requests are documented in [CD3313_IOCTL_ABI.md](CD3313_IOCTL_ABI.md). Cancellation/coalescing semantics and actual callback timing remain unknown.

That same analysis confirms a static length-validation concern: command 0x15 reads four 32-bit arguments although cmd_len_list permits a size-field value of 2. The emulator continues to require all four words and does not reproduce a possible out-of-bounds read. The allocation/copy path also deserves a hardened-usercopy audit because the command cache object is 256 bytes while the maximum copied payload is 256 bytes in addition to the prepended 4-byte header. This is an audit finding, not a confirmed exploitable vulnerability.


## Datasheet correlation update

The driver control-byte fields are now cross-checked against the manufacturer CD3313-AX-BJ-24 datasheet (revision 2012-01-B1): [PDF](https://datasheet4u.com/pdf/831243/CD3313.pdf). The 2-bit selector values correspond to chip input numbers 1..4; input 4 has no external pins. The 2-bit gain field represents 0, 3.75, 7.5, or 11.25 dB, and the preserved bit 2 is the active-low loudness control. This identifies chip input numbers but still does not label the board's physical sources as AUX, radio, Bluetooth, or phone. The complete mapping and decoded attenuation tables are recorded in [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md).
