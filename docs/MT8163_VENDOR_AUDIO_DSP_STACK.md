# MT8163 vendor audio dependency and DSP pipeline

**Evidence source:** user-supplied firmware library archive `lib_bf57dda0-1874-4303-acf8-5302f3787609.zip` (325 files, ZIP CRC test passed; 43,323,642 bytes). The selected binaries are ELF32 ARM shared objects. This is static analysis only.

## Dependency chain recovered

The supplied `audio.primary.mt8163.so` links to the vendor audio component engine and related platform libraries. The matching `libaudiocomponentengine_vendor.so` has these direct dependencies:

- `libaudiocompensationfilter_vendor.so`
- `libbessound_hd_mtk_vendor.so`
- `libmtklimiter_vendor.so`
- `libaudiodcrflt_vendor.so`
- `libmtkshifter_vendor.so`
- `libblisrc32_vendor.so`

This is a concrete vendor DSP component stack, not a hypothetical replacement chain. Presence in DT_NEEDED/import tables does not alone prove that every component is active for every route.

## ACF/HCF and custom-parameter storage

### `libaudiocompensationfilter_vendor.so`

Confirmed exported symbols include:

- `android::getDefaultAudioCompFltParam(AudioCompFltType_t, _AUDIO_ACF_CUSTOM_PARAM_STRUCT*)`
- `android::audioComFltCustParamInit()`
- `getAudioCompFltCustParamFromStorage`
- `setAudioCompFltCustParamToStorage`

Strings identify default NVRAM/custom-storage scenes:

- `audio_hcf_custom_default`
- `audio_musicdrc_custom_default`
- `audio_ringtonedrc_custom_default`

**The exact size of `_AUDIO_ACF_CUSTOM_PARAM_STRUCT` is confirmed as 1260 bytes (`0x4EC`).** ARM Thumb disassembly of `getDefaultAudioCompFltParam()` loads `0x4EC` as the structure-copy size and returns that size. `getAudioCompFltCustParamFromStorage()` compares the storage record size against the same `0x4EC` value and falls back to default parameters on mismatch. The component-engine custom-parameter cache also copies/compares records using `0x4EC`.

This establishes the top-level structure size, **not** its complete field offsets, nested array dimensions, coefficient formats, or enum meanings. Those still require data-flow/layout recovery.

The library depends on `libcustom_nvram.so`, `libnvram.so`, and `libaudiocustparam_vendor.so`. It logs a size mismatch and falls back to defaults when a stored parameter structure has the wrong size.

### `libaudiocomponentengine_vendor.so`

Exported symbols expose the vendor processing objects:

- `android::MtkAudioLoud::setParameter(unsigned int, void*)`
- `android::MtkAudioLoud::process(void*, unsigned int*, void*, unsigned int*)`
- `android::MtkAudioLoud::open()`, `init()`, `close()`, `resetBuffer()`
- `android::MtkAudioLoud::convertBesLoudCoeffV4ToV5(...)`
- `android::MtkAudioLoud::setCustSceneName(char const*)`
- `android::MtkAudioLoud::getCustSceneName(char*)`
- `android::MtkAudioCustParamCache::loadParameter(...)` and `saveParameter(...)`

The engine imports `BLOUD_HD_Filter_Design`, `Limiter_Open/Process/Reset`, `DCR_Process` (including 24-bit variants), `Shifter_Process`, and sample-rate conversion functions. This directly ties the component engine to the BES loudness DSP, limiter, DC-removal, shifter, and SRC libraries.

The BES library exports `BLOUD_HD_Internal_Process`, `BLOUD_HD_Filter_Design`, `Design_Filter_Coef`, `IIR_Filter_Bank`, `Apply_DRC`, `Generate_MBDRC_Flt_Bank_Coef`, and 16-/32-bit coefficient helpers. These are candidate native processing primitives for later comparison with our PCM DSP, but they are not evidence of a one-to-one equivalent to any particular ViPER module.

### Parameter selector caution

The Thumb implementation of `MtkAudioLoud::setParameter(unsigned int, void*)` contains a selector dispatch for values up to 15, with additional internal state and coefficient handling. The recovered `UpdateACFHCF()` caller uses only selectors 0, 1, and 4 for the three update paths it handles. Do not assume that all selector values are interchangeable, or that the caller's ACF/HCF type enum is identical to the engine's `setParameter` selector. The payload pointer must reference a valid 1260-byte structure for the ACF/HCF update path; no live invocation should be attempted with an invented structure.

## XML parameter parser / control plane

`libaudio_param_parser-vnd.so` exports a broad XML parameter-tree API and imports Android Binder ProcessState methods. Its strings explicitly contain the parameter categories:

- `PlaybackACF`
- `PlaybackDRC`
- `PlaybackHCF`

It also exports `audioSystemGetParameters` and `audioSystemSetParameters`, plus XML loading/reload and category-path APIs. This strongly indicates that vendor tuning parameters are represented in XML and can be accessed through Android AudioSystem parameter calls. The exact key/value grammar and the XML files are not included in this library archive, so no key names or payload syntax are assumed here.

## Other relevant libraries

- `libaudiocustparam_vendor.so` uses NVRAM for audio and speech custom parameters; its strings include speech and HFP records. Do not conflate those records with playback ACF/HCF.
- `libaudiodcrflt_vendor.so` exports 16-bit and 24-bit DC-removal processing entry points.
- `libmtklimiter_vendor.so` exports buffer sizing, open, parameter, process, reset, status, and version functions.
- `libmtkshifter_vendor.so` exports `Shifter_Process`.
- `libaudioprimarydevicehalifclient.so` depends on Android Audio HAL 4.0 HIDL interfaces, which may be useful when tracing the framework-to-primary-device control path.

## Next static-analysis steps

1. Recover the exact field offsets, nested arrays, and coefficient layout inside the 1260-byte `_AUDIO_ACF_CUSTOM_PARAM_STRUCT` from matching headers, debug metadata, or data-flow analysis of `getDefaultAudioCompFltParam`, `getAudioCompFltCustParamFromStorage`, and `MtkAudioLoud::setParameter`.
2. Trace the `PlaybackACF`, `PlaybackDRC`, and `PlaybackHCF` XML documents and the parser's AudioSystem key/value dispatch to identify supported user-space controls.
3. Map the actual `AudioMTKFilterManager` slots to concrete engine objects by inspecting construction and filter-manager setup code; do not infer slot semantics from array position or enum names.
4. Establish the active media output PCM format and route before any integration. Keep SCO, voice, microphone, FM, USB, and HDMI paths untouched until independently mapped.
5. Compare the vendor BES DSP's capabilities and coefficient formats with our independent PCM engine before deciding whether to reuse, wrap, or bypass any vendor processing.

## CD3313 boundary

No direct CD3313 device transport, ioctl request, I2C operation, or register mapping is established by this library dependency analysis. The presence of BES processing or speaker-filter terminology must not be treated as proof of direct CD3313 control. Keep chip-control recovery separate from ACF/HCF PCM processing.

## Confidence

| Finding | Confidence |
|---|---|
| Vendor component engine directly depends on ACF, BES, limiter, DCR, shifter, and SRC libraries | High — ELF DT_NEEDED and import symbols |
| `_AUDIO_ACF_CUSTOM_PARAM_STRUCT` top-level size is 1260 bytes (`0x4EC`) | High — repeated immediate constant in ARM Thumb copy and size-check paths |
| MtkAudioLoud exposes setParameter/process and lifecycle methods | High — exported symbols |
| Parser recognizes PlaybackACF/DRC/HCF and exposes AudioSystem get/set parameters | High — exported symbols and strings |
| BES library contains IIR, DRC, multiband coefficient-generation and processing primitives | High — exported symbols |
| Complete ACF structure field layout and XML key grammar | Not yet recovered |
| Any of these libraries directly controls CD3313 | Unproven |


## Recovered selector behavior in `MtkAudioLoud::setParameter`

ARM Thumb disassembly of the 680-byte implementation shows a bounds-checked 16-entry `tbh` dispatch table (selectors 0 through 15). The following observations are directly visible in the machine code:

| Selector | Confirmed low-level behavior | Interpretation status |
|---:|---|---|
| 0 | Stores the supplied second argument at object offset `+0x0C` | Pointer/state slot; payload meaning not proven |
| 1 | Stores the argument at object offset `+0x10`, writes `0x200` at `+0x9B8`, then validates/configures a small value range | Format/configuration path; exact enum not proven |
| 2 | Accepts values 1 or 2 and stores the value at `+0x24`; other values take an early exit | Small enumerated setting; meaning not proven |
| 3 | Stores the supplied argument at object offset `+0x28` | Field setter; meaning not proven |
| 4 | Stores the supplied second argument at object offset `+0x2C` | Pointer/state slot; payload meaning not proven |
| 5 | Calls internal helpers using the state at `+0x0C` and a working area beginning at `+0x5C` | Internal parameter/configuration path; exact role not proven |
| 6 | Calls helpers and copies exactly `0x4EC` (1260) bytes into the working area at object offset `+0x5C` | Strong ACF-structure handling candidate; exact field layout still unknown |
| 7, 8, 10 | Share a common dispatch target that updates internal state/flags | Exact meaning not proven |
| 9, 11, 12 | Enter callback/dispatch paths using object fields around `+0x34` and `+0x40` | Callback/control paths; exact meaning not proven |
| 13 | Enters a helper path using the working area at `+0x5C` and additional state near `+0x9BC` | Parameter processing path; exact meaning not proven |
| 14 | Sets the field at `+0x9B4` to either `0` or `0x50`, depending on the argument | Numeric mode/setting; exact meaning not proven |
| 15 | Enters another helper/parameter path using the working area at `+0x5C` and state near `+0x9BC` | Parameter processing path; exact meaning not proven |

**Important distinction:** the HAL's recovered `UpdateACFHCF()` caller invokes `setParameter(0, payload)`, `setParameter(1, payload)`, and `setParameter(4, payload)` for its three update cases. The 1260-byte copy is visible in selector 6, not selector 0, 1, or 4. Therefore the top-level HAL caller path and the engine's internal copy path are not yet fully reconciled. The pointers saved by selectors 0 and 4 may be consumed later by helper/callback paths. Do not call these selectors with guessed buffers, and do not reinterpret selector 6 as the proven HAL ACF update selector.

The library's `.gnu_debugdata` section was also checked. It contains a small mini-ELF with unwind/runtime symbols but no useful C++ type or DWARF structure definitions, so it does not directly recover the ACF member offsets.

### Next reverse-engineering target

Trace the consumers of object fields `+0x0C`, `+0x2C`, `+0x5C`, `+0x9B4`, `+0x9B8`, and `+0x9BC`, then follow the function-pointer callbacks around `+0x40`. This is the shortest route to reconcile the HAL's ACF/HCF selector calls with the engine's internal parameter representation. Keep the analysis static; no runtime calls with fabricated structures.


## Partial ACF structure layout recovered from `MtkAudioLoud::copyParam()`

Further ARM Thumb analysis of `copyParam()` (symbol at `0x4CF9`, size 1200 bytes) recovers a set of source-to-destination copies. The destination is reached by loading the pointer at `*(this + 0x34) + 0x0C`; the table below records the source offsets relative to the `MtkAudioLoud` object and destination offsets relative to that parameter structure.

| ACF destination offset | Bytes copied | Source object offset | Operation |
|---:|---:|---:|---|
| `0x000` | 4 | `0x140` | 32-bit word |
| `0x004` | 4 | `0x144` | 32-bit word |
| `0x008` | 4 | `0x148` | 32-bit word |
| `0x010` | 4 | `0x14C` | 32-bit word |
| `0x014` | 4 | `0x150` | 32-bit word |
| `0x018` | 16 | `0x160` | NEON 128-bit copy |
| `0x024` | 16 | `0x154` | NEON 128-bit copy |
| `0x034` | 160 (`0xA0`) | `0x170` | memory copy |
| `0x0D4` | 160 (`0xA0`) | `0x210` | memory copy |
| `0x174` | 16 | `0x2B0` | NEON 128-bit copy |
| `0x184` | 16 | `0x2C0` | NEON 128-bit copy |
| `0x194` | 16 | `0x2D0` | NEON 128-bit copy |
| `0x1A4` | 16 | `0x2E0` | NEON 128-bit copy |
| `0x1B4` | 4 | `0x2F0` | 32-bit word |
| `0x1B8` | 192 (`0xC0`) | `0x2F4` | memory copy |
| `0x278` | 192 (`0xC0`) | `0x394` | memory copy |
| `0x338` | 192 (`0xC0`) | `0x474` | memory copy |
| `0x3F8` | 4 | `0x534` | 32-bit word |
| `0x3FC` | 4 | `0x538` | 32-bit word |
| `0x400` | 4 | `0x53C` | 32-bit word |
| `0x404` | 4 | `0x540` | 32-bit word |

The offsets above are grounded in explicit loads/stores, NEON vector loads/stores, and helper calls whose arguments are destination pointer, source pointer, and byte count. The three `0xA0/0xC0`-byte copies are therefore known block boundaries, but their semantic meaning (for example, filter count, coefficient type, or channel arrangement) remains unknown.

The recovered copies cover through destination offset `0x407`; the top-level structure is `0x4EC` bytes. The remaining tail is not mapped by this copy routine and must not be assumed to be padding.

The routine also reads configuration/state fields at object offsets `+0x9B4`, `+0x9B0`, `+0x518`, and `+0x54C`, and examines per-entry state at offsets including `+0x6C`, `+0x8C`, `+0xAC`, `+0xDC`, `+0xFC`, and `+0x11C`. These are internal engine-object offsets, not ACF structure offsets.

**Limit:** this is a partial layout map, not a reconstructed C header. Do not serialize, modify, or pass a hand-built structure based only on these offsets. Next, trace the code that populates the source regions (object offsets `+0x140` through `+0x540`) and the consumers of the destination blocks to identify their types and units.


## BES sampling-rate indices and coefficient conversion

The implementation of `MtkAudioLoud::getBesLoudSamplingRateIndex(unsigned int)` compares the input rate against explicit constants and returns these exact indices:

| Input sample rate | Returned BES index |
|---:|---:|
| 48,000 Hz | 0 |
| 44,100 Hz | 1 |
| 32,000 Hz | 2 |
| 24,000 Hz | 3 |
| 22,050 Hz | 4 |
| 16,000 Hz | 5 |
| 12,000 Hz | 6 |
| 11,025 Hz | 7 |
| 8,000 Hz | 8 |
| Any other rate | `0xFFFFFFFF` (unsupported) |

This is the set explicitly handled by this helper; it does not establish which rates the complete HAL route supports or whether another conversion stage is used upstream.

The exported `convertBesLoudCoeffV4ToV5(unsigned int, _AUDIO_ACF_CUSTOM_PARAM_STRUCT_FILTER_PARAM*, BLOUD_HD_FilterCoef*)` provides additional nested-layout clues:

- Rejects a null input/output pointer and returns an error when the index exceeds 8.
- For each accepted index, addresses a source record at an index-dependent 20-byte offset.
- Copies five 16-byte coefficient vectors from source records separated by `0xB4` bytes, and copies an adjacent 32-bit word for each record.
- For indices up to 5, executes an additional conversion loop over source regions based around offsets `0x3A8`, `0x3B0`, and `0x168`, producing 24-byte-spaced output records.
- Initializes several output fields/regions before conversion, including zeroing fields at offsets `0x20`, `0xEC`, and `0xF0`.

This confirms a coefficient-conversion routine with bounded index and repeated records, but does not yet establish the semantic names, units, or complete C layout of the nested filter parameter and BES coefficient structures.
