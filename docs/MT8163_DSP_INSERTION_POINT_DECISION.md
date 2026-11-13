# MT8163 DSP insertion-point decision

## Purpose

This note converts the recovered Normal playback disassembly into an explicit
integration decision. It distinguishes a usable mono processing boundary from
a stereo-preserving boundary and prevents the existing four-channel test
engine from being attached to an incompatible HAL callback.

Source identity for every HAL conclusion below:

- ELF Build ID: `f66eaf9e6ac950259f0c253be618d15a`
- HAL SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`

Static analysis only. No HAL patch, device write, or runtime validation was
performed.

## 1. The existing MTK process callback is downstream of the speaker fold

The Normal playback write order is:

```text
doDcRemoval
  -> doStereoToMonoConversionIfNeed
  -> doPostProcessing
  -> doBliSrc
  -> doBitConversion
  -> pcmWrite
```

The mono helper checks the output-device mask at
`stream_attribute_t + 0x18`. When speaker bit `0x02` is present, it averages
each adjacent L/R pair and writes the average into both sample positions.
This applies to speaker-only mask `0x02` and combined masks `0x06` and
`0x0A`.

Consequently, on speaker-containing Normal routes the existing
`doPostProcessing()` / `AudioMTKFilterManager::process()` callback receives
a duplicated mono signal, not the original stereo image. A stereo DSP placed
at this callback cannot restore the original L/R separation.

The helper recognizes sample-format discriminator 1 as signed PCM16 and 3
as signed PCM32 for this averaging operation. This is not a complete format
contract for every stream or handler.

## 2. Route-specific behavior matters

The filter manager selects its speaker slot when device mask bit `0x02` is
set. For headphone bits `0x0C`, its optional filter slots at manager offsets
`+0x18` and `+0x1C` are null in the recovered default construction path;
that branch therefore returns a zero-byte bypass. Earpiece processing is
feature-flag dependent. Other masks bypass.

The Normal handler also bypasses the manager when either:
- `stream_attribute_t + 0x120` is nonzero; this is populated from
  `AudioSpeechEnhanceInfo::IsAPDMNRTuningEnable()`;
- `stream_attribute_t + 0x41` is nonzero; it includes a manager-owned flag
  and is forced on for sample rates >= 48,001 Hz.

The input byte count must also fit the 128 KiB scratch capacity. These are
additional gates, not a universal guarantee that all music streams reach the
callback.

## 3. Two valid product choices — do not conflate them

### Option A: stereo-preserving media DSP

If the desired sound processing must retain the original left/right image
before the speaker fold, the DSP must run upstream of
`doStereoToMonoConversionIfNeed()`, or at another independently proven
stereo-preserving boundary.

The existing legacy stereo PCM16 core is the relevant starting point for
this option. The four-channel Float32 engine is not a substitute: the
recovered Normal handler is a two-channel path, and the CD3313 is a stereo
analog-input / four-analog-output processor, not a four-channel PCM input.

The code-level Normal-path boundary is now identified: immediately after
`doDcRemoval()` and before `doStereoToMonoConversionIfNeed()`. The fold helper
checks speaker-device bit `0x02` in `stream_attribute_t + 0x18`; for PCM16 and
PCM32 it averages adjacent L/R samples and duplicates the result. This is a
confirmed stereo-preserving location in the factory write order, but it is
not yet an approved production hook. Before implementation, recover the exact
producer buffer contract, all route/format coverage, write-block sizes,
lifecycle, ownership and fail-open behavior. An AudioFlinger effect or a HAL
patch must not be selected merely because it is a familiar integration mechanism.

### Option B: mono speaker-path enhancement

If processing after the speaker fold is acceptable, the existing
`doPostProcessing()` stage is a candidate. It can select a scratch output
buffer and return a produced byte count before BLI SRC and bit conversion.

This option must be explicitly described as mono-domain processing on
speaker-containing routes. It must not be advertised as stereo enhancement,
independent front/rear PEQ, or four-channel PCM processing.

The exact callback input format, channel count, stride, block size, filter
manager lifecycle, and runtime gates still need confirmation before adding an
adapter.

## 4. Non-negotiable adapter contract

Whichever option is selected, a production adapter must:

1. Preserve the stock HAL route/device lifecycle and PCM write path.
2. Default to bit-transparent bypass.
3. Process only explicitly supported format/rate/channel combinations.
4. Preserve the original input pointer and byte count on bypass or failure.
5. Use preallocated bounded scratch; no heap allocation, mutex, logging, or
   blocking operation in the real-time callback.
6. Publish immutable parameter snapshots from a control thread.
7. Bypass calls, SCO, microphone/capture, FM, HDMI, and unknown handlers
   until each route is separately mapped.
8. Keep CD3313 control operations out of the PCM callback.
9. Provide a hard disable/recovery path independent of replacing the vendor
   HAL.

## 5. Decision for the current project

- Keep the four-channel Float32 engine as an offline/test foundation until a
  real four-channel PCM boundary is proven.
- Keep the legacy stereo PCM16 core as the candidate for stereo-preserving
  media processing.
- A host-side `NormalPlaybackPcmAdapter` scaffold now exercises the
  pre-fold boundary contract. It is PCM16 stereo only, processes only an
  explicitly supplied PrimaryMedia/Normal context, and bit-exact bypasses
  unsupported routes, formats and modes. It is not connected to Android.
- Do not connect either engine to the existing MTK post-processing callback
  yet: speaker routes have already been folded to mono, and the remaining
  buffer contract is incomplete.
- Continue static reverse engineering of the upstream Normal write buffer
  producer and stream lifecycle around the newly identified post-DC/pre-fold
  boundary. Then build a host-side adapter harness against the recovered
  contract before any Android integration.
- Treat the CD3313's FL/FR/RL/RR attenuation as four analog output-level
  controls, separate from digital PCM channel topology.

## Related evidence

- [MT8163 MTK process call-site audit](MT8163_MTK_PROCESS_CALLSITE_AUDIT.md)
- [Android audio integration gate](ANDROID_AUDIO_INTEGRATION_GATE.md)
- [Normal speaker PCM format and mono-fold audit](MT8163_NORMAL_SPEAKER_PCM_FORMAT.md)
- [MTK filter-manager lifecycle](MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md)
- [CD3313 register image and datasheet correlation](CD3313_REGISTER_IMAGE.md)
