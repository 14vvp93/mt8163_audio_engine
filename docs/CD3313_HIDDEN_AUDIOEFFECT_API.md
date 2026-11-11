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

The class/field/method references establish this route:

**android.audio.AudioEffect**
→ Binder interface **ISourceService.doExtAudioEffect(int, int[])**
→ **SourceService.doExtAudioEffect(int, int[])**
→ native **ExtAudioMuxer.SendCmdAudioMux(int, int[])**
→ **/dev/audiomux**
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

The Java/Binder path and device-node name are therefore recovered. The
native implementation that builds the actual ioctl request is not present in
the supplied archive.

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
2. The exact ioctl request word, including the low _IOC_NR byte, direction,
   size calculation and userspace buffer framing.
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
