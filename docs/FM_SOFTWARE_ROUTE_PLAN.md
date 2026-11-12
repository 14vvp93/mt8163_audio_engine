# FM software DSP route plan

## Goal

Apply the shared PCM DSP to the *audible* FM-to-speaker path without changing
the tuner driver, CD3313 input selection, or unrelated audio routes.

## Confirmed firmware paths

1. The stock FM policy route is an FM Tuner In AudioPatch. The HAL's
   `AudioALSAHardware::createAudioPatch()` branch enables it through
   `AudioALSAStreamManager::setFmEnable()` and
   `AudioALSAFMController::setFmEnable()`.
2. The enable tuple reaches `setFmDirectConnection_l()`, which resolves
   `FM_I2S_Playback`, opens the PCM endpoint and starts it. This function
   contains no `pcm_write()` buffer path for a software effect to modify.
3. A separate input-stream route exists. For `AUDIO_SOURCE_FM_TUNER`
   (1998 / `0x7CE`), the HAL can select
   `AudioALSACaptureHandlerFMRadio`; its read method gets PCM from the FM
   capture provider and returns it to an input client.
4. The recovered RadioService JNI libraries control tuner state and do not
   expose an app-owned AudioTrack/AudioRecord PCM loop in their native imports.
   Current static evidence does not show that the stock RadioService consumes
   the capture stream to play FM.

## FM_DIRECT_CONTROL is not a software-route switch

The policy extension's `setFMIndirectMode(rate)` sends
`FM_DIRECT_CONTROL=1` for values below 48,001 and `0` for values at or
above 48,001. The primary HAL stores this value in the boolean returned by
`checkFmNeedUseDirectConnectionMode()`. When true, the FM enable path asks
the tuner driver to use 48 kHz; the paired MT6627 driver's rate-setter
callback is unpopulated, so that ioctl may return `-1002` and the HAL ignores
the result.

Crucially, the FM AudioPatch enable path separately calls
`setFmDirectConnection_l(true, ...)`, which opens and starts
`FM_I2S_Playback`. The disable tuple calls the same controller with its
first argument false. Therefore `FM_DIRECT_CONTROL` is a rate/control flag,
not a proven selector between direct hardware playback and software PCM
playback. Changing it is not an approved way to activate the DSP bridge.

## Candidate software route

The only currently demonstrated userspace PCM boundary is the FM capture
input stream. A possible software route is:

`FM tuner I2S -> FM_I2S_Capture -> AUDIO_SOURCE_FM_TUNER input stream
-> FmPcmAdapter -> standard Android output stream -> speaker`

The adapter is already implemented and tested for 32 kHz PCM16 stereo. It
fails open unless the caller confirms an active FM route and normal audio
mode; unsupported rates/layouts and disabled DSP are bit-exact bypass.

This candidate is **not yet a working FM playback route**. The stock direct
AudioPatch must be disabled or replaced first, otherwise direct and software
audio may play simultaneously. No evidence yet establishes a safe runtime
control for switching that patch to the software route.

## Required target checks before integration

- Run `tools/collect_mt8163_audio_diag.ps1` while FM is playing and while it
  is stopped; compare AudioPolicy patches, AudioFlinger input/output threads,
  ALSA PCM state and FM control logs.
- Confirm whether the FM capture PCM endpoint is opened during normal stock
  playback, or only when an explicit FM input stream is requested.
- Confirm the active PCM sample rate, channel count and format. Do not assume
  every FM policy profile is the same as the MT6627 driver's 32 kHz metadata.
- Identify a reversible way to suppress the direct FM AudioPatch and verify
  that the tuner remains powered and muted/volume-controlled correctly.
- Only then connect `FmPcmAdapter` to the confirmed buffer and add a
  software output bridge. Keep calls, non-normal audio modes, route loss and
  unsupported PCM formats on exact bypass.

## Safety boundary

No vendor HAL, policy XML, kernel module, system partition or device setting
has been modified. Host tests and ARMv7 compilation validate the adapter
component only; they do not validate the FM speaker route on the target.


## Policy-extension call-site boundary — 2026-11-12

A second pass over the exact paired
`libaudiopolicycustomextensions.so` confirms that
`AudioPolicyManagerCustomImpl::setFMIndirectMode(unsigned int)` is installed
in the custom policy-manager vtable at address point offset `+0xf4`
(relocation slot `0x1a09c`). The method itself builds an AudioParameter and
sends `FM_DIRECT_CONTROL` through the policy client's `setParameters`
callback. Its threshold comparison is literal: `rate < 48001` sends 1;
otherwise it sends 0.

The extension's own disassembly contains a PLT entry for this method but no
resolved direct internal call site. This is consistent with invocation
through the custom policy-manager virtual interface; the exact framework
caller and the semantic meaning of the integer passed at that call site are
not recovered from this extension binary alone. Do not promote the likely
sample-rate interpretation into a confirmed call-site fact.

The same binary has separate FM hooks for AudioPatch collection add/remove,
FM activity/direct-activity predicates, and output-device routing signals.
Those hooks manage patch lifecycle and app/output routing state; they do not
establish a software PCM playback bridge. In particular, the existence of
`fm_signalAPProutingFromSetOutputDevice()` is not evidence that the FM
capture-provider buffer feeds the active output.

**Updated conclusion:** there is still no statically proven, supported switch
that replaces the stock `FM_I2S_Playback` AudioPatch with the capture-stream
software path. Keep `FM_DIRECT_CONTROL` untouched and retain the target-side
read-only diagnostic gate before any integration change.
