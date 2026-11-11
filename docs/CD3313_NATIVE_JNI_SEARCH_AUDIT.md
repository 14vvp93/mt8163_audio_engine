# CD3313 native JNI search audit

## Scope

This audit rechecks the currently available extracted `files2` firmware archive for the missing native implementation behind:

`android.sourceservice.ExtAudioMuxer.nativeInit()`
and
`android.sourceservice.ExtAudioMuxer.SendCmdAudioMux(int, int[])`.

The archive contains 325 entries. This is a bounded scan of the supplied archive, not a claim that the complete firmware filesystem was supplied.

## Confirmed Java-side artifacts

The archive contains:

- `framework/boot-carservices.vdex`
- `framework/arm/boot-carservices.oat`
- `framework/oat/arm/services.vdex`
- `framework/services.jar`
- `framework/carservices.jar`
- `framework/mediatek-services.jar`
- `system/AudioInputSource.vdex`
- `system/AudioInputSource/oat/arm/AudioInputSource.vdex`
- `system/AudioInputSource.apk`

The VDEX files are optimized bytecode artifacts. The corresponding JAR files in this archive are stripped/placeholder containers (the framework service JARs contain only a manifest), so they do not supply the original class source or native implementation.

String inspection confirms:
- `boot-carservices.vdex` contains the recovered `ExtAudioMuxer`, `SendCmdAudioMux`, `nativeInit`, `/dev/audiomux`, and `android.sourceservice` references.
- `services.vdex` also contains references to `android/sourceservice/ExtAudioMuxer`, `nativeInit`, `/dev/audiomux`, and the `sourceinfo` service.
- `AudioInputSource.vdex` contains a reference to `android/sourceservice/ExtAudioMuxer`.

These additional VDEX references corroborate that the class is used in the system/service environment. They do **not** identify which native shared object registers or implements the methods.

## Native-library inventory result

The 325-entry archive contains a limited set of native libraries, primarily audio HAL/HIDL components. It does not contain:
- `libandroid_servers.so`;
- a native library whose filename identifies `sourceservice` or `audiomux`;
- a native library containing the strings `ExtAudioMuxer`, `SendCmdAudioMux`, or `/dev/audiomux`.

The supplied `libem_audio_jni.so` was previously inspected and did not expose the CD3313 device-node/JNI strings. The supplied primary HAL and MediaTek audio HIDL libraries likewise did not expose a direct CD3313 client.

The most likely next native search targets remain:
- `/system/lib/libandroid_servers.so` (32-bit system_server native library);
- `/system/lib/libandroid_runtime.so` and other system-server-loaded libraries;
- any vendor/system library loaded by the sourceinfo service or registered through `JNI_OnLoad`.

These are search targets only. No provider library has been identified yet.

## Consequence

The Java/Binder command API and command-array layouts are recovered, but the native JNI transport remains unresolved. In particular, the archive does not establish:
- the native library that registers the methods;
- whether the native layer uses `ioctl()`, `write()`, or another transport;
- the exact ioctl request word, including its low `_IOC_NR` byte;
- the exact userspace framing and payload-size calculation;
- whether the native implementation opens the same `/dev/audiomux` instance created by the analyzed `cd3313.ko`.

Do not implement a direct userspace device client or send live CD3313 commands until those items are recovered. The app-side Binder approach remains a candidate because the native method executes in the service process, but it still requires the read-only runtime probe and then a controlled transaction test.

## Next collection target

From the exact running firmware, collect the following files if present:

1. `/system/lib/libandroid_servers.so`
2. `/system/lib/libandroid_runtime.so`
3. `/system/lib/libandroidfw.so`
4. `/system/lib/libbinder.so`
5. Any additional `/system/lib/*.so` or `/vendor/lib/*.so` discovered by scanning for JNI registration names and `/dev/audiomux`.

Also record the build fingerprint and the SHA-256 of each collected binary. Do not mix libraries from a different firmware build without recording that mismatch.

No device node was opened, no ioctl was sent, and no audio or hardware state was changed in this audit.


## 2026-11-11 expanded binary/artifact scan

A fresh local scan of all 325 entries in `files2-extracted.zip` was run, not just a filename-filtered `*.so` scan. The scan checked exact bridge/device strings in every archive member and separately identified ELF files by their `7F 45 4C 46` magic bytes.

Results:

- No ELF file contains `SendCmdAudioMux`, `ExtAudioMuxer`, `/dev/audiomux`, or `audiomux`.
- The only ELF with generic JNI markers is `system/libem_audio_jni.so` (41,392 bytes, SHA-256 `b36141ed837908255d6e7fac6a9165b9fdcc8446f810755b619059e096911668`). It contains `JNI_OnLoad` and `registerNativeMethods`, but no CD3313 bridge/device strings. This is not evidence that it implements ExtAudioMuxer.
- `framework/arm/boot-carservices.art` contains the literal `/dev/audiomux` and sourceinfo service labels in its ART image string area. This corroborates the framework-side class/service evidence but is not a native ELF provider.
- `framework/boot.vdex` and `framework/boot-framework.vdex` contain generic `sourceinfo` strings; neither contains the exact native method names.
- `boot-carservices.vdex`, `services.vdex`, and `AudioInputSource.vdex` retain the Java-side references already described above.

The expanded scan confirms that the native provider is absent from this particular 325-entry archive. It does not prove the provider is absent from the complete firmware: the archive does not include `libandroid_servers.so` or the complete system/vendor native-library directories.

## Updated collection tool

`tools/find_cd3313_native_provider.ps1` now scans all files (subject to a configurable maximum size), detects ELF by magic rather than extension, searches both strong CD3313 markers and supporting JNI markers, and records SHA-256 and relative paths. Generic `JNI_OnLoad`/ `RegisterNatives` strings are classified as supporting evidence only. See `docs/CD3313_NATIVE_PROVIDER_SCAN.md`.

No firmware binary was executed. No device node was opened, no ioctl was sent, and no audio/hardware state was changed.


### Focused inspection of the only JNI-bearing ELF

The only ELF with generic JNI registration markers, `system/libem_audio_jni.so`, was extracted and inspected with ELF metadata, dynamic symbols and ASCII strings:

- ELF32, little-endian ARM, EABI5, shared object.
- Exports `JNI_OnLoad`.
- Imports `android::AndroidRuntime::registerNativeMethods`.
- Its registration strings identify `com/mediatek/engineermode/audio/AudioTuningJni`, not `android/sourceservice/ExtAudioMuxer`.
- Its native method/string inventory is EngineerMode audio tuning (for example `setAudioCommand`, `getAudioCommand`, `setAudioData`, `getAudioData`, and audio parameter parser functions).
- It has no `SendCmdAudioMux`, `ExtAudioMuxer`, `/dev/audiomux`, or `sourceinfo` string.

Therefore `libem_audio_jni.so` is not the missing CD3313 JNI bridge in this firmware set. Its name and generic `JNI_OnLoad` symbol were only a false-positive candidate.


## 2026-11-11 expanded firmware-library artifact — previous gap closed

The earlier findings above remain valid for the bounded 325-entry `files2`
archive. A separate, larger artifact was subsequently generated from all
available split firmware archives:
[firmware-library-analysis run 43089987118](https://github.com/14march93/mt8163_audio_engine/actions/runs/43089987118).
The artifact includes the previously missing native provider:

`lib/lib/libcarservices_jni.so`

Identity:
- ELF32 ARM, EABI5, Android 28.
- Build ID `bf391e545edd2e482b72cd8a431d1c59`.
- SHA-256 `ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`.

The library registers `android/sourceservice/ExtAudioMuxer` and implements
the JNI methods `nativeInit` and `SendCmdAudioMux`. It contains the
`/dev/audiomux` string and imports `ioctl`, `__open_2`, and `close`.
Static ARM disassembly confirms:

- `nativeInit()`: opens `/dev/audiomux` read-only and sends
  `0x8080F000`; the CD3313 driver returns the eight-byte chip ID
  `cd3313\\0\\0`.
- `SendCmdAudioMux(command, values)`: opens `/dev/audiomux` write-only,
  passes the JNI int-array elements directly to `ioctl`, and closes the
  descriptor.
- Native request formula:
  `0x40000000 | ((array_length + 1) << 16) | (command << 8)`.
  This makes the low request-number byte zero and encodes the command ID in
  bits 8..15.
- The driver copies `(array_length + 1) * 4` bytes, one 32-bit word more
  than the declared Java array length. The CD3313 `0x15` callback consumes
  four speaker words and does not consume that fifth copied word. The extra
  word's contents are unknown; do not invent a value or reproduce the
  potentially unsafe over-read in a new client.

The native-provider search is now complete for the supplied firmware
archives. The outstanding work is to explain the one-word mismatch, test
the read-only Binder/API probe on the exact unit, and plan a controlled
hardware test only after a safe transaction strategy is established.
No live ioctl or hardware change was made during this analysis.
