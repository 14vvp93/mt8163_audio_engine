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
