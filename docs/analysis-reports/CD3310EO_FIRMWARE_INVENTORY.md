# CD3310EO firmware inventory — read-only analysis

Date: 2026-11-13  
Target repository: `14vvp93/public_calculations`  
Analysis workflow: `public_calculations`, run `43238133570`  
Firmware-library artifact: `firmware-library-analysis`, SHA-256 `b5724b927f675fd32dc6f38339b0075bfb6e09d583a71ad46f7aef863e006942`  
Target device: MT8163 / Android 9 head unit  
Physical audio IC: **CD3310EO only** (user-confirmed)

## Executive result

The available firmware artifacts expose one clear external audio-DSP control implementation named CD3313. Its kernel module is bound to I2C address `0x44`, has I2C ID `cd3313`, and OF compatible `hcn,cd3313`. The recovered probe path sends a one-byte `0x3f` write and treats a successful one-byte I2C transfer as probe success before registering the `audiomux` misc device.

The inspected probe path does **not** show an explicit silicon-ID read. This makes the CD3313-named module a candidate software path to investigate against the physically populated CD3310EO, but it does not prove protocol or silicon compatibility. The module name, DT compatible and firmware comments identify the vendor's intended CD3313 target; they do not override the user's physical chip identification.

A second apparent candidate, `hxchipset-i2c.ko`, is conclusively a Himax touchscreen driver and is excluded from the audio investigation.

## 1. Physical board fact

The user explicitly confirmed that the physical head unit contains **CD3310EO only**. This is authoritative for this unit. No other external audio processor is considered physically populated.

## 2. CD3313 module — verified static facts

Artifact: `vendor/lib/modules/cd3313.ko`

- Size: 188,520 bytes in the extracted vendor tree.
- SHA-256: `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`.
- ELF: 32-bit ARM, EABI5, relocatable, with DWARF debug information.
- Build ID: `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`.
- Module metadata: description `DSP Spectrum Analyzer`, author `HCN`, vermagic `4.9.117+ SMP preempt mod_unload modversions ARMv7 p2v8`.
- DWARF source path: `drivers/misc/cd3313/cd3313.c`.
- I2C address constant: `CD3313_I2C_ADDR = 68` decimal, i.e. `0x44`.
- I2C device ID table contains `cd3313`.
- OF match string is `hcn,cd3313`.
- Driver registers a misc device named `audiomux`.

### Probe behavior

The recovered `dsp_probe` disassembly:

1. Constructs a one-byte initialization command `0x3f`.
2. Calls `cd3313_write_reg`.
3. Repeats the attempt up to three times when the I2C write does not return exactly one byte.
4. On success, configures several GPIOs, registers the `audiomux` misc device and starts its handler thread.

The write helper calls `i2c_master_send(client, buffer, 1)`; the probe's success condition is a one-byte transfer result. No explicit silicon-ID read is visible in this probe path. This is a limited static finding about the recovered probe function, not proof that no identity check exists anywhere else in the module.

### Interpretation

The firmware's CD3313 module is not automatically a valid CD3310EO driver. However, because its probe does not visibly validate a chip ID, its presence alone also cannot prove that the physical IC is CD3313. The only defensible next step is to compare the CD3310EO's documented/known I2C behavior, address and initialization protocol with this module, then establish whether the actual board's DT and runtime driver bind to it.

## 3. hxchipset-i2c false lead — excluded

Artifact: `vendor/lib/modules/hxchipset-i2c.ko`

- SHA-256: `9cd9d175f2a11d0cfd2617997515a555ec1a62a27f43155b8b141f11a096281f`.
- Module description: `Himax_common driver`.
- DWARF source paths are under `drivers/input/touchscreen/HXCHIPSET-I2C/`.
- Strings identify Himax touchscreen components and HX83102 variants.

This is a touchscreen driver, not an audio processor. It is excluded from the CD3310EO search. Its I2C-related name was a false positive.

## 4. Firmware startup and device-node references

- `init.mt8163.rc` lines 563–566 request `cd3313.ko`, `ak7604.ko`, `fy7604.ko` and `si475x.ko`.
- `factory_init.rc` lines 530–537 request CD3313/AK7604/FY7604 module variants.
- `ueventd.rc` line 139 sets permissions for `/dev/audiomux`.
- These are configuration and startup requests only. They do not prove successful module binding, physical population or current runtime ownership.
- The extracted vendor module inventory contains `ak7604.ko`, `cd3313.ko`, `fy7604.ko` and `hxchipset-i2c.ko` among the relevant names. The latter is the Himax touchscreen driver described above.
- The inspected `build.prop` has no CD3310/CD3310EO/audio-processor string match.
- A firmware-wide text search of the downloaded analysis artifact found no literal `CD3310` or `CD3310EO` string.

## 5. What remains unknown

- Whether CD3310EO accepts the CD3313 module's one-byte `0x3f` probe write.
- Whether CD3310EO uses I2C address `0x44` on this board.
- Whether the CD3313 command/register protocol is compatible with CD3310EO.
- Which module actually binds at runtime and owns `/dev/audiomux`.
- Whether the supplied DTBO overlay is active on the user's exact board.
- CD3310EO register map, power/reset/mute wiring and audio route ownership.

No web search result for the exact `CD3310EO` marking was found in the quick public search; no datasheet or register map is assumed.

## 6. Next investigation

1. Recover the CD3310EO manufacturer's datasheet or reliable board-specific documentation, if available.
2. Compare its I2C address and startup sequence with the CD3313 module's `0x44` / `0x3f` probe behavior.
3. Inspect the exact DTB/DTBO entries and GPIO properties for the board variant; correlate them with the physical board marking.
4. Trace the Audio HAL/JNI/Binder caller path to `/dev/audiomux`, and identify whether the vendor module is actually the control endpoint used by this unit.
5. Inspect all CD3313 module command handlers and register writes only after the compatibility question has a grounded answer.
6. If available, collect read-only runtime evidence from the target: loaded module list, `/sys/bus/i2c/devices`, `/sys/bus/i2c/drivers`, `/dev/audiomux` major/minor and open-file ownership. Do not issue control writes.

## Safety boundary

No guessed I2C address/register write, ioctl, module load, GPIO operation, mixer write or flash operation is permitted. Keep all work read-only until chip compatibility, bus ownership, board wiring and runtime ownership are independently established.

## Evidence labels

- Physical IC population: **USER CONFIRMED** — CD3310EO only.
- CD3313 module presence and metadata: **STATIC FILE VERIFIED**.
- CD3313 module I2C address / ID / OF compatible: **STATIC ELF/DWARF VERIFIED** — `0x44`, `cd3313`, `hcn,cd3313`.
- CD3313 probe transfer behavior: **STATIC DISASSEMBLY VERIFIED** — one-byte `0x3f` write, retry, success on one-byte transfer.
- CD3313 compatibility with CD3310EO: **UNKNOWN**.
- hxchipset-i2c role: **STATIC ELF/DWARF VERIFIED** — Himax touchscreen; not audio.
- Runtime `/dev/audiomux` owner: **UNKNOWN**.
- CD3310EO register map/protocol: **UNKNOWN**.
- Hardware/device validation: **NOT PERFORMED**.

## 7. Userspace client scan — string-table leads only

The `audiomux-userspace-client-scan` artifact from workflow run `43238133570` (artifact SHA-256 `2f463e328bb54592bc2369a4ea25024d0b5f36601abc345504e70996c3ed1bea`) reports these string-table hits:

- `system/framework/boot-carservices.vdex`: `/dev/audiomux`, `DoAudioMux`, `ExtAudioMuxer`, `ExtAudioMuxer.java`, `SendCmdAudioMux`, and `cd3313`.
- `system/AudioInputSource.vdex`: class descriptor `Landroid/sourceservice/ExtAudioMuxer;`.
- The scan did not recover method-level VDEX callsites, command IDs or actual integer-array payloads. These are string-table leads only; do not infer that the named class actually sends a specific command or that its implementation matches the CD3313 module.

This confirms that the framework contains an audiomux-facing client surface, but the call graph and runtime caller remain unresolved.

## 8. Method-level audiomux path recovered from Android 9 CompactDex

The earlier string-only conclusion is superseded by method-level evidence in the same firmware-analysis artifact. The artifact already contains a VDEX 019 extraction/disassembly of `boot-carservices.vdex`; its `carservices-vdex/disassembly.log` identifies the class and method bodies. The extracted class payload is `boot-carservices_classes.cdex` (SHA-256 `ce6b18430de96b1e99c929316cfbadb2463e56bb3bdd366df32842b5f08f800e`). This is static firmware evidence, not evidence of the user's live runtime.

### Recovered call chain

1. `android.audio.AudioEffect` obtains the Binder service named `sourceinfo` through `ServiceManager.getService("sourceinfo")`.
2. Its public `doExtAudioEffect(int, int[])` method calls `ISourceService.doExtAudioEffect(int, int[])`.
3. The `SourceService.doExtAudioEffect(int, int[])` implementation forwards the command and array to `ExtAudioMuxer.SendCmdAudioMux(int, int[])`, except command `1000`, which is handled as a special software-property operation.
4. `ExtAudioMuxer` declares native `SendCmdAudioMux(I[I)I`, `nativeInit()V` and `SendCmdVideoMux(String)I`. Its static initializer checks whether `/dev/audiomux` exists and stores the result in `ExtAudioAvailable`.
5. `libcarservices_jni.so` contains the JNI registration for these three native methods and the `/dev/audiomux` string. The native client path is therefore linked to the device node at the binary level.

The call chain proves that this firmware build contains a framework/Binder/JNI client for `/dev/audiomux`. It does **not** prove that this client or the CD3313-named kernel module is active on the user's physical CD3310EO board.

### Framework wrapper command map

The following command IDs and Java array lengths are recovered from the `AudioEffect` and `ExtAudioMuxer` method bytecode. Payload entries are listed in the order written by the Java wrapper. This table records what the firmware client constructs; it is not a CD3310EO protocol specification.

| Command | Framework method | Java int[] length | Recovered payload construction |
|---|---|---:|---|
| `0x01` | `DoAudioMux(EnumSet)` | 1 | Source selector: HFP/A2DP/MUSIC = 1; RGEAR/ONSTAR/AUX = 2; RADIO/RDS = 0; AUX2 = 4; DAB = 5; unmatched route = -1 |
| `0x03` | `AudioEffect.setInputGain(int,int)` | 2 | Two arguments, in order |
| `0x04` | `setInputPathMute(int,boolean,int)` | 3 | Path, mute flag, third argument |
| `0x09` | `setMasterVolumeMax(int)` | 1 | Maximum value |
| `0x10` | `setMasterVolumeIndex(int,int,int,int)` | 1 | Fourth method argument; sent only on selected active-stream conditions |
| `0x11` | `setMasterMute(boolean,int)` | 2 | Boolean flag, second argument |
| `0x12` | `setMastVolumePercentage(int)` | 1 | Percentage value |
| `0x15` | `AudioEffect.setBalance(int,int,int,int)` | 4 | Four arguments, in order |
| `0x20` | `DoMix(boolean,int)` | 3 | Enabled: [1, 32, second argument]; disabled: [0, 32, 0] |
| `0x50` | `AudioEffect.setEQValue(int,int,int,int)` | 4 | Four arguments, in order |
| `0x55` | `setBassBoost(int,int)` | 4 | [first argument > 0 ? 1 : 0, first argument, second argument, 10] |
| `0x56` | `setLoud(int)` | 3 | [argument > 0 ? 1 : 0, argument, 0x7fffffff] |
| `0x57` | `setSurround(int)` | 2 | [argument > 0 ? 1 : 0, argument] |
| `0x58` | `setCompressor(int)` | 3 | [argument > 0 ? 1 : 0, argument, 0x7fffffff] |
| `0x59` | `setFreqExpansion(int)` | 3 | [argument > 0 ? 1 : 0, argument, 0x7fffffff] |
| `0x5B` | `setAlto(int,int)` | 4 | [first argument > 0 ? 1 : 0, first argument, second argument, 10] |
| `0x5C` | `setTreble(int,int)` | 4 | [first argument > 0 ? 1 : 0, first argument, second argument, 10] |
| `0x60` | `setDelay(int,int,int,int,int,int)` | 6 | Six arguments, in order |
| `0xA1` | `setXOverLowFreq(int,int,int)` | 4 | [second argument != 0 ? 1 : 0, first argument, second argument, third argument] |
| `0xA2` | `setXOverHeighLowFreq(int,int,int)` | 4 | [first argument > 0 ? 1 : 0, second argument, third argument, fourth register argument] |
| `0xA3` | `setSubWoofer(int,int)` | 4 | [first argument > 0 ? 1 : 0, first argument, second argument, 10] |

Command `1000` (`0x3e8`) is a special case in `SourceService.doExtAudioEffect`: it reads the first two array elements and writes `sys.audio.lr.balance` as a comma-separated system property. It does not proceed to `SendCmdAudioMux` in that branch. The framework method named `setMasterVolume(float,float)` scales both floats to integer percentages in the range 0..100 before calling this special case; the method name is not evidence that command 1000 is a hardware master-volume command.

The service also exposes the generic `doExtAudioEffect(int,int[])` Binder method. For command values other than the special `1000` branch, the service forwards the supplied command and array rather than mapping only a fixed set of high-level methods. Thus the high-level wrapper list is not necessarily the complete set of commands a Binder client can request.

### Direct framework callers found

- `SourcePolicy.onNormalAudioPath`, `onBluetoothConnectStatus` and `onBluetoothVoiceStatus` call `DoAudioMux`.
- `SourcePolicy.onMixingStatus` calls `DoMix`.
- `McuService` methods call `setInputPathMute`, `setMasterMute`, `setMasterVolumeIndex` and `setMasterVolumeMax`.
- `CarStatusService` calls `setMasterMute`.
- A settings observer calls `setMastVolumePercentage`.
- `AudioEffect` exposes the generic command/array Binder wrapper; the class contains concrete wrappers for commands `0x03`, `0x15`, `0x50`, `0x55`–`0x60`, and `0xA1`–`0xA3`.

### Native framing and remaining limits

The paired JNI/kernel analysis already recorded the legacy framing defect: the native ioctl request encodes one more 32-bit word than the Java array contains. The newly recovered method bodies now provide concrete array lengths for the listed wrappers, but do not make the legacy over-read safe. A replacement must use a separately bounded and initialized compatibility frame or a deliberately revised ABI; it must not reproduce the factory over-read.

Still unresolved:

- Whether this exact framework/JNI/kernel path is selected and loaded on the user's physical board.
- Whether the physical CD3310EO shares the CD3313 module's I2C address, initialization sequence or command protocol.
- Whether the board's active device tree and runtime module ownership correspond to this firmware artifact.
- The semantic ranges and hardware effects of every opaque argument; the bytecode reveals payload construction, not all valid values or chip-level meaning.
- The full set of non-framework clients that may call the generic Binder method.

**Safety status:** all findings above are static artifact analysis. No ioctl, I2C write, module load, GPIO/mixer operation or hardware probe was performed. CD3313 command IDs must not be sent to the CD3310EO until compatibility is independently proven.

