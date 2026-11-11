# MT8163 Audio Engine — project status

Updated: 2026-11-11

## Current milestone

**The CD3313 native JNI transport has been recovered statically.** The previous
largest blocker—finding the implementation behind
`android.sourceservice.ExtAudioMuxer.SendCmdAudioMux(int,int[])`—is closed
for the supplied firmware archives.

## Confirmed evidence

- Kernel module: `vendor_lib/lib/modules/cd3313.ko`, ELF32 ARM with DWARF.
- Character device: `/dev/audiomux`.
- JNI provider: `lib/lib/libcarservices_jni.so`, ELF32 ARM, Android 28.
- JNI provider Build ID: `bf391e545edd2e482b72cd8a431d1c59`.
- JNI provider SHA-256:
  `ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`.
- Java/Binder path:
  `AudioEffect → ISourceService → SourceService → ExtAudioMuxer JNI`.
- `nativeInit()` opens `/dev/audiomux` read-only and issues
  `0x8080F000`; the driver returns `cd3313\0\0` in its 8-byte response.
- `SendCmdAudioMux()` opens the same device write-only and issues ioctl.
- Native write request formula:
  `0x40000000 | ((array_length + 1) << 16) | (command << 8)`.
  The low request-number byte is zero; the command ID occupies bits 8..15.

## Outstanding ABI issue

The native request encodes one more 32-bit word than the Java array declares.
The CD3313 driver multiplies the encoded size by four and copies that extra
word from the pointer returned by JNI `GetIntArrayElements()`. For command
`0x15`, the callback consumes four speaker indices and ignores the fifth
copied word. The extra word's value and intended origin are unknown.

A plausible explanation is that the vendor JNI counts the ioctl header in
the size field while the driver separately prepends that header and copies
the full size again. This is an inference, not proven intent.

**Do not invent a padding value or reproduce this extra-word copy in a new
hardware client.** The host emulator intentionally builds safe frames from
complete, validated payload words and does not mimic the native over-read.

## Test status

- Firmware library analysis: [run 43089987118](https://github.com/14march93/mt8163_audio_engine/actions/runs/43089987118) — success.
- HEQ decompilation: [run 43090340667](https://github.com/14march93/mt8163_audio_engine/actions/runs/43090340667) — success.
- CD3313 host and sanitizer tests: [run 43092920696](https://github.com/14march93/mt8163_audio_engine/actions/runs/43092920696) — both jobs success.
- Read-only Android API probe APK: [build run 43021678454](https://github.com/14march93/mt8163_audio_engine/actions/runs/43021678454). The probe checks OEM API availability and Binder liveness; it does not send a CD3313 control command.

These are static-analysis and host-CI results. No live ioctl, I²C write,
audio-setting change, or physical head-unit test has been performed.

## Next work

1. Run the read-only API/Binder probe on the exact head unit and record the
   build fingerprint and result.
2. Investigate the native one-word size discrepancy without sending writes.
3. Define a safe transaction and rollback plan for a future controlled
   hardware test.
4. Continue tracing `carstatus.ko` and its JNI/user-space client to connect
   ACC/vehicle-state events with CD3313 mute reasons.
5. Keep the host emulator and real-device backend separate.

The same expanded artifact also includes its paired `cd3313.ko` build
(Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`, SHA-256
`6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`).
Its ioctl framing and chip-ID response were cross-checked against the older
driver build already used by the emulator; the relevant behavior matches.
