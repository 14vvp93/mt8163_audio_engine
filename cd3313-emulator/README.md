# CD3313 host-side protocol and register-image emulator

This is an offline parser and state/register-image model for the command buffer handled by the recovered ARM32 `cd3313.ko` driver. It does not communicate with a physical CD3313.

## Ioctl frame model

The queued event object starts with the original 32-bit Linux ioctl request word in little-endian order, followed by the user payload:

- direction: bits 30..31 (`1` = write, `2` = read);
- command/request ID: bits 8..15;
- size field: bits 16..29;
- copied user payload: `size_field * 4` bytes, capped at 256 bytes by the driver.

The 12 `cmd_len_list` entries are minimum size-field thresholds, not exact payload lengths. The parser enforces the recovered minimum and the actual copied payload length. It preserves opaque argument bytes where command semantics are not fully proven.

The dispatcher has two additional branches, `0x56` and `0xA3`, that are not in the 12-entry length table; both read at least one 32-bit argument, so the parser requires a one-word payload for them.

## Separate raw register interface

The driver's `cmd_write()` path is different: it parses hexadecimal text with base 16, truncates the parsed integer to one byte, and sends that byte over I²C. The emulator represents it through `write_raw_command_byte()` and records it with a distinct journal source; it is not parsed as an ioctl frame.

## Implemented

- Little-endian ioctl request-word parsing and encoding.
- Write-direction validation, command ID and 14-bit size-field extraction.
- 32-bit-word payload sizing, 256-byte cap, recovered minimum-size validation.
- Typed builders for input channel/gain/mute/mixing, master volume/mute, and four-channel levels.
- State model for master-volume step and percentage, independent master-mute reasons, seven input channels (selection, recovered two-bit selector mapping, per-channel gain and mute-reason masks), mixing mode and HFP-active state, and FL/FR/RL/RR output levels.
- Recovered 41-entry master-volume lookup table and stepped register-byte ramp.
- Recovered 15-byte bass/treble lookup table and stepped register-byte ramps; first command argument remains opaque.
- Active-low loudness control bit and recovered input selector/gain fields in the `0x47` control byte.
- Requested DSP power and subwoofer GPIO states; recovered input-routing GPIO 396/398 output levels without claiming physical GPIO readback.
- Recovered probe guard write and event-0 initialization sequence.
- Diagnostic 16-page command-byte view plus an exact 8-slot reg_bufs cache mirror selected through TAB.33841.
- Deterministic command event journal with before/after state snapshots.
- Register-write journal with sequence, source, command page, reg_bufs slot, byte, and previous values in both cache views.
- Recovered reason-24 mute timer request: remove queued event 3, then request event 3 after 1000 ms; queue internals and actual callback timing remain external.
- Four-channel Balance/Fader mapping, speaker register-byte encoder, and independent rear-level compensation (implemented by front attenuation, since the chip cannot boost).
- Host unit tests for command encoding, bounds, state transitions, startup sequence, volume ramps, bass/treble ramps, loudness bit transitions, and journal reset.

## Not implemented / not yet verified

- Userspace `/dev/audiomux` controller and fully confirmed ioctl type/magic compatibility.
- Complete CD3313 physical register map and hardware readback.
- Full semantics for every command; unknown fields remain opaque.
- Exact kernel I²C transfer-failure behavior; the emulator assumes a successful one-byte transfer after safe text parsing.
- I²C timing, electrical behavior, bus failures, and real GPIO effects.
- Device-level verification on the MT8163 head unit.

## Recovered startup sequence

Static analysis of `dsp_probe()` and `event_callback(event=0)` recovered:

1. `dsp_probe()` writes `0x3F` as a guard byte, retrying up to three times if the I²C write fails. The host model records one successful attempt and does not simulate bus failures.
2. `dsp_probe()` calls `set_master_mute(1, 31)`, so startup mute reason bit 31 is active.
3. The eight initialized command bytes are `3F C0 E0 80 A0 47 6F 7F`.
4. `event_callback(event=0)` sends those eight bytes twice, then writes `0x47` once more: 17 writes in that event.

The emulator exposes `apply_recovered_probe_setup()` and `apply_recovered_initialization_sequence()` for these two phases. The latter restores neutral FL/FR/RL/RR attenuation bytes (`80 A0 C0 E0`), bass/treble bytes (`6F`/`7F`), and the initial loudness control byte (`47`).

See [the command-dispatch reconstruction](../docs/CD3313_COMMAND_DISPATCH.md), [the register-image reconstruction](../docs/CD3313_REGISTER_IMAGE.md), and [the protocol reconstruction](../docs/CD3313_PROTOCOL_RECONSTRUCTION.md) for evidence and confidence labels.

### Input switch transition model

The emulator reconstructs the ordered set_input_channel() sequence: ramp attenuation to 0x3F in three-code increments, write the input selector, request GPIO 396/398 levels, update master mute reason 31, then restore the requested master volume unless the selected input is muted while HFP is inactive. The ordinary selector path uses the driver's 8 ms delay; mixing enable selects channel 1 with a zero delay. Inspect Emulator::input_transition_journal() for ordered attenuation, selector, GPIO, mute, and delay events.

This is an offline static model. Delays are nominal driver arguments, not measured bus timings, and GPIO events are requested raw output levels rather than hardware readback.

### Master mute and GPIO 393

The recovered `set_master_mute()` path updates a reason bitmask and computes GPIO 393's requested raw level as `(mute && !hfp_active) || reason24_is_set || !acc_on`. Command `0x11` with reason 2 updates the ACC latch. Asserting reason 24 through command `0x11` also requests removal of queued event 3 followed by a delayed event-3 post at 1000 ms; event 3 clears reason 24. The probe initializes ACC on before startup mute. System notifier events 2 and 3 clear reasons 30 and 24 respectively. The emulator exposes these notifier operations and records GPIO 393 writes separately from aggregate mute state. It also journals each active-channel input-mute GPIO write, including writes where the computed raw level does not change. Raw GPIO polarity is modeled; board-level signal meaning is not inferred.


### Recovered shutdown sequence

The ARM32 `dsp_shutdown()` disassembly confirms a distinct shutdown path. It marks the device shutdown flag, sets master-mute reason 30, and writes GPIO 393 using `!hfp_active || !acc_on || reason24_is_set`. This is not the ordinary `set_master_mute()` expression: while HFP is active, GPIO can remain low only if ACC is on and reason 24 is clear. The function then requests removal of queued event IDs 0 and 1, in that order, and sends a final `0x3F` byte over I2C. The emulator records this ordered sequence and the final command-page write. Event-removal entries represent driver requests, not proof that a queued callback was physically cancelled. All behavior remains an offline static model.
