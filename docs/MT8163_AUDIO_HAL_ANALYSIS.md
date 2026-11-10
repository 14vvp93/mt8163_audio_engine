# MT8163 Audio HAL reverse-engineering findings

## Source binary

- File: `files/vendor/audio.primary.mt8163.so`
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- Format: ELF32 ARM EABI5, Android 28 build target
- Build ID: `f66eaf9e6ac950259f0c253be618d15a`
- Symbols are retained in the dynamic symbol table sufficiently to identify key MTK filter functions.

This document describes static findings from the supplied binary and XML configuration. It is not a runtime validation on the head unit.

## 1. MTK filter manager: confirmed architecture

The HAL contains these exported functions:

- `android::AudioMTKFilterManager::setParameter(unsigned int, void*)` at `0x9e754`
- `android::AudioMTKFilterManager::setSpkFilterParam(unsigned int, unsigned int, int)` at `0x9e864`
- `android::AudioMTKFilter::setFilterParam(unsigned int, unsigned int, int)` at `0x9d66c`
- `android::AudioMTKFilter::init()` at `0x9cfe0`
- `android::AudioALSAStreamManager::UpdateACFHCF(int)` at `0x7f18c`
- `SetACFPreviewParameter(void*, int)` at `0x7f330`
- `SetHCFPreviewParameter(void*, int)` at `0x7f3a8`

### Manager parameter selectors

The switch in `AudioMTKFilterManager::setParameter()` accepts selectors 0 through 4. The code dispatches to manager object members at offsets +0x14, +0x18, +0x1c, and +0x20; selector 4 reuses the first filter slot but calls `setParameter2Sub()`.

| Selector | Observed target | Confidence |
|---|---|---|
| 0 | Filter slot at manager +0x14, calls `AudioMTKFilter::setParameter()` | High |
| 1 | Filter slot at manager +0x18, calls `AudioMTKFilter::setParameter()` | High |
| 2 | Filter slot at manager +0x1c, calls `AudioMTKFilter::setParameter()` | High |
| 3 | Filter slot at manager +0x20, calls `AudioMTKFilter::setParameter()` | High |
| 4 | Reuses manager +0x14 and calls `AudioMTKFilter::setParameter2Sub()` | High |

The exact semantic names of every slot should not be hard-coded into our API until the constructor call sites and customer parameter structures are fully mapped. The HAL log strings explicitly mention “Update ACF”, “Update HCF”, and “Update ACFSub”, and the update routine invokes selectors 0, 1, and 2 respectively; this is strong evidence for the first three mappings.

### Filter backend

`AudioMTKFilter::init()` calls the factory `newMtkAudioLoud(unsigned int)`, stores the returned `MtkAudioLoudBase*`, and configures it through virtual methods. The filter is therefore a wrapper around a proprietary MTK audio-processing backend, not a self-contained generic AOSP effect implementation.

`AudioMTKFilter::setFilterParam()` locks a mutex and calls a virtual method at vtable offset 0x30 with three integer arguments. The manager's `setSpkFilterParam()` forwards the same three values to this method. This is the existing MTK speaker-filter parameter path that should be preserved when integrating our engine.

## 2. ACF/HCF update and preview paths

`AudioALSAStreamManager::UpdateACFHCF(int)` iterates over all registered output filter instances (count at object offset +0xb0, pointer array at +0xac). Based on its argument it selects one of three manager selectors:

- 0: first filter chain, associated with ACF by the update log strings;
- 1: second filter chain, associated with HCF;
- 2: third filter chain, associated with ACFSub.

For each instance it prepares a parameter block and calls `AudioMTKFilterManager::setParameter()`. The function also checks a global generation/state value before returning, indicating that the update path participates in synchronized parameter replacement.

The preview functions behave similarly:
- `SetACFPreviewParameter()` broadcasts the supplied opaque parameter block to selector 0 for every registered output filter.
- `SetHCFPreviewParameter()` broadcasts it to selector 1.

The two preview functions do not perform PCM sample processing themselves; they distribute parameter blocks to the MTK filter backend.

## 3. Audio policy constraints from the supplied XML

The vendor primary audio policy declares:
- primary output: PCM 16-bit, stereo, 44.1 kHz and 48 kHz;
- primary input: PCM 16-bit, mono/stereo, 8/16/32/44.1/48 kHz;
- Bluetooth SCO output: 8 kHz and 16 kHz;
- speaker as the default output device;
- primary output routes to speaker, wired headset/headphones, AUX digital, BT SCO, dock and FM output;
- primary input routes from built-in microphones, wired headset mic, BT SCO mic, AUX digital, FM tuner and voice-call input.

These are declared profiles/routes, not proof that every route is active or supported by every physical head unit configuration. Our DSP must negotiate the actual stream format and must not assume all streams are 48 kHz stereo.

## 4. AOSP effects configuration

The supplied `audio_effects.xml` registers standard AOSP effect libraries:
- bundle (BassBoost, Virtualizer, Equalizer, Volume);
- reverb;
- visualizer;
- downmix;
- loudness enhancer;
- dynamics processing.

This file does not register a custom effect library for our future DSP. We should not add a placeholder library or UUID now. The correct integration mechanism must be chosen after confirming the Android 9 AudioFlinger effect-chain and HAL path on this build.

## 5. CD3313 coupling: what is and is not proven

The inspected `audio.primary.mt8163.so` contains many ioctl call sites for other hardware functions, but the binary's string table does not contain `/dev/audiomux` or `cd3313`. Static evidence therefore does **not** establish that this primary HAL directly opens the CD3313 misc device.

Do not wire the CD3313 protocol emulator into this HAL based on assumptions. The CD3313 driver may be controlled by a separate vendor service, an application, another library, or a different HAL component. The next investigation should inspect `audiocmdservice_atci`, its init service definition, other vendor executables/libraries, and all available references to `/dev/audiomux`.

## 6. Recommended architecture for our project

Preserve the stock MTK HAL as the hardware transport, routing, and call-path owner. Build our own DSP core as a separate, testable C++ library with no dependency on the proprietary MTK filter implementation.

Recommended layers:

1. **PCM DSP core** — format-aware processing for supported PCM streams, with explicit bypass and deterministic latency.
2. **Android integration adapter** — attach only at a proven output processing point; preserve stream/device routing and bypass call/SCO paths initially.
3. **CD3313 control service/library** — independent protocol and state model; connect only after locating the real userspace caller and confirming the ioctl ABI.
4. **Control API/UI** — expose EQ, tone, loudness and routing controls without directly manipulating kernel registers from the UI.

Initial DSP scope should target primary music/media output at PCM16 stereo 44.1/48 kHz. Navigation and notifications can be added after stream-type handling is proven. Bluetooth SCO, voice calls, microphone capture, FM, USB and HDMI should remain untouched/bypassed until their paths are explicitly mapped.

This is safer than replacing `audio.primary.mt8163.so` or injecting a global effect immediately. It reduces the risk of breaking boot audio, telephony, microphone paths, or device routing.

## 7. Next reverse-engineering tasks

1. Inspect `audiocmdservice_atci` and its `.rc` file for a possible CD3313 userspace control path.
2. Search every supplied executable, library and init script for `audiomux`, ioctl constants, and the command IDs already recovered from `cd3313.ko`.
3. Decode `AudioMTKFilterManager` constructor call sites to label all five filter slots accurately.
4. Recover the parameter block layouts passed to ACF/HCF preview/update functions.
5. Map AudioFlinger effect-chain and HAL processing call sites before implementing any hook or replacement.
