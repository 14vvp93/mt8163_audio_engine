# Android 9 stereo DSP insertion candidate: AudioFlinger output-mix effect

## Status

**Candidate identified; not yet approved for device integration.**

The supplied MT8163 primary HAL performs an optional stereo-to-mono fold before
its only direct MTK post-processing call in
`AudioALSAPlaybackHandlerNormal::write()`. A DSP inserted at that HAL callback
cannot recover the original L/R difference on speaker-containing routes.

Android's AudioFlinger output-mix effect chain is a plausible earlier
insertion point. In the AOSP PlaybackThread implementation, the chain for
`AUDIO_SESSION_OUTPUT_MIX` is ordered after track-specific effect chains and
before `AUDIO_SESSION_OUTPUT_STAGE`; the output-mix chain is processed as a
global chain. Its input/output buffers are the PlaybackThread mix buffers.
This processing occurs in AudioFlinger before the mixed block is written to
the vendor HAL. Therefore, on a compatible mixer output, an output-mix effect
can see the stereo mix before the MT8163 HAL's later speaker mono fold.

AOSP Android 9 audio-effects documentation states that output effect processing
was upgraded to multichannel float in Android 9, that software effects use
stereo float, and that legacy effects are supported through format adapters
which convert float to int16 as needed.

References:
- AOSP AudioFlinger output-mix chain ordering and processing:
  https://android.googlesource.com/platform/frameworks/av/+/725dfe9/services/audioflinger/AudioFlinger.cpp
- AOSP Android 9 output-effect format changes:
  https://source.android.com/docs/core/audio/highres-effects

The AOSP references establish the framework design, not the exact behavior of
this vendor's Android 9 build. The matching source tag/vendor modifications
must still be checked.

## 1. Why this is a better stereo-preserving candidate

The recovered MT8163 Normal path is:

```text
AudioFlinger mixed output
  -> vendor AudioALSAStreamOut / Normal handler
  -> doDcRemoval()
  -> doStereoToMonoConversionIfNeed()
  -> doPostProcessing()        // existing MTK/BES filter path
  -> doBliSrc()
  -> doBitConversion()
  -> pcmWrite()
```

The speaker-bit conversion happens before `doPostProcessing()` and overwrites
each adjacent L/R pair with the average duplicated into both positions.
An AudioFlinger output-mix effect runs on the mixed output before the HAL
receives that block, so it is upstream of this vendor-specific fold.

This is the first credible software insertion candidate found so far that can
preserve stereo for the ordinary speaker route without patching the vendor HAL
write method itself.

## 2. Format implications for the current DSP core

The existing legacy `pcm_dsp::Engine` accepts interleaved signed PCM16 stereo
at 44.1/48 kHz. Android 9 output effects use a float processing path for
software effects, with compatibility adapters available for legacy int16
effects.

Two implementation choices remain:

1. Implement a standard Android effect-library wrapper around the existing
   PCM16 stereo engine and let the framework's legacy-format adapter handle
   float/int16 conversion, if the exact vendor framework supports that path.
2. Add a native Float32 stereo engine/API and use it directly from the effect
   implementation, avoiding an unnecessary float-to-int16-to-float roundtrip.

Do not feed the current four-channel engine by duplicating L/R into
FL/FR/RL/RR. The CD3313 is a stereo analog-input / four analog-output device;
its four speaker controls are not four PCM inputs.

## 3. Preferred attachment: per-stream music post-processing

The AOSP `audio_effects_conf` v2.0 schema defines a `postprocess` section
with output stream types including `music`, `ring`, `alarm`,
`notification` and `voice_call`. Its example binds a named effect to
`<stream type="music">`.

The corresponding AOSP `AudioPolicyEffects::addOutputSessionEffects()`
implementation looks up the effect list by the selected output stream type,
creates the configured effect instance on that audio session/output, and
enables the processor. This is a more precise candidate than attaching one
global output-mix effect: it can target the legacy Music stream without
intentionally attaching the same effect to Ring, Alarm, Notification or
Voice Call streams.

This is still a legacy stream-type boundary. Android 9 automotive usage
mapping and OEM policy may map navigation guidance or other usages to the
Music stream, so the exact target stream classification must be checked on
the device. The current extracted vendor `audio_effects.xml` is documented
as containing stock AOSP libraries/effects; its exact postprocess section and
loaded version must be inspected before adding any entry.

AOSP references:
- Audio effects v2.0 XML schema, including the `postprocess/music` element:
  https://android.googlesource.com/platform/hardware/interfaces/+/b5433136dd099d533e866028367ba4ed829fab02/audio/effect/2.0/xml/audio_effects_conf_V2_0.xsd
- AudioPolicyEffects output-session attachment:
  https://android.googlesource.com/platform/frameworks/av/+/e3c46b0a49d045fa38d35e5cf7ad4becaacbbed6/services/audiopolicy/service/AudioPolicyEffects.cpp
- Standard effect commands and notification flags:
  https://android.googlesource.com/platform/system/media/+/a8cff157ff0ed02fa7e29438f4889a9933c37768/audio/include/system/audio_effect.h

The output-mix chain remains an alternative for a deliberately global effect.
AOSP orders `AUDIO_SESSION_OUTPUT_MIX` after track-specific chains and before
`AUDIO_SESSION_OUTPUT_STAGE`, and processes it before writing to the HAL.
However, it is global to the selected mixer output and may include non-music
content; prefer the per-stream `music` postprocess configuration if it is
supported by this exact vendor build.

## 4. Output-mix effect constraints

An output-mix effect is global to the selected PlaybackThread mix, not
automatically limited to the music stream. It may affect navigation prompts,
notifications and other tracks mixed onto that output.

The AOSP implementation rejects output-mix effects on direct output threads.
Consequently, direct/offload paths cannot be assumed to pass through the
candidate effect. The effect may also move with the selected output when
policy changes routes.

Initial eligibility must therefore be narrow and fail closed:

- only the intended primary PCM mixer output;
- stereo only;
- 44.1 kHz or 48 kHz only;
- supported sample format only;
- normal audio mode only;
- bypass during in-call, communication, SCO/HFP, capture and unsupported
  output modes;
- do not assume offloaded, direct, HDMI, USB or FM streams traverse the effect.

The effect must default to disabled/bypass until explicitly enabled. It must
not change the AudioFlinger mix format, HAL stream configuration, route
selection, or CD3313 control state.

## 5. Registration and lifecycle work still required

Before implementation, recover the exact vendor Android 9 effect framework
contract:

1. Identify the exact `frameworks/av` tag/commit corresponding to the target
   build fingerprint and vendor patches.
2. Inspect the supplied `audio_effects.xml` and confirm supported library,
   effect and postprocess/global-effect registration syntax.
3. Confirm whether the build loads effects from `/vendor/etc`,
   `/system/etc`, or both, and whether vendor effect libraries are allowed.
4. Trace the target's output-mix effect creation, selected output thread,
   format negotiation, channel count and buffer size.
5. Verify the effect receives stereo frames before the HAL mono fold.
6. Implement and test the standard effect command/state lifecycle, including
   enable/disable, device, mode, reset, parameter changes and teardown.
7. Confirm that profile updates are published from a control thread and that
   the process callback has no allocations, locks, logging or blocking calls.
8. If the descriptor advertises device/mode notifications, implement
   `EFFECT_CMD_SET_DEVICE` and `EFFECT_CMD_SET_AUDIO_MODE`; set
   `EFFECT_FLAG_DEVICE_IND` and `EFFECT_FLAG_AUDIO_MODE_IND` respectively.
   The initial processing gate must be closed outside normal audio mode and
   for unsupported output devices. Treat any offload state as bypass unless a
   separate offload implementation is deliberately validated.

The exact vendor effect configuration and AudioFlinger runtime state are not
fully established by the extracted static archive. No effect library,
XML registration, system image or device configuration was modified.

## 6. Comparison with the HAL callback candidate

| Property | MTK Normal post-processing | AudioFlinger output-mix candidate |
|---|---|---|
| Before speaker mono fold | No | Yes, on mixer path |
| Stereo preserved for speaker route | No | Expected; must verify live |
| Scope | Normal handler only | Global selected output mix |
| Framework effect API | No | Yes |
| Requires vendor HAL patch | Likely for custom replacement | Not necessarily; requires effect registration/privilege |
| Offload/direct coverage | Not established | Not guaranteed; output-mix rejected on direct threads |
| Current project status | Static call site proven | Framework candidate; target-specific validation pending |

## 7. Recommended direction

Prioritize a small Android effect-library prototype around the stereo engine,
registered first as a `music` postprocess effect if the vendor's Android 9
AudioPolicyEffects implementation supports the recovered v2.0 configuration.
Use a global output-mix attachment only if the stream-specific route is
unavailable and its wider scope is explicitly accepted. Do not patch
`audio.primary.mt8163.so`.

Keep the first prototype disabled by default and restrict it to normal-mode
stereo PCM mixer output at 44.1/48 kHz. Preserve the original buffer on every
unsupported configuration or processing failure. Keep the existing vendor
BES/ACF path and CD3313 controls independent until coexistence and ordering
are verified.

The four-channel Float32 engine remains a separate offline capability until
a true four-channel digital boundary is demonstrated. The practical first
product target is stereo digital EQ/dynamics followed by the CD3313's
independent analog four-speaker level controls.
