# CD3313 command dispatcher reconstruction

This document records behavior recovered from the ARM32 disassembly and DWARF line information in `cd3313.ko`. It is a static reconstruction, not a hardware-verified protocol specification.

## Evidence and build identity

- Module: `cd3313.ko`
- SHA-256: `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb`
- Kernel target: ARM32, Linux 4.9.117+
- Source path embedded in DWARF: `drivers/misc/cd3313/cd3313.c`
- Dispatcher: `do_cmd_write()`, inlined into `event_callback()`
- The latest analysis workflow emits `dwarf-lines.txt`, `disassembly-lines.txt`, and `address-to-source.txt` to preserve source-line correlation.

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
| `0x15` | Set four-channel balance/gain values | Four 32-bit values at +4/+8/+12/+16, corresponding to FL, FR, RL, RR. Each is converted into the corresponding register field. | High |
| `0x20` | Switch the active input path/mode | One value at +4. The false/zero path clears the mode and calls `set_input_channel(8, 8)`; the true/nonzero path sets the mode and calls `set_input_channel(1, 0)`. | Medium: calls and state transition are clear; names for the two modes are not. |
| `0x55` | Bass-boost adjustment | Two-word command accepted by the dispatcher. The second word at +8 is converted to a bounded 0..14 transition target; writes use the bass register prefix `0x60` and the driver's lookup table. The first word's purpose is not established from this optimized path. | Medium |
| `0x56` | Loudness enable/state | One value at +4 controls the driver's loudness state. The handler updates state value 0/4 and the loudness control bit in the register image. | High |
| `0x5C` | Treble adjustment | Two-word command accepted by the dispatcher. The second word at +8 is converted to a bounded 0..14 transition target; writes use the treble register prefix `0x70` and the driver's lookup table. The first word's purpose is not established from this optimized path. | Medium |
| `0xA3` | Subwoofer GPIO control | Boolean value at +4 is converted to 0/1 and written to GPIO 409 (`0x199`). This is a special dispatcher branch, not one of the 12 entries in `cmd_len_list`. | High |
| `0xF0` | Unresolved/special | The write dispatcher has a branch, but no meaningful state update was recovered. Do not treat it as a confirmed setter. | Low |
| `0xF1` | DSP power control | Boolean value at +4 selects wake-up or shutdown. Nonzero enters the wake-up path; zero calls `dsp_shutdown()`. | High |

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

## Potential bounds issue in command 0x15

The balance handler reads four 32-bit arguments at offsets +4, +8, +12, and +16. However, the recovered minimum size threshold for `0x15` is only 2 words (8 bytes). A caller passing exactly that minimum would cause the handler to read beyond the copied payload. The original vendor caller may always pass a larger size; this is a static-analysis warning that should be checked before reproducing the ABI. The host parser validates the actual copied payload size and does not emulate out-of-bounds reads.

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

1. Recover the exact caller-side structs and ioctl encodings, including the unit of the size field.
2. Establish the user-facing meaning of `0x01` and the two modes selected by `0x20`.
3. Determine the first argument of `0x55` and `0x5C` from userspace call sites.
4. Decode the full `0xF0` behavior and any readback/status ABI.
5. Correlate every register prefix and lookup table with the CD3313 datasheet or verified bus traces.

Until these are resolved, the emulator must preserve unknown fields and mark unverified semantics explicitly rather than inventing behavior.
