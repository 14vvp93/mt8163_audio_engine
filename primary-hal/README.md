# MT8163 Primary HAL bootstrap

This directory starts the Android 9 / ARM32 Primary HAL replacement at the
factory module boundary.

## Current behavior

- Exports the Android HAL module symbol `HMI`.
- Uses the pinned Android 9 `libhardware` audio interface.
- Declares the factory module identity and API versions.
- Enforces the recovered 32-bit factory device-wrapper layout at compile time:
  - public `audio_hw_device`: 156 bytes;
  - private callback tail: offset 156, 16 pointer slots;
  - legacy hardware pointer: offset 220;
  - complete wrapper: 224 bytes.
- Rejects invalid module IDs and validates output pointers.
- Allocates the 224-byte factory-compatible device wrapper and populates the
  Android 9 public `audio_hw_device` callback table.
- Implements deterministic error behavior for unsupported controls and stream
  opens; output/input stream pointers are always cleared on failure.
- `init_check` returns `-ENODEV` because no verified hardware PCM endpoint is
  configured. Stream open callbacks also fail closed with `-ENODEV`.

The module is deliberately marked `installable: false`. It is a build-time
bootstrap, not a deployable HAL and not a replacement for the factory binary.

## Build integration

The Soong module is named `audio.primary.mt8163`, targets only 32-bit ARM and
uses the Android platform's `libhardware_headers`. Build it from an Android 9
platform tree after this repository's source is included in the vendor build.

## Safety and completion gates

- No PCM device is opened.
- No TinyALSA call, mixer write, ioctl, I2C, GPIO or module operation exists.
- No output/input stream is exposed until a verified PCM endpoint backend is wired.
- No unsupported route is silently mapped to Normal playback.
- Do not set `installable: true` or stage this artifact until the public HAL
  callbacks and all required factory-private callbacks are implemented and
  verified against the recovered Android 9 ABI.
