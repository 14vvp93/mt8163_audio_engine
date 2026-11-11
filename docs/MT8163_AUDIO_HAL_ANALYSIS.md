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
| +0x00 | Written by AudioALSAStreamOut::set() from the requested audio format; compared with values 1 and 3 by the mono helper | audio_format_t field. Values 1 and 3 are PCM16 and PCM32 in the helper's sample arithmetic. |
| +0x08 | Written by AudioALSAStreamOut::set() from the requested channel mask | audio_channel_mask_t field. |\n| +0x0C | Written by AudioALSAStreamOut::set() from the output-flags argument; bit 2 is tested in Normal handler open() | audio_output_flags_t field. The exact semantic name of the tested bit is not assigned here. |
| +0x18 | Written by AudioALSAStreamOut::set() from the output-device argument; bit 1 tested by doStereoToMonoConversionIfNeed() | audio_devices_t output-device mask. The AUDIO_DEVICE_OUT_SPEAKER bit (0x02) triggers the adjacent-pair average/duplicate operation. |
| +0x24 | Written by AudioALSAStreamOut::set() from the population count of the accepted channel mask | Channel-count field. |\n| +0x28 | Written by AudioALSAStreamOut::set() from the accepted sample rate; compared against 48,001 in createPlaybackHandler() | Sample-rate field; +0x41 is forced on for values >= 48,001 Hz. |
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


### Semantic recovery of manager flags copied into stream attributes

The manager byte at +0xD6 is no longer semantically unknown. The exported method AudioALSAStreamManager::setBypassDLProcess(bool) at 0x806A4 stores its boolean argument directly into manager +0xD6. createPlaybackHandler() copies that byte into stream_attribute_t +0x41, and doPostProcessing() bypasses the MTK filter manager when +0x41 is nonzero. Thus this bypass is explicitly the manager's Downlink-processing bypass flag, with an additional independent high-rate override at >=48,001 Hz.

The adjacent manager flags are also named by their setters:
- AudioALSAStreamManager::setBGSDlMute(bool) at 0x7B47C stores the inverse of its argument at manager +0xD7. createPlaybackHandler() copies it to stream_attribute_t +0x160.
- AudioALSAStreamManager::setBGSUlMute(bool) at 0x7B4D4 stores the inverse of its argument at manager +0xD8. createPlaybackHandler() copies it to stream_attribute_t +0x161.

These two mute flags are copied into the stream attributes but are not directly tested by doPostProcessing()'s two bypass checks. Do not conflate them with the Downlink-processing bypass at +0x41. The boolean inversion in the setters is directly visible in the XOR/RSB sequence; the names above follow the exported method names.


## 9. AudioALSAStreamOut configuration gate before stream creation

The public HAL wrapper path is now connected to the stream manager:

- `AudioALSAHardware::openOutputStream()` at `0xCAC58` forwards the output configuration arguments to the stream manager through its interface call.
- The corresponding flagged overload, `AudioALSAHardware::openOutputStreamWithFlags()` at `0xD1244`, forwards the flags and the same configuration values through that manager interface.
- `AudioALSAStreamOut::set()` at `0x8172C` receives pointers to the requested format, channel mask and sample rate, plus the output device and status/configuration arguments. It stores the accepted format at stream object +0x48, channel mask at +0x50, output device at +0x60, channel count at +0x6C, and accepted sample rate at +0x70.

### Format and channel-mask validation

The format validation tests the requested format value against a bit mask and accepts the values represented by bits 1, 3 and 4 in that test. The function stores the requested value in the stream object only on the accepted path. This is consistent with the HAL's PCM16/PCM32-related handling seen in the playback helper, but the complete enum mapping should still be confirmed from the exact Android audio-format definitions used by this build.

For the ordinary output-device path, the channel-mask check accepts the stereo mask value 3 and computes its channel count using a population-count sequence. A special path for output device value 1024 accepts masks 0x63F or 0x3F and also computes the channel count from the mask. Do not interpret that special device value as the primary speaker route without separately resolving the Android device constant and actual route.

### Sample-rate whitelist and fallback

The stream setter compares the requested sample rate against an explicit list. The observed accepted values are:

- 8,000; 11,025; 12,000 Hz;
- 22,050; 24,000; 32,000 Hz;
- 44,100; 48,000; 88,200; 96,000 Hz;
- 176,400; 192,000 Hz.

If the requested rate is not in this list, the code writes 44,100 Hz back through the sample-rate pointer and records an invalid-argument status. This is the stream-wrapper validation/fallback behavior; it is not proof that the downstream ALSA PCM device runs at that same rate, because the Normal playback handler may subsequently select a target rate and BLI SRC may convert it.

### What this establishes—and what remains open

We now have a confirmed configuration chain from the HAL output-stream wrapper into the stream manager, and a confirmed record of the requested/accepted format, channel mask, output device, channel count and sample rate in the stream object. This narrows the path to inspect when reconstructing `stream_attribute_t`.

It does **not** yet prove the exact assignments from these `AudioALSAStreamOut` fields into every `stream_attribute_t` member, the final PCM layout at the MTK filter-manager input, or the route-specific handler selected at runtime. The next step is to trace the interface object and its stream-attribute construction/copy routine, then correlate that with the Normal handler's actual target-rate and period calculations. No HAL binary, routing, or runtime behavior was changed.


### Direct stream-manager to AudioALSAStreamOut::set() handoff

The `AudioALSAStreamManager::openOutputStream()` implementation at `0x77E30` allocates and initializes a stream object, obtains its virtual table, and calls the function pointer at vtable offset `+0x5C`. The call arguments are assembled from the output device, dereferenced format/channel-mask/sample-rate/status values, and output flags. Their order and types match the exported `AudioALSAStreamOut::set(unsigned int, int*, unsigned int*, unsigned int*, int*, unsigned int)` entry at `0x8172C`.

This connects the public `AudioALSAHardware::openOutputStream()` wrapper to the concrete stream setter:

`AudioALSAHardware::openOutputStream()` → `AudioALSAStreamManager::openOutputStream()` → virtual stream setup at vtable +0x5C → `AudioALSAStreamOut::set()`.

The flagged hardware overload follows the same manager path while forwarding the output flags. This is an ABI-level correspondence from the disassembly; the virtual call itself remains indirect in the machine code.

The setter's fields at +0x48, +0x50, +0x60, +0x6C and +0x70 therefore belong to the concrete stream object created by the manager, rather than being unrelated configuration values. The next unresolved edge is the transfer from this stream object into the per-playback `stream_attribute_t` passed to `createPlaybackHandler()`. We should follow the stream-out interface/open lifecycle and any attribute-copy helper before assigning meanings to additional offsets.


## 10. Recovered stream_attribute_t origin, field layout and handler selection

### stream_attribute_t is embedded in AudioALSAStreamOut

The call at `AudioALSAStreamOut::open()` (`0x83F44–0x83F58`) is now resolved through the HAL's PLT relocation table:

- `r0 = [AudioALSAStreamOut + 0x04]` supplies the `AudioALSAStreamManager*`;
- `r1 = AudioALSAStreamOut + 0x48` supplies the `stream_attribute_t*`;
- the call at `0x83F48` resolves to `AudioALSAStreamManager::createPlaybackHandler(stream_attribute_t*)` at `0x79368`;
- the returned playback-handler pointer is stored at stream object +0x08, then its virtual `open()` method is invoked.

Therefore the stream attributes are embedded directly at offset +0x48 in the `AudioALSAStreamOut` object. This is not an independently allocated attribute block. The earlier `AudioALSAStreamOut::set()` stores are writes into this embedded structure.

### Confirmed field mapping from set() to stream_attribute_t

The base address passed to `createPlaybackHandler()` is `AudioALSAStreamOut + 0x48`. Combining that with the stores in `AudioALSAStreamOut::set()` at `0x8172C` gives the following field map:

| stream_attribute_t offset | Source in AudioALSAStreamOut::set() | Recovered meaning |
|---:|---:|---|
| +0x00 | object +0x48 | Requested/accepted `audio_format_t` |
| +0x08 | object +0x50 | Requested/accepted `audio_channel_mask_t` |
| +0x0C | object +0x54 | `audio_output_flags_t` passed as the final setter argument |
| +0x18 | object +0x60 | Output `audio_devices_t` mask |
| +0x24 | object +0x6C | Channel count calculated from the accepted channel mask |
| +0x28 | object +0x70 | Accepted sample rate in Hz |

The format values 1 and 3 are handled as 16-bit and 32-bit samples respectively by the Normal handler's mono helper. The sample-rate field is used by the already documented >=48,001 Hz post-processing bypass rule. The output-device field explains the mono-conversion gate: bit 0x02 is the speaker device bit, and the helper averages adjacent stereo samples and duplicates the result when that bit is present.

This field map is based on direct stores and the exact pointer passed to `createPlaybackHandler()`; it is substantially stronger than inferring fields from offsets used in isolated helper functions. It does not yet describe every member of the full `stream_attribute_t` structure.

### Playback-handler selection is route-aware

The beginning of `AudioALSAStreamManager::createPlaybackHandler()` shows that handler selection is not one universal Normal handler:

- It first checks the HFP controller. When the controller is active and the top byte at attribute +0x03 is zero, it constructs `AudioALSAPlaybackHandlerHfpClient`; otherwise the corresponding Bluetooth route can construct `AudioALSAPlaybackHandlerBTSCO`.
- The manager's `audio_mode_t` field at +0x9C equal to 2 (`AUDIO_MODE_IN_CALL`), or the manager flag at +0xA1, selects `AudioALSAPlaybackHandlerVoice`.
- Otherwise it switches on the output-device mask at attribute +0x18. Common device masks including 1 and 2 take the Normal-handler constructor path.
- The special output-device value 1024 (0x400) selects `AudioALSAPlaybackHandlerHDMI`.
- The special output-device value 1,048,576 (0x100000) selects `AudioALSAPlaybackHandlerFMTransmitter`.

The constructor targets were resolved from the HAL's PLT relocation entries, rather than guessed from allocation sizes. The exact semantic name of manager state +0x9C and flag +0xA1 still needs an independent setter/call-site trace, so the selection conditions are reported as observed numeric/state conditions.

### Integration consequences

We can now build a format-aware adapter contract around actual per-stream attributes instead of guessed defaults:

1. Read format at +0x00, channel mask at +0x08, output flags at +0x0C, output-device mask at +0x18, channel count at +0x24 and sample rate at +0x28.
2. Use the route-selected handler as a hard boundary: Normal, Voice, BT SCO/HFP, HDMI and FM transmitter are separate paths. Do not treat the Normal handler's post-processing call as universal coverage.
3. Preserve the speaker-specific mono conversion before the MTK processing stage.
4. Keep the >=48,001 Hz and A-PDM tuning bypasses intact.
5. Do not modify the HAL binary until the remaining stream lifecycle, final PCM buffer representation, and filter-manager lifetime are mapped.

No binary patch, code modification to the HAL, or runtime route change has been made.


## 11. Normal-handler target PCM rate selection

The Normal playback handler does not necessarily open ALSA at the sample rate stored in `stream_attribute_t +0x28`.

At the start of `AudioALSAPlaybackHandlerNormal::open()`, the HAL obtains the base primary-output rate from:

`AudioALSASampleRateController::getInstance()` → `getPrimaryStreamOutSampleRate()`.

The getter returns the controller's stored primary-output rate (controller object +0x04). The Normal handler then applies the same decision logic exposed by `AudioALSAPlaybackHandlerNormal::ChooseTargetSampleRate(unsigned int, unsigned int)`, using the controller rate and the stream's output-device mask. The selected value is stored in playback-handler +0x38 and is used in subsequent period/buffer calculations.

The recovered decision table is:

| Controller rate | Exact output-device mask | Selected Normal-handler target rate |
|---:|---:|---:|
| 48,001–96,000 Hz | Any mask | Keep controller rate |
| 96,001–192,000 Hz | 0x04 or 0x08 | 192,000 Hz |
| 96,001–192,000 Hz | Any other mask | 44,100 Hz |
| Below 48,001 Hz or above 192,000 Hz | Any mask | 44,100 Hz |

The 0x04/0x08 branch uses exact equality comparisons, not a bitwise inclusion test. The adjacent `DeviceSupportHifi(unsigned int)` helper independently returns true only for values 4 and 8, corroborating that these two exact device values are treated as the Hifi routes by this handler.

This is the Normal handler's target-rate decision, not a universal rule for Voice, BT SCO/HFP, HDMI or FM transmitter handlers. It also does not replace the earlier `AudioALSAStreamOut::set()` whitelist: that whitelist validates the requested stream configuration, while this later stage chooses the Normal handler's downstream PCM target rate. BLI SRC still sits after MTK post-processing in the write pipeline.

### Practical consequence for our DSP adapter

The adapter must not infer the PCM sample rate at `AudioMTKFilterManager::process()` solely from the framework-requested rate in `stream_attribute_t +0x28`. It must use the actual handler-side rate and format after the Normal handler has configured its PCM path, and must preserve the later BLI SRC/bit-conversion order. The current evidence narrows the likely target rate for Normal playback but does not yet prove the exact sample representation, channel interleaving, or bytes-per-frame at the MTK filter input for every route.

No HAL binary or runtime configuration was modified.


## 12. Source PCM domain at the MTK filter-manager boundary

The Normal handler's initialization routines make the processing domains explicit:

### BLI SRC compares source attributes with the handler's target

`AudioALSAPlaybackHandlerBase::initBliSrc()` at `0xD2958` reads:

- source sample rate from `stream_attribute_t +0x28`;
- source channel count from `stream_attribute_t +0x24`;
- source format discriminator from `stream_attribute_t +0x00`;
- target sample rate from playback-handler +0x38;
- target channel count from playback-handler +0x34.

If source rate and channel count already equal the handler's target values, it does not create a BLI SRC instance. Otherwise it initializes the SRC using both source and target rate/channel values. The function explicitly branches for source format values 1 and 3 before creating the SRC, consistent with the PCM16/PCM32 handling already observed in the Normal write path.

For the Normal handler, open() sets target channel count at handler +0x34 to 2 and selects the target sample rate at +0x38. Therefore the source stream rate/channel count and downstream PCM target rate/channel count are separate values.

### Bit conversion compares source format with the handler's PCM format

`AudioALSAPlaybackHandlerBase::initBitConverter()` at `0xD2D94` compares `stream_attribute_t +0x00` with playback-handler +0x10. If they differ, it creates a bit-conversion instance using the source and target formats, source channel count and source sample rate. The Normal handler initializes its target format field +0x10 to value 4, then `transferAudioFormatToPcmFormat()` maps that format code for the ALSA PCM configuration.

The Normal write pipeline calls `doBitConversion()` only after `doPostProcessing()` and `doBliSrc()`. This confirms that MTK filter-manager processing is upstream of both sample-rate conversion and the source-to-target bit-format conversion.

### What the DSP sees at doPostProcessing()

Combining the confirmed call order and conversion setup, the Normal handler's MTK filter-manager input is in the source stream's PCM domain:

- sample representation is selected by `stream_attribute_t +0x00` (observed PCM16 value 1 and PCM32 value 3);
- source sample rate is `stream_attribute_t +0x28`, not automatically the handler's target rate at +0x38;
- the Normal handler's target output channel count is 2;
- the speaker-device mono helper may replace each adjacent stereo pair with its average duplicated into both positions, but it does not change the byte count or buffer's two-position layout;
- BLI SRC and bit conversion occur after the MTK filter-manager stage.

This is now a concrete adapter contract for the Normal path: process the source PCM format/rate before the HAL's own SRC and bit conversion, and preserve the existing two-channel buffer layout. The DSP must not assume that the MTK filter input is already at the ALSA target rate or target PCM format. It also must not assume that this contract applies to Voice, BT SCO/HFP, HDMI, FM transmitter or capture handlers.

### Remaining proof before any integration patch

The call chain now establishes the source-vs-target rate and format boundary for Normal playback. Still to be confirmed are the exact PCM interleaving/stride at every supported source format, the complete meaning of all `stream_attribute_t` members, and the runtime handler/device selected on the user's head unit. We should next inspect the concrete PCM configuration and write buffer-size arithmetic, then compare it against the tinyalsa PCM configuration ABI. No HAL patch or runtime route change has been made.


## 13. Normal handler tinyalsa PCM configuration

The Normal handler's `pcm_open()` call is reached through `AudioALSAPlaybackHandlerBase::openPcmDriver()` and `openPcmDriverWithFlag()`. The latter passes a pointer to handler +0x190 as the fourth argument to `pcm_open(card, device, flags, config)`. The stores in Normal::open() establish the leading fields of this PCM configuration block.

### Recovered PCM configuration fields

| Config offset (relative to handler +0x190) | Source/store | Meaning |
|---:|---|---|
| +0x00 | handler +0x34 | ALSA target channel count; Normal handler sets it to 2 |
| +0x04 | handler +0x38 | Selected ALSA target sample rate |
| +0x08 | handler +0x198 | Period size calculated from PCM hardware parameters |
| +0x0C | handler +0x19C | Period count; default path sets 2, special path sets 4 |
| +0x10 | result of transferAudioFormatToPcmFormat(handler +0x10) | Tinyalsa PCM format enum derived from handler target format code 4 |
| +0x14 | period_size × period_count, except a special device-index case sets 0 | Start threshold |
| +0x18 | 0 | Stop threshold |
| +0x1C | 0 | Silence threshold |

The pointer passed to `pcm_open()` is exactly handler +0x190, so the stores at handler +0x190 through +0x1AC form the PCM configuration passed to tinyalsa. The mapping above follows the standard tinyalsa `pcm_config` field order; the code's start-threshold special case is retained rather than normalized away.

### Period selection

Normal::open() queries the selected PCM device using `pcm_params_get(card, device, 0)`, then calls `pcm_params_get_max()` with parameter ID 13 and stores the returned maximum period value for its period calculation.

- With output-flags bit 0x04 clear, the Normal handler selects period_count = 2 and derives period_size from the queried maximum and its channel/format scaling.
- With output-flags bit 0x04 set, it enters a special branch. That branch requires manager mode 3 or the manager flag at +0xA1, then sets period_count = 4 and calculates period_size from the stream buffer attributes and target channel/format parameters. Otherwise it follows an error/bypass branch.
- The exact public semantic name of output flag bit 0x04 is not assigned in this document; the machine code proves the bit test and branch behavior.

The handler then calls `SetLowJitterMode(true, target_rate)` and opens the selected PCM device with the configuration at +0x190. The selected target rate and target format are therefore hardware-facing settings, distinct from the source stream attributes consumed by the MTK filter stage.

### Interleaving and adapter boundary

The speaker mono helper reads successive left/right sample positions as adjacent 16-bit or 32-bit values, averages each pair and writes the average back to both positions. This directly confirms an interleaved two-sample pair for that helper's supported formats. The normal handler's target channel count is 2, and the SRC setup compares source and target channel counts before deciding whether conversion is needed.

For our Normal-path adapter, the safe current contract is:
- consume the source buffer before SRC/bit conversion;
- honor source format at attribute +0x00 and source rate at +0x28;
- preserve the two-channel interleaved layout expected by the Normal path;
- accept that the speaker route may already contain duplicated mono samples;
- do not impose the final tinyalsa target format or target rate on the DSP input;
- preserve the HAL's selected PCM config, period parameters, low-jitter setup, and write sequence.

The exact tinyalsa PCM format enum produced for target format code 4, plus every field in the complete PCM configuration and the special period branch, should be checked against the matching tinyalsa headers/source before writing a binary-compatible replacement. No HAL patch or runtime change has been made.
