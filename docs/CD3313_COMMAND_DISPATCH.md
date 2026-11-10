# CD3313 command dispatcher reconstruction

This document records behavior recovered from the ARM32 disassembly and DWARF line information in `cd3313.ko`. It is a static reconstruction, not a hardware-verified protocol specification.

## Evidence and build identity

- Module: `cd3313.ko`
- SHA-256: `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb`
- Kernel target: ARM32, Linux 4.9.117+
- Source path embedded in DWARF: `drivers/misc/cd3313/cd3313.c`
- Dispatcher: `do_cmd_write()`, inlined into `event_callback()`
- The latest detailed analysis workflow [42849777588](https://github.com/14march93/mt8163_audio_engine/actions/runs/42849777588) emits `dwarf-lines.txt`, `disassembly-lines.txt`, and `address-to-source.txt` to preserve source-line correlation.

The event object begins with the original 32-bit ioctl request word. The dispatcher extracts:
- direction from bits 30..31 (1 = write, 2 = read);
- command ID from bits 8..15;
- a 14-bit size field from bits 16..29.

For write requests, `cmd_ioctl()` copies `size_field * 4` bytes from userspace, capped at 256 bytes, then prepends the ioctl request word to the event object. The first command argument is therefore at offset +4, followed by 32-bit arguments at +8, +12, and +16. The size field counts 32-bit words for the copy operation; the 12 `cmd_len_list` values are minimum thresholds, not exact argument counts.

## Reconstructed command map

| ID | Reconstructed operation | Observed argument layout / behavior | Confidence |
|---|---|---|---|
| `0x01` | Update current channel selection state | One 32-bit value at +4. `-1` is converted to 6. The value is stored in driver state; under a state guard the handler calls `set_input_channel(8, 8)`. | Medium: behavior is clear; user-facing meaning of the value needs caller/source context. |
| `0x03` | Set input gain | `channel` at +4, `gain` at +8. The gain is masked to 2 bits and encoded into the channel-select register. | High |
| `0x04` | Set input mute | `channel` at +4, `mute` at +8, `reason` at +12. Updates per-channel mute state; when the channel is current, it also updates master mute/output attenuation state. | High |
| `0x10` | Set master volume step | One 32-bit value at +4; accepted range is 0..40. Calls the incremental master-volume routine. | High |
| `0x11` | Set master mute reason | `mute` at +4 and mute-reason/bit index at +8. Calls `set_master_mute(mute, reason)`; mute state is maintained as a bitmask. | High |
| `0x12` | Set master-volume percentage | One 32-bit value at +4; accepted range is 1..100. Stores the percentage and reapplies the current master-volume step. | High |
| `0x15` | Set four-channel balance/gain values | Four 32-bit values at +4/+8/+12/+16, corresponding to FL, FR, RL, RR. The inlined `set_balance(fl, fr, rl, rr)` converts each value into a separate register field. | High |
| `0x20` | Switch the active input path/mode | One value at +4. The false/zero path clears the mode and calls `set_input_channel(8, 8)`; the true/nonzero path sets the mode and calls `set_input_channel(1, 0)`. | Medium: calls and state transition are clear; names for the two modes are not. |
| `0x55` | Bass-boost adjustment | Two-word command accepted by the dispatcher. The second word at +8 is converted to a bounded 0..14 transition target; writes use the bass register prefix `0x60` and the driver's lookup table. The first word's purpose is not established from this optimized path. | Medium |
| `0x56` | Loudness enable/state | One value at +4 controls the driver's loudness state. The handler updates state value 0/4 and the loudness control bit in the register image. | High |
| `0x5C` | Treble adjustment | Two-word command accepted by the dispatcher. The second word at +8 is converted to a bounded 0..14 transition target; writes use the treble register prefix `0x70` and the driver's lookup table. The first word's purpose is not established from this optimized path. | Medium |
| `0xA3` | Subwoofer GPIO control | Boolean value at +4 is converted to 0/1 and written to GPIO 409 (`0x199`). This is a special dispatcher branch, not one of the 12 entries in `cmd_len_list`. | High |
| `0xF0` | Unresolved/special | The write dispatcher has a branch, but no meaningful state update was recovered. Do not treat it as a confirmed setter. | Low |
| `0xF1` | DSP power control | Boolean value at +4 selects wake-up or shutdown. Nonzero enters the wake-up path; zero calls `dsp_shutdown()`. | High |

### Four-channel control: Balance and Front/Rear Fader

The DWARF record identifies the inlined helper as `set_balance(fl, fr, rl, rr)` (source line 232). The correlated disassembly shows four separate argument-dependent register-field updates at source lines 237–265. The command handler obtains the four 32-bit arguments from offsets +4, +8, +12, and +16. This is direct evidence that the driver has a four-channel output-level control path, not merely a stereo L/R control.

This gives us the hardware-level basis for two independent UI controls:
- **Balance L/R:** apply a coordinated level difference between FL and FR, and the same difference between RL and RR.
- **Fader Front/Rear:** apply a coordinated level difference between the front pair (FL/FR) and rear pair (RL/RR).

The command's recovered driver minimum is only 2 words, despite the handler reading four 32-bit arguments. The DWARF global-variable record places `cd3313_speaker` at offset `0x7c`; the four inlined lookups load one byte from this 16-entry table using each argument as an index. The disassembly then writes four distinct register bytes: `0x80 | table[fl]`, `0xA0 | table[fr]`, `0xC0 | table[rl]`, and `0xE0 | table[rr]`. The table is `1F 1C 1A 18 16 14 12 10 0E 0C 0A 08 06 04 02 00`. Therefore each argument is an index in the range 0..15, not an arbitrary raw gain value. The host parser now distinguishes the driver's minimum from the safe argument size, requires all four words, and rejects indices outside 0..15. This also prevents reproducing the driver's unchecked table-index access. The manufacturer's CD3313 datasheet confirms the output attenuation encoding. The low five bits are `B1 B0 A2 A1 A0`; attenuation is `10 dB * B + 1.25 dB * A`, with `B=(code>>3)&3` and `A=code&7`. Applying this to the driver's table gives indices 0..15: `-38.75, -35, -32.5, -30, -27.5, -25, -22.5, -20, -17.5, -15, -12.5, -10, -7.5, -5, -2.5, 0 dB`. Thus index 15 is neutral (0 dB) and index 0 is maximum attenuation. The datasheet also confirms the channel command prefixes: FL=`0x80`, FR=`0xA0`, RL=`0xC0`, RR=`0xE0`.

The emulator now has a pure Balance/Fader mapper. Positive Balance moves the image right by attenuating FL/RL; negative Balance attenuates FR/RR. Positive Fader moves the image forward by attenuating RL/RR; negative Fader attenuates FL/FR. Each control spans 0..100% of the chip's 38.75 dB attenuation range. Contributions are summed per channel, clamped to the chip range, and quantized to the nearest one of the driver's 16 supported levels. Centered controls produce index 15 on all channels. The mapper and register-byte encoder are host-tested; they do not yet communicate with `/dev/audiomux`.

Still not proven: whether all four physical output pins are connected to speakers in this particular head unit, and whether the factory UI uses the same slider polarity/scaling. Our own UI mapping is explicit and deterministic.

### Notes on the mute model

`set_master_mute(mute, reason)` updates a reason bit in a shared mute mask. This is not a simple one-bit mute switch: independent reasons can keep the output muted until each reason is cleared. The helper also drives GPIO 393 (`0x189`), which is part of the external amplifier/mute path.

The input-mute command similarly maintains per-channel mute bits. When the affected channel is the currently selected channel, the handler recalculates the effective master mute state and output attenuation.

### Notes on volume

There are two distinct controls:
- `0x10`: a 0..40 master-volume step;
- `0x12`: a 1..100 percentage scaling value used when calculating the effective attenuation.

The volume routine changes the hardware register incrementally, one step per I²C write, with a delay between writes. It can temporarily assert and later release a dedicated master-mute reason while transitioning.

### Notes on bass and treble

The `0x55` and `0x5C` handlers use different register prefixes (`0x60` and `0x70`) and a shared lookup-table style conversion. They perform a stepped transition with approximately 20 ms sleeps between writes. The exact user-visible scale and the meaning of the first argument remain open questions.

## Internal event callback behavior

The outer `event_callback()` switch handles internal event types in addition to command IDs:
- event 0 initializes the CD3313 register image and writes the initialization sequence;
- event 2 clears master-mute reason 30;
- event 3 clears master-mute reason 24;
- event 1 dispatches the command word described above.

This confirms that the command path is asynchronous: the userspace-facing ioctl/write path queues work, and the event callback performs the actual state changes and I²C operations.

## Confirmed hardware transport

- I²C writes are one byte at a time through `i2c_master_send()`.
- The driver updates its cached register image after writes.
- The I²C client address constant is `0x44`.
- The misc device is `/dev/audiomux`.

## Open questions before implementing full emulator semantics

1. Recover the exact caller-side structs and ioctl encodings, including the unit of the size field. The supplied `files.zip` archive scan found no CD3313 userspace client; see [the scan report](CD3313_USERSPACE_CLIENT_SCAN.md).
2. Establish the user-facing meaning of `0x01` and the two modes selected by `0x20`.
3. Determine the first argument of `0x55` and `0x5C` from userspace call sites.
4. Decode the full `0xF0` behavior and any readback/status ABI.
5. Correlate every register prefix and lookup table with the CD3313 datasheet or verified bus traces.
6. Recover the four-channel level scale and verify the physical FL/FR/RL/RR speaker mapping before issuing `0x15` from a userspace controller.

Until these are resolved, the emulator must preserve unknown fields and mark unverified semantics explicitly rather than inventing behavior.

## Host-side typed command builders

The emulator now exposes safe typed builders for the commands whose payloads and ranges are recovered with high confidence:

- `make_master_volume_command(step)`: command `0x10`, one little-endian 32-bit word, range 0..40.
- `make_master_mute_command(mute, reason)`: command `0x11`, two little-endian 32-bit words; the mute value is 0/1 and the reason bit index is restricted to 0..31.
- `make_four_channel_level_command(fl, fr, rl, rr)`: command `0x15`, four little-endian 32-bit speaker-table indices in FL/FR/RL/RR order; each index is restricted to 0..15.

These builders create emulator command frames only. They do not open `/dev/audiomux`, issue ioctls, or write I²C. The command parser continues to reject the unsafe short `0x15` payload even though the original driver's minimum-length table accepts a smaller size.

The implementation and host tests passed in [CD3313 protocol emulator run 42850101047](https://github.com/14march93/mt8163_audio_engine/actions/runs/42850101047).
