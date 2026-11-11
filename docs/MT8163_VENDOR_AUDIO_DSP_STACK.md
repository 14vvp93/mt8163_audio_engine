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

The library depends on `libcustom_nvram.so`, `libnvram.so`, and `libaudiocustparam_vendor.so`. It logs a size mismatch and falls back to defaults when a stored parameter structure has the wrong size. This confirms size-sensitive persistence, but the actual structure size and field layout still need to be recovered from the exact implementation or matching headers.

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

1. Recover the exact `_AUDIO_ACF_CUSTOM_PARAM_STRUCT` and nested filter-coefficient layout from matching headers, debug metadata, or data-flow analysis of `getDefaultAudioCompFltParam`, `getAudioCompFltCustParamFromStorage`, and `MtkAudioLoud::setParameter`.
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
| MtkAudioLoud exposes setParameter/process and lifecycle methods | High — exported symbols |
| Parser recognizes PlaybackACF/DRC/HCF and exposes AudioSystem get/set parameters | High — exported symbols and strings |
| BES library contains IIR, DRC, multiband coefficient-generation and processing primitives | High — exported symbols |
| Exact ACF structure layout and XML key grammar | Not yet recovered |
| Any of these libraries directly controls CD3313 | Unproven |
