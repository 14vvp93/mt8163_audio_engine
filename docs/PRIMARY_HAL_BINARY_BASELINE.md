# Factory primary HAL binary baseline

Date: 2026-11-12  
Target: MT8163 / Android 9 / ARM32  
Factory artifact: `/vendor/lib/hw/audio.primary.mt8163.so`  
Status: static binary and policy evidence; no replacement HAL implemented

## 1. Binary identity

| Property | Recovered value |
|---|---|
| Extracted source path | `firmware/extracted/vendor/lib/hw/audio.primary.mt8163.so` |
| Factory destination | `/vendor/lib/hw/audio.primary.mt8163.so` |
| SHA-256 | `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4` |
| Size | 1,671,852 bytes |
| ELF | ELF32 ARM EABI5 |
| Android target | API 28 |
| Build ID | `f66eaf9e6ac950259f0c253be618d15a` |

The hash and metadata describe the supplied extracted firmware, not a live device. Preserve this exact original for rollback.

## 2. Recovered MTK filter symbols

The dynamic symbol table retains these relevant symbols:

- `android::AudioMTKFilterManager::setParameter(unsigned int, void*)` at `0x9e754`
- `android::AudioMTKFilterManager::setSpkFilterParam(unsigned int, unsigned int, int)` at `0x9e864`
- `android::AudioMTKFilter::setFilterParam(unsigned int, unsigned int, int)` at `0x9d66c`
- `android::AudioMTKFilter::init()` at `0x9cfe0`
- `android::AudioALSAStreamManager::UpdateACFHCF(int)` at `0x7f18c`
- `SetACFPreviewParameter(void*, int)` at `0x7f330`
- `SetHCFPreviewParameter(void*, int)` at `0x7f3a8`

The manager parameter selector accepts 0..4. Selectors 0..3 dispatch to filter slots at manager offsets +0x14, +0x18, +0x1c and +0x20. Selector 4 reuses slot +0x14 and invokes `setParameter2Sub()`. The semantic identity of every slot must not be inferred solely from its offset.

The filter wrapper creates a proprietary `MtkAudioLoud` backend through `newMtkAudioLoud(unsigned int)`; it is not a generic AOSP effect implementation.

## 3. Proven Normal playback processing boundary

The supplied binary contains one direct call to `AudioALSAPlaybackHandlerBase::doPostProcessing()`, from `AudioALSAPlaybackHandlerNormal::write()` at `0xD4744`.

Observed order:

1. `doDcRemoval()`
2. `doStereoToMonoConversionIfNeed()`
3. `doPostProcessing()`
4. `doBliSrc()`
5. `doBitConversion()`
6. `pcmWrite()`

The post-processing helper requires a non-null filter manager, stream-attribute byte +0x120 equal to zero, byte +0x41 equal to zero, and input byte count no greater than the allocated scratch capacity. The capacity is `0x20000` (131,072 bytes); it is not a PCM period size.

When processing is skipped or returns no output, the original pipeline buffer is retained. When processing succeeds, the selected scratch buffer and produced byte count are passed to BLI SRC and bit conversion.

The +0x120 bypass flag is populated from `AudioSpeechEnhanceInfo::IsAPDMNRTuningEnable()`. The +0x41 flag is copied from manager +0xD6 and forced on for sample rates >= 48,001 Hz. The semantic name of the manager flag remains unresolved.

## 4. Proven speaker conversion constraint

Before the filter callback, `doStereoToMonoConversionIfNeed()` tests output-device mask bit `0x02` (`AUDIO_DEVICE_OUT_SPEAKER`). When set, it averages adjacent L/R samples and writes the same mono value into both positions. This is applied for the ordinary speaker mask and combined masks containing the speaker bit. The Normal handler's ALSA output is configured for two channels.

Therefore, this callback cannot be treated as a four-independent-channel PCM interface. The external CD3313/amplifier distribution remains a separate hardware question. Do not duplicate stereo into four channels to satisfy the existing quad DSP.

## 5. Policy profiles are not runtime proof

The supplied Android 9 policy declares:
- primary output: PCM16 stereo, 44.1/48 kHz;
- primary input: PCM16 mono/stereo, 8/16/32/44.1/48 kHz;
- Bluetooth SCO output: 8/16 kHz;
- primary routes including speaker, wired headset/headphones, AUX digital, BT SCO, dock and FM output;
- input routes including built-in/headset mic, BT SCO mic, AUX digital, FM tuner and voice-call input.

These are policy declarations. They do not prove the actual PCM endpoint, active route, callback buffer stride, or physical wiring.

## 6. ABI still to recover before implementation

The internal MTK symbols above do not replace the Android HAL module contract. The following must still be recovered from the exact target binary and matching Android 9 headers/source:

- exported `HAL_MODULE_INFO_SYM` and module/device open entry points;
- exact `audio_hw_device`, `audio_stream_out` and `audio_stream_in` structure layout and method coverage;
- exported symbol table, SONAME, DT_NEEDED and linker namespace requirements;
- stream negotiation, standby, drain, flush, presentation position and error behavior;
- patch/device connection and input/output lifecycle;
- SCO/HFP, A2DP, FM, capture, voice-call, HDMI and offload dispatch;
- vendor HIDL service and client compatibility;
- actual Normal-handler input format, frame count, channel stride and manager lifecycle.

Do not infer the Android HAL ABI from the MTK filter symbols. The first replacement module must retain the factory basename and path and be built only after the complete module contract is frozen.

## 7. Integration decision

The Normal speaker callback is a potential stereo processing point only after its buffer contract and lifecycle are recovered. It is not a universal DSP hook. The unified replacement HAL must own device and stream state; AudioCore remains an internal component. Preserve factory routing and telephony paths, and keep unsupported formats/routes bit-transparent bypasses.

No binary patch, factory replacement, CD3313 command or device test was performed in this workstream.
