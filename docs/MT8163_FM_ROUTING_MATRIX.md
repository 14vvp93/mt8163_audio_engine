# MT8163 FM routing matrix — paired firmware evidence

## Scope and evidence identity

This document correlates the Java RadioService path, the exact firmware-paired CD3313 module, the MT8163 primary Audio HAL, and the Audio Policy declarations. It is a static firmware reconstruction. No physical device writes, I²C writes, GPIO changes, or live route switching were performed.

### Exact binaries

| Component | Build identity |
|---|---|
| `vendor/audio.primary.mt8163.so` | Build ID `f66eaf9e6ac950259f0c253be618d15a` |
| `vendor_lib/lib/modules/cd3313.ko` | Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`; SHA-256 `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594` |

HAL binary and report: workflow run [43114692814](https://github.com/14march93/mt8163_audio_engine/actions/runs/43114692814). Paired CD3313 module report: workflow run [43113850964](https://github.com/14march93/mt8163_audio_engine/actions/runs/43113850964).

## 1. Java RadioService to CD3313

The VDEX disassembly shows RadioService using Java inputPath ID 0 for its FM mute operations (initialization, retune, MCU mute-state handling, scan, and its generic radio mute wrapper).

The current paired CD3313 module independently labels logical channel index 0 as `RADIO`. Its seven labels are:

`0 RADIO, 1 ARM, 2 AUX, 3 EXBT, 4 AUX2, 5 DAB, 6 NONE`.

Therefore the confirmed static logical chain is:

`RadioService inputPath 0 → CD3313 logical channel 0 (RADIO)`.

## 2. CD3313 selector table and silicon source numbers

The current module's `.rodata` symbol `CSWTCH.201` at offset `0xb4` contains six 32-bit values:

`2, 1, 0, 3, 0, 2`.

The current `set_input_channel()` indexes this table for logical channels 0..5 and uses selector code 3 for channel 6 / sentinel `-1`. The CD3313-AX-BJ-24 datasheet decodes the two selector bits as code 0 = Source 1, 1 = Source 2, 2 = Source 3, 3 = Source 4. Source 4 has no external input pins.

| Channel | Label | Selector bits | CD3313 input |
|---:|---|---:|---|
| 0 | RADIO | 2 | Source 3 |
| 1 | ARM | 1 | Source 2 |
| 2 | AUX | 0 | Source 1 |
| 3 | EXBT | 3 | Source 4 (no external pins) |
| 4 | AUX2 | 0 | Source 1 |
| 5 | DAB | 2 | Source 3 |
| 6 / -1 | NONE | 3 | Source 4 (no external pins) |

This revalidates the earlier Source 3 mapping against the exact paired module. Thus:

`RadioService inputPath 0 → RADIO → selector code 2 → CD3313 Source 3`.

## 3. Firmware source-switch GPIOs

The current module's DWARF declares:

- `GPIO39_Aux_Aux2_SW = 396`
- `GPIO_Radio_DAB_SW = 474`
- `GPIO52_subwoof = 409`
- mute GPIO constant = 393

The current `set_input_channel()` code requests raw GPIO 396 LOW only for logical channel 2 and HIGH otherwise; it requests raw GPIO 474 HIGH only for logical channel 0 and LOW otherwise.

| Channel | Label | CD3313 input | GPIO 396 — AUX/AUX2 | GPIO 474 — RADIO/DAB |
|---:|---|---:|---|---|
| 0 | RADIO | 3 | HIGH | HIGH |
| 1 | ARM | 2 | HIGH | LOW |
| 2 | AUX | 1 | LOW | LOW |
| 3 | EXBT | 4 | HIGH | LOW |
| 4 | AUX2 | 1 | HIGH | LOW |
| 5 | DAB | 3 | HIGH | LOW |
| 6 / -1 | NONE | 4 | HIGH | LOW |

The firmware therefore groups RADIO and DAB onto CD3313 Source 3, distinguished by GPIO 474; AUX and AUX2 share Source 1, distinguished by GPIO 396. These are driver-requested raw levels, not measured electrical levels. The GPIO names and channel labels establish intended firmware logic, not the exact physical trace or external switch circuit.

## 4. MT8163 FM tuner metadata and HAL PCM selection

`WCNChipController::InitAudioFMInfo()` opens `/dev/fm` and issues ioctl `0xC004F530` (`FM_IOCTL_GET_AUDIO_INFO`). The returned FM audio-info structure includes:

| Field | Offset | Confirmed behavior |
|---|---:|---|
| `aud_path` | 12 | Value 2 means merge-interface supported; value 3 is error; analog value is rejected during init |
| I2S mode | 20 | Value 1 selects slave-mode result; value 2 is error |
| sample rate | 24 | 0 = 32,000 Hz; 1 = 44,100 Hz; 2 = 48,000 Hz; 3 = error |
| I2S pad | 28 | Value 0 selects CONN result; value 2 is error |

The setter maps 32,000 / 44,100 / 48,000 Hz back to enum 0 / 1 / 2 and issues ioctl `0xC004F521`.

The exact HAL Card/PCM string pairs were recovered from the relocation-aware global `String8` initializers and verified at both call sites:

| `IsFMMergeInterfaceSupported()` | Direct FM playback: Card and PCM | AP FM capture: Card and PCM |
|---|---|---|
| true | `MRGRX_PLayback` | `MRGRX_CAPTURE` |
| false | `FM_I2S_Playback` | `FM_I2S_Capture` |

For each row, the same identifier is passed to both `GetCardIndexByString()` and `GetPcmIndexByString()`. The capture path opens with `PCM_IN` (`0x10000000`); the direct playback path opens with flags 0. Numeric card/PCM indexes are resolved from the runtime AudioALSADeviceParser configuration and are not encoded in these names.

## 5. Separate control and audio planes

These interfaces must remain distinct in any model:

1. `/dev/fm`: FM tuner/chip metadata and I2S configuration ioctl plane.
2. ALSA PCM names `MRGRX_PLayback`, `MRGRX_CAPTURE`, `FM_I2S_Playback`, `FM_I2S_Capture`: HAL direct connection and AP capture plane.
3. `/dev/audiomux` and CD3313 logical channel `RADIO`: analog input selection and mute-control plane.
4. Android Audio Policy device `FM Tuner In`: policy-level input route to output devices.

Static evidence confirms these separate software routes. It does not yet prove whether the board feeds one FM signal to both the CD3313 Source 3 analog input and the AP-side I2S capture path, or which route is active in a given runtime mode.

## 6. Remaining board-level verification

The next evidence pass searches the extracted firmware configuration and device-tree material for the exact pinmux and board routing:

- the DT/DTBO mapping for GPIO 396 and GPIO 474;
- I2S pad selection and pinctrl for the FM tuner;
- AudioALSADeviceParser definitions that resolve the four PCM identifiers to numeric card/PCM indexes;
- the active Audio Policy configuration and any FM-specific route overrides;
- any schematic or board configuration identifying the CD3313 Source 1/2/3 analog wires.

Until those are located, the firmware-level mux mapping is resolved, but the physical board wiring remains unverified.

## Related documentation

- [Java / HAL / RadioService audio flow](CARSERVICES_JAVA_AUDIO_FLOW.md)
- [CD3313 register image and selector map](CD3313_REGISTER_IMAGE.md)
- [CD3313 command dispatch](CD3313_COMMAND_DISPATCH.md)
- [CD3313 protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md)


## FM_DIRECT_CONTROL state and enable/disable call chain

The exact HAL binary also resolves the previously unexplained `FM_DIRECT_CONTROL` string.

- `AudioALSAStreamOut::setParameters()` (VMA `0x8289c`) constructs the AudioParameter key `FM_DIRECT_CONTROL` and calls `AudioParameter::getInt()`. If present, it removes the key, normalizes the value to boolean 0/1, and invokes the `AudioALSAFMController` virtual method at vtable offset `+0x28`.
- The `AudioALSAFMController` vtable symbol is `0x001909f0`, size 72 bytes; the vptr starts at `0x001909f8`. Slot `+0x28` points to a small setter at `0x000afeb8`, whose body stores the boolean in object byte offset 26. Slot `+0x2c` is the named `checkFmNeedUseDirectConnectionMode()` method at `0x000ae7fc`, which returns that same byte.
- `AudioALSAFMController::setFmEnable()` calls the getter at vtable offset `+0x2c`. When the flag is true, it asks `WCNChipController` to set the FM chip sample rate to 48,000 Hz.
- The same `setFmEnable()` method calls `setFmDirectConnection_l()` through vtable offset `+0x34`: the enable path passes first argument 1, the disable path passes first argument 0; the second argument is 1 and the third is forwarded from the caller. This is the HAL's actual direct-PCM start/stop control path.
- `AudioALSAStreamManager::setFmEnable()` delegates to `AudioALSAFMController::setFmEnable()` through its vtable slot `+0x0c`.

This proves `FM_DIRECT_CONTROL` is an AudioParameter boolean controlling the HAL's direct-connection-mode state; it is not a CD3313 ioctl and not a Linux system property. It also proves the FM enable lifecycle invokes the direct-PCM controller. Do not infer that the parameter alone selects MRGRX versus FM_I2S: that endpoint branch is separately selected by `WCNChipController::IsFMMergeInterfaceSupported()`.

## Runtime ALSA name resolution and policy-file discrepancy

The exact HAL exposes `AudioALSADeviceParser`. Its constructor calls `ParseCardIndex()`, `GetAllPcmAttribute()`, and `QueryPcmDriverCapability()`. `GetCardIndexByString()` and `GetPcmIndexByString()` search runtime-populated descriptor arrays using `strcmp` and return the stored numeric index. Thus the symbolic names recovered above are exact, but the numeric card/PCM indexes are discovered at runtime and are not statically encoded in the vendor `audio_device.xml`.

The extracted `vendor/audio_device.xml` contains codec mixer-control paths (speaker, receiver, microphone, headset, etc.); it contains no `FM_I2S` or `MRGRX` Card/PCM definitions. It is not the source of those four names.

Two different policy candidates exist in the supplied firmware dump:

- `vendor/audio_policy_configuration.xml` declares `FM Tuner In` as an attached primary input, gives it PCM16 profiles at 8/16/32/44.1/48 kHz (mono/stereo), and routes it to Speaker, Wired Headset, and Wired Headphones. It separately declares `FM Tuner Out`.
- `system/audio_policy_configuration.xml` is a different generic policy variant: its attached devices include Built-In Back Mic but not FM Tuner In, and its routes do not include FM Tuner In.

The vendor `audio_policy.conf` also lists `AUDIO_DEVICE_IN_FM_TUNER` in the primary module's attached input devices. The static dump does not identify which policy file the running image actually loads, so the vendor FM-enabled policy must not be treated as runtime proof until the selected path is observed.

The focused firmware inventory found no `.dts`, `.dtsi`, `.dtb`, `.dtbo`, boot image, or preloader image in the extracted file set. Consequently, the GPIO-to-pad pinmux and physical board traces cannot be established from the currently supplied partition files.
