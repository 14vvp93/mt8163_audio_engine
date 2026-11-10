# CD3313 userspace-client scan

## Result

The GitHub Actions workflow **Scan vendor archive for CD3313 userspace clients** completed successfully on 2026-11-10.

- Workflow run: [42804202355](https://github.com/14march93/mt8163_audio_engine/actions/runs/42804202355)
- Artifact: `audiomux-userspace-client-scan`
- Artifact SHA-256: `21267401f156cccad16565ae0dd8ef89bbbb7a6d6331df212450d95277e0a6ee`
- Scope: the audio-related files extracted from the repository's `files.zip` archive (36 files identified by the scan).

## Findings

No userspace client for `/dev/audiomux` was identified in this archive.

The binary and text scan found no actual string occurrences of:

- `/dev/audiomux` / `audiomux`
- `cd3313`
- `dsp_i2c_driver`
- `STATUS_WRITE_CMD`
- `STATUS_READ_CMD`

The text/source match report was empty. The binary string report contained generic `ioctl` references in `audio.primary.mt8163.so`, but the associated strings identify unrelated interfaces, including HDMI audio settings, FM controls, Bluetooth SCO, and CCCI/modem devices. These matches are not evidence of a CD3313 caller.

The archive also contains `audiocmdservice_atci`. Its previously analyzed strings and imports point to the MediaTek ACF/HCF/BES audio-parameter path; this scan did not find a `/dev/audiomux` or CD3313 reference in it. Do not treat that service as the CD3313 ioctl client.

## What this means for the ABI

The kernel module's dispatcher-side behavior remains recoverable from `cd3313.ko`, but the caller-side ioctl encoding and argument structures are not established by this archive. In particular, do not invent an ioctl magic/type, request word, or command payload layout and do not send experimental writes to the live device.

The driver-side reconstruction currently indicates that the request word carries direction, command ID, and a size field, and that the event callback reads command arguments after the 32-bit request word. This is not enough by itself to claim a fully validated userspace ABI.

## Next evidence to obtain

1. A fuller vendor/system image or extracted filesystem containing the factory audio settings application and its native libraries.
2. Any vendor APK/JAR/native library that opens `/dev/audiomux`, or source/header files defining the ioctl macros and command structures.
3. If a rooted target becomes available, read-only runtime evidence such as device-node permissions, process file descriptors, and relevant app/service package inventory. Do not issue write ioctls during discovery.

## Limitations

A negative string scan is not proof that no client exists on the actual head unit. The client may be outside `files.zip`, may construct the device path dynamically, or may use an indirect library call. This result only narrows the supplied archive and prevents us from incorrectly attributing unrelated HAL ioctl calls to CD3313.
