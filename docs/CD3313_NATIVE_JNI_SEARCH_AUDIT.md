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
