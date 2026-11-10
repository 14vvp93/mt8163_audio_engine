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
| `0x01` | Select current input channel | One 32-bit channel ID at +4. The sentinel `-1` maps to channel 6. The selected channel is remembered; when mixing mode is off, `set_input_channel(channel, 8)` applies it. | High for state transition; exact UI names for channel IDs remain unknown. |
| `0x03` | Set per-channel input gain | `channel` at +4 and `gain` at +8. The gain is masked to two bits and cached in `channel_gain[channel]`; the active control byte's bits 3..4 are updated. The dispatcher minimum is three words although this optimized handler reads only the first two. | High for field use and bit mask; channel IDs are bounded to 0..6 in the safe emulator. |
| `0x04` | Set per-channel input mute | `channel` at +4, `mute` at +8, `reason` at +12. Maintains a 32-bit mute-reason mask for each of seven channels. Channel `-1` maps to 6. If the channel is active, reason 31 of master mute follows whether that channel's mute mask is nonzero. Channel 1 / reason 16 also updates `hfp_active`. | High |
| `0x10` | Set master volume step | One 32-bit value at +4; accepted range is 0..40. Calls the incremental master-volume routine. | High |
| `0x11` | Set master mute reason | `mute` at +4 and mute-reason/bit index at +8. Calls `set_master_mute(mute, reason)`; mute state is maintained as a bitmask. | High |
| `0x12` | Set master-volume percentage | One 32-bit value at +4; accepted range is 1..100. Stores the percentage and reapplies the current master-volume step. | High |
| `0x15` | Set four-channel balance/gain values | Four 32-bit values at +4/+8/+12/+16, corresponding to FL, FR, RL, RR. The inlined `set_balance(fl, fr, rl, rr)` converts each value into a separate register field. | High |
| `0x20` | Toggle input mixing mode | First 32-bit value at +4 is a boolean; the dispatcher requires three words, but the optimized handler reads only the first. Enabling sets `is_mixing` and switches the active input path to channel 1. Disabling clears it and restores the remembered current channel. | High for state transition; exact signal-level meaning of the mode is not established. |
| `0x55` | Bass-boost adjustment | Two-word command accepted by the dispatcher. The second word at +8 is converted to a bounded 0..14 transition target; writes use the bass register prefix `0x60` and the driver's lookup table. The first word's purpose is not established from this optimized path. | Medium |
| `0x56` | Loudness enable/state | One value at +4 controls the driver's loudness state. The handler updates state value 0/4 and the loudness control bit in the register image. | High |
| `0x5C` | Treble adjustment | Two-word command accepted by the dispatcher. The second word at +8 is converted to a bounded 0..14 transition target; writes use the treble register prefix `0x70` and the driver's lookup table. The first word's purpose is not established from this optimized path. | Medium |
| `0xA3` | Subwoofer GPIO control | Boolean value at +4 is converted to 0/1 and written to GPIO 409 (`0x199`). This is a special dispatcher branch, not one of the 12 entries in `cmd_len_list`. | High |
| `0xF0` | Static identifier read / write no-op | In read direction, cmd_ioctl() copies the static 8-byte .rodata identifier cd3313\\0\\0. In write direction, the callback has no recovered state mutation; the command is not a confirmed setter. | High for the read bytes and observed write-path no-op |
| `0xF1` | DSP power control | Boolean value at +4 selects wake-up or shutdown. Nonzero enters the wake-up path; zero calls `dsp_shutdown()`. | High |

### Input routing, gain, and mute reconstruction

The input-control handlers are distinct from the four-output-speaker command `0x15`:

- `0x01` stores the requested current input channel. The signed sentinel `-1` is normalized to channel 6. When `is_mixing` is false, the driver applies the selection through `set_input_channel(channel, 8)`; while mixing is active it remembers the requested channel without replacing the active channel-1 path.
- `0x03` stores a two-bit input-gain value in the seven-entry `channel_gain` array. The first argument is the channel index (0..6 in the safe model), the second is the gain (low two bits). The register byte at cached offset +17 has bits 3..4 replaced with this gain. The command-length table requires three words although the optimized handler consumes only the first two; the trailing word is retained in the raw command journal but assigned no invented meaning.
- `0x04` updates the per-channel `channel_mute` bitmask. Its arguments are channel, mute boolean, and reason bit (0..31). The driver normalizes channel `-1` to channel 6. For the currently active channel, any remaining input-mute reason asserts master-mute reason 31; clearing the final reason releases it. Channel 1 / reason 16 also tracks the driver's `hfp_active` flag.
- `0x20` toggles the `is_mixing` state. A nonzero first argument switches the active input path to channel 1; zero restores the remembered current channel. The dispatcher minimum is three words, but the optimized handler reads only the first word.

The emulator now models the selected and active channel separately, the mixing flag, seven input-gain values, seven per-channel mute masks, the HFP-active flag, and the shared master-mute reason-31 interaction. Static analysis of `set_input_channel()` recovers the control-byte selector field in bits 0..1: channel IDs 0..6 map to selector codes `{2, 1, 0, 3, 0, 2, 3}`. The routine preserves bits 2 and 5..7, writes the selected channel's two-bit gain into bits 3..4, and updates the selector field. The emulator now applies these fields when a channel becomes active and journals a changed control byte. The two raw GPIO output conditions are also recovered: GPIO 396 is low only for channel 2; GPIO 398 is low only for channel 0. The emulator records these requested logic levels without assigning electrical source names or claiming physical readback. The driver's attenuation/mute timing sequence remains outside the model. Channel labels such as AUX, radio, Bluetooth, or phone are intentionally not assigned to numeric IDs without caller-side evidence.

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
4. The 0xF0 read path is now recovered as a static cd3313 identifier string; the remaining ABI uncertainty is the exact userspace ioctl macro and low _IOC_NR byte.
5. Correlate every register prefix and lookup table with the CD3313 datasheet or verified bus traces.
6. Recover the four-channel level scale and verify the physical FL/FR/RL/RR speaker mapping before issuing `0x15` from a userspace controller.

Until these are resolved, the emulator must preserve unknown fields and mark unverified semantics explicitly rather than inventing behavior.

## Host-side typed command builders

The emulator now exposes safe typed builders for the commands whose payloads and ranges are recovered with high confidence:

- `make_master_volume_command(step)`: command `0x10`, one little-endian 32-bit word, range 0..40.
- `make_master_mute_command(mute, reason)`: command `0x11`, two little-endian 32-bit words; the mute value is 0/1 and the reason bit index is restricted to 0..31.
- `make_four_channel_level_command(fl, fr, rl, rr)`: command `0x15`, four little-endian 32-bit speaker-table indices in FL/FR/RL/RR order; each index is restricted to 0..15.
- `make_input_channel_command(channel)`: command `0x01`, accepts channel IDs 0..6 or sentinel -1.
- `make_input_gain_command(channel, gain, opaque_word)`: command `0x03`, channel 0..6, gain 0..3, preserves the otherwise-uninterpreted third word.
- `make_input_mute_command(channel, mute, reason)`: command `0x04`, channel 0..6 or -1, boolean mute, reason 0..31.
- `make_input_mixing_command(enabled, opaque_word0, opaque_word1)`: command `0x20`, boolean mode plus two preserved opaque words.


These builders create emulator command frames only. They do not open `/dev/audiomux`, issue ioctls, or write I²C. The command parser continues to reject the unsafe short `0x15` payload even though the original driver's minimum-length table accepts a smaller size.

The implementation and host tests passed in [CD3313 protocol emulator run 42850101047](https://github.com/14march93/mt8163_audio_engine/actions/runs/42850101047).


## Host-side CD3313 state emulator

The command emulator applies a deterministic subset of recovered command semantics and records an event journal. It remains an offline model: no ioctl, I²C, GPIO, or hardware access is performed.

Modeled state:
- `0x10`: master-volume step (0..40).
- `0x11`: independent master-mute reason bits; output is considered muted while any bit remains set.
- `0x12`: volume percentage (1..100); its second argument is preserved as opaque data because its meaning is unresolved.
- `0x15`: four FL/FR/RL/RR speaker indices and corresponding register bytes, using the recovered 16-entry attenuation table and register prefixes.
- `0x55` / `0x5C`: bass/treble transition indices (3..10) derived from the second argument and the recovered `cd3313_equalizer` table; the first argument is retained verbatim and remains opaque.
- `0x56`: loudness enabled state.
- `0xA3`: requested subwoofer GPIO state only; physical routing is not inferred.
- `0xF1`: zero invokes the recovered `dsp_shutdown()` path; nonzero invokes `dsp_wakeup.part.1()` only when the shutdown flag is set. The emulator now journals the shutdown and wakeup side effects for this command.

The model's reset baseline sets the synthetic master-mute reason mask to zero. Other values remain unset until a corresponding command is observed. Each accepted command produces a monotonically numbered journal event containing the raw parsed command, whether its semantics are modeled, and complete before/after state snapshots. Commands whose semantics remain unresolved are retained as unmodeled journal events. Invalid modeled values are rejected atomically without changing state or appending a journal event.

The journal and state are useful for deterministic sequence tests and future adapter development, but are not a readback of the physical CD3313. Unknown registers and arguments remain unknown.


## Partial command-page register image emulator

The host emulator maintains a 16-entry diagnostic command-page view and a separate 8-entry reg_bufs_cache that mirrors the driver's actual cache. TAB.33841 maps each command-byte high nibble to the 8-slot cache. This is a software cache model, not a complete silicon register map.

- The recovered 0x15 four-channel command updates four observed command bytes using the confirmed channel prefixes and 16-entry attenuation table.
- The driver's separate raw cmd_write() interface is represented by Emulator::write_raw_command_byte(). It is not parsed as an ioctl frame.
- Each observed byte produces a register journal event with a monotonic sequence, source, command page, TAB.33841 cache slot, current byte, and previous values in both cache views.
- Unobserved pages and cache slots remain unset. Reset clears both synthetic cache views.
- The initialization callback is known to iterate and transmit a register image, but its complete byte sequence has not yet been reconstructed from the available analysis. The emulator therefore does not invent a startup image or claim a full physical register map.

This is an offline observation model only. It does not call /dev/audiomux, issue I2C writes, or read hardware state.


### Recovered probe and event-0 initialization bytes

The additional `dsp_probe()` disassembly identifies a successful guard write of `0x3F` (the code retries up to three times when the I2C write fails) and a call to `set_master_mute(1, 31)`, which sets startup mute reason bit 31. It initializes the driver's cached command bytes at structure offsets +12 through +19 to:

`3F C0 E0 80 A0 47 6F 7F`

The `event_callback()` event-0 loop sends the eight cached bytes twice in that order, with the driver's per-write delays, then writes the byte at offset +17 (`0x47`) one additional time. This gives 17 writes in event 0, plus the earlier probe guard write. The emulator models the successful probe write and mute bit, then replays the 17 event-0 bytes into its command-page cache and write journal. It does not emulate I2C errors, electrical timing, or GPIO hardware.

The four speaker bytes are the neutral attenuation settings: FL=`0x80`, FR=`0xA0`, RL=`0xC0`, RR=`0xE0`. The probe's master-volume state starts at step 0, with the percentage initialized to 100 and the guard register value `0x3F`. This is static reconstruction from the supplied module, not a live hardware readback.


For the reconstructed probe guard write, exact event-0 initialization sequence, 16-page cache model, and master-volume lookup/ramp implementation, see [CD3313 Register Image Emulator](CD3313_REGISTER_IMAGE.md). The extended static-analysis artifact [run 42852859151](https://github.com/14march93/mt8163_audio_engine/actions/runs/42852859151) includes the `dsp_probe` disassembly, ELF section map, relocations, and `.rodata`/`.data` dumps used to recover these values.


The bass/treble lookup and transition path is now documented in [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md): `cd3313_equalizer` contains `00 01 02 03 04 05 06 07 0E 0D 0C 0B 0A 09 08`; commands `0x55` and `0x5C` derive an internal transition index 3..10 from their second word and write stepped `0x60`/`0x70` command bytes. The first argument remains unresolved.


Command `0x56` now has a register-level model for the recovered active-low loudness bit: a nonzero argument clears bit 2 of the `0x47` control byte (enabled, `0x43`), and zero sets it (disabled, `0x47`). Repeated requests for the current state do not emit a redundant write. Other control-byte bits remain opaque.

### Reconstructed input-switch transition sequence

The set_input_channel(channel, delay) disassembly now supports a deterministic ordered transition model:

1. Read the cached master-volume command byte and ramp its low six bits toward 0x3F in increments of three. Each intermediate I²C write is followed by msleep(delay).
2. Write the terminal 0x3F attenuation byte and wait once more.
3. Build and write the input control byte: preserve bits 2 and 5..7, place the active channel gain in bits 3..4, and the selector in bits 0..1.
4. Write GPIO 396, then GPIO 398, using the recovered per-channel raw levels.
5. Call set_master_mute(channel_mute[channel] != 0, 31) and wait.
6. If the selected channel has any input-mute reason and hfp_active is false, wait once more and leave attenuation at 0x3F. Otherwise, calculate the target from master_volume, volume_percentage, and the recovered volume table; ramp down from 0x3F in three-code increments, waiting after each intermediate write, then perform the final target write.

The command dispatcher calls this helper with an 8 ms delay for ordinary channel selection and when leaving mixing mode. Enabling mixing selects channel 1 with a zero delay. A channel-selection command received while mixing is active only updates the remembered channel and does not run the helper.

The emulator records the ordered sequence as InputTransitionEvent entries (attenuation writes, selector write, GPIO writes, master-mute reason-31 state, and explicit delay events). It separately tracks the current volume command byte so a transition starts from the last modeled output level rather than merely from the requested target. The timeline is a deterministic offline reconstruction: it records requested operations and nominal sleep intervals, not measured I²C latency or physical GPIO readback.

The hfp_active exception is modeled exactly as the disassembly's branch condition: an input mute does not keep the volume attenuation forced at 0x3F while HFP is active. This does not imply that master mute reason 31 is cleared; mute reasons remain independent.



### Master mute, ACC latch, and GPIO 393

The `set_master_mute(mute, reason)` ARM32 disassembly at `0x63c` recovers the raw GPIO 393 output expression:

`gpio393_raw = (mute && !hfp_active) || master_mute_reason_mask[24] || !acc_on`

The function first sets or clears the requested reason bit in the 32-bit `master_mute` mask, then evaluates this expression and calls `gpiod_set_raw_value()` for GPIO 393. This is the raw electrical level requested by the driver; the board-level meaning of HIGH/LOW is not inferred.

The event callback's command `0x11` path has a special case for reason 2: it copies the mute argument into the global `acc_on` latch before calling `set_master_mute()`. Therefore reason 2 is also the recovered ACC-state input. The probe path initializes `acc_on = 1` before asserting startup mute reason 31. The emulator now tracks `acc_on` and the last requested raw GPIO 393 level separately from the aggregate `master_muted()` helper.

System notifier callback event 2 clears reason 30; event 3 clears reason 24. The emulator exposes `apply_recovered_system_mute_event(2|3)` for these two recovered clear operations. Unknown notifier event IDs are rejected. GPIO 393 is recalculated after each callback. The shutdown path separately recomputes the same GPIO using its own shutdown branch and is not treated as a normal `set_master_mute()` call.

A separate `event_callback()` branch for command `0x11` with `mute=1` and `reason=24` first calls `remove_event(handler, 3)`, then `post_event_delayed(handler, 3, 0, 1000)`. Event 3 later clears reason 24. The emulator records these two queue requests in `event_queue_requests()` as a replacement request with a 1000 ms delay. This proves the driver's requested scheduling sequence, but not the internal queue helper's cancellation/coalescing policy or the exact wall-clock callback time.

The transition journal now includes `Gpio393Write` after the reason-31 update. This models requested raw output levels, not physical readback or measured timing.


### Shutdown path reconstruction

The `dsp_shutdown()` disassembly at `0xFF4` is a separate path from `set_master_mute()`:

1. Set the driver's shutdown flag.
2. Set master-mute reason bit 30.
3. Compute raw GPIO 393 as `!hfp_active || !acc_on || reason24_is_set` and call `gpiod_set_raw_value()`.
4. Request removal of queued event IDs 0 and 1, in that order.
5. Send the final stack-local byte `0x3F` through `i2c_master_send()`, then update the driver's cached command-page byte.

The shutdown GPIO expression is recovered directly from the conditional sequence and global symbols: `hfp_active` at `.bss+0x20`, `acc_on` at `.data+0x38`, and reason 24 in `master_mute` at `.bss+0x18`. It must not be replaced with the ordinary `set_master_mute()` expression. The host model journals requested event removals and the final write; it cannot prove queue cancellation or physical GPIO/I2C behavior.


### Reconstructed PM notifier and wakeup path

The static pm_event_notify() disassembly at 0x10EC handles only notifier action 16. Other action values return success without changing state. For action 16, the event code is read from the notifier payload at offset +4:

- Codes 0 and 1 enter the wakeup branch only when the driver's shutdown flag is set. Their external semantic labels are not established by the available binary, so they remain numeric event codes.
- Codes 2, 3, and 4 call dsp_shutdown().
- Values greater than 4 return -EINVAL (-22).

The command 0xF1 path has the same state gate: a zero first word calls dsp_shutdown() unconditionally; a nonzero first word logs the wake-up request and calls dsp_wakeup.part.1() only if the shutdown flag is currently set. A nonzero request while already awake does not replay the wakeup sequence. The emulator routes this command through the recovered shutdown/wakeup models and verifies the five delayed/remove queue requests after a shutdown.

The recovered dsp_wakeup.part.1() path clears the shutdown flag and recalculates the cached master-volume byte from master_volume and volume_percentage. The percentage is clamped to 0..100 for this calculation; attenuation is (100 - percentage) / 3, added to the selected entry in the 41-byte cd3313_voltable_40 table and capped at 0x3F. A zero master-volume step selects 0x3F. This updates the driver's cached byte; it is not itself an immediate I2C write.

Wakeup then requests these event operations in order:

1. Remove queued event 0.
2. Post event 0 with a 50 ms delay.
3. Remove queued event 2.
4. Post event 2 with a 1200 ms delay.
5. Post event 2 with a 1500 ms delay.

The host emulator exposes apply_recovered_pm_event(action, event_code) and a separate wakeup journal for these requested queue operations. It intentionally records both delayed event-2 posts without assuming whether the kernel event-queue implementation keeps both, replaces one, or coalesces them. Event 2 later clears master-mute reason 30 in event_callback(). The emulator does not execute kernel workqueues, prove callback timing, or demonstrate physical wakeup behavior.



## ioctl ABI and queue API follow-up

The focused artifact from workflow run [42878596077](https://github.com/14march93/mt8163_audio_engine/actions/runs/42878596077) resolves the ioctl request-word layout, write-buffer framing, static 0xF0 read response, and imported event-queue API prototypes. See [CD3313_IOCTL_ABI.md](CD3313_IOCTL_ABI.md) for the exact bit fields, 12 command-length thresholds, queue scheduling sequence, and remaining limitations.

The event-queue declarations recovered from DWARF are:
- post_event_delayed(handler*, event_id, data, delay_ms)
- remove_event(handler*, event_id)
- callback: int callback(event_id, data)

The helper implementations and opaque handler structures are not present in cd3313.ko. Therefore the driver's request order is known, but cancellation/coalescing behavior and actual callback timing are not.

The focused analysis also flags two static bounds-audit items without claiming exploitability:
1. Command 0x15 reads four 32-bit speaker arguments although its minimum size-field threshold is two words.
2. cmd_ioctl() prepends a 4-byte header to a payload that may be copied up to 256 bytes into a 256-byte slab object. __check_object_size() is called before usercopy; exact behavior must be checked against the target kernel's hardened-usercopy implementation.


The follow-up table analysis now confirms that TAB.33841 is the command-page-to-reg_bufs-slot mapping and that CSWTCH.194 is the six-entry input selector map. The emulator now mirrors the actual eight-slot reg_bufs cache in addition to retaining the 16-page diagnostic view. Its tests cover every TAB.33841 mapping and the aliasing of different command pages onto the same cache slot.


## Manufacturer datasheet cross-check

The driver tables and bit fields are cross-checked against the manufacturer document **CD3313-AX-BJ-24, revision 2012-01-B1** ([PDF](https://datasheet4u.com/pdf/831243/CD3313.pdf)).

- Input control byte format is 010 G1 G0 S2 S1 S0. G1G0 (bits 4..3) select 0/3.75/7.5/11.25 dB input gain; S2 (bit 2) is loudness (0=on, 1=off); S1S0 (bits 1..0) select chip input 1..4.
- The datasheet specifies three external stereo inputs; input 4 has no external pins. The driver's channel map 0..6 therefore resolves to chip input numbers 3, 2, 1, 4, 1, 3, 4. This does not identify AUX/radio/Bluetooth/phone wiring.
- Startup control byte 0x47 selects input 4, 0 dB input gain, and loudness off. The driver changes S1S0 and G1G0 while preserving the loudness bit; command 0x56 toggles that bit.
- The four-speaker attenuation lookup spans -38.75 dB to 0 dB in the recovered table order. The 16 levels are -38.75, -35, -32.5, -30, -27.5, -25, -22.5, -20, -17.5, -15, -12.5, -10, -7.5, -5, -2.5, and 0 dB.
- The datasheet defines bass and treble control in 2 dB steps. The driver's transition routine currently uses lookup indices 3..10, which decode to -8, -6, -4, -2, 0, +2, +4, and +6 dB. This is the range of the recovered command path, not the chip's full ±14 dB capability.

The complete raw tables, register-cache mappings, and master-volume dB list are in [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md). Datasheet correlation does not prove board-level wiring or runtime behavior on the target head unit.
