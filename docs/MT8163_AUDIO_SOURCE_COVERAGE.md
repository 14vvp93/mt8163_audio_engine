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

The FM route must retain a hard bypass for calls, unsupported sample rates,
non-stereo buffers and any path whose buffer ownership/format is not proven.

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
| Bluetooth A2DP | vendor A2DP module, PCM16 stereo 44.1 kHz, three output masks | adapter allows A2DP at 44.1 kHz; 48 kHz bypass, host tests and ARMv7 ABI build passed in workflow run 43168512245 | target AudioFlinger attachment/device notifications and offload verification |
| Bluetooth SCO | separate 8/16 kHz communication devices | bypass | keep excluded |
| FM | vendor FM Tuner In policy route plus separate MT6627 FM_I2S HAL/ALSA path | not yet attached to shared DSP | trace active FM PCM path and choose a proven insertion point |

No effect XML, product makefile, vendor HAL, kernel module, device configuration,
or live CD3313 state has been modified by this work.
