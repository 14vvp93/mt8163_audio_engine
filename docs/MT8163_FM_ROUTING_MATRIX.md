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

`WCNChipController::InitAudioFMInfo()` opens `/dev/fm` and issues ioctl `0xC004F530` (`FM_IOCTL_GET_AUDIO_INFO`). The returned FM audio-info buffer is 20 bytes (five 32-bit fields) and begins at controller-object offset 12. Thus the fields occupy object offsets 12, 16, 20, 24, and 28 respectively. It includes:

| Field | Offset in HAL controller object | Confirmed behavior |
|---|---:|---|
| `aud_path` | 12 | Value 2 means merge-interface supported; value 3 is error; analog value is rejected during init |
| I2S mode | 20 | Value 1 selects slave-mode result; value 2 is error |
| sample rate | 24 | 0 = 32,000 Hz; 1 = 44,100 Hz; 2 = 48,000 Hz; 3 = error |
| I2S pad | 28 | Value 0 selects CONN result; value 2 is error |

The setter maps 32,000 / 44,100 / 48,000 Hz back to enum 0 / 1 / 2 and issues ioctl `0xC004F521`.
The paired MT6627 `fmradio_drv.ko` selects configuration class 1 for chip ID `0x6627`. Its init-time audio metadata is `aud_path=1, i2s_state=1, i2s_mode=0, i2s_rate=0, i2s_pad=0`: valid non-merge path, I2S state enabled, master mode, 32 kHz, and `FM_I2S_PAD_CONN`. Thus the non-merge row below is the driver-configured path, not merely a hypothetical HAL branch. The customer file `fm_cust.cfg` is loaded through `request_firmware()` for tuner thresholds and does not change these five audio metadata fields.

The setter callback at FM-core offset `0x8c` is not populated by the MT6627 low-ops registration, and no other provider was found in the extracted module set. If null, ioctl `0xC004F521` returns `-1002`. The HAL logs but ignores that ioctl result, while its direct PCM configuration obtains the actual rate from `GetFmChipSamplingRate()`; do not treat the HAL 48 kHz request as proof that the chip rate changed from 32 kHz.

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
3. `/dev/audiomux` and CD3313 logical channel `RADIO`: CD3313 input selection and mute-control plane.
4. Android Audio Policy device `FM Tuner In` plus the non-merge HAL I2S endpoints: policy-level FM input and AP-side PCM route.

Static evidence confirms these separate software routes. The MT6627 metadata selects non-merge I2S, while RadioService independently mutes CD3313 logical RADIO. It is not yet proven whether the board feeds the tuner to both paths, whether one is dormant, or how the paths are selected at runtime.

## 6. Remaining board-level verification

The next evidence pass searches the extracted firmware configuration and device-tree material for the exact pinmux and board routing:

- the DT/DTBO mapping for GPIO 396 and GPIO 474;
- the SoC I2S pinmux/pad routing for the FM tuner (the driver reports `FM_I2S_PAD_CONN`, but the DT/DTBO is missing);
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


The `/dev/fm` owner and its nonstandard ioctl copy sizes are documented in [MT6627_FM_DRIVER_ABI.md](MT6627_FM_DRIVER_ABI.md). The exact paired `fmradio_drv.ko` handles `0xC004F530` as a 20-byte five-field metadata query and `0xC004F521` as a 12-byte three-field I2S setting request, despite both ioctl numbers encoding `_IOC_SIZE=4`.


The paired `wmt_drv.ko` (Build ID `c2e5f3477920471011b2d929be212100b0d41ff8`; SHA-256 `90d31e43f4883f9e3cf707cc0ae351672afa2c57ab98d7c866b7efd0d1fb1e8b`) contains a dedicated `wmt_plat_i2s_ctrl()` entry in its platform GPIO-control function table. That routine checks `gpio_ctrl_info` offsets `0x240`, `0x26c`, and `0x298`, reporting missing I2S CK, WS, or data-clock pin definitions. The actual pin values are not present in the supplied extracted tree because no DT/DTBO/boot image was found. See [MT6627_FM_DRIVER_ABI.md](MT6627_FM_DRIVER_ABI.md).


The firmware module inventory includes several alternative tuner drivers (`qn8027`, `qn8035`, `si475x`, `tda7708LX`) alongside `fmradio_drv.ko`. The latter has a `depends=wmt_drv` dependency and exactly implements the HAL's `/dev/fm` ioctl values. The extracted init/config scan found no explicit module-load command, so the intended provider is identified statically but its live loaded state remains to be checked on the target.




## WMT audio-interface state machine and compiled FM mode

The exact paired `wmt_drv.ko` was disassembled with ARM-aware tooling:

- Build ID: `c2e5f3477920471011b2d929be212100b0d41ff8`
- SHA-256: `90d31e43f4883f9e3cf707cc0ae351672afa2c57ab98d7c866b7efd0d1fb1e8b`
- `wmt_plat_audio_ctrl()`: VMA `0x5690`, size 392 bytes.
- `wmt_plat_i2s_ctrl()`: VMA `0x4820`, size 308 bytes.

The binary's `wmt_plat_audio_ctrl()` switch selects the two platform pin groups (group 8 = PCM, group 9 = I2S). Reading the actual jump table and calls in the paired binary gives this **raw numeric function-argument mapping**:

| Raw state argument | PCM group | I2S group |
|---:|---|---|
| 0 | INIT | INIT |
| 1 | DEINIT | INIT |
| 2 | INIT | DEINIT |
| 3 | DEINIT | DEINIT |

Do not label these raw values as `CMB_STUB_AIF_0..3` yet: the upstream MediaTek reference source documents AIF_0 as analog FM line-in/out, AIF_1 as BT PCM + analog FM, AIF_2 as digital FM I2S, and AIF_3 as both interfaces, but its switch ordering does not match the paired binary's raw argument mapping. The caller/wrapper that translates the AIF enum into this function's raw state is still unresolved. Reference source: [wmt_plat_alps.c, android-mtk-3.18](https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/connectivity/common/combo/platform/alps/wmt_plat_alps.c), especially `wmt_plat_audio_ctrl()` and `wmt_plat_i2s_ctrl()`. Treat it as a semantic cross-check, not as proof that every line or enum value is identical to this firmware.

**Important build finding:** the exact binary's `wmt_plat_i2s_ctrl()` checks the I2S CK/WS/DAT GPIO definitions, but the compiled function contains the warning path “FM digital mode is not set” / “no I2S GPIO settings should be modified by combo driver” and does not contain the I2S pinctrl/GPIO operations present in the reference source's `#if defined(FM_DIGITAL_INPUT) || defined(FM_DIGITAL_OUTPUT)` branch. This is strong evidence that the WMT build was compiled without those FM digital-mode flags; its combo-driver I2S pin control is effectively a no-op for FM in this build.

This proves only that the WMT combo driver does not dynamically switch the FM I2S GPIO/pinctrl state in this build; it does **not** establish an analog route. The paired MT6627 driver selects class-1 metadata (`aud_path=1`, I2S state enabled, master mode, 32 kHz, `FM_I2S_PAD_CONN`), and the primary HAL selects the non-merge `FM_I2S_Playback` / `FM_I2S_Capture` route. The `CONN` pad mode is consistent with a fixed connection to the SoC, where pinmux may be established by board configuration, bootloader, or device tree. The supplied dump has no DTB/DTBO/boot image, so actual SoC pinmux and wiring remain unresolved. Keep this AP-side I2S path distinct from the CD3313 logical RADIO input/mute path; the board may expose both, but the physical relationship is not proven.



## Audio Policy custom extension: source of FM_DIRECT_CONTROL

The firmware-wide string scan found `FM_DIRECT_CONTROL` in the primary HAL and in the separate ARM library `libaudiopolicycustomextensions.so` (the only additional binary hit). Exact identity:

- Build ID: `f471dbca5ef93ec7e59d87999c3053f9`
- SHA-256: `9f4b6940d241a4bc6517214f1fc3fd0b3d69964f54aaa04e5ceb836d47d04afb`
- Workflow artifact: `mt8163-board-audio-routing-evidence`, run `43124452351`, file `policy-extension/libaudiopolicycustomextensions.so`.

The stripped library still exports these relevant symbols:

- `AudioPolicyManagerCustomImpl::setFMIndirectMode(unsigned int)` at VMA `0x0000df80`, size 272 bytes. Its vtable entry is at `0x001a09c` (vptr-relative offset `+0xf4`).
- `AudioPolicyManagerCustomImpl::isFMDirectMode(android::sp<AudioPatch> const&)` at VMA `0x0000ce88`, size 40 bytes.
- The library also implements `fm_addAudioPatch()` and `fm_removeAudioPatch()`.

`setFMIndirectMode(rate_like_value)` constructs an `AudioParameter`, adds integer key `FM_DIRECT_CONTROL`, serializes it, and sends it through the Audio Policy client's `setParameters` callback. Its branch is exact:

| Argument | Parameter sent |
|---:|---:|
| less than 48,001 | `FM_DIRECT_CONTROL=1` |
| 48,001 or greater | `FM_DIRECT_CONTROL=0` |

The 48,001 threshold is consistent with a sample-rate decision. This identifies the policy extension—not RadioService or an app—as the source of the parameter in the analyzed firmware.

`isFMDirectMode()` returns true only when three patch fields match: field offset 28 equals 1, field offset `0xd60` equals 1, and the device field at offset `0xc4` equals `0x80002000` (`AUDIO_DEVICE_IN_FM_TUNER`). Thus the direct-mode predicate is explicitly tied to an FM Tuner input patch.

This completes the static chain: Audio Policy custom extension selects `FM_DIRECT_CONTROL` for an FM Tuner patch → HAL stores the boolean → when true, `AudioALSAFMController::setFmEnable()` forces the FM chip sample rate to 48 kHz and uses its direct-connection control path. The extension's rate threshold and the HAL's 48 kHz override are both binary-verified. The actual runtime patch and selected policy XML still need target-side confirmation.

## Policy XML selection and runtime ALSA index resolution

The paired `libaudiopolicymanagerdefault.so` contains the Android policy-loader property checks and the candidate names `audio_policy_configuration_a2dp_offload_disabled.xml` and `audio_policy_configuration.xml`. Its behavior matches the Android 9 policy-manager source: if `ro.bluetooth.a2dp_offload.supported` and `persist.bluetooth.a2dp_offload.disabled` are both true, it tries the special offload-disabled XML first; it then tries the standard XML. For each filename, the search order is `/odm/etc`, `/vendor/etc`, then `/system/etc` ([AOSP AudioPolicyManager.cpp](https://android.googlesource.com/platform/frameworks/av/%2B/4c1ef4b64d113be6ee1106375bd10cdc643e80d8/services/audiopolicy/managerdefault/AudioPolicyManager.cpp)).

The supplied firmware contains both `vendor/audio_policy_configuration.xml` and `system/audio_policy_configuration.xml`. The vendor XML attaches `FM Tuner In`, supports PCM 16-bit at 8, 16, 32, 44.1, and 48 kHz (mono/stereo), and routes it to Speaker, Wired Headset, and Wired Headphones. The system XML does not declare `FM Tuner In`. Given the loader search order, the vendor policy is the expected selection unless an `/odm` override or a property-selected special XML takes precedence. The full extracted image contains no ODM policy XML and no A2DP-offload-disabled special XML, so the expected selected file is `/vendor/etc/audio_policy_configuration.xml`. This is a static selection result; the live AudioPolicyService-loaded path has not been observed.

The numeric ALSA card/PCM indexes are not statically encoded in `audio_device.xml`. `AudioALSADeviceParser::ParseCardIndex()` reads `/proc/asound/cards`; `GetAllPcmAttribute()` reads `/proc/asound/pcm`; `GetCardIndexByString()` and `GetPcmIndexByString()` then resolve names such as `FM_I2S_Playback` and `FM_I2S_Capture` against those runtime lists. The parser therefore derives numeric indexes at runtime. A read-only snapshot of `/proc/asound/cards` and `/proc/asound/pcm` from the target is required to name the exact indexes; no firmware-only guess is justified. A string scan of the extracted kernel modules and text configs found the `FM_I2S_Playback` / `FM_I2S_Capture` labels only in `audio.primary.mt8163.so`, not in a kernel module or static audio XML. This is consistent with the HAL resolving those names against the live ALSA enumeration rather than a fixed XML index.
The latest full-tree scan (workflow [run 43126583592](https://github.com/14march93/mt8163_audio_engine/actions/runs/43126583592)) found no `/odm/etc` policy XML and no `audio_policy_configuration_a2dp_offload_disabled.xml`; it found the standard vendor and system XML copies. The two Bluetooth property names exist in the policy manager and Bluetooth libraries, but no explicit values were found in the extracted `.prop` files. Since the special XML is absent and the vendor standard XML exists ahead of the system fallback, `/vendor/etc/audio_policy_configuration.xml` is the expected policy file. This is a static selection conclusion, not a live `dumpsys` observation.

The same scan found no `fm_cust.cfg` file and no user-space `/proc/fm` writer. The MT6627 driver requests `fm_cust.cfg` through `request_firmware()`; if unavailable at runtime, only tuner threshold overrides are missing—the class-1 audio metadata remains as described above.