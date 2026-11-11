# Carservices Java Audio Flow

## Source and evidence

This note is based on the successful GitHub Actions run `43104002118` and its artifact `firmware-library-analysis`, specifically:

- `carservices-vdex/disassembly.log`
- `carservices-vdex/targeted-hits.txt`
- `NATIVE_AUDIO_SCAN.txt`

The VDEX input SHA-256 is recorded in `carservices-vdex/sha256.txt`.

## Confirmed Java-to-native audio path

`SourceService.doExtAudioEffect(int, int[])` treats command ID `1000` specially: it reads the first two array elements and writes them as a comma-separated value to system property `sys.audio.lr.balance`. This branch returns without calling `ExtAudioMuxer.SendCmdAudioMux`.

For all other command IDs, the method forwards the original command ID and integer array to `ExtAudioMuxer.SendCmdAudioMux(int, int[])`.

The native method is declared in `ExtAudioMuxer` as a static native method. This confirms the Java dispatch boundary; the native transport details are documented separately in `CD3313_IOCTL_ABI.md`.

## Mute command wrappers

The VDEX disassembly confirms these wrappers:

| Java method | CD3313 command | Integer array |
|---|---:|---|
| `ExtAudioMuxer.setInputPathMute(int, boolean, int)` | `0x04` | `[inputPath, mute, reason]` |
| `ExtAudioMuxer.setMasterMute(boolean, int)` | `0x11` | `[mute, reason]` |

These are Java-side payload shapes. They do not resolve the native driver's extra ioctl-size word or establish that every input-path identifier is supported by every driver build.

## ACC and Reverse integrations

### Carletter

`CarletterInterface.sendAccState(Context, int)` writes the supplied value to `Settings.System` under `acc_state`, then notifies observers of that setting URI. This is a settings/observer integration, not a direct CD3313 ioctl.

`CarletterInterface.sendReverseState(Context, int)` checks `Settings.System` key `audio_rev_volume`. When the value is positive, it stores the reverse state in `carletter_reserve_state` and notifies observers. When the setting is zero or negative, this method returns without updating that setting.

### Zlink

`CarZlinkInterface.sendReverseState(int)` sends package-scoped broadcasts to `com.zjinnova.zlink`:

- `com.zjinnova.zlink.action.BACKCAR_START` when the supplied reverse state is positive.
- `com.zjinnova.zlink.action.BACKCAR_STOP` otherwise.

It also reads `audio_rev_volume` and sets the broadcast extra `muteMedia` according to that setting.

`CarZlinkInterface.sendAccState(int)` contains additional Zlink-specific power/USB handling. On ACC-off it sends a package-scoped `POWER_OFF` broadcast, disables the Zlink service property, and force-stops the Zlink package. On ACC-on it inspects its configured USB path and may write `host` to that path when the detected mode is `device`. This is Zlink integration and should not be confused with the MCU ACC GPIO input itself.

## Radio mute integration

The VDEX shows the FM radio path updating its internal `mIsMuted` state and invoking `ExtAudioMuxer.setInputPathMute(...)`; it also sends `mute=10001000` or `mute=10000000` through `McuService.sendCmdCarStatus(String)`.

The exact semantic meaning of each numeric mute mask and the full mapping of the three input-path arguments remain separate ABI questions. Do not infer them from the string literals alone.

## Important separation of control planes

- `/dev/audiomux` and CD3313 ioctl commands are the DSP-control plane.
- `/dev/carstatus` string commands are the car-status/MCU integration plane.
- `Settings.System` values and ContentResolver notifications are Android settings/observer integrations.
- Zlink and Carletter broadcasts are third-party projection-app integrations.

These planes interact, but they are not interchangeable. Keep their interfaces separately modeled in the emulator and any future device backend.

## Remaining work

1. Decode the full `CarStatusService` ACC transition handler and its delayed messages.
2. Trace every caller of `setMasterMute` and identify the reason values used at each call site.
3. Decode the FM radio input-path arguments and relate them to driver command `0x04`.
4. Continue matching the native Audio HAL and audio policy routing to the Java service layer.
5. Keep all real-device control writes disabled until the complete payload and ioctl ABI is proven.


## Detailed ACC transition handling

The CarStatusService disassembly resolves the ACC transition timing and several state guards.

### Input and event handling

- The kernel uevent observer removes any pending status-read message (what=4) and schedules a fresh read after 10 ms.
- The boot-completed receiver starts observing DEVPATH=/devices/platform/carstatus, then requests an immediate status read and schedules additional startup work. The status read is therefore not dependent on a future ACC edge.
- readStatus() opens /dev/carstatus, reads up to 128 bytes, parses the acc field, and calls setAccStatus(value) only when the parsed value is non-negative.
- setAccStatus() ignores calls before boot completion and suppresses duplicate values using mReadacc. The first valid value is also published to the status map/file.
- Before scheduling a new transition, it removes pending MSG_ACC_DELAYED (what=2) and MSG_GOINGTO_SLEEP (what=5) messages. It then queues MSG_ACC_DELAYED with the new ACC value in arg1, delayed by 2,500 ms. A newer input therefore replaces pending delayed transition work.

### ACC OFF path

When the accepted ACC value is zero, setAccStatus() immediately:
1. Sends the MCU car-status string mute=10001.
2. Calls ExtAudioMuxer.setMasterMute(true, 2).
3. Removes pending MSG_ACC_UNMUTE (what=11) so a stale delayed unmute cannot undo the OFF mute.

The delayed ACC handler (MSG_ACC_DELAYED, 2,500 ms) invokes onAccStatusInner(0). That method reads Settings.System key power_down_afteraccoff, with a default of 30,000 ms, and schedules MSG_GOINGTO_SLEEP (what=5) for that delay. It also publishes the ACC status and performs related service/power-management work. The actual sleep path is separate from the immediate mute.

### ACC ON path

When the accepted ACC value is one, the same 2,500 ms delayed transition invokes onAccStatusInner(1). The ON branch:
- schedules message 10 after 500 ms and message 12 after 1,000 ms;
- calls RadioService.nativeSetPower(1);
- sends usb5v=1 through McuService.sendCmdCarStatus;
- invokes storage mounting;
- starts configured ACC services, including the configured com.hcn.autoeq remote-control service and link service;
- applies board-dependent amplifier/fan handling.

The delayed unmute handler checks that ACC is still ON before sending mute=10000 and calling ExtAudioMuxer.setMasterMute(false, 2). The OFF path explicitly removes this pending message. This establishes reason value 2 at both the immediate ACC-OFF mute and delayed ACC-ON unmute call sites.

### Engineering implications for mt8163_tools

1. Treat the kernel event as a hint to reread state, not as the state payload itself. The service's uevent callback does exactly that.
2. Preserve an initial read after boot and after observer registration; do not rely only on edges.
3. Debounce state changes and cancel stale delayed work when a newer value arrives. The firmware uses a 2,500 ms delayed ACC transition.
4. Keep ACC state separate from power-off scheduling. The default delayed sleep is 30 seconds, configurable through power_down_afteraccoff.
5. For the app, do not copy the firmware's power, USB, mute, or sleep side effects. Only consume the state and expose the required automation behavior.
6. The firmware's sendBroadcastAsUser action is emitted from onAccStatusInner, after the 2,500 ms delay, rather than directly in the kernel uevent callback. Therefore the Android broadcast is not necessarily an immediate ACC edge notification.

These timings and actions are firmware observations, not a recommendation to reproduce its power-control behavior in the utility app. Validate actual timing on the target device before changing the app's current polling fallback.


## Complete setMasterMute call-site inventory

A search of the complete carservices VDEX disassembly finds seven Java call sites for ExtAudioMuxer.setMasterMute(boolean, int). They group into three independent control sources:

| Source method | Boolean mute value | Reason | Condition / role |
|---|---:|---:|---|
| CarStatusService.setAccStatus | true | 2 | Immediate ACC-OFF mute |
| CarStatusService delayed ACC unmute handler | false | 2 | Delayed unmute only while ACC remains ON |
| McuService.SilenceMode | true | 1 | Silence mode becomes active |
| McuService.SilenceMode | true | 24 | Exit/transition branch applies a temporary mute |
| McuService.SilenceMode | false | 1 | Clears the SilenceMode-owned mute in that branch |
| SourceService.onBluetoothVoiceStatus | true | 3 | Bluetooth HFP voice-state transition |
| SourceService.onBluetoothVoiceStatus | false | 3 | Releases the temporary Bluetooth voice mute when the path/volume logic allows it |

The VDEX proves the numeric reason arguments and call-site conditions. The matching cd3313.ko has since been disassembled and confirms that each reason is a bit index in a mute mask, with no per-reason reference counter. See the driver confirmation section below. This is firmware-specific behavior, not a public stable ABI for unrelated devices.

This inventory accounts for all seven textual call sites in the analyzed disassembly; no other ExtAudioMuxer.setMasterMute call sites were found in this VDEX.


## FM input-path mute arguments

The complete VDEX search finds seven calls to ExtAudioMuxer.setInputPathMute(int, boolean, int). RadioService consistently uses Java input-path ID 0; this is the firmware's Java-side identifier for its radio mute path. The disassembly does not yet prove which physical CD3313 input this ID selects.

| Caller | Java payload | Confirmed behavior |
|---|---|---|
| RadioService constructor | [0, true, 16] | Initializes the radio path muted |
| RadioService.sendFreq | [0, true, 1], then [0, false, 1] | Mutes before retuning, then unmutes after the selected delay |
| RadioService.onMessage (FM mute status) | [0, packet_bit_0x02, 2] | Mirrors the decoded FM mute flag; the same branch sends MCU mask mute=10001000 when true or mute=10000000 when false |
| RadioService.scan | [0, true, 2] | Mutes the radio path before starting a scan |
| RadioService.setMute(boolean) | [0, caller_value, 4] | Exposes a reason-4 radio-path mute wrapper |
| SourceService.setInputPathMute(path, mute) | [path, mute, 4] | Generic Binder-facing wrapper; caller supplies path and mute state |

In sendFreq(), the code waits 120 ms after muting before calling nativeSetFreq(). It then waits 600 ms when the new frequency's FM-range classification differs from the current frequency, otherwise 100 ms, before unmuting with reason 1. This is a firmware timing detail, not a safe universal delay for a new client.

The FM mute-status handler decodes the relevant boolean from bit 0x02 of a radio MCU packet, calls setInputPathMute(0, decodedFlag, 2), and sends the corresponding MCU mute mask. The scan path also uses reason 2, but for a separate operation. Therefore the numeric reason alone is not enough to identify why the radio path is muted.

The driver command 0x04 is known to receive the Java payload shape [inputPath, mute, reason], but the mapping from Java path ID 0 to a physical CD3313 source remains unproven. Keep that mapping explicitly unresolved until the matching cd3313.ko handler is disassembled and checked against runtime routing.


## Audio HAL and policy cross-check (FM path)

The firmware artifact also contains vendor/audio_policy_configuration.xml and vendor/audio.primary.mt8163.so. These provide a second, lower-level view of the FM route:

- The active-looking primary module policy declares FM Tuner In as an attached input device. Its input profile supports PCM 16-bit at 8, 16, 32, 44.1, and 48 kHz, mono or stereo.
- The policy routes FM Tuner In as a source to Speaker, Wired Headset, and Wired Headphones. The default output device is Speaker. FM Tuner Out is separately declared as an output sink for primary output; do not confuse FM Tuner In with FM Tuner Out.
- The stripped primary HAL still exports symbols for AudioALSAFMController::setFmEnable, routing, setFmDirectConnection_l, setFmVolume, AudioALSACaptureHandlerFMRadio, AudioALSACaptureDataProviderFMRadio, and AudioALSAPlaybackHandlerFMTransmitter.
- WCNChipController symbols and strings show a separate FM-chip control interface: /dev/fm and /proc/fm, with strings for FM_IOCTL_GET_AUDIO_INFO, FM_IOCTL_I2S_SETTING, and FM_IOCTL_SETVOL. The HAL also contains FM_I2S_Playback and FM_I2S_Capture modes and reads FM chip I2S pad/mode/rate information.
- AudioALSAStreamManager exposes setFmEnable(bool, bool, bool, unsigned int) and setFmVolume(float), delegating into the dedicated FM HAL path.

This establishes a policy/HAL FM route and a separate FM-chip control interface. It does not yet establish the exact physical wiring, selected I2S pads, or whether the CD3313 Java input-path ID 0 is the same physical signal as policy device FM Tuner In. The CD3313 /dev/audiomux mute plane, the FM tuner chip /dev/fm control plane, and Android audio-policy routing must remain separate in the model until the HAL routing implementation and board configuration are fully correlated.

The legacy vendor/audio_policy.conf also lists AUDIO_DEVICE_IN_FM_TUNER among primary input devices, but its surrounding global section is commented generic configuration. Prefer the XML policy for the current route description and verify the actual runtime-selected policy file on the target image before treating either file as definitive.


## CD3313 driver confirmation: mute reasons and input-path IDs

The matching vendor_lib/lib/modules/cd3313.ko was extracted from the same firmware artifact and disassembled. Its Build ID is e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56, matching the previously recorded module.

This resolves an important part of the mute ABI:

- Command 0x04 is dispatched by event_callback as input-path mute. The payload fields are read as input path index, mute boolean, and reason index, matching the Java call order [path, mute, reason].
- The driver keeps a seven-entry channel_mute array (7 x 32-bit values). The supplied input path is used as an index into that per-channel mute-reason state. Therefore Java path ID 0 is confirmed as CD3313 logical input/channel index 0, not yet as a named physical connector or tuner pin.
- For a mute request, the driver sets bit (1 << reason) in that channel's mask; for unmute it clears that bit. When the affected path is the currently selected channel, the driver propagates the effective channel mute to master mute using reserved reason 31.
- The separate set_master_mute() function also treats its reason argument as a bit index: it sets or clears (1 << reason) in the global master_mute mask, then derives the output mute state and drives GPIO 393 (0x189). The master-mute reasons are therefore bit flags, not arbitrary descriptive integers.
- There is no per-reason reference counter in this function: state is represented by one bit per reason. A caller that clears a reason bit clears that reason globally; callers sharing a reason must coordinate their set/clear lifecycle.
- Input-path reason bits and global master-mute reason bits are separate state domains. For example, input-path reason 2 is stored in channel_mute[path], then may be reflected through global master reason 31; it is not the same storage bit as global master reason 2 used by ACC.

This confirms the driver's mute arbitration mechanism but does not identify the physical source wired to logical path 0. The board-level mapping still requires DTS/pin/routing data or controlled read-only runtime observation. Do not write to /dev/audiomux or drive GPIO 393 during investigation.


## FM-to-CD3313 logical input correlation

The Java RadioService call sites and the existing CD3313 driver reconstruction can be correlated at the logical channel level, with an important build-identity qualification:

- RadioService uses inputPath ID 0 for its FM mute operations (constructor, retune, MCU mute-state handling, scan, and the generic radio mute wrapper).
- The earlier detailed CD3313 reconstruction (module SHA-256 `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb`) records the character-device channel-name table as logical channel 0 = RADIO.
- That same reconstructed module maps logical channel 0 to CD3313 chip Source 3 (selector code 2). The datasheet identifies Source 3 as one of the three externally pinned stereo inputs.
- The paired `cd3313.ko` extracted from the newer firmware-library artifact is a different build (Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`). Its matching channel-name and selector mapping has not yet been independently revalidated against that exact binary.

Thus the current best correlation is RadioService inputPath 0 → earlier CD3313 logical channel 0 / RADIO → chip Source 3. Treat the RADIO/Source 3 mapping as a strong cross-build lead, not yet a proven identity for the newer paired module. It still does not prove the board-level wire from the FM tuner to the CD3313 Source 3 pins, nor that the Android Audio Policy FM Tuner In PCM route and the CD3313 analog RADIO route are the same signal path. The primary HAL exposes a separate FM I2S path and /dev/fm tuner control plane; that route must not be conflated with the CD3313 analog input selector without board wiring or live routing evidence.

The selector map and channel labels from the earlier module are documented in [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md) and [CD3313_COMMAND_DISPATCH.md](CD3313_COMMAND_DISPATCH.md). The Java-side FM path and payloads are documented above. This remains static firmware evidence, with the newer-module comparison open.


## Revalidation against the exact paired CD3313 module

The compact `cd3313-current-module-analysis` artifact from workflow run `43113850964` now provides a direct check against the CD3313 module paired with this firmware, rather than relying only on the earlier reconstructed build.

- Current module Build ID: `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`.
- Current module SHA-256: `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`.
- Its `.rodata` name-pointer table contains seven entries in this exact order: `RADIO`, `ARM`, `AUX`, `EXBT`, `AUX2`, `DAB`, `NONE`.
- Its BSS symbol `channel_mute` is 28 bytes, i.e. seven 32-bit channel mute masks. The `internal_radio` state is also present.
- The DWARF debug information names a `channel_to_cd3313(channel)` inline helper at source line 411, but this module's symbol/disassembly output does not expose a standalone body for that helper. The exact logical-channel-to-chip-selector translation therefore remains to be recovered from the optimized code/data tables.

This directly confirms that the current paired driver labels logical channel index 0 as `RADIO`, which matches RadioService's Java inputPath 0. The earlier Source 3 selector mapping came from the older detailed CD3313 reconstruction; do not automatically carry that selector value across builds. For the current paired binary, the verified conclusion is `RadioService inputPath 0 → CD3313 logical channel 0 (RADIO)`; the hardware selector/pin and board-level FM wiring remain open.


## MT8163 FM HAL mode selection and I2S metadata

The focused disassembly of `vendor/audio.primary.mt8163.so` from successful workflow run `43114231310` resolves the FM mode checks more precisely.

### FM-chip metadata

`WCNChipController::InitAudioFMInfo()` opens the FM control node (the HAL string table contains `/dev/fm`) and issues ioctl request `0xC004F530`, identified by the adjacent HAL strings as `FM_IOCTL_GET_AUDIO_INFO`. It reads the returned FM audio-info structure, then closes the file descriptor. If opening the node fails, it retries up to 30 times with a 100 ms sleep between attempts.

The controller exposes these checks over the returned structure:

- `IsFMMergeInterfaceSupported()` reads `aud_path` at structure offset 12; it returns true when the value is 2, and treats value 3 as `FM_AUD_ERR`. `InitAudioFMInfo()` separately asserts that the path is not `FM_AUD_ANALOG`. This confirms a distinct merge-interface capability flag; it does not by itself identify the board wiring.
- `IsFmChipPadSelConnSys()` reads `i2s_pad` at offset 28; value 0 returns true, value 2 is the error enum. The HAL string table names the pad choices `FM_I2S_PAD_CONN`, `FM_I2S_PAD_IO`, and `FM_I2S_PAD_ERR`.
- `IsFmChipUseSlaveMode()` reads I2S mode at offset 20; value 1 returns true, value 2 is the error enum. The corresponding labels are `FM_I2S_MASTER`, `FM_I2S_SLAVE`, and `FM_I2S_MODE_ERR`.
- `GetFmChipSamplingRate()` reads the rate field at offset 24 and treats enum value 3 as `FM_I2S_SR_ERR`. The HAL contains labels `FM_I2S_32K`, `FM_I2S_44K`, and `FM_I2S_48K`; the numeric-to-rate lookup still needs relocation-aware verification.

### Direct connection and capture paths

`AudioALSAFMController::setFmDirectConnection_l(bool, bool, unsigned int)` consults `IsFMMergeInterfaceSupported()`, resolves ALSA card and PCM indices through `AudioALSADeviceParser`, and manages a PCM handle with `pcm_open()` / `pcm_start()`. Its disable path calls `pcm_stop()` and `pcm_close()`. The HAL string table contains the device identifiers `FM_I2S_Playback` and `FM_I2S_Capture`; the precise card/PCM string pairing selected by each metadata branch is still being resolved.

Separately, `AudioALSACaptureDataProviderFMRadio::open()` also consults the merge-interface flag, chooses one of two card/PCM name pairs, resolves them through the same parser, opens the stream with flag `0x10000000` (`PCM_IN`), starts it, and creates a capture thread. This is the AP-side FM capture path used by the Audio HAL. It is distinct from the CD3313 driver's logical RADIO input mute path.

### Current architecture conclusion

There are at least three cooperating but separate FM-related interfaces in this firmware:

1. `/dev/fm` — FM tuner/chip metadata and I2S configuration ioctls handled by `WCNChipController`.
2. ALSA PCM endpoints `FM_I2S_Playback` / `FM_I2S_Capture` — HAL direct-connection and AP capture paths.
3. `/dev/audiomux` / CD3313 logical channel `RADIO` — the DSP input mute/source-control plane used by `RadioService`.

The XML audio policy additionally declares `FM Tuner In` and routes it to output devices. Static evidence does not yet prove whether a given board configuration feeds the same physical FM signal to both the CD3313 RADIO input and the AP-side FM I2S capture path. Do not merge these paths in `mt8163_tools` until the actual board routing and runtime policy selection are verified.


## Exact FM HAL virtual table and sample-rate enum

The exact primary HAL binary from successful workflow run `43114692814` was downloaded and its Android packed `.rel.dyn` section decoded (APS2 format, 2,431 relocation entries). This makes the `WCNChipController` vtable readable instead of treating the zero-filled on-disk `.data.rel.ro` bytes as actual function pointers.

- HAL Build ID: `f66eaf9e6ac950259f0c253be618d15a`.
- Vtable symbol: `_ZTVN7android17WCNChipControllerE`, VMA `0x001906c4`, size 96 bytes.
- The vptr begins at VMA `0x001906cc`; virtual method offset `+0x1c` is conclusively `IsFMMergeInterfaceSupported()`, matching the call sites in `AudioALSAFMController::setFmDirectConnection_l()` and the FM capture provider.

Resolved vtable order (offset relative to the vptr):

| Offset | Method |
|---:|---|
| `+0x00` | `WCNChipController::~WCNChipController()` (D2) |
| `+0x04` | `WCNChipController::~WCNChipController()` (D0) |
| `+0x08` | `GetFmChipPowerInfo()` |
| `+0x0c` | `SetFmChipVolume(unsigned int)` |
| `+0x10` | `InitAudioBTInfo()` |
| `+0x14` | `InitAudioFMInfo()` |
| `+0x18` | `IsFmChipUseSlaveMode()` |
| `+0x1c` | `IsFMMergeInterfaceSupported()` |
| `+0x20` | `IsBTMergeInterfaceSupported()` |
| `+0x24` | `IsFmChipPadSelConnSys()` |
| `+0x28` | `GetFmChipSamplingRate()` |
| `+0x2c` | `SetFmChipSampleRate(unsigned int)` |
| `+0x30` | `SetBTCurrentSamplingRateNumber(unsigned int)` |
| `+0x34` | `BTChipHWInterface()` |
| `+0x38` | `BTUseCVSDRemoval()` |
| `+0x3c` | `BTChipSamplingRate()` |
| `+0x40` | `BTChipSyncFormat()` |
| `+0x44` | `BTChipSyncLength()` |
| `+0x48` | `BTChipSecurityHiLo()` |
| `+0x4c` | `GetBTCurrentSamplingRateNumber()` |
| `+0x50` | `IsCustomizedPlatformSupportFM()` |
| `+0x54` | `IsSupportFM()` |

### Numeric I2S sample-rate mapping (now resolved)

The relocation-aware lookup table used by `GetFmChipSamplingRate()` contains these values, indexed by the FM audio-info structure field at offset 24:

| Enum value | Label | Returned rate |
|---:|---|---:|
| 0 | `FM_I2S_32K` | 32,000 Hz |
| 1 | `FM_I2S_44K` | 44,100 Hz |
| 2 | `FM_I2S_48K` | 48,000 Hz |
| 3 | `FM_I2S_SR_ERR` | Error path; not a valid rate |

The setter independently confirms the inverse mapping: input 32,000 maps to enum 0, 44,100 maps to enum 1, and 48,000 maps to enum 2. It opens `/dev/fm` and issues ioctl `0xC004F521` for the I2S setting operation. Unknown input rates take the error path. This resolves the enum-to-rate question without relying on string ordering.

The direct-connection function's merge-capability call is now also confirmed at the ABI level: the virtual call loads vtable offset `+0x1c`, which the decoded vtable resolves to `IsFMMergeInterfaceSupported()`. This does not yet identify the exact card/PCM names for each branch or establish physical board wiring; those remain separate open items.
