# Android 9 ARM32 primary HAL ABI baseline

Date: 2026-11-13  
Target: MT8163 / Android 9 / ARM32 EABI5  
Factory artifact: `/vendor/lib/hw/audio.primary.mt8163.so`  
Status: AOSP platform contract pinned to the matching Android release; vendor binary's actual API version and vendor extensions remain to be verified.

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

The module metadata is decoded from the actual bytes at the `HMI` object, not inferred from AOSP defaults. The `open` function pointer has been recovered, but its implementation and returned `hw_device_t.version` still require disassembly and control-flow analysis.

## 5. Vendor-specific facts that remain unresolved

The factory binary is known from the extracted firmware:
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- size: 1,671,852 bytes
- ELF32 ARM EABI5, Android API 28
- Build ID: `f66eaf9e6ac950259f0c253be618d15a`.

The current static-analysis reports establish MTK filter and stream-manager symbols, but do not yet publish the following primary-HAL loader facts:
1. Whether `HMI` is present and exported in `.dynsym`.
2. Actual `hw_module_t.module_api_version` and `hal_api_version` values.
3. Actual `hw_device_t.version` returned by `open()`.
4. The device method table and any vendor-specific structure tail.
5. Exact `DT_SONAME`, `DT_NEEDED`, symbol-version requirements and linker namespace assumptions.
6. Actual module/device open path and whether vendor extensions wrap or replace any AOSP callback.

Do not fill these values from the AOSP reference table. They must be recovered from the supplied binary's ELF symbols, relocations and disassembly.

## 6. ABI audit tooling change

`tools/audit_primary_hal_binary.sh` now checks:
- factory filename;
- ELF shared-object type (`ET_DYN`);
- ELF32 and ARM machine identity;
- ARM EABI5 flags;
- exported `HMI` in the dynamic symbol table;
- build ID, program/section headers, SONAME, dependencies, symbol versions, relocations and exported symbols for manual review;
- a warning when the artifact size or SHA-256 differs from the recorded factory baseline.

This tool is static evidence only. It does not establish callback semantics, runtime linker compatibility or device behavior. Repository CI checks its shell syntax.

## 7. Next exact recovery steps

1. Run the improved audit on the immutable factory binary and retain its complete output.
2. Disassemble the module constructor and `HMI` data object; identify module API fields and the `methods->open` function pointer.
3. Trace `open("primary")` to the returned `audio_hw_device` allocation and recover the device API version and method table.
4. Map every method-table entry to its implementation and cross-check against AOSP r3 offsets.
5. Recover the actual dynamic dependency/symbol contract and linker namespace from the same firmware build.
6. Only then create the factory-named HAL shell and compile it against the pinned vendor headers.

No hardware writes, module loading, binary patching or factory replacement are part of this ABI-recovery stage.
