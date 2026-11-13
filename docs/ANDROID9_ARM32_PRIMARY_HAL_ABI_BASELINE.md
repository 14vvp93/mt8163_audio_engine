# Android 9 ARM32 primary HAL ABI baseline

Date: 2026-11-13  
Target: MT8163 / Android 9 / ARM32 EABI5  
Factory artifact: `/vendor/lib/hw/audio.primary.mt8163.so`  
Status: AOSP platform contract pinned; factory HMI/module/device ABI, vendor device tail, stream wrappers and APS2 vtables recovered from the SHA-256-verified binary. External vendor callback callers, exact route negotiation and linker namespace visibility remain open.

## 1. Exact platform release evidence

The supplied firmware's `firmware/dumps/mt8163/build.prop` records:

- `ro.vendor.build.fingerprint=alps/full_tb8163p3_bsp/tb8163p3_bsp:9/PPR1.180610.011/publisher2803261725:user/release-keys`
- `ro.product.first_api_level=28`
- `ro.board.platform=mt8163`
- `ro.vendor.mediatek.version.release=alps-mp-p0.mp6-V1.364`
- vendor build date: 2026-03-26.

The Android Open Source Project maps build `PPR1.180610.011` to `android-9.0.0_r3` (Android 9 Release 3). This is a stronger platform reference than an arbitrary Android 9 tag, but the ALPS/MediaTek vendor tree may contain private header changes and must still be reconciled.

Pinned public reference:
- Tag: [android-9.0.0_r3](https://github.com/aosp-mirror/platform_hardware_libhardware/tree/android-9.0.0_r3)
- `platform_hardware_libhardware` commit: `51d03179c9ddeeb71f36bfac40e445263b5e76e2`
- [hardware/audio.h](https://github.com/aosp-mirror/platform_hardware_libhardware/blob/android-9.0.0_r3/include/hardware/audio.h)
- [hardware/hardware.h](https://github.com/aosp-mirror/platform_hardware_libhardware/blob/android-9.0.0_r3/include/hardware/hardware.h)
- [Official Android build-number mapping](https://source.android.com/docs/setup/reference/build-numbers)

## 2. Android 9 module contract

The AOSP module loader resolves the audio primary module through the class/instance identity `audio.primary`; the expected library name is `audio.primary.mt8163.so`.

The module metadata symbol is:

- C identifier: `HAL_MODULE_INFO_SYM`
- exported ELF symbol: `HMI`
- module identifier: `AUDIO_HARDWARE_MODULE_ID` = `"audio"`
- `hw_module_t.methods->open()` receives the instance id (normally `"primary"`) and returns an `hw_device_t` whose first member is the common device header.

The exported `HMI` data symbol is required for the normal libhardware module-loading path. The presence of similarly named C++ symbols is not a substitute.

## 3. ARM32 structure layout reference

The following sizes and offsets are derived from the pinned AOSP r3 headers under the target ABI assumptions: 32-bit pointers, 32-bit `size_t`, 32-bit function pointers and the ARM32 C ABI. These are AOSP reference layouts, not yet a claim that the stripped vendor binary uses no private extension or a different device API version.

| Structure | AOSP r3 members | ARM32 size |
|---|---:|---:|
| `hw_module_t` | fixed header + 25 reserved words | 128 bytes |
| `hw_device_t` | fixed header + 12 reserved words + close | 64 bytes |
| `audio_stream` | 14 function pointers | 56 bytes |
| `audio_stream_out` | common stream + 16 function pointers | 120 bytes |
| `audio_stream_in` | common stream + 10 function pointers | 96 bytes |
| `audio_hw_device` | common device + 23 function pointers | 156 bytes |

### Key offsets

All offsets are decimal bytes from the beginning of the structure.

**`hw_module_t`**
- `tag`: 0
- `module_api_version`: 4
- `hal_api_version`: 6
- `id`: 8
- `name`: 12
- `author`: 16
- `methods`: 20
- `dso`: 24
- `reserved`: 28..127

**`hw_device_t`**
- `tag`: 0
- `version`: 4
- `module`: 8
- `reserved`: 12..59
- `close`: 60

**`audio_stream`** — 14 pointers in this exact order:
`get_sample_rate`, `set_sample_rate`, `get_buffer_size`, `get_channels`, `get_format`, `set_format`, `standby`, `dump`, `get_device`, `set_device`, `set_parameters`, `get_parameters`, `add_audio_effect`, `remove_audio_effect`.

**`audio_stream_out`** — `common` at 0, then:
| Member | Offset |
|---|---:|
| `get_latency` | 56 |
| `set_volume` | 60 |
| `write` | 64 |
| `get_render_position` | 68 |
| `get_next_write_timestamp` | 72 |
| `set_callback` | 76 |
| `pause` | 80 |
| `resume` | 84 |
| `drain` | 88 |
| `flush` | 92 |
| `get_presentation_position` | 96 |
| `start` | 100 |
| `stop` | 104 |
| `create_mmap_buffer` | 108 |
| `get_mmap_position` | 112 |
| `update_source_metadata` | 116 |

**`audio_stream_in`** — `common` at 0, then:
| Member | Offset |
|---|---:|
| `set_gain` | 56 |
| `read` | 60 |
| `get_input_frames_lost` | 64 |
| `get_capture_position` | 68 |
| `start` | 72 |
| `stop` | 76 |
| `create_mmap_buffer` | 80 |
| `get_mmap_position` | 84 |
| `get_active_microphones` | 88 |
| `update_sink_metadata` | 92 |

**`audio_hw_device`** — `common` occupies bytes 0..63:
| Member | Offset |
|---|---:|
| `get_supported_devices` | 64 |
| `init_check` | 68 |
| `set_voice_volume` | 72 |
| `set_master_volume` | 76 |
| `get_master_volume` | 80 |
| `set_mode` | 84 |
| `set_mic_mute` | 88 |
| `get_mic_mute` | 92 |
| `set_parameters` | 96 |
| `get_parameters` | 100 |
| `get_input_buffer_size` | 104 |
| `open_output_stream` | 108 |
| `close_output_stream` | 112 |
| `open_input_stream` | 116 |
| `close_input_stream` | 120 |
| `get_microphones` | 124 |
| `dump` | 128 |
| `set_master_mute` | 132 |
| `get_master_mute` | 136 |
| `create_audio_patch` | 140 |
| `release_audio_patch` | 144 |
| `get_audio_port` | 148 |
| `set_audio_port_config` | 152 |

The AOSP r3 header declares `AUDIO_DEVICE_API_VERSION_CURRENT = 3.0` and minimum supported device API 2.0. Do not assume the factory module advertises 3.0: recover the actual `hw_device_t.version` and method-table behavior from the factory module before using any appended method.

## 4. Factory module metadata recovered from the binary

The immutable factory artifact was fetched directly by GitHub Actions from `14vvp93/public_calculations`. The audit verified the expected size and SHA-256 before parsing it. The exported module object is now confirmed:

- Dynamic symbol: `HMI`
- Symbol type/binding/visibility: `OBJECT GLOBAL DEFAULT`
- Symbol value: `0x00195014`
- Symbol size: `128` bytes
- Section: `.data` (section 21)
- `hw_module_t.tag`: `0x48574d54` (`HARDWARE_MODULE_TAG`)
- `module_api_version`: `0x0001` (module API 0.1)
- `hal_api_version`: `0x0100` (HAL API 1.0)
- Module ID string: `audio`
- Module name: `MTK Audio HW HAL`
- Module author: `MTK`
- `hw_module_methods_t` pointer: `0x00195010`
- `hw_module_methods_t.open` function pointer: `0x0009b70c`
- `dso`: null; all 25 reserved words are zero in the file image.

The module metadata is decoded from the actual bytes at the `HMI` object, not inferred from AOSP defaults. The `open` entry at `0x0009b70c` has been disassembled. It validates the requested instance name, allocates a zeroed `0xe0`-byte (224-byte) device object, writes `HARDWARE_DEVICE_TAG` at offset 0, writes device API version `0x0300` (Audio HAL device API 3.0) at offset 4, and stores the module pointer at offset 8. It initializes the common close callback at offset 60 and the AOSP audio device callback slots beginning at offset 64. The AOSP r3 methods at offsets 132 and 136 are left zero by the calloc-backed construction; the methods at 140, 144, 148 and 152 are populated. The function then initializes a vendor-specific callback tail at offsets 156 through 216 and stores the result of `createMTKAudioHardware()` at offset 220. If that factory returns null, the allocation is freed and `open()` returns `-EIO`; otherwise the device pointer is returned and success is reported.

The 224-byte allocation is 68 bytes larger than the 156-byte AOSP r3 `audio_hw_device` layout. The 68-byte vendor tail therefore consists of 16 callback-sized slots (offsets 156..216) and one private hardware-object pointer (offset 220). Callback target addresses are being mapped to implementations; do not treat the vendor tail as part of the public AOSP ABI.

### Factory ELF linker contract recovered

The first successful binary audit also confirmed:

- Android ELF note API level: `0x1c` (28).
- SONAME: `audio.primary.mt8163.so`.
- Dynamic flags include `BIND_NOW` (`DF_1_NOW`).
- Symbol-version requirements observed: `LIBC` versions 2, 4 and 5, plus `LIBC_N` version 3.
- Build ID: `f66eaf9e6ac950259f0c253be618d15a`.

Exact `DT_NEEDED` set, in ELF order:

```text
libhidlbase.so
libhidltransport.so
libhwbinder.so
android.hardware.power@1.0.so
vendor.mediatek.hardware.power@2.0.so
libcutils.so
libutils.so
libalsautils.so
libbinder.so
libhardware_legacy.so
libhardware.so
libblisrc_vendor.so
libnvram.so
libaudiosetting.so
libcvsd_mtk.so
libmsbc_mtk.so
libaudioutils.so
libaudiocomponentengine_vendor.so
libtinyalsa.so
libtinycompress.so
libaudiodcrflt_vendor.so
libtinyxml.so
libaudiotoolkit_vendor.so
liblog.so
libspeech_enh_lib.so
libladder.so
libaedv.so
libbluetooth_mtk_pure.so
libc++.so
libc.so
libm.so
libdl.so
```

This dependency list is a compatibility constraint, not a recommendation to link every library into the replacement. Each dependency must be classified as required framework contract, vendor service/control client, codec/transport, or implementation detail before designing the replacement's linker surface.

### AudioALSAHardware vtable and device callback map

The factory ELF stores the C++ vtable as dynamic symbol:

- Mangled symbol: `_ZTVN7android17AudioALSAHardwareE`
- Demangled: `vtable for android::AudioALSAHardware`
- Address: `0x00190a84`
- Size: `236` bytes
- Recovered entries: 57 `R_ARM_ABS32` relocations from the Android packed `APS2` `.rel.dyn` section.
- Object vptr uses the Itanium ABI address point at vtable symbol + 8 (two header words precede virtual methods).

The device adapter at offset 220 holds the `AudioALSAHardware` singleton returned by `createMTKAudioHardware()`. The public device callbacks forward through that object's vtable. Recovered mapping:

| `audio_hw_device` offset | Android callback | Factory target |
|---:|---|---|
| 64 | `get_supported_devices` | Constant mask `0xc0103cff` |
| 68 | `init_check` | `AudioALSAHardware::initCheck` |
| 72 | `set_voice_volume` | `setVoiceVolume` |
| 76 | `set_master_volume` | `setMasterVolume` |
| 80 | `get_master_volume` | `getMasterVolume` |
| 84 | `set_mode` | `setMode` |
| 88 | `set_mic_mute` | `setMicMute` |
| 92 | `get_mic_mute` | `getMicMute` |
| 96 | `set_parameters` | `setParameters` |
| 100 | `get_parameters` | `getParameters` |
| 104 | `get_input_buffer_size` | `getInputBufferSize` |
| 108 | `open_output_stream` | `openOutputStreamWithFlags` |
| 112 | `close_output_stream` | `closeOutputStream` |
| 116 | `open_input_stream` | `openInputStreamWithFlags` |
| 120 | `close_input_stream` | `closeInputStream` |
| 124 | `get_microphones` | `getMicrophones` |
| 128 | `dump` | `dumpState` |
| 132 | `set_master_mute` | Null in the constructed device object |
| 136 | `get_master_mute` | Null in the constructed device object |
| 140 | `create_audio_patch` | `createAudioPatch` |
| 144 | `release_audio_patch` | `releaseAudioPatch` |
| 148 | `get_audio_port` | `getAudioPort` |
| 152 | `set_audio_port_config` | `setAudioPortConfig` |

The `set_master_mute` and `get_master_mute` slots remain zero in the calloc-backed device allocation. A C++ `setMasterMute` method exists in the class vtable, but this HAL device table does not expose it through those AOSP fields.

The 68-byte vendor tail is populated as follows:

| Device offset | Factory forwarding target |
|---:|---|
| 156 | `AudioALSAHardware::xWayPlay_Start` |
| 160 | `xWayPlay_Stop` |
| 164 | `xWayPlay_Write` |
| 168 | `xWayPlay_GetFreeBufferCount` |
| 172 | Alternate entry into the `xWayRec_Stop` forwarding thunk; expects the C++ object pointer directly |
| 176 | `xWayRec_Stop` forwarding thunk; resolves the C++ object from the device wrapper |
| 180 | `xWayRec_Read` |
| 184 | `ReadRefFromRing` |
| 188 | `GetVoiceUnlockULTime` |
| 192 | `SetVoiceUnlockSRC` |
| 196 | `startVoiceUnlockDL` |
| 200 | `stopVoiceUnlockDL` |
| 204 | `getVoiceUnlockDLInstance` |
| 208 | `setupParametersCallback` |
| 212 | `setAudioParameterChangedCallback` |
| 216 | `clearAudioParameterChangedCallback` |
| 220 | Pointer to the `AudioALSAHardware` singleton object |

The vendor tail is not part of the public AOSP audio HAL ABI. It is a private MediaTek contract and must be preserved only if corresponding vendor clients remain in the replacement architecture. The duplicate `xWayRec_Stop` entries have different first-argument conventions and must not be collapsed without tracing their callers.

The audit helper `tools/audit_android_packed_relocations.py` decodes APS2 relocations and reconstructs the C++ vtable symbol mapping. This avoids relying on `readelf -r`, which does not display the Android packed `.rel.dyn` records as ordinary REL entries.

### Factory stream implementation vtables

The same APS2 decoder recovered the concrete stream implementation vtables:

- `_ZTVN7android18AudioALSAStreamOutE` — `android::AudioALSAStreamOut`, address `0x0018ffd4`, size 120 bytes.
- `_ZTVN7android17AudioALSAStreamInE` — `android::AudioALSAStreamIn`, address `0x00191990`, size 112 bytes.

The output implementation exposes these C++ methods in vtable order after the two destructor entries:

```text
sampleRate, bufferSize, channels, format, latency, setVolume, write,
standby, standbyStreamOut, dump, setParameters, getParameters,
getRenderPosition, getNextWriteTimestamp, getPresentationPosition,
updateSourceMetadata, setCallBack, pause, resume, drain, flush, set,
bufferSizeTimeUs, open, close
```

The input implementation exposes:

```text
sampleRate, bufferSize, channels, format, setGain, read, dump, standby,
setParameters, getParameters, getInputFramesLost, addAudioEffect,
removeAudioEffect, getCapturePosition, getActiveMicrophones,
updateSinkMetadata, set, getStandby, open, close,
checkOpenStreamFormat, checkOpenStreamChannels, checkOpenStreamSampleRate
```

The `AudioALSAStreamOut` constructor writes its C++ vptr at object offset 0. Therefore this implementation object is not itself the Android C `audio_stream_out` structure, whose first member is the C `audio_stream` callback table. The HAL stream adapter/wrapper and its exact C callback layout still need to be recovered from the stream-manager allocation and open/close paths. The same distinction must be verified for input streams before implementing the replacement stream ABI.

### Android C stream wrapper structures recovered

The device callbacks allocate a separate C ABI wrapper around the C++ stream implementation. The C++ vptr is not exposed as the Android stream pointer.

**Output stream**

- Allocation: `calloc(1, 124)`.
- Public `audio_stream_out` callback table: bytes `0..119` (120 bytes).
- Private `AudioALSAStreamOut *legacy_out`: offset `120` (`0x78`).
- The wrapper pointer is written to the framework's `audio_stream_out **` output parameter.
- `legacy_out` is returned by `AudioALSAHardware::openOutputStreamWithFlags`.

| Offset | Android callback | Factory implementation |
|---:|---|---|
| 0 | `get_sample_rate` | Forward to `AudioALSAStreamOut::sampleRate` |
| 4 | `set_sample_rate` | Logging no-op; returns 0 |
| 8 | `get_buffer_size` | `bufferSize` |
| 12 | `get_channels` | `channels` |
| 16 | `get_format` | `format` |
| 20 | `set_format` | Logging no-op; returns 0 |
| 24 | `standby` | `standby` |
| 28 | `dump` | Adapter implementation |
| 32, 36 | `get_device`, `set_device` | Null |
| 40, 44 | `set_parameters`, `get_parameters` | Adapter implementations |
| 48, 52 | `add_audio_effect`, `remove_audio_effect` | Return `-EINVAL` |
| 56 | `get_latency` | `latency` |
| 60 | `set_volume` | `setVolume` |
| 64 | `write` | `write` |
| 68 | `get_render_position` | `getRenderPosition` |
| 72 | `get_next_write_timestamp` | `getNextWriteTimestamp` |
| 76 | `set_callback` | `setCallBack` |
| 80, 84, 88, 92 | `pause`, `resume`, `drain`, `flush` | Matching C++ methods |
| 96 | `get_presentation_position` | `getPresentationPosition` |
| 100, 104, 108, 112 | `start`, `stop`, `create_mmap_buffer`, `get_mmap_position` | Null |
| 116 | `update_source_metadata` | `updateSourceMetadata` |
| 120 | Private field | `legacy_out` pointer |

**Input stream**

- Allocation: `calloc(1, 100)`.
- Public `audio_stream_in` callback table: bytes `0..95` (96 bytes).
- Private `AudioALSAStreamIn *legacy_in`: offset `96` (`0x60`).
- The wrapper pointer is written to the framework's `audio_stream_in **` output parameter.

| Offset | Android callback | Factory implementation |
|---:|---|---|
| 0 | `get_sample_rate` | Forward to `AudioALSAStreamIn::sampleRate` |
| 4 | `set_sample_rate` | Logging no-op; returns 0 |
| 8 | `get_buffer_size` | `bufferSize` |
| 12 | `get_channels` | `channels` |
| 16 | `get_format` | `format` |
| 20 | `set_format` | Logging no-op; returns 0 |
| 24 | `standby` | `standby` |
| 28 | `dump` | Adapter implementation |
| 32, 36 | `get_device`, `set_device` | Null |
| 40 | `set_parameters` | Adapter at `0x0009ccd0`; forwards to `AudioALSAStreamIn::setParameters` |
| 44 | `get_parameters` | Adapter at `0x0009cda8`; forwards to `AudioALSAStreamIn::getParameters` |
| 48, 52 | `add_audio_effect`, `remove_audio_effect` | Matching C++ methods |
| 56 | `set_gain` | `setGain` |
| 60 | `read` | `read` |
| 64 | `get_input_frames_lost` | `getInputFramesLost` |
| 68 | `get_capture_position` | `getCapturePosition` |
| 72, 76, 80, 84 | `start`, `stop`, `create_mmap_buffer`, `get_mmap_position` | Null |
| 88 | `get_active_microphones` | `getActiveMicrophones` |
| 92 | `update_sink_metadata` | `updateSinkMetadata` |
| 96 | Private field | `legacy_in` pointer |

### Stream-manager forwarding path

The factory HAL delegates stream creation and teardown through the `AudioALSAHardware` singleton to `AudioALSAStreamManager`. The recovered implementation entry points are:

| Method | Address | Size |
|---|---:|---:|
| `AudioALSAHardware::openOutputStreamWithFlags` | `0x000d1244` | 56 |
| `AudioALSAHardware::openInputStreamWithFlags` | `0x000cacc8` | 8 |
| `AudioALSAHardware::closeOutputStream` | `0x000cac88` | 8 |
| `AudioALSAHardware::closeInputStream` | `0x000cacd0` | 8 |
| `AudioALSAStreamManager::openOutputStream` | `0x00077e30` | 1492 |
| `AudioALSAStreamManager::closeOutputStream` | `0x00078404` | 1284 |
| `AudioALSAStreamManager::openInputStream` | `0x00078908` | 1452 |
| `AudioALSAStreamManager::closeInputStream` | `0x00078eb4` | 1204 |
| `AudioALSAStreamOut::set` | `0x0008172c` | 1376 |
| `AudioALSAStreamOut::open` | `0x00083d54` | 776 |
| `AudioALSAStreamOut::close` | `0x00084298` | 744 |
| `AudioALSAStreamIn::set` | `0x00128318` | 1320 |
| `AudioALSAStreamIn::open` | `0x0012b510` | 520 |
| `AudioALSAStreamIn::close` | `0x0012b868` | 684 |

The HAL adapter owns the small C wrapper allocation and its callback table. The manager owns the legacy C++ stream object and its route/PCM lifecycle. The stream `set()` methods perform format/device/configuration validation before stream activation; the exact supported combinations and all error branches still require full method-level analysis.

### Factory route-specific PCM handler selection

The C++ stream `open()` methods delegate actual playback/capture handler creation to the stream manager:

- `AudioALSAStreamOut::open` calls `AudioALSAStreamManager::createPlaybackHandler(stream_attribute_t*)` at `0x00079368`.
- `AudioALSAStreamIn::open` calls `AudioALSAStreamManager::createCaptureHandler(stream_attribute_t*)` at `0x00079cf0`.
- Both open paths also interact with `AudioALSASampleRateController` for scenario/rate coordination.

The factory binary contains these concrete handler constructors:

| Direction | Handler class |
|---|---|
| Playback | `AudioALSAPlaybackHandlerNormal` |
| Playback | `AudioALSAPlaybackHandlerVoice` |
| Playback | `AudioALSAPlaybackHandlerHfpClient` |
| Playback | `AudioALSAPlaybackHandlerBTSCO` |
| Playback | `AudioALSAPlaybackHandlerBTCVSD` |
| Playback | `AudioALSAPlaybackHandlerFMTransmitter` |
| Playback | `AudioALSAPlaybackHandlerHDMI` |
| Capture | `AudioALSACaptureHandlerNormal` |
| Capture | `AudioALSACaptureHandlerVoice` |
| Capture | `AudioALSACaptureHandlerFMRadio` |
| Capture | `AudioALSACaptureHandlerBT` |
| Capture | `AudioALSACaptureHandlerAEC` |

This confirms that factory PCM handling is route-specific rather than one universal playback/capture loop. The exact `stream_attribute_t` predicates selecting each handler, and the handler-to-ALSA endpoint/rate/format mappings, still need method-level analysis and P0 runtime corroboration.

The playback factory's dispatch table also exposes several concrete branches:
- The `stream_attribute_t` selection field at offset `0x18` equal to `0x400` constructs `AudioALSAPlaybackHandlerHDMI`.
- The same field equal to `0x100000` constructs `AudioALSAPlaybackHandlerFMTransmitter`.
- A Bluetooth SCO-specific branch consults `WCNChipController` and chooses `AudioALSAPlaybackHandlerBTSCO` when its capability check succeeds, otherwise `AudioALSAPlaybackHandlerBTCVSD`.
- Other common/default cases construct `AudioALSAPlaybackHandlerNormal`; voice/HFP paths are selected in earlier branches.

These are static dispatch facts. The semantic name of every vendor selection value and its active runtime use still require matching headers and target evidence.

### Normal playback endpoint and write pipeline

Static analysis of `AudioALSAPlaybackHandlerNormal::open` and `write` establishes:

- The normal handler resolves PCM and card indices through `AudioALSADeviceParser::GetPcmIndexByString` and `GetCardIndexByString`; it does not rely solely on a hard-coded PCM index.
- The parser uses the live ALSA inventory at `/proc/asound/pcm`. The existing read-only diagnostic collector already captures this file plus per-PCM `info` and substream `status`.
- It queries TinyALSA capabilities with `pcm_params_get`, `pcm_params_get_max` and `pcm_params_free`.
- It consults `AudioALSASampleRateController::getPrimaryStreamOutSampleRate` and the platform isolated-deep-buffer policy.
- `/vendor/etc/audio_device.xml` is a separate mixer-control/device-sequence configuration parsed by `AudioALSADeviceConfigManager`; it is not the source of the dynamic card/PCM indices.

The factory Normal write order is recovered directly from `AudioALSAPlaybackHandlerNormal::write`:

```text
doDcRemoval
-> doStereoToMonoConversionIfNeed
-> doPostProcessing
-> doBliSrc
-> doBitConversion
-> dodataPending
-> pcmWrite
-> AudioVUnlockDL::WriteStreamOutToRing
```

This confirms the earlier pipeline analysis at method level. The actual card/PCM indices and selected device-name keys still depend on target runtime inventory and the device parser configuration; do not hard-code them into the replacement before P0 evidence is collected.

**Stereo fold details recovered from the factory helper**

`AudioALSAPlaybackHandlerBase::doStereoToMonoConversionIfNeed` at `0x000d20ac`:
- checks speaker-device bit `0x02` in the output-device mask byte at `stream_attribute_t + 0x18`; if clear, it leaves the buffer unchanged. This includes speaker-only and combined output masks containing the speaker bit;
- for format value `1` (`AUDIO_FORMAT_PCM_16_BIT`), reads signed 16-bit left/right samples, arithmetic-shifts each by one, adds them and writes the same result to both channels;
- for format value `3` (`AUDIO_FORMAT_PCM_32_BIT`), performs the equivalent operation on 32-bit samples;
- other format values bypass this fold helper.

Therefore the first confirmed stereo-preserving DSP insertion point in the Normal path is **after `doDcRemoval` and before `doStereoToMonoConversionIfNeed`**. A replacement must not insert a stereo DSP after this fold and claim that independent left/right information is preserved.


### Factory PCM open and capture-provider lifecycle

The base playback PCM opener `AudioALSAPlaybackHandlerBase::openPcmDriverWithFlag`:
- receives the resolved PCM index and stream flags;
- obtains the card index from `AudioALSADeviceParser`;
- calls TinyALSA `pcm_open(card, pcm, flags, config)`;
- checks `pcm_is_ready`, calls `pcm_prepare`, and closes/clears the handle on failure.

Normal playback initialization calls the base PCM opener and initializes the handler stages for DC removal, MTK post-processing, BLI SRC, bit conversion and pending-data handling. It also configures the factory low-jitter/sample-rate policy.

Normal capture uses a shared provider/client design:
1. `AudioALSACaptureHandlerNormal::open` obtains the singleton `AudioALSACaptureDataProviderNormal` and attaches an `AudioALSACaptureDataClient` using the stream attributes.
2. `AudioALSACaptureDataProviderNormal::open` resolves card/PCM indices through `AudioALSADeviceParser`, queries TinyALSA parameters, calls `pcm_open`, `pcm_is_ready` and `pcm_prepare`, then starts its read thread.
3. The provider read thread sets its thread priority, waits for PCM start, reads through `AudioALSACaptureDataProviderBase::pcmRead`, obtains capture timestamps and calls `provideCaptureDataToAllClients`.
4. Each input stream's `AudioALSACaptureDataClient::read` consumes data from that shared provider.

This is a shared capture endpoint with client fan-out, not evidence that every Android input stream owns an independent ALSA PCM. The replacement AudioCore model must preserve shared-provider ownership and per-client delivery semantics where the factory route uses this path.





The device callback adapters pass the following recovered arguments into the C++ open methods:

- Output: `devices`, `flags`, `&config->format`, `&config->channel_mask`, `config`, and a local handle pointer. `openOutputStreamWithFlags` returns the legacy C++ output-stream pointer or null.
- Input: `devices`, `&config->format`, `&config->channel_mask`, `config`, a local handle pointer, zero acoustics, and `flags`. `openInputStreamWithFlags` returns the legacy C++ input-stream pointer or null.

The C wrapper stores the returned legacy pointer in its private tail and separately publishes the 124-byte or 100-byte C callback wrapper to the framework.


**Stream close and ownership**

The output close callback:
1. Reads `legacy_out` from wrapper offset 120.
2. Calls the device's `AudioALSAHardware::closeOutputStream(legacy_out)` forwarding method.
3. Tail-calls the factory's `free@LIBC` PLT entry with the C wrapper pointer.

The input close callback follows the same pattern using `legacy_in` at offset 96 and `AudioALSAHardware::closeInputStream(legacy_in)`, then frees the C wrapper. The legacy C++ stream object is handed to the AudioALSA stream manager for closure; the HAL C adapter frees only its own 124-byte or 100-byte wrapper. Do not double-free the legacy object in a replacement adapter.

**Input callback mapping verified**

The two parameter callbacks are independent function entries with valid prologues, despite lacking standalone dynamic symbol names:
- `0x0009ccd0` loads the private `legacy_in` pointer and dispatches through the `AudioALSAStreamIn::setParameters` vtable slot.
- `0x0009cda8` loads the private `legacy_in` pointer and dispatches through the `AudioALSAStreamIn::getParameters` vtable slot.

The earlier apparent mid-function targets were caused by using incorrect literal-pool values during the first manual address calculation. The corrected PC-relative calculations and disassembly confirm the expected input parameter callback mapping.


### Recovered factory module metadata and dynamic link contract

The factory binary audit and disassembly now confirm:

| Field | Recovered value |
|---|---|
| Dynamic module symbol | `HMI` |
| ELF symbol binding/visibility | `GLOBAL DEFAULT`, defined in the image |
| `HMI` address / size | `0x00195014` / 128 bytes |
| `hw_module_t.tag` | `0x48574d54` |
| `module_api_version` | `0x0001` |
| `hal_api_version` | `0x0100` |
| Module ID | `audio` |
| Module name | `MTK Audio HW HAL` |
| Author | `MTK` |
| `hw_module_methods_t` | `0x00195010` |
| `methods->open` | `0x0009b70c` |
| `hw_module_t.dso` | null |
| Device API version returned by open | `0x0300` |
| Factory device allocation | 224 bytes |

The module's `open` callback creates the 224-byte factory device wrapper, stores the C++ `AudioALSAHardware` object at offset 220, and initializes the AOSP callback table plus the vendor tail documented above.

**Dynamic link contract**

- `DT_SONAME`: `audio.primary.mt8163.so`
- `DT_FLAGS`: `BIND_NOW`
- `DT_FLAGS_1`: `NOW`
- Required symbol versions:
  - `libc.so`: `LIBC`, `LIBC_N`
  - `libdl.so`: `LIBC`
  - `libm.so`: `LIBC`

The factory `DT_NEEDED` list, in ELF order:

```text
libhidlbase.so
libhidltransport.so
libhwbinder.so
android.hardware.power@1.0.so
vendor.mediatek.hardware.power@2.0.so
libcutils.so
libutils.so
libalsautils.so
libbinder.so
libhardware_legacy.so
libhardware.so
libblisrc_vendor.so
libnvram.so
libaudiosetting.so
libcvsd_mtk.so
libmsbc_mtk.so
libaudioutils.so
libaudiocomponentengine_vendor.so
libtinyalsa.so
libtinycompress.so
libaudiodcrflt_vendor.so
libtinyxml.so
libaudiotoolkit_vendor.so
liblog.so
libspeech_enh_lib.so
libladder.so
libaedv.so
libbluetooth_mtk_pure.so
libc++.so
libc.so
libm.so
libdl.so
```

This is the factory binary's declared dependency contract. It does not by itself prove that every library is present in the target linker namespace or that a replacement with the same SONAME will be accepted by the device's configured namespace policy.

The extracted firmware tree contains all 32 dependency basenames. Vendor-specific dependencies are present under `/vendor/lib`; platform dependencies are present under `/system/lib`, with VNDK copies for several platform libraries under `/system/lib/vndk-28` or `/system/lib/vndk-sp-28`. This confirms artifact presence only, not namespace visibility.

No `ld.config*.txt` or equivalent linker namespace configuration file was found in the current `public_calculations` extracted tree. The actual target namespace search paths and permitted vendor/system library links therefore remain unresolved.


## 5. Method-level stream negotiation findings

The following results were recovered from the SHA-256-pinned factory ELF using ARM32 disassembly in workflow run [43395498637](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43395498637) and its follow-up audit. They are static binary findings, not proof that every combination is enabled by the live board policy.

### Output stream: `AudioALSAStreamOut::set`

Entry point: `0x0008172c`, size 1,376 bytes. The independent `SampleRateSupport(unsigned int)` helper is at `0x00081c8c`.

- Accepted format enum values are `1`, `3`, and `4`: PCM16, PCM32, and PCM8_24. Other values set the error result to `-EINVAL`; the format pointer is normalized to PCM16 on the error path.
- For the HDMI output device mask `0x400`, the channel-mask values checked by the factory are `0x63f` and `0x3f`.
- For the non-HDMI branch, the observed predicate is `(channel_mask | 0x2) == 0x3`, i.e. mask `0x1` or `0x3`. The factory then computes the channel count from the mask.
- The exact output sample-rate allowlist returned by `SampleRateSupport` is: **8,000; 11,025; 12,000; 22,050; 24,000; 32,000; 44,100; 48,000; 88,200; 96,000; 176,400; 192,000 Hz**. 16,000 Hz is not in this helper's accepted set.
- The set path stores an 8,192-byte buffer quantity and derives an additional internal timing/size field from the negotiated format, channel count and rate. Do not treat the internal field as the Android `get_buffer_size()` result until its getter and caller contract are reconciled.
- On unsupported sample rate, the disassembly shows normalization to 44,100 Hz and an `-EINVAL` status result. This is not equivalent to silently accepting the originally requested rate.

### Input stream: `AudioALSAStreamIn::set`

Entry point: `0x00128318`, size 1,320 bytes. Validation helpers:

| Helper | Address | Recovered behavior |
|---|---:|---|
| `checkOpenStreamFormat(int*)` | `0x00127fac` | Accepts PCM16 (`1`); for another value writes PCM16 to the supplied field and returns false |
| `checkOpenStreamChannels(unsigned int*)` | `0x0012800c` | Accepts `0x0c` directly; otherwise uses representation bits `0xc000` and rejects masks containing bit `0x10`. Preserve this as a raw predicate until vendor channel-mask declarations are pinned |
| `checkOpenStreamSampleRate(unsigned int, unsigned int*)` | `0x00128094` | Route-dependent checks described below |

The input rate validator branches on the raw input-device mask:
- `0x80000020`: requires 44,100 Hz;
- `0xc0000000`: requires 16,000 Hz;
- `0x80002000`: compares against the current rate returned by `AudioALSAFMController`;
- other masks consult `AudioALSAStreamManager` state and route flags, with 16,000/48,000-Hz handling and a 48,000-Hz fallback path.

Do not assign semantic names to the two vendor-specific masks without matching vendor headers. The helper may rewrite the requested rate on a rejected path; callers must preserve and test both the returned status and the updated configuration.

### Open and buffer timing

- `AudioALSAStreamOut::open()` (`0x83d54`) creates a playback handler through `AudioALSAStreamManager::createPlaybackHandler`, then invokes the handler's open method.
- `AudioALSAStreamIn::open()` (`0x12b510`) follows the equivalent capture path through `createCaptureHandler`.
- A missing handler or failed handler open returns an error; these methods do not select a hard-coded ALSA PCM index themselves.
- `AudioALSAStreamOut::bufferSizeTimeUs()` (`0x81d58`) computes the duration of the 8,192-byte quantity as `1,000,000 * 8192 / (channels * sample_rate * bytes_per_sample)`. The byte width comes from the selected PCM format.
- `AudioALSAHardware::openOutputStream()` (`0x000cac58`) forwards the legacy Android entry point to `AudioALSAStreamManager::openOutputStream()` with the final output-flags argument set to zero.
- `AudioALSAHardware::openOutputStreamWithFlags()` (`0x000d1244`) reorders the supplied `audio_output_flags_t` into the manager's final argument. The manager forwards it to `AudioALSAStreamOut::set()`; the stream object stores the raw value at offset `0x54`. This proves propagation, not the semantics of individual flag bits.
- The output `bufferSize()` getter reads the stream-object field at offset `0x74`; the successful `set()` path stores `8192` there. The recovered C wrapper's `get_buffer_size` callback therefore exposes an 8,192-byte output buffer on this path.
- The manager uses `createPlaybackHandler(stream_attribute_t*)` and `createCaptureHandler(stream_attribute_t*)` as route-handler factories. Handler selection is not equivalent to a simple one-to-one mapping from Android output flags; keep exact flag-bit behavior open until every branch is decoded.
- `AudioALSAHardware::openInputStream()` (`0x000cac90`) forwards the legacy entry point with input flags set to zero. `openInputStreamWithFlags()` (`0x000cacc8`) tail-forwards the supplied `audio_input_flags_t` unchanged to the manager.
- `AudioALSAStreamIn::set()` stores input flags at stream-object offset `0x54` and tests bit `0x01`. With `AUDIO_INPUT_FLAG_FAST` (`0x01`), it selects a **5 ms** buffer duration. Without FAST, the normal path is **20 ms**; one WCN-controller-gated path selects **60 ms** when the controller query returns zero and **20 ms` otherwise.
- The input `bufferSize()` getter reads offset `0x74`. The input `set()` method calculates this field from sample rate, normalized channel count, bytes per sample and the selected 5/20/60 ms duration. The recovered input format validator normalizes unsupported formats to PCM16.
- `AudioALSAStreamManager::getInputBufferSize()` (`0x0007eee8`) separately computes a 20 ms quantity using its third argument and a format-width lookup. The exact table for format enum values 1–5 is **[2, 1, 4, 4, 0] bytes**; values outside 1–5 fall back to 2 bytes. Consequently, enum value 5 returns a zero-width result in this helper. The method does not visibly convert the third argument from a channel mask to a channel count, so that argument's exact caller-side contract remains unresolved.
- Input FAST behavior and buffer-duration selection are recovered; other input flag bits, exact third-argument semantics, every error branch, all channel-mask combinations and vendor-header reconciliation remain open.

These findings close only the static format/rate predicate portion of P2. They do not establish runtime policy acceptance, physical endpoint capabilities, or route behavior.

### External-client and HIDL bridge recovery

The factory audit workflow now scans every extracted shared library under `system/lib` and `vendor/lib`, rather than a small hand-picked client set. In [run 43399592976](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43399592976), it scanned **1,052 shared libraries** and found four libraries with matching symbols or strings:

| Library | SHA-256 | Evidence |
|---|---|---|
| `vendor/lib/hw/android.hardware.audio@4.0-impl-mediatek.so` | `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc` | Implements MediaTek HIDL 4.1 extension methods and dispatches into the legacy device tail |
| `vendor/lib/vendor.mediatek.hardware.audio@4.1.so` | `553704e5285c4ab1e505bab569ac261f40ce7e6aa8c0c9173e038acc59dc3be5` | Generated Bp/Bn HIDL interface and callback transport methods |
| `vendor/lib/libaudioprimarydevicehalifclient.so` | `b5e805b6cf20d995babb730f70b6287d4d4ff0c7168c189de326d05e8d58c22c` | Factory client loads/uses the MTK primary-device interface |
| `vendor/lib/hw/audio.primary.mt8163.so` | Factory HAL SHA-256 above | Exports the legacy device callback implementations |

The scan is symbol/string based. It does not prove absence of indirect calls from stripped executables or code that accesses the private tail by numeric offset. It does, however, identify the actual vendor HIDL bridge that consumes a substantial part of that tail.

#### Confirmed HIDL 4.1 to legacy HAL callback mapping

Disassembly of `android.hardware.audio@4.0-impl-mediatek.so` confirms that the HIDL `Device` adapter obtains the legacy `audio_hw_device` pointer from its device wrapper and invokes these function pointers by their exact byte offsets:

| Private device offset | HIDL 4.1 method | Legacy callback |
|---:|---|---|
| 188 (`0xbc`) | `getVoiceUnlockULTime` | `GetVoiceUnlockULTime(void*)` |
| 192 (`0xc0`) | `setVoiceUnlockSRC` | `SetVoiceUnlockSRC(unsigned int, unsigned int)` |
| 196 (`0xc4`) | `startVoiceUnlockDL` | `startVoiceUnlockDL()` |
| 200 (`0xc8`) | `stopVoiceUnlockDL` | `stopVoiceUnlockDL()` |
| 204 (`0xcc`) | `getVoiceUnlockDLInstance` | `getVoiceUnlockDLInstance()` |
| 208 (`0xd0`) | `setupParametersCallback` | `setupParametersCallback(callback, cookie)` |
| 212 (`0xd4`) | `setAudioParameterChangedCallback` | `setAudioParameterChangedCallback(callback, cookie)` |
| 216 (`0xd8`) | `clearAudioParameterChangedCallback` | `clearAudioParameterChangedCallback(cookie)` |

The `Device::audioParameterChangedCallback(char const*, void*)` implementation forwards parameter-change notifications through the registered `IAudioParameterChangedCallback::audioParameterChangedCallback(hidl_string)` HIDL callback. This establishes both registration and notification directions for the parameter-change callback.

The HIDL adapter has null-pointer branches for absent legacy callbacks and returns an error result instead of invoking a null function pointer. Exact public HIDL result-enum interpretation and callback lifetime/concurrency semantics still need to be reconciled against the generated interface declarations and runtime service behavior.

#### Vendor HIDL service registration and ATCI client entry

The service executable and init configuration were also included in [factory audit run 43401616001](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43401616001):

- `vendor/android.hardware.audio@4.0-service-mediatek` SHA-256: `1f2848726bb752eb291c5abe583144e6cc4f4b49047b38750f2af21b96e9a827`. Its dynamic imports include `IDevicesFactory::descriptor`, `getService()` and `registerAsService()`; strings include `registered IDevicesFactory`.
- `vendor/android.hardware.audio@4.0-service-mediatek.rc` SHA-256: `20089785ff5d27955f2ce22314804b78bad89b0bb00f6c125b39330a95b3fe7f`. Init starts the service as `vendor.audio-hal-4-0`, runs it as `audioserver`, and declares `android.hardware.audio@4.0::IDevicesFactory default`.
- `vendor/audiocmdservice_atci` SHA-256: `302eee8562067a54d4b9e65ef982a100b94b36be6f73a993b9e01e059421a566`. Its dynamic imports include `android::MTKPrimaryDevicesHalClient::getInstance()`.

This establishes two separate factory client paths: Android's HIDL DevicesFactory service and the ATCI service's MediaTek primary-device client. It does not prove that ATCI calls the private xWay/recording callbacks; its recovered client bridge still exposes only the parameter-client `setParameters/getParameters` path.

#### What remains unresolved in the private tail

- Offsets 156–184: `xWayPlay_Start/Stop/Write/GetFreeBufferCount`, both `xWayRec_Stop` entry forms, `xWayRec_Read`, and `ReadRefFromRing` have not yet been tied to all their external callers.
- The duplicate `xWayRec_Stop` entries have different first-argument conventions and must remain distinct.
- The six-library negative scan recorded in the earlier audit is superseded by the 1,052-library scan above.
- The scan did not include every executable, APK/JNI payload, or runtime-loaded component; indirect callers remain possible.

The linker-namespace portion of the same full-tree audit found no `ld.config.txt` or `ld.config.28.txt` in the extracted `system/etc` or `vendor/etc` locations. It did find the system/vendor audio policy XML, legacy `vendor/audio_policy.conf`, and `vendor/audio_device.xml`. Those policy files are not substitutes for linker namespace configuration.

Do not populate or replace unresolved private callbacks with guessed signatures. Preserve only the exact factory ABI until the remaining caller and lifetime contracts are understood.

## 6. Vendor-specific facts that remain unresolved

The factory binary is known from the extracted firmware:
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- size: 1,671,852 bytes
- ELF32 ARM EABI5, Android API 28
- Build ID: `f66eaf9e6ac950259f0c253be618d15a`.

The following runtime/vendor integration facts remain unresolved:
1. Whether every `DT_NEEDED` library is present and permitted in the target Android 9 linker namespace.
2. Exact target linker namespace configuration and library search paths.
3. Runtime semantics and external callers for every private vendor callback, including the duplicate `xWayRec_Stop` entry forms.
4. Exact vendor headers/source revision and any private type/signature differences from the pinned AOSP r3 headers.
5. Route-by-route behavior and stream negotiation under the factory framework clients.

The static HMI, version, device table, stream wrapper sizes, C++ vtables, SONAME and dependency list are now recovered from the supplied binary; do not reclassify these as unresolved.

## 7. ABI audit tooling change

`tools/audit_primary_hal_binary.sh` now checks:
- factory filename;
- ELF shared-object type (`ET_DYN`);
- ELF32 and ARM machine identity;
- ARM EABI5 flags;
- exported `HMI` in the dynamic symbol table;
- build ID, program/section headers, SONAME, dependencies, symbol versions, relocations and exported symbols for manual review;
- a warning when the artifact size or SHA-256 differs from the recorded factory baseline.

This tool is static evidence only. It does not establish callback semantics, runtime linker compatibility or device behavior. Repository CI checks its shell syntax.

## 8. Next exact recovery steps

1. Keep the factory ELF audit and APS2 vtable decoder as reproducible CI gates.
2. Reconcile the full `DT_NEEDED` list with the target firmware's linker namespace and exact library locations.
3. Recover the matching vendor header/source revision and compare its types/signatures with the AOSP r3 baseline.
4. Trace external callers and runtime semantics of the private MediaTek device and stream callbacks.
5. Implement a typed factory-compatible HAL shell using the recovered module/device/stream layouts.
6. Add fake-backend lifecycle tests before porting any hardware or route behavior.

No hardware writes, module loading, binary patching or factory replacement are part of this ABI-recovery stage.
