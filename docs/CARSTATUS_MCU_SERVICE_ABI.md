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

The carstatus `update_mute()` function opens `/dev/audiomux` with
`O_RDONLY`, updates the carstatus module's own mute mask under a mutex,
schedules its own delayed events, and drives GPIO 393 (`0x189`) in the recovered call path. It then closes the
audiomux file. This is the same numeric GPIO used by the CD3313 driver's
DSP shutdown/wakeup path, so the two modules appear to share a physical
control line; ownership, polarity, and race behavior still need analysis. No ioctl or
write call to `/dev/audiomux` is made by this function.

Therefore this path confirms that carstatus checks for the presence of the
CD3313 device and coordinates a separate amplifier-mute GPIO, but it does
**not** establish that carstatus sends CD3313 command `0x11` or mute reason
`2`. The ACC-to-CD3313 reason-2 sender remains unresolved and must be
traced elsewhere in the service/native stack.

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
