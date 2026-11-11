# Android audio integration gate: channel topology and real-time contract

## Status

**Integration remains blocked at the PCM boundary.** The DSP core is buildable and host-tested, but the available static evidence does not yet establish that the MTK HAL filter callback carries four-channel PCM in the format required by the current engine.

This document is a safety gate, not a claim that Android integration is complete.

## 1. Verified interfaces

### Android policy side

The supplied Android 9 primary audio policy declares the primary output as PCM16, stereo, at 44.1/48 kHz. It also declares other routes and formats (including SCO, A2DP, USB, FM and voice-call paths). Policy declarations are not proof of the actual buffer layout at every internal HAL stage.

### MTK HAL side

The primary HAL exports:

- `AudioMTKFilterManager::process(void*, unsigned int, void*, unsigned int)`
- `AudioMTKFilterManager::setParameter(unsigned int, void*)`
- `AudioMTKFilter::setFilterParam(unsigned int, unsigned int, int)`
- ACF/HCF update and preview functions.

Focused disassembly now finds one direct call site to the process wrapper in the supplied primary HAL: `AudioALSAPlaybackHandlerNormal::write()` at `0xD4744`. The wrapper passes the filter manager, input pointer and byte count, scratch output pointer and scratch capacity to `AudioMTKFilterManager::process()`; the returned value is output byte count, with zero selecting the original input as a bypass. The call is byte-oriented. Source format is inherited from the stream attributes (format codes 1 and 3 are recognized by the pre-processing helper), source channel count is read from `stream_attribute_t +0x24`, and source sample rate from `+0x28`. The exact per-route source channel count, frame/stride contract, and active filter-manager configuration remain unresolved. The ACF/HCF functions are parameter-update paths; they must not be mistaken for the PCM processing callback. See [MT8163_MTK_PROCESS_CALLSITE_AUDIT.md](MT8163_MTK_PROCESS_CALLSITE_AUDIT.md).

### DSP side

The production core currently exposes:

- `QuadEngine`: interleaved normalized Float32 FL/FR/RL/RR (4 channels).
- `ProfiledQuadEngine`: two preconfigured engines and a crossfade, with one control producer and one audio consumer.
- The audio processing path uses fixed-size scratch buffers, no locks, and no heap allocation.
- `AudioProfile` validation occurs before an entire profile is prepared and published.
- The JNI bridge is an offline/bring-up interface. Java array pinning/copying and synchronized calls make it unsuitable for a real-time HAL callback.

## 2. The unresolved topology mismatch

There is currently a mismatch between the known outer policy and the DSP core:

| Layer | Known representation |
|---|---|
| Android primary output policy | PCM16 stereo, 44.1/48 kHz |
| Candidate MTK filter process function | Opaque pointers and lengths; actual buffer layout unresolved |
| Current four-channel DSP core | Float32 interleaved FL/FR/RL/RR |
| CD3313 control path | Separate device/control ABI; not the PCM stream itself |

Do **not** solve this by blindly duplicating stereo L/R into FL/FR/RL/RR, downmixing the four-channel result back to stereo, or assuming the CD3313 receives a particular bus layout. Any of those could silently remove rear-channel independence, alter gain, or process the wrong stream.

The four-channel requirement can only be connected safely after one of these is demonstrated from the target HAL/bus:

1. The MTK filter boundary really carries four independent channels and its sample format/stride are recovered; or
2. A documented downstream four-channel PCM boundary exists before the CD3313/amplifier; or
3. The architecture is deliberately changed to a stereo DSP stage plus separately validated CD3313 per-speaker controls. This third option is not equivalent to four independent software EQ channels and must be treated as a product decision, not a transparent adapter.

## 3. Required reverse-engineering evidence before coding the HAL adapter

1. The direct call-site inventory is complete for this supplied primary HAL: the only direct call to `doPostProcessing()` is in `AudioALSAPlaybackHandlerNormal::write()`. Verify this against the exact live firmware revision before relying on it.
2. Trace the producer of the Normal handler's input buffer and consumer of its selected output buffer.
3. Recover the per-route source channel count, interleaving/stride, frame count, and exact byte-length relationship. Format codes 1 (16-bit) and 3 (32-bit) are recognized in the mono-fold helper, but that alone is not a complete buffer-format contract.
4. Identify whether the function runs on the primary music path, and whether it is invoked for notifications, FM, USB, HDMI, or other routes.
5. Recover filter-manager construction/registration and the lifetime/locking rules for each filter instance.
6. Confirm whether the output buffer is in-place, separate, or partially overlapping.
7. Establish a bypass condition for SCO, voice calls, microphone capture, and all unsupported formats/routes.
8. Only then implement a thin adapter with explicit format conversion, bounded block sizes, preallocated scratch, no callback locks/allocations/logging, and exact bit-transparent bypass.

## 4. Runtime and failure-safety requirements

The eventual adapter must:

- leave the stock MTK HAL and its routing/device lifecycle intact;
- default to bypass until explicitly enabled;
- reject unsupported formats without modifying the input buffer;
- preserve the original output on DSP failure (or use a proven fail-open copy path);
- never process capture/microphone or call/SCO streams in the first release;
- avoid loading or mutating CD3313 register state from the PCM callback;
- use a control-thread-to-audio-thread immutable profile handoff;
- expose a hard disable/recovery mechanism that does not require replacing the vendor HAL.

No HAL replacement, AudioFlinger hook, global effect registration, or live CD3313 write should be attempted on the strength of the current static evidence alone.

## 5. ProfiledQuadEngine concurrency contract

The current implementation uses a single-control-producer/single-audio-consumer ownership model:

- Call `configure()` before starting audio processing.
- Call `apply_profile()` from one control thread only.
- Call `process()` from exactly one audio thread.
- Do not call `configure()` concurrently with processing.
- Do not destroy the engine while the audio thread can still access it.
- A profile update is rejected while the second engine slot is pending or transitioning.
- Keep processing blocks flowing until a transition completes; otherwise the retired engine slot cannot be reused.

The Java test wrapper serializes its methods and is not a model for the production callback. The production adapter must call the C++ engine directly and implement its own lifecycle-safe control handoff.

## 6. Current decision

Proceed with static call-site and buffer-layout recovery. Do not create a guessed HAL hook or claim four-channel system integration yet. The next implementation should be the smallest adapter that matches a proven native buffer contract, not a speculative replacement for the vendor audio stack.


## 7. Newly correlated CD3313 four-speaker control path

A separate static analysis of the HEQ application and `cd3313.ko` provides an important refinement to the topology model:

- HEQ selects a CD3313 ASP path when the system property `ro.hw.dsp.chip` is `cd3313`.
- Its hidden vendor `android.audio.AudioEffect` API exposes `setBalance(FL, FR, RL, RR)`, bass, treble, loudness, subwoofer, and master-volume methods.
- The kernel driver's command `0x15` reads four 32-bit speaker-level indices in FL/FR/RL/RR order and maps each through a 16-entry attenuation table before writing four distinct register fields.
- The driver's 16-entry speaker table is attenuation control, not evidence of four independent software PEQ channels.
- Commands `0x55` and `0x5C` control bass and treble transition targets; their optimized handler argument semantics are only partially recovered.

This gives us a credible **hardware four-speaker level-control path** to investigate separately from PCM processing. It does not establish that the HAL's `AudioMTKFilterManager::process()` callback carries four-channel PCM.

### Revised integration split

Keep these responsibilities distinct:

1. **PCM DSP:** initially process the proven primary media PCM format at the verified MTK boundary. The current HAL policy indicates PCM16 stereo 44.1/48 kHz, but the callback's actual buffer contract must still be recovered.
2. **CD3313 speaker controls:** expose four-channel attenuation/balance/fader only through a verified vendor framework/native transport. Do not call the kernel ABI directly until the matching framework implementation and exact ioctl encoding are recovered.
3. **Four-channel EQ claim:** do not label the hardware balance/fader path as four independent PEQ. If the physical unit exposes only stereo PCM into the CD3313, independent FL/FR/RL/RR EQ requires a separately demonstrated four-channel PCM boundary or additional DSP hardware/software routing.

The hidden `android.audio.AudioEffect` implementation is not included in the current archive. HEQ's Java call sites identify the high-level API, but not its native transport, argument scaling, ioctl request values, or runtime behavior. Obtain the matching framework JAR and associated native/service libraries before implementing this adapter.

The source evidence and command limitations are detailed in [HEQ_APK_ANALYSIS.md](HEQ_APK_ANALYSIS.md), [CD3313_COMMAND_DISPATCH.md](CD3313_COMMAND_DISPATCH.md), and [CD3313_USERSPACE_CLIENT_SCAN.md](CD3313_USERSPACE_CLIENT_SCAN.md).


## 8. Normal speaker path: mono fold occurs before MTK post-processing

The focused disassembly audit in [MT8163_NORMAL_SPEAKER_PCM_FORMAT.md](MT8163_NORMAL_SPEAKER_PCM_FORMAT.md)
resolves the Normal handler's actual write order:

`doDcRemoval -> doStereoToMonoConversionIfNeed -> doPostProcessing -> doBliSrc -> doBitConversion -> pcmWrite`.

The conversion helper reads the output-device field at
`stream_attribute_t +0x18` and tests bit `0x02`. The Normal handler route
audit confirms that field is passed to `startOutputDevice()` unchanged and
that `0x02` is `AUDIO_DEVICE_OUT_SPEAKER`. When this bit is present, adjacent
stereo samples are folded to one value and duplicated (`L,R -> M,M`) before
`AudioMTKFilterManager::process()`.

This applies to the ordinary speaker mask `0x02` and to combined masks
`0x06` and `0x0A`, because all contain the speaker bit. On these Normal
handler routes the MTK post-processing callback cannot recover the original
left/right difference. The Normal handler's ALSA output is also configured
for two channels.

This is stronger evidence than the framework's stereo policy declaration:
the actual Normal-handler call order and helper operation are statically
recovered. It still does not establish how the two-channel/mono feed is
distributed by the external CD3313 or amplifier.

### Updated gate

- Do not present the existing Normal-handler MTK filter callback as a
  four-independent-channel interface.
- Do not assume that moving the current four-channel engine behind that
  callback preserves stereo or creates rear channels.
- Locate and verify the physical split point downstream (if the CD3313 or
  another DSP derives four outputs), or find a genuinely multichannel PCM
  path before implementing FL/FR/RL/RR integration.
- A stereo-only DSP mode could be considered separately, but it must preserve
  the stock speaker mono-fold behavior and must not be described as four-
  channel independent processing.

## 9. Filter-manager route branches and default headphone bypass

The manager's device mask is stored at `AudioMTKFilterManager +0x2C`.
Its `process()` method branches in this order:
- speaker bit `0x02`: speaker filter slot `+0x14`, then optional feature-3
  slot `+0x20` and VibSpkAddTone slot `+0x24`;
- headphone bits `0x0C`: optional slots `+0x1C` and `+0x18`;
- earpiece bit `0x01`: feature-3/feature-2-gated slot `+0x20` and
  VibSpkAddTone slot `+0x24`;
- other masks: zero-byte bypass.

The complete manager implementation in this supplied HAL does not assign
non-null values to slots `+0x18` or `+0x1C`; the constructor zeroes them and
`deinit()` only clears them. Thus the default headphone branch skips both
optional filters and returns zero (bypass). The speaker filter at `+0x14`
is corroborated by the speaker gain and filter-parameter setters.

This means the stock MTK manager's effective processing is route-specific and
speaker-oriented; the mere fact that the Normal handler calls
`AudioMTKFilterManager::process()` does not mean every route is processed.
See [MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md](MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md).

### Integration consequence

If a future adapter replaces or augments the manager's processing, it must
make an explicit route policy:
- speaker-containing masks arrive after the stock mono fold;
- headphone-only masks retain the stereo pair at this boundary, but the
  stock manager bypasses them in the recovered default initialization;
- earpiece processing depends on feature flags;
- unsupported routes must remain bypassed until their handlers are traced.


## 10. ATCI ACF/HCF service bridge recovered

The supplied `vendor/audiocmdservice_atci` binary contains the
`MM+EACF`/`MM+EHCF` command family and the named
`DoCustACFHCFParam`, `DLCustAudioFLTParamToNV()`, and
`ULCustAudioFLTParamFromNV()` functions. Its five service tuning types map
to NVRAM IDs `[0, 1, 4, 5, 6]` for ACF, HCF, ACF-SUB, Music-DRC and
RingTone-DRC. The payload block is 1260 bytes (`0x4EC`) and is transferred
through Base64; the service writes it with the vendor custom-parameter storage
API and emits `UpdateACFHCFParameters=N` through
`MTKPrimaryDevicesHalClient`.

This cross-correlates with the primary HAL's `UpdateACFHCF(int)`:
- update suffix 0 loads custom-filter type 0 and calls manager selector 0;
- suffix 1 loads type 1 and calls manager selector 1;
- suffix 2 loads type 4 and calls manager selector 4.

Manager selector 4 is valid and targets filter slot `+0x14` through
`AudioMTKFilter::setParameter()`. Selector 1 targets optional slot
`+0x18`, which remains null in the recovered default manager initialization.
The HCF update therefore appears to be skipped in that default construction
path; this needs confirmation against the live configuration or an alternate
initialization path. Service update strings 3 and 4 exist, but this HAL
`UpdateACFHCF()` function only handles suffixes 0..2.

See [AUDIOCMD_ATCI_ACF_HCF_PROTOCOL.md](AUDIOCMD_ATCI_ACF_HCF_PROTOCOL.md).
This recovers a vendor ACF/HCF control route, not a CD3313 transport or
four-channel PCM path.


## 11. ACF parameter payload reaches the BES processor

The cross-binary call chain is now statically connected:

`audiocmdservice_atci`
→ NVRAM custom-filter block (`0x4EC` bytes)
→ `UpdateACFHCFParameters=N`
→ `AudioALSAStreamManager::UpdateACFHCF()`
→ `AudioMTKFilterManager::setParameter()`
→ `AudioMTKFilter::setParameter()` / `setParameter2Sub()`
→ inner `MtkAudioLoud` object
→ BES processing path.

The inner object is created through `newMtkAudioLoud(type)`. The default
speaker filter wrapper at manager slot `+0x14` uses type 0/mode 4. Its
parameter wrapper forwards the custom payload to inner selector 8 for
`setParameter2Sub()` (ACF update suffix 0), or selector 6 for
`setParameter()` (ACF-SUB update suffix 2). HCF suffix 1 targets manager
slot `+0x18`, which remains null in the default manager initialization
recovered from this binary.

This establishes a real vendor BES/MTK processing chain for the ordinary
speaker route; it is not a CD3313 command path. The Normal handler reaches
that chain only after its speaker-bit stereo-to-mono fold. See
[MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md](MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md)
and [AUDIOCMD_ATCI_ACF_HCF_PROTOCOL.md](AUDIOCMD_ATCI_ACF_HCF_PROTOCOL.md).


## 12. CD3313 is stereo-analog-in / four-analog-out, not four-channel PCM

The manufacturer CD3313-AX-BJ-24 datasheet confirms:
- three selectable stereo analog inputs;
- left/right analog processing paths for volume, bass, treble and loudness;
- four independent analog output attenuators/mute paths;
- analog outputs LF, RF, LR and RR on pins 25, 24, 23 and 22;
- independent four-output attenuation/balance/loudness control, but no
  multichannel PCM input interface and no four independent per-output PEQ
  banks.

See [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md), section
“CD3313 signal architecture: stereo analog input, four analog outputs”.

This reconciles the driver command `0x15`: it controls four real, independent
analog speaker output levels. It does **not** represent four independent
digital PCM channels.

### Updated project architecture gate

- Retain FL/FR/RL/RR as four analog output-level controls mapped to the CD3313
  when its userspace API is recovered.
- Treat the digital DSP input as stereo or mono according to the selected
  Normal-handler route. The speaker-containing route is folded to mono before
  the MTK/BES post-processing stage.
- Apply digital PEQ, bass and clarity in the stereo/mono PCM domain before
  the DAC/CD3313 analog input path; do not send a four-channel PCM buffer into
  the existing two-channel Normal handler.
- Do not claim independent front/rear PEQ or per-output delay through CD3313.
  Those require a separate verified multichannel DSP path or additional
  hardware.
- Keep the four-channel engine as an offline/test capability until the actual
  board topology proves a multichannel digital insertion point.

This is a capability boundary from the chip datasheet plus static HAL
disassembly, not a claim that the board's complete analog wiring has been
recovered. The live DAC-to-CD3313 input connection still needs DT/mixer/board
evidence.


## 13. Hidden CD3313 Java API and Binder bridge recovered

The files2 archive's boot-carservices.vdex contains the custom
android.audio.AudioEffect API, ISourceService Binder interface,
SourceService implementation and ExtAudioMuxer wrapper. The recovered route is:

android.audio.AudioEffect
→ ISourceService.doExtAudioEffect(int, int[])
→ SourceService.doExtAudioEffect()
→ native ExtAudioMuxer.SendCmdAudioMux()
→ /dev/audiomux
→ cd3313.ko.

The Java setBalance(FL, FR, RL, RR) method sends command 21 (0x15) with
four integers in that order. This exactly matches the kernel driver's
four-output FL/FR/RL/RR attenuation handler. The Java bass, loudness, treble,
input-gain and subwoofer commands also match the driver's implemented command
IDs. Command 1000 is special: the Binder service writes sys.audio.lr.balance
and does not call the native muxer.

The Java API exposes additional EQ/delay/compressor/crossover commands, but
the recovered CD3313 driver has no write-dispatch cases for those IDs. They
must not be advertised as working CD3313 features on this firmware.

The Java/Binder route is now known. The remaining userspace gap is the native
implementation of ExtAudioMuxer.nativeInit() and SendCmdAudioMux(), which
builds the ioctl request and buffer framing. The low _IOC_NR byte and exact
native request must be recovered from the registering native library before
any live /dev/audiomux client is written.

See [CD3313_HIDDEN_AUDIOEFFECT_API.md](CD3313_HIDDEN_AUDIOEFFECT_API.md)
and [CD3313_API_RECOVERY_REQUIREMENTS.md](CD3313_API_RECOVERY_REQUIREMENTS.md).
