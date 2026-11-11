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

## Related command service

The separate `audiocmdservice_atci` executable is analyzed in [MTK_AUDIO_COMMAND_SERVICE.md](MTK_AUDIO_COMMAND_SERVICE.md). It bridges ATCI tuning commands to MTK's primary-device HAL client and reads/writes structured ACF filter parameters in persistent storage. It does not currently provide evidence of direct CD3313 access. Its three linked vendor libraries are not present in the supplied archive, so the filter parameter ABI remains an open dependency.


## 7. Confirmed playback-handler post-processing call site

The complete disassembly contains one direct call site to AudioALSAPlaybackHandlerBase::doPostProcessing(), from AudioALSAPlaybackHandlerNormal::write() at 0xD4744. The Normal write pipeline is ordered as follows:

1. doDcRemoval() at 0xD4710;
2. doStereoToMonoConversionIfNeed() at 0xD471C;
3. doPostProcessing() at 0xD4744;
4. doBliSrc() at 0xD4768;
5. doBitConversion() at 0xD478C;
6. pcmWrite() at 0xD47D4, after optional dump handling.

This establishes the location of the MTK filter-manager processing stage in the normal playback write path: after DC removal and optional stereo-to-mono conversion, but before BLI sample-rate conversion and bit-depth conversion. The data reaching post-processing is therefore not necessarily the original AudioFlinger buffer; earlier pipeline stages may already have modified or converted it.

### doPostProcessing() gates and buffer contract

AudioALSAPlaybackHandlerBase::doPostProcessing() at 0xD24B0 performs these visible checks:

- the filter-manager pointer at playback-handler offset +0x1E0 must be non-null;
- stream-attribute byte at offset +0x120 must be zero;
- stream-attribute byte at offset +0x41 must be zero;
- the requested input byte count must fit the allocated post-processing capacity at handler offset +0x1E8.

The post-processing initializer sets capacity to 0x20000 (131,072 bytes / 128 KiB) and allocates the corresponding scratch buffer, whose pointer is stored at +0x1E4. This is a maximum scratch-buffer capacity, not the size of every processed block.

When the gates pass, doPostProcessing() calls AudioMTKFilterManager::start(bool), then AudioMTKFilterManager::process() with:

- input pointer and input byte count from the current write pipeline;
- scratch output pointer at handler +0x1E4;
- scratch capacity at handler +0x1E8.

It returns the selected output pointer and output byte count through its pointer arguments. The caller then passes that selected buffer into BLI SRC and bit conversion. If post-processing is skipped, the original pipeline buffer is retained. The two stream-attribute bytes are not assigned semantic names here; their exact meanings must be recovered from the stream-attribute definition/callers before deciding which streams are bypassed.

### Integration implications

This is currently the strongest candidate for a safe DSP integration adapter because it is an existing output-processing stage in the stock playback path and already supports a replaceable output buffer. It is not yet an authorization to patch the HAL: the actual input PCM format, channel layout, stream-type gates, and lifecycle of the filter manager still need to be proven. The call site is in AudioALSAPlaybackHandlerNormal::write(); it must not be assumed to cover every playback handler or every output route.

Preserve the order of the stock pipeline and keep calls, SCO, microphones, and non-normal playback paths bypassed until their handling is independently verified. No HAL binary or runtime route was modified.


### Provenance of the two stream-attribute bypass flags

The stream-attribute preparation in AudioALSAStreamManager::createPlaybackHandler() at 0x79368 explains both byte flags checked by doPostProcessing():

- At 0x79478–0x7947C, the manager calls AudioSpeechEnhanceInfo::IsAPDMNRTuningEnable() and stores the returned byte at stream_attribute_t +0x120. Thus doPostProcessing() bypasses the MTK filter manager when this A-PDM non-real-time tuning flag is enabled.
- At 0x79480–0x7948C, it copies a manager-owned byte from manager +0xD6 into stream_attribute_t +0x41.
- At 0x7949C–0x794A8, it compares the stream sample-rate field at stream_attribute_t +0x28 with 48,001 and forces +0x41 to 1 for rates at or above 48,001 Hz.

Therefore the +0x41 bypass condition covers at least the manager flag and high-rate streams (strictly 48,001 Hz or above in this comparison). The semantic name of the manager flag at +0xD6 is still unknown. These are exact field-flow findings; no assumption is made about all other routing or format constraints.


### Input format and optional mono conversion before post-processing

The Normal playback write pipeline's preceding helper, AudioALSAPlaybackHandlerBase::doStereoToMonoConversionIfNeed() at 0xD20AC, provides additional format evidence:

- It reads stream_attribute_t +0x18 and runs only when bit 1 (mask 0x02) is set.
- It then checks the first 32-bit stream-attribute field. For value 1, it treats the input as signed 16-bit samples, averages each adjacent stereo pair, and writes the averaged sample to both positions.
- For value 3, it performs the same operation on 32-bit samples.
- Other first-field values take the no-conversion path in this helper.
- The helper does not change the byte count; it modifies the buffer in place.

Because this helper runs immediately before doPostProcessing() in AudioALSAPlaybackHandlerNormal::write(), the MTK filter manager can receive either the unchanged input channel data or a mono downmix duplicated into the two channel positions, depending on stream_attribute_t +0x18. The code explicitly supports 16-bit and 32-bit sample handling in this conversion helper. It does not prove the complete format mapping for every stream or every playback handler.

This further reinforces that our integration adapter must inspect the actual stream attributes and support the selected sample representation. It must not hard-code four-channel processing at this HAL point: the normal handler's observed pre-processing is pair-based stereo/mono handling. Four-channel routing remains a separate question for the hardware path and other handlers.


## 8. Refined stream-attribute and post-processing contract

The following details were rechecked directly against the ARM disassembly of the supplied HAL Build ID f66eaf9e6ac950259f0c253be618d15a.

### Stream-attribute fields consumed by this path

| Offset | Directly observed use | What is safe to conclude |
|---:|---|---|
| +0x00 | Compared with values 1 and 3 in the in-place stereo-to-mono helper | Format/type discriminator. Values 1 and 3 select 16-bit and 32-bit sample arithmetic in this helper; do not generalize the complete enum mapping from this one function. |
| +0x0C | Bit 2 tested in Normal handler open() | A flag bit affects a later buffer/period calculation. Its public semantic name is not recovered. |
| +0x18 | Bit 1 tested by doStereoToMonoConversionIfNeed() | A flag controlling whether that helper averages each adjacent stereo pair and duplicates the average. |
| +0x28 | Compared against 48,001 in createPlaybackHandler() | Sample-rate field; +0x41 is forced on for values >= 48,001 Hz. |
| +0x41 | Tested by doPostProcessing() | Nonzero means bypass MTK post-processing. It is copied from manager +0xD6 and forced on for high sample rates. |
| +0x120 | Tested by doPostProcessing() | Nonzero means bypass; createPlaybackHandler() fills it from AudioSpeechEnhanceInfo::IsAPDMNRTuningEnable(). |
| +0x160 / +0x161 | Copied from manager +0xD7 / +0xD8 | Two additional one-byte manager flags; their consumers/semantics are not yet mapped. |
| +0x170 | Set from manager +0xA1 or a derived nonzero condition | Another handler-creation flag; semantic name not yet recovered. |

### Exact Normal::write() ordering

The call sequence at 0xD4710–0xD47D4 is:

1. doDcRemoval(input, bytes, ...)
2. doStereoToMonoConversionIfNeed(input, bytes)
3. doPostProcessing(input, bytes, &selected_buffer, &selected_bytes)
4. doBliSrc(selected_buffer, selected_bytes, ...)
5. doBitConversion(...)
6. pcmWrite(...)

The byte counts returned by the mono conversion and post-processing helpers are carried forward separately. The post-processing result is not automatically assumed to be the original buffer: on a successful nonzero result, the caller switches to the scratch buffer and the produced byte count; otherwise it retains the input pointer and original byte count.

### Post-processing scratch buffer and capacity behavior

initPostProcessing() at 0xD2448 writes exactly 131,072 (0x20000) to handler +0x1E8 and allocates exactly that many bytes, storing the pointer at +0x1E4. This is a per-handler scratch allocation/capacity, not a fixed PCM block size.

doPostProcessing() at 0xD24B0 first checks:
- filter manager at handler +0x1E0 is non-null;
- stream-attribute byte +0x120 is zero;
- stream-attribute byte +0x41 is zero;
- input byte count is no greater than scratch capacity +0x1E8.

If input bytes exceed capacity, it takes a diagnostic/error branch and bypasses processing rather than writing beyond the scratch buffer. Otherwise it calls the manager's start() and process() methods, passing input pointer/count, scratch pointer/capacity, and then returns the selected output pointer and produced byte count. The exact manager method prototypes should be taken from the recovered call ABI, not inferred from their C++ names alone.

### Mono conversion arithmetic

When stream-attribute +0x18 bit 1 is set:
- Format discriminator 1: the helper loads two signed 16-bit samples, halves each with arithmetic shift, adds them, and writes the same result into both 16-bit positions.
- Format discriminator 3: it loads two 32-bit samples, arithmetic-shifts each right by one, adds them, and writes the same result into both 32-bit positions.

This is an in-place stereo-pair average with no change to the input byte count. It does not prove that every normal playback stream is stereo; it proves the helper's operation for streams whose +0x18 flag requests this conversion.

### Integration guardrails

For a future adapter, read the actual per-stream format and flags; validate byte count against the supplied buffer capacity; preserve the stock order relative to DC removal, SRC, and bit conversion; and bypass whenever either +0x120 or +0x41 is set. Do not assume this Normal handler path covers HDMI, FM transmitter, voice, BT SCO, compressed, or capture streams. No binary patch or routing change has been made.


### Filter-manager start/process return ABI

The ARM call sequence inside doPostProcessing() at 0xD2584–0xD25A8 resolves the manager interface more precisely:

- The byte at playback-handler +0x1EC is zero-extended and passed as the second argument to AudioMTKFilterManager::start(bool).
- The subsequent AudioMTKFilterManager::process() call receives the manager pointer, original input pointer, original input byte count, scratch output pointer, and scratch capacity (the fifth argument is passed on the stack).
- The process() return value is tested. A zero return means the wrapper retains the original input pointer and input byte count. A nonzero return means it selects the scratch pointer and uses the returned value as the produced output byte count.
- The wrapper writes the selected pointer and byte count to its caller's output parameters.

This is stronger than merely identifying a replaceable buffer: the manager's process return is the output byte count, while the output storage is supplied by the caller. A compatible adapter must respect the supplied capacity and return a valid produced-byte count; it must not assume the manager returns an output pointer.
