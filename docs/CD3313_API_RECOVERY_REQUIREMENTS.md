# CD3313 hidden API recovery: required target artifacts

## Current evidence boundary

The repository contains:
- the HEQ APK analysis and its high-level calls to hidden `android.audio.AudioEffect`;
- the ARM32 `cd3313.ko` driver reconstruction;
- the extracted Android 9 MT8163 audio HAL and `audiocmdservice_atci`;
- the ATCI service's direct dependency names and Binder/socket behavior.

The current `files.zip` archive does **not** contain:
- the framework JAR that implements `android.audio.AudioEffect`;
- the native library or Binder service that transports those calls;
- `libaudiocompensationfilter_vendor.so`;
- `libaudiocomponentengine_vendor.so`;
- `libaudioprimarydevicehalifclient.so`.

Therefore the HEQ Java API cannot yet be mapped safely to the driver's ioctl request word and payload structures.

## Highest-value files to collect

From the exact head unit firmware that contains the analyzed HEQ APK and CD3313 module:

1. Framework/vendor JARs and their optimized companions:
   - all JARs under `/system/framework` and `/vendor/framework`;
   - corresponding `.odex` / `.vdex` files if present;
   - prioritize any JAR that defines `android.audio.AudioEffect`.
2. Native libraries:
   - `libaudiocompensationfilter_vendor.so`;
   - `libaudiocomponentengine_vendor.so`;
   - `libaudioprimarydevicehalifclient.so`;
   - any additional audio-related vendor library referenced by the class or service after the JAR is inspected.
3. Vendor services:
   - `/vendor/bin/audiocmdservice_atci` and its init file (already present);
   - any additional service discovered by searching for `AudioEffect`, `audiomux`, or CD3313 references in the framework/native artifacts.
4. Firmware identity:
   - `ro.build.fingerprint`;
   - `ro.build.version.release`;
   - `ro.hw.dsp.chip`.

Do not substitute files from a different head-unit firmware unless their build fingerprint and module/library versions are explicitly recorded.

## Read-only collection commands

Run from a computer with ADB connected to the head unit. These commands only read files and properties; they do not send audio commands or write to CD3313.

```sh
adb shell getprop ro.build.fingerprint
adb shell getprop ro.build.version.release
adb shell getprop ro.hw.dsp.chip

adb shell 'for d in /system/framework /vendor/framework; do [ -d "$d" ] && find "$d" -type f \( -name "*.jar" -o -name "*.odex" -o -name "*.vdex" \); done 2>/dev/null' > framework-file-list.txt

adb shell 'for d in /system/lib /vendor/lib; do [ -d "$d" ] && find "$d" -type f \( -name "libaudiocompensationfilter_vendor.so" -o -name "libaudiocomponentengine_vendor.so" -o -name "libaudioprimarydevicehalifclient.so" \); done 2>/dev/null' > audio-library-file-list.txt

# Pull framework directories only if they exist on this firmware.
for p in /system/framework /vendor/framework; do
  adb shell "[ -d $p ]" && adb pull "$p" || true
done

# Pull only the three missing 32-bit vendor libraries when present.
for name in libaudiocompensationfilter_vendor.so libaudiocomponentengine_vendor.so libaudioprimarydevicehalifclient.so; do
  for base in /vendor/lib /system/lib; do
    adb pull "$base/$name" 2>/dev/null && break
  done
done
```

The directory pulls may be large. If bandwidth is limited, use the two file lists first and pull only the missing JARs and three named libraries. Preserve the original paths in the upload or archive.

If ADB cannot read a file, do not change permissions or remount partitions for this investigation. Record the exact path and error instead.

## Offline analysis sequence

Once the files are available:

1. Identify the JAR containing `android/audio/AudioEffect` using `jar tf` / DEX class inspection.
2. Decompile the class and enumerate every native method, Binder transaction, service name, and parameter conversion.
3. Resolve JNI registrations and native method implementations in the matching ARM32 libraries.
4. Trace the call to its actual transport: Binder, socket, or device node.
5. Correlate each method with the kernel command ID and exact payload size/word order.
6. Verify signedness, scaling, valid ranges, defaults, error reporting, and asynchronous completion behavior.
7. Implement a read-only capability probe first. Enable writes only after the complete mapping is supported by evidence and the target firmware identity matches.

## Safety rules

- No guessed ioctl magic, low request byte, size field, or payload structure.
- No experimental writes to `/dev/audiomux`.
- No direct CD3313 access from the PCM audio callback.
- Preserve the existing MTK HAL and its ACF/HCF/BES path.
- Keep CD3313 hardware controls separate from the four-channel PCM DSP.


## 4. Hidden framework API and Binder route recovered from boot-carservices.vdex

The files2 archive includes the optimized car-services boot image. Static
analysis of framework/boot-carservices.vdex recovers:

- android.audio.AudioEffect and its method signatures;
- ISourceService Binder interface, Stub and Proxy;
- SourceService.doExtAudioEffect(int, int[]);
- ExtAudioMuxer with nativeInit() and SendCmdAudioMux(int, int[]);
- the /dev/audiomux existence check in ExtAudioMuxer;
- the call from SourceService construction to ExtAudioMuxer.nativeInit().

The command IDs and integer-array payloads are documented in
[CD3313_HIDDEN_AUDIOEFFECT_API.md](CD3313_HIDDEN_AUDIOEFFECT_API.md).
There is now an exact high-confidence match between Java setBalance(FL,FR,RL,RR)
(command 21 / 0x15 with four integers) and the kernel driver's four-output
speaker-level handler. The Java bass, loudness, treble, input-gain and
subwoofer command IDs also match the corresponding implemented driver IDs.

The remaining missing component is not the Java framework API: it is the
native JNI implementation of ExtAudioMuxer.nativeInit() and
ExtAudioMuxer.SendCmdAudioMux(). That code constructs the ioctl request and
userspace buffer. The original low _IOC_NR byte and exact framing must be
recovered from the native library before a client is implemented.

Next files to scan from the full firmware tree:
- /system/lib/libandroid_servers.so;
- /system/lib64/libandroid_servers.so, if present;
- any system/vendor native library containing ExtAudioMuxer,
  SendCmdAudioMux, JNI registration strings or /dev/audiomux.

The library names above are search targets, not confirmed providers. Do not
guess an ioctl request from the Java command IDs alone.
