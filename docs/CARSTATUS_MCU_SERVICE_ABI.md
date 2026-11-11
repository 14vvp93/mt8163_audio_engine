# MT8163 carstatus JNI and character-device path

Updated: 2026-11-11

## Evidence identity

The expanded `firmware-library-analysis` artifact contains both the JNI
provider and the vehicle-status module:

- JNI provider: `lib/lib/libcarservices_jni.so`
  - Build ID: `bf391e545edd2e482b72cd8a431d1c59`
  - SHA-256: `ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`
- Kernel module: `vendor_lib/lib/modules/carstatus.ko`
  - Build ID: `dad390e3aee9a1be8e6cae2e3416cf75eb96123e`
  - SHA-256: `7d25bc2d3cf0b6de2823336189acb0c3ffc2fb8c29e889a06396683eb6cebe54`

These are same-artifact static findings. Neither binary was executed.

## JNI method recovered

The JNI provider registers `android/carsource/McuService` and the method:

`sendCmdCarStatus(String) : void`

The method implementation:
1. Obtains the Java string with `GetStringUTFChars()`.
2. Copies up to 0x28 (40) bytes into a native stack buffer.
3. Releases the JNI string.
4. If the copied command is non-empty, opens `/dev/carstatus` with
   `O_WRONLY`.
5. Calls `write(fd, buffer, 0x28)` (a fixed 40-byte write).
6. Closes the descriptor.

This is a text-command path, not the CD3313 ioctl path. The method is
registered separately from `ExtAudioMuxer.SendCmdAudioMux()`.

## Kernel character-device interface

`carstatus.ko` registers a misc device named `carstatus`. Its file-operations
table contains `open`, `release`, `read`, and `write` handlers; no ioctl
handler pointer is present in the recovered table.

- `cmd_read()` returns a formatted state report containing reversing,
  parking, ACC, headlight, telephone-mute, ACC/reverse counters, LCD VCOM /
  AVDD state, mute mask, and backlight range.
- `cmd_write()` accepts text in a 128-byte local buffer and searches for
  key/value tokens. Recovered tokens include
  `rev_disable=`, `accoff_delay=`, `amp_sw_fan=`, `amp_power=`,
  `fan_power=`, `power_12v=`, `360module=`, `rev=`, `cvbs_out=`,
  `pled=`, `usb5v=`, `usben=`, `mute=`, `avdd=`, `vcom=`, and
  `backlight_range=`.
- The JNI sender writes 40 bytes even though the kernel parser's local
  buffer allows 128 bytes. The exact command strings emitted by the
  higher-level McuService have not yet been recovered.

## Vehicle GPIO/state evidence

The module contains explicit labels for:
- `gpio86_acc`
- `gpio43_rev`
- `gpio85_headlt`
- `gpio87_parking`
- `gpio44_telmute`

It also references other board controls such as amplifier power, fan power,
USB 5 V, radio antenna, CVBS output, and backlight. The read-side status
report is assembled from the module's global state; it is not proof of the
physical signal source beyond the named GPIO request strings.

## Relationship to CD3313 mute

The carstatus `update_mute()` function always updates its internal mute mask
under a mutex and schedules delayed events. It then probes
`/dev/audiomux` with `filp_open(..., O_RDONLY, 0)`:

- If the open succeeds, it closes the file and returns without driving GPIO
  393.
- If the open fails (an `ERR_PTR` result), it falls back to computing the
  mute output and drives GPIO 393 (`0x189`) through
  `gpiod_direction_output_raw()`.

GPIO 393 is the same numeric GPIO used by the CD3313 driver's DSP
shutdown/wakeup path. The branch condition strongly suggests intentional
ownership selection: when `/dev/audiomux` is available, the CD3313 driver
owns the line; carstatus drives it only as a fallback when the audio device
cannot be opened. This avoids the apparent concurrent GPIO writes in the
normal configuration, although the exact electrical polarity and failure
cases still need hardware confirmation.

This path confirms device-presence coordination, but it does **not**
establish that carstatus sends CD3313 command `0x11` or mute reason `2`.
The ACC-to-CD3313 reason-2 sender remains unresolved and must be traced
elsewhere in the service/native stack.

## Next evidence

1. Recover the McuService callers and exact 40-byte strings sent to
   `sendCmdCarStatus()`.
2. Determine whether ACC state comes directly from GPIO 86, an MCU/CAN
   source, or both on the target board.
3. Trace the separate CD3313 master-mute reason-2 path; do not equate the
   carstatus GPIO36 mute with the CD3313 driver's GPIO393 mute.
4. Validate read-only status on the exact head unit before any write tests.

No live `/dev/carstatus` or `/dev/audiomux` operation, I²C write, GPIO
change, or audio-setting change was performed.
