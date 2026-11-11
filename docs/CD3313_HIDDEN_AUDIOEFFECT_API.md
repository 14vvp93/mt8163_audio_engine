# CD3313 hidden AudioEffect API and native command route

## Evidence identity

The supplied files2 firmware archive contains:

- framework/boot-carservices.vdex — 343,992 bytes,
  SHA-256 af384924cc16755d9ffca03f713132774ad3a4f1b66734d1f4bec5671a931b58.
- framework/arm/boot-carservices.oat — 30,096 bytes,
  SHA-256 11168f5e2cbc1a18dfdf0dc357264258f999a7e145239cd80b062ec2c690b37e.

The VDEX is vdex019 and contains one cdex001 CompactDex image. Its class
metadata and bytecode were inspected directly. The recovered classes include
android.audio.AudioEffect, android.sourceservice.ISourceService,
ISourceService$Stub, ISourceService$Stub$Proxy, SourceService and
ExtAudioMuxer.

This is static firmware analysis. No Binder call, native command, ioctl,
I²C transaction or live audio setting was changed.

## 1. Recovered control-plane call chain

The recovered Java layer establishes the first four steps and strongly indicates the device-node target:

**android.audio.AudioEffect**
→ Binder interface **ISourceService.doExtAudioEffect(int, int[])**
→ **SourceService.doExtAudioEffect(int, int[])**
→ native **ExtAudioMuxer.SendCmdAudioMux(int, int[])**
→ likely **/dev/audiomux**
→ CD3313 kernel driver.

Evidence:
- AudioEffect holds its service in a field typed android.sourceservice.ISourceService.
- Its doExtAudioEffect(int, int[]) wrapper invokes the Binder method.
- SourceService.doExtAudioEffect() sends ordinary command IDs and their
  integer arrays to ExtAudioMuxer.SendCmdAudioMux().
- Command 1000 is special-cased: it writes the system property
  sys.audio.lr.balance as "%d,%d" from the first two array values and
  does not call the native muxer.
- ExtAudioMuxer.<clinit>() checks whether /dev/audiomux exists and
  stores the result in ExtAudioAvailable.
- SourceService.<init>() calls ExtAudioMuxer.nativeInit().
- SendCmdAudioMux(int, int[]) and nativeInit() are native methods.

The Java/Binder path and the Java-side /dev/audiomux existence check are
recovered. Because the native implementation is absent, its actual open/ioctl
calls and the final hop into this exact driver are strongly indicated but not
yet proven.

### Service lookup and app-access boundary

`AudioEffect.getInstance()` is a singleton factory. Its constructor calls
`ServiceManager.getService("sourceinfo")`, converts the returned Binder with
`ISourceService.Stub.asInterface()`, and stores it. If the service is absent,
the constructor throws a RuntimeException with the message
`can not find sourceinfo serivce`.

No calls to `Context.checkCallingOrSelfPermission()`,
`Binder.getCallingUid()` or `Binder.getCallingPid()` were found in the
recovered SourceService or ISourceService Stub methods. No explicit Java
permission check was identified in the doExtAudioEffect route. This does not
prove that an ordinary third-party app can call it: hidden-API enforcement,
service registration policy, SELinux and the native bridge still require a
read-only runtime probe on the target unit.

### Integration implication

If the app can obtain this Binder reference, it can call the OEM
AudioEffect methods through reflection. The ExtAudioMuxer JNI executes in the
system service process; the app does not need to bundle that native library
or open /dev/audiomux itself. The missing JNI binary is therefore required
for exact low-level ABI recovery, but may not be required for app integration.

The Android Test APK now includes a read-only probe that calls
AudioEffect.getInstance() and checks for the supported public method
signatures. It sends no setBalance, bass, treble, mute or other control
command. A successful probe confirms that the class loaded and a Binder
reference was obtained; it does not yet prove that a Binder transaction or
native device operation succeeds.

## 2. AudioEffect command builders

The Java methods build the following command arrays before invoking
ISourceService.doExtAudioEffect(). Boolean-like fields are derived from
whether the relevant input value is greater than zero.

| Java API | Command ID | Integer array sent |
|---|---:|---|
| setBalance(fl, fr, rl, rr) | **0x15 (21)** | [fl, fr, rl, rr] |
| setInputGain(channel, gain) | **0x03 (3)** | [channel, gain] |
| setBassBoost(value, extra) | **0x55 (85)** | [value>0, value, extra, 10] |
| setLoud(value) | **0x56 (86)** | [value>0, value, INT_MAX] |
| setTreble(value, extra) | **0x5C (92)** | [value>0, value, extra, 10] |
| setSubWoofer(value, extra) | **0xA3 (163)** | [value>0, value, extra, 10] |
| setSurround(value) | **0x57 (87)** | [value>0, value] |
| setCompressor(value) | **0x58 (88)** | [value>0, value, INT_MAX] |
| setFreqExpansion(value) | **0x59 (89)** | [value>0, value, INT_MAX] |
| setAlto(value, extra) | **0x5B (91)** | [value>0, value, extra, 10] |
| setEQValue(a,b,c,d) | **0x50 (80)** | [a,b,c,d] |
| setDelay(a,b,c,d,e,f) | **0x60 (96)** | [a,b,c,d,e,f] |
| setXOverLowFreq(a,b,c) | **0xA1 (161)** | [c>0, a, b, c] |
| setXOverHeighLowFreq(a,b,c) | **0xA2 (162)** | [c>0, a, b, c] |

setMasterVolume(float,float) is different: it converts its two inputs to
0..100 percentages and sends command 1000. The Binder service writes those
values to sys.audio.lr.balance; this method is not the CD3313 master-volume
ioctl.

## 3. Exact matches against the CD3313 driver

The Java command IDs match the driver's command IDs in the recovered
cd3313.ko dispatcher. The strongest correlations are:

| Java operation | Java command | Driver command | Correlation |
|---|---:|---:|---|
| Four-output setBalance | 21 | 0x15 | Exact: Java sends four integers; driver reads FL/FR/RL/RR indices |
| setInputGain | 3 | 0x03 | Exact command ID and channel/gain argument order |
| setBassBoost | 85 | 0x55 | Exact command ID; driver uses the second word for bass transition |
| setLoud | 86 | 0x56 | Exact command ID; driver uses the first word as loudness enable/state |
| setTreble | 92 | 0x5C | Exact command ID; driver uses the second word for treble transition |
| setSubWoofer | 163 | 0xA3 | Exact command ID; driver toggles GPIO 409 from the first word |

This confirms that the hidden setBalance(fl,fr,rl,rr) API is the userspace
four-output level interface for the CD3313. It is not a four-channel PCM
interface.

The driver also supports command 0x04 for per-input mute, 0x10 for
master-volume step, 0x11 for master-mute reason, 0x12 for volume percentage,
0x01 for input selection, and 0x20 for input mixing. The Java ExtAudioMuxer
class exposes matching builders for these commands.

### Important unsupported API calls

The Java class is shared across a wider vendor audio stack. The inspected
CD3313 driver does not implement write-dispatch cases for these enum
identifiers, even though the Java API exposes them:

- 0x50 EQ band;
- 0x57 surround;
- 0x58 compressor;
- 0x59 HFE/frequency expansion;
- 0x5B Alto;
- 0x60 delay;
- 0xA1 low crossover;
- 0xA2 high crossover.

The driver also has no recovered write-dispatch case for command 0x09,
which the separate ExtAudioMuxer.setMasterVolumeMax() wrapper emits.
Do not treat these Java methods as working CD3313 features on this firmware.
Their IDs may belong to another chip variant or a shared vendor interface.

For the commands the driver does implement, the payload semantics are
narrower than some Java method names imply:
- 0x56 uses the first word as a loudness on/off value; the remaining Java
  array values do not create a continuous loudness-level control.
- 0xA3 controls GPIO 409 as a boolean; it does not establish an LPF
  frequency or subwoofer EQ.
- 0x55 and 0x5C use the second word as a transition target. The first
  word's purpose remains unresolved in the optimized driver path.

## 4. ExtAudioMuxer source and volume controls

The Java ExtAudioMuxer wrapper emits:

| Method | Command ID | Array |
|---|---:|---|
| DoAudioMux(EnumSet<PATH>) | 0x01 | one selected input-channel ID |
| DoMix(enabled, value) | 0x20 | [enabled, 32, value] |
| setInputPathMute(channel, mute, reason) | 0x04 | [channel, mute, reason] |
| setMasterVolumeIndex(...) | 0x10 | one volume-step integer |
| setMasterMute(mute, reason) | 0x11 | [mute, reason] |
| setMastVolumePercentage(percent) | 0x12 | [percent] |
| setMasterVolumeMax(value) | 0x09 | [value] |

The input-source priority recovered from DoAudioMux() is:

| Priority (first matching path wins) | Source path | CD3313 channel ID |
|---:|---|---:|
| 1 | HFP | 1 (ARM) |
| 2 | RGEAR | 2 (AUX) |
| 3 | ONSTAR | 2 (AUX) |
| 4 | RADIO | 0 (RADIO) |
| 5 | RDS | 0 (RADIO) |
| 6 | AUX | 2 (AUX) |
| 7 | A2DP | 1 (ARM) |
| 8 | MUSIC | 1 (ARM) |
| 9 | AUX2 | 4 (AUX2) |
| 10 | DAB | 5 (DAB) |
| fallback | no recognized path | -1 (driver normalizes to channel 6) |

The recovered method does not select driver channel 3 (EXBT). That channel is
present in the kernel driver's seven-channel state map, but no mapping to it
was found in this Java method.

## 5. What remains missing

The following is now recovered:
- hidden Java API class and method signatures;
- Binder interface and service implementation;
- exact Java command IDs and integer-array layouts;
- source-selection priority;
- /dev/audiomux device-node check;
- the mapping of key Java commands to the CD3313 kernel dispatcher.

Still required before a safe userspace client can be implemented:
1. The native JNI implementation of ExtAudioMuxer.nativeInit() and
   ExtAudioMuxer.SendCmdAudioMux(int,int[]).
2. Confirmation that JNI uses ioctl rather than another transport; if ioctl,
   recover the request word (including low _IOC_NR), direction, size
   calculation and userspace buffer framing.
3. Runtime confirmation that the native method opens the same
   /dev/audiomux device created by this exact cd3313.ko.
4. A hardware readback or controlled test of source selection, mute, master
   volume and four-output attenuation.

The most useful next firmware files are the system-server/native libraries
that register android.sourceservice.ExtAudioMuxer JNI methods, especially
/system/lib/libandroid_servers.so and any library containing
ExtAudioMuxer, SendCmdAudioMux or /dev/audiomux strings. These names are
search targets, not an assertion that libandroid_servers.so is the
actual provider.

No guessed ioctl request is permitted. The existing host emulator remains
offline and must not be connected to the live character device until the
native ABI is recovered.

### State readback limitation

The recovered AudioEffect class exposes setters for the controls listed
above, but no matching getters for the four speaker attenuator indices.
Therefore the app cannot safely infer the current FL/FR/RL/RR values or
automatically restore hardware levels at startup. Any future hardware-level
UI must start in an unknown/unapplied state and require an explicit user
action. A locally saved value can only mean “last value this app sent”, not
verified current chip state.

### Return values

The typed AudioEffect control methods (including setBalance, setBassBoost,
setTreble, setLoud, setSubWoofer and setInputGain) return an integer status.
The public doExtAudioEffect(int, int[]) method also returns an integer. The
status is propagated from the Binder service/native muxer and can be logged
by the app; it is not a hardware readback. setMasterVolume(float,float) is
the exception and returns void because it updates a system property.

## 6. Java-array length versus kernel command length

A cross-layer comparison exposes two important size mismatches. The Java/Binder layer supplies these arrays:

| Command | Java array length | Kernel minimum size field | Kernel handler reads | Status |
|---|---:|---:|---:|---|
| 0x03 input gain | 2 words | 3 words | first 2 words | Native layer must account for one additional word |
| 0x12 master-volume percentage | 1 word | 2 words | first 2 words | Native layer must account for one additional word |
| 0x15 four-channel levels | 4 words | 2 words | 4 words | Kernel minimum is insufficient; safe client must send all 4 |
| 0x55 bass boost | 4 words | 2 words | first 2 words | Java supplies extra words not consumed by this handler |
| 0x5C treble | 4 words | 2 words | first 2 words | Java supplies extra words not consumed by this handler |

For 0x03 and 0x12, the Java array length alone cannot satisfy the recovered kernel dispatcher threshold if the native layer copies exactly that many words and sets the size field to the array length. The missing JNI implementation must therefore be inspected to determine whether it pads the payload, uses a fixed command-specific size, or applies another framing rule. The kernel's second 0x12 word is retained as opaque data by the emulator; its value and meaning are not established.

For 0x15, the inverse hazard exists: the kernel's length table accepts a two-word size field even though the handler reads four words. A safe native bridge must enforce four complete words regardless of the driver's weaker minimum.

These are cross-layer facts, not evidence of the native bridge's actual behavior. Do not infer padding values, ioctl size fields, or the low request-number byte until the JNI provider is recovered. The emulator's typed builders deliberately construct safe kernel-side frames and must not be treated as a byte-for-byte reproduction of the Java arrays.


## 7. Native JNI bridge recovered from firmware

### Binary identity

The firmware-library-analysis artifact contains the exact JNI provider:

- Path in source archive: `lib/lib/libcarservices_jni.so`
- ELF: 32-bit ARM, EABI5, Android 28, stripped
- Build ID: `bf391e545edd2e482b72cd8a431d1c59`
- Exported registration function:
  `android::register_android_server_ExtAudioMuxer(JNIEnv*)`
- Registration strings: `android/sourceservice/ExtAudioMuxer`,
  `nativeInit`, `SendCmdAudioMux`, `SendCmdVideoMux`
- The binary imports `__open_2`, `ioctl`, and `close`.

This closes the previous “native JNI implementation missing” gap. The observations below are from static ARM disassembly of this exact binary, not a live device trace.

### `nativeInit()`

The native initializer:

1. Opens `/dev/audiomux` with flags `0` (`O_RDONLY`).
2. Zero-initializes a 128-byte stack buffer.
3. Calls `ioctl(fd, 0x8080F000, buffer)`.
4. Closes the file descriptor.
5. Returns the ioctl result; if open fails, the observed return path is `-2`.

The request word is `0x8080F000`: direction READ (`2`), type `0xF0`, request number `0`, encoded size `0x80`. The CD3313 driver's read-direction ioctl branch accepts type `0xF0` and copies a fixed 8-byte response to userspace. The encoded size is therefore not the actual copy length in this driver branch. The returned 8 bytes are not yet semantically decoded.

### `SendCmdAudioMux(int command, int[] values)`

Static disassembly establishes this sequence:

1. Calls JNI `GetIntArrayElements(values, null)`.
2. Calls JNI `GetArrayLength(values)`.
3. Opens `/dev/audiomux` with flags `1` (`O_WRONLY`).
4. Builds the ioctl request from the command ID and Java array length.
5. Calls `ioctl(fd, request, int_array_elements)`.
6. Closes the descriptor.
7. Calls `ReleaseIntArrayElements(values, elements, JNI_ABORT)` (mode `2`).
8. Returns the ioctl result. An open failure takes the `-1` return path.

The exact request construction visible in ARM instructions is:

```c
request = 0x40000000u
        | ((array_length + 1u) << 16)
        | ((command & 0xffu) << 8);
ioctl(fd, request, values);
```

In Linux `_IOC` terms this is direction `_IOC_WRITE`, type = command ID, request number = `0`, and encoded size = `array_length + 1`. The low request-number byte is zero. The native method passes the JNI array-elements pointer directly as the ioctl argument; it does not construct a separate command/payload structure in userspace.

The native method also formats the integer array for diagnostic logging when its audio-muxer debug property is enabled. That logging path is separate from the ioctl payload.

### Cross-check against `cd3313.ko`

The recovered `cmd_ioctl` implementation independently confirms the request interpretation:

- Bits 30–31 select direction.
- Bits 16–29 are extracted as the size field and multiplied by four.
- Bits 8–15 are extracted as the CD3313 command ID.
- For WRITE requests, the driver stores the complete ioctl request word in its internal frame, then copies the encoded number of bytes from the userspace pointer.
- For the READ request with type `0xF0`, the driver copies a fixed 8-byte result.

This proves that the JNI bridge really does use `ioctl` on the same `/dev/audiomux` node created by the CD3313 driver. The previously uncertain final transport hop is now statically confirmed.

### Important length discrepancy — do not paper over it

The JNI code encodes `array_length + 1` words, while the pointer passed to the kernel is the pointer returned by `GetIntArrayElements` for a Java array containing exactly `array_length` integers. The driver's WRITE path multiplies the encoded size by four and copies all of those bytes. Consequently, the static code requests a copy of one 32-bit word more than the declared Java array length.

Examples:

| Command | Java array length | Encoded size field | Kernel copy size |
|---|---:|---:|---:|
| `0x03` input gain | 2 | 3 words | 12 bytes |
| `0x12` volume percentage | 1 | 2 words | 8 bytes |
| `0x15` four speaker levels | 4 | 5 words | 20 bytes |
| `0x55` bass | 4 | 5 words | 20 bytes |
| `0x5C` treble | 4 | 5 words | 20 bytes |

This is an observed native/kernel ABI discrepancy and a potential one-word userspace over-read. It is not evidence that the extra word has a known value or meaning. Do not invent padding, append a guessed zero, or copy this framing into a new client until the extra word is explained by runtime evidence or additional vendor code. The existing emulator's safe kernel-frame builders are not a byte-for-byte JNI reproduction.

### Current confidence after this finding

- JNI provider library: **confirmed**.
- Device path and open mode: **confirmed**.
- Use of `ioctl`: **confirmed**.
- Request direction, type field, request number and size formula: **confirmed by native disassembly**.
- Driver interpretation of those fields: **confirmed by kernel disassembly**.
- Extra-word discrepancy: **confirmed statically; runtime consequences unresolved**.
- Meaning of the 8-byte `nativeInit` response: **unknown**.
- Live hardware behavior: **not tested**.

No live audio command was sent and no hardware state was changed during this analysis.


### Reproducibility details and concrete request examples

SHA-256 of the extracted JNI binary:
`ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`.

The request words implied by the native formula are:

| Command | Array length | Native ioctl request |
|---|---:|---:|
| `0x03` | 2 | `0x40030300` |
| `0x12` | 1 | `0x40021200` |
| `0x15` | 4 | `0x40051500` |
| `0x55` | 4 | `0x40055500` |
| `0x5C` | 4 | `0x40055C00` |

These are request values reconstructed from the machine instructions; they are not permission to issue the commands against hardware. The size field is a count of 32-bit words in this vendor driver ABI, not a conventional byte count.

The CD3313 module's READ branch checks for type `0xF0` and copies exactly 8 bytes from its internal state to the caller. The backing state is not yet mapped to a documented chip identifier/name, so the two returned words remain opaque.


### Chip identity response decoded

The 8-byte READ response is now identified for the supplied CD3313 module. In `cmd_ioctl`, the READ/type-`0xF0` branch copies 8 bytes from the module's `.rodata` base. The first eight bytes of this exact `cd3313.ko` are:

```
63 64 33 33 31 33 00 00
 c  d  3  3  1  3 \\0 \\0
```

Therefore `nativeInit()`'s chip probe returns the ASCII identifier `cd3313` (NUL-padded to 8 bytes) through the output buffer, while the ioctl return code is logged separately as the `%d` field in `ExtAudioMuxer chip [%d] : [%s]`. This confirms the native bridge and this kernel module agree on the chip-identification query, not merely on the device-node name.

SHA-256 of the extracted `cd3313.ko` used for this cross-check:
`6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`.
