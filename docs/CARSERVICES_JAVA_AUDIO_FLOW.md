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

The VDEX proves the numeric reason arguments and call-site conditions. It does not, by itself, prove the driver's internal arbitration policy for overlapping reasons or whether every reason is treated as a strict independent mute owner. Do not model these reason values as a public stable ABI without validating the matching driver implementation and runtime behavior.

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
