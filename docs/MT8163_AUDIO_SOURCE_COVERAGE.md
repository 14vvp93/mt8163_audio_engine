# MT8163 audio source coverage: Music, Bluetooth A2DP and FM

## Purpose

The target is one shared PCM DSP engine for ordinary media, Bluetooth A2DP
music and FM radio. The engine is shared; the source-specific integration
points are not necessarily shared. Keep call/SCO and microphone paths out of
the first release.

This document records firmware-archive evidence and the current integration
decision. It does not claim runtime validation on a head unit.

## 1. Ordinary Android Music

The Android 9 effect-library adapter is attached through an output-stream
postprocess mapping selected by AudioPolicyEffects. The descriptor is a
software INSERT effect, so the intended PCM path is AudioFlinger before the
vendor HAL's speaker mono fold.

The firmware's vendor `audio_effects.xml` uses schema v2.0, registers only
stock libraries/effects, and has no active postprocess section. Its Music
postprocess example is commented documentation. The custom DSP library and
UUID are not registered in the firmware baseline.

Status: adapter and host/ARMv7 ABI scaffold exist; target registration and
runtime load remain intentionally disabled.

## 2. Bluetooth A2DP

The recovered vendor `a2dp_audio_policy_configuration.xml` declares a
separate `a2dp` module (HAL 2.0) with:

- `a2dp output`: PCM16, 44,100 Hz, stereo;
- output device ports `AUDIO_DEVICE_OUT_BLUETOOTH_A2DP`,
  `AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES`, and
  `AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER`;
- all three output ports route from `a2dp output`;
- no compressed-offload flag or compressed format is declared in this
  vendor A2DP fragment.

The matching system A2DP fragment has the same three device ports and 44.1 kHz
stereo output profiles, although its mix-port profile is left unspecified.
The vendor copy is the more explicit profile for this target.

### A2DP HAL binary cross-check

The firmware archive also contains `audio.a2dp.default.so` in both the
system and vendor_lib locations. The two copies are byte-identical:

- ELF32 ARM, Android 28;
- Build ID `50451f06f3c570299d158ca4444da1b4`;
- SHA-256 `2e071c95c5b6b8ecc3da2abe9e7018b84447f03e808ac81d56502634e965305e`.

The HAL reads and writes the negotiated A2DP output codec configuration at
runtime (sample rate, bits per sample and channel mode), and its command
strings include `A2DP_CTRL_CMD_OFFLOAD_START`. It therefore contains an
offload-control path even though the recovered vendor A2DP policy fragment
does not declare a compressed/direct offload mix-port profile. Do not infer
that offload is impossible from the policy fragment alone; the actual runtime
offload state still needs confirmation. The effect adapter retains its
`EFFECT_CMD_OFFLOAD` bypass for this reason.

The primary system policy separately declares a compressed-offload mix port
for MP3/AAC/AAC-LC on the primary output route. That is not the same as the
vendor A2DP module's PCM output profile. A2DP offload must still be checked
against runtime properties and any alternative policy overlays; the current
firmware archive contains no special A2DP-offload-disabled policy XML.

### Adapter change

The Android effect adapter now accepts the three A2DP output device masks in
addition to Speaker and wired headset/headphones, but only at the vendor's
verified 44.1 kHz profile. A2DP at 48 kHz is fail-open bit-exact bypass.
The host smoke test checks DSP processing at 44.1 kHz for all three masks and
exact bypass at 48 kHz. Bluetooth SCO remains an exact bypass.

This is a source/configuration-based implementation change. The smoke test
must pass in CI, and actual AudioFlinger attachment/device notifications must
still be confirmed on the target. If the framework reports offload, the
adapter continues to bypass.

## 3. Bluetooth SCO / phone calls

The primary policy declares SCO, SCO Headset and SCO Car Kit as separate
output devices with 8/16 kHz PCM profiles. These are communication/telephony
paths, not A2DP music.

SCO stays explicitly excluded from the DSP. Do not enable it merely because
the Bluetooth A2DP path is supported. Microphone capture and in-call audio
remain bypassed.

## 4. FM radio

The vendor primary audio policy:

- attaches `FM Tuner In` as an input device;
- declares PCM16 mono/stereo profiles at 8, 16, 32, 44.1 and 48 kHz;
- routes `FM Tuner In` to Speaker, Wired Headset and Wired Headphones.

This proves that the policy exposes an FM tuner input route. It does not by
itself prove that the FM app's audible path is processed by the ordinary
AudioFlinger Music session chain.

Independent static HAL analysis also recovered a dedicated FM I2S path:
`AudioALSAFMController::setFmDirectConnection_l()` selects
`FM_I2S_Playback` in non-merge mode (the current MT6627 class-1 metadata),
and the FM capture provider selects `FM_I2S_Capture`. The paired MT6627
driver reports I2S enabled, master mode, 32 kHz and `FM_I2S_PAD_CONN`.
The HAL resolves ALSA card/PCM indexes dynamically from procfs.

### Stock FM tuner control versus PCM transport

The extracted FM control libraries were cross-checked separately from the
primary audio HAL:

- `libcarservices_jni.so`: Build ID
  `bf391e545edd2e482b72cd8a431d1c59`; SHA-256
  `ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`.
  It contains the
  `android::register_android_radio_RadioService()` JNI registration,
  `nativeSetPower` and other RadioService native-method names, and tuner
  control routines including `MainThread::RadioPowerUp()`,
  `RadioFreqSet()`, `RadioStart()` and `RadioPowerDown()`. Its strings
  include `/dev/fmradio` and `/dev/fm`.
- `libfmjni.so`: Build ID
  `4459396f1ba98c792e0635454be1aa33`; SHA-256
  `7e2d0959e5dfe3c5c65befa7bd3fe6dffe4b937cfebe6b59eb0be4665bedfc42`.
  It exports the FM tuner command API
  (`FMR_pwr_up`, `FMR_pwr_down`, `FMR_tune`, `FMR_seek`,
  `FMR_set_mute`, RDS and signal-quality routines).
- Neither library's dynamic imports expose Android `AudioRecord`,
  `AudioTrack`, or `AudioSystem` PCM-stream APIs. This is consistent with
  these JNI libraries controlling the tuner rather than carrying the audible
  PCM stream; it is not a substitute for observing the running app.

The extracted FM JNI and policy-extension binaries are available in the
firmware audit artifact:
https://github.com/14march93/mt8163_audio_engine/actions/runs/43170784980/artifacts/13922186760

The paired policy-extension binary `libaudiopolicycustomextensions.so`
has Build ID `f471dbca5ef93ec7e59d87999c3053f9` and SHA-256
`9f4b6940d241a4bc6517214f1fc3fd0b3d69964f54aaa04e5ceb836d47d04afb`.
The policy extension's `fm_addAudioPatch()` recognizes the FM Tuner input
patch (device mask `0x80002000`) and maintains its FM patch state. The
primary HAL's FM-specific `createAudioPatch()` branch then drives the
`AudioALSAFMController::setFmEnable()` lifecycle, which uses the dedicated
FM direct-connection controller. This is the strongest static evidence for
the stock FM-to-output route: a policy AudioPatch feeding the direct
`FM_I2S_Playback` endpoint, rather than an app-owned AudioTrack PCM loop.

The `AUDIO_SOURCE_FM_TUNER` input-stream capture handler described below is
a separate software capture path. It can deliver FM PCM to an input client,
but the current firmware evidence does not show the stock RadioService
consuming that stream to play FM. Therefore the new `FmPcmAdapter` is useful
for a future software bridge, but it cannot affect the stock direct FM
AudioPatch until that route is deliberately changed or a confirmed buffer
boundary is found inside the direct path.

### Newly verified software capture boundary

The HAL's capture-handler factory compares the input stream source field
against `0x7CE` (decimal 1998, Android `AUDIO_SOURCE_FM_TUNER`). When its
additional handler-selection guards permit the FM path, it constructs
`AudioALSACaptureHandlerFMRadio`. `AudioALSAStreamIn::open()` calls this
factory. In `AudioALSAStreamIn::read()`, the HAL loads the selected handler
from the stream object and dispatches its virtual `read(buffer, bytes)`
method. The FM handler's `open()` obtains the singleton
`AudioALSACaptureDataProviderFMRadio` and constructs an
`AudioALSACaptureDataClient` for the stream attributes. Its
`AudioALSACaptureHandlerFMRadio::read()` dispatches to the client's read
method when the client exists. This proves a real AudioRecord/input-stream FM
capture route delivering PCM to the input stream, not just the existence of an
unused provider class. The extra guard fields still need semantic mapping.

The disassembly was checked against the extracted Android 9 primary HAL:
`audio.primary.mt8163.so`, Build ID
`f66eaf9e6ac950259f0c253be618d15a`, SHA-256
`112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`.
Relevant exported symbols include `AudioALSACaptureDataProviderFMRadio::open()`
(`0x000e78f8`), `readThread()` (`0x000e7ef4`),
`AudioALSACaptureDataProviderBase::provideCaptureDataToAllClients()`
(`0x000e24a0`), `AudioALSACaptureHandlerFMRadio::read()`
(`0x000d8424`), and
`AudioALSAFMController::setFmDirectConnection_l()` (`0x000af2f0`).


The primary HAL also contains a concrete FM capture data path:

- `AudioALSACaptureDataProviderFMRadio::readThread()` reads PCM from its
  capture PCM handle into an 8 KiB stack buffer using the HAL's PCM-read
  wrapper.
- It then calls `AudioALSACaptureDataProviderBase::provideCaptureDataToAllClients()`,
  which iterates attached capture clients and invokes their data callback.
- `AudioALSACaptureHandlerFMRadio::read()` delegates to its provider/client
  interface to return captured data to the caller.

Thus the FM capture path does expose PCM samples in software memory before
they are delivered to capture clients. This is a potential DSP insertion
boundary, subject to identifying which clients attach and whether the active
FM-to-speaker AudioPatch consumes this capture stream.

By contrast, the FM-specific `AudioALSAHardware::createAudioPatch()`
branch drives `AudioALSAFMController::setFmEnable()`. The controller's
`setFmDirectConnection_l()` opens and starts the separate
`FM_I2S_Playback` PCM endpoint; the direct path has no userspace PCM write
buffer in that function. Keep the AudioPatch/direct playback route and the
`AUDIO_SOURCE_FM_TUNER` capture/fanout route distinct. The capture provider
is a proven software PCM boundary for FM input streams, but is not yet proven
to be the buffer feeding the direct speaker patch.

These are two distinct pieces of evidence: an Audio Policy FM input route and
a dedicated HAL/ALSA FM I2S endpoint. Until the active FM app-to-HAL call path
is traced on the target, do not assume they are the same route or that the
Music postprocess effect sees the FM samples.

The paired `libaudiopolicycustomextensions.so` adds a further FM-specific
AudioPatch control path: it exports `fm_addAudioPatch()`,
`fm_removeAudioPatch()` and `isFMDirectMode()`. The direct-mode predicate
recognizes an `AUDIO_DEVICE_IN_FM_TUNER` patch with two additional patch-state
fields. Its `setFMIndirectMode(rate)` method sends
`FM_DIRECT_CONTROL=1` below 48,001 and `0` at/above 48,001; the primary HAL
stores that boolean and uses it in the FM enable/direct-connection lifecycle.
This reinforces that FM is managed through a dedicated policy patch and HAL
controller, not merely as an ordinary Android Music track. It still does not
prove the exact PCM buffer ownership or where a software effect can safely be
inserted.

### FM integration decision

Do not add `AUDIO_DEVICE_IN_FM_TUNER` to the current output effect's allowed
device mask: it is an input-device type and is not the output device reported
to an output effect.

Next trace the exact FM start/stop path through RadioService, policy patches,
AudioALSAFMController and PCM read/write ownership. If FM is routed through an
AudioFlinger output track, verify whether it is classified as Music and
whether the Music effect is attached. If it remains a direct FM_I2S path,
create a separate bounded PCM adapter at the verified FM HAL boundary that
calls the same DSP core. Do not patch the vendor HAL or intercept ALSA based
only on endpoint names.

The stereo PCM16 DSP core supports and host-tests 32 kHz, matching the
MT6627 driver's class-1 metadata. A separate `FmPcmAdapter` scaffold now
accepts only 32 kHz PCM16 stereo and processes only when the caller confirms
an active FM route and normal audio mode. It bypasses bit-exactly for inactive
routes, non-normal modes, unsupported rates/layouts and disabled DSP.
The FM adapter host tests passed in workflow run `43170383602` (all 11/11
CMake tests passed, including `pcm_dsp_fm_adapter_tests`); the full Android
ARMv7 DSP archive build passed in run `43170383626`, and the Stability Lab
passed in run `43170383558`. The ARMv7 archive contains
`libmt8163_pcm_dsp.a` with the `FmPcmAdapter` methods:
https://github.com/14march93/mt8163_audio_engine/actions/runs/43170383626/artifacts/13921359099
This adapter is not wired into the vendor HAL or the direct FM_I2S playback
patch. The FM route must retain a hard bypass for calls and any path whose
buffer ownership/format is not proven.

Firmware extraction artifact containing the primary HAL, A2DP HAL and policy binaries:
https://github.com/14march93/mt8163_audio_engine/actions/runs/43168955195/artifacts/13920953355

## 5. Shared engine / independent route adapters

The intended architecture is:

```text
Android Music ── AudioFlinger Music INSERT ─┐
Bluetooth A2DP ─ A2DP PCM output INSERT ───┼── shared PCM DSP core
FM radio ─────── verified FM PCM adapter ──┘
                                               │
                                    profile / EQ / limiter
```

The three adapters may differ, but they must use the same parameter model,
filter coefficients and profile state. Only one source is normally audible
at a time; route transitions must reset or safely transition DSP state to
avoid stale filter state/clicks.

## 6. Evidence and validation gates

| Source | Firmware evidence | DSP status | Remaining gate |
|---|---|---|---|
| Music | vendor effects v2.0 XML; AOSP Android 9 effect contract | software INSERT scaffold | registration/runtime Music classification |
| Bluetooth A2DP | vendor A2DP module, PCM16 stereo 44.1 kHz, three output masks | adapter allows A2DP at 44.1 kHz; 48 kHz bypass, host A2DP tests passed in workflow run 43168512245; latest ARMv7 ABI build (with 32 kHz core) passed in workflow run 43169030035 | target AudioFlinger attachment/device notifications and offload verification |
| Bluetooth SCO | separate 8/16 kHz communication devices | bypass | keep excluded |
| FM | vendor FM Tuner In policy route; AudioPatch/direct FM_I2S playback; AUDIO_SOURCE_FM_TUNER input stream selects FM capture handler/provider | shared 32 kHz core plus separate fail-open FmPcmAdapter scaffold; not connected to target HAL | determine whether the active app uses AudioRecord capture or direct AudioPatch; map factory guard fields and choose the audible PCM path |

No effect XML, product makefile, vendor HAL, kernel module, device configuration,
or live CD3313 state has been modified by this work.
