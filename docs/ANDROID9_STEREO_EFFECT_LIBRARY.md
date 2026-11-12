# Android 9 per-stream stereo software insert effect

## Status

The first Android Effect Library adapter is implemented in
pcm-dsp/android/effect_library.cpp. The optional platform build recipe is
pcm-dsp/Android.mk.

This is an integration scaffold, not a device-ready installation:

- the extracted vendor audio_effects.xml currently declares stock AOSP effect
  libraries only and has no active postprocess stream chains;
- no audio_effects.xml or product makefile has been changed, so the effect is
  not registered or loaded by the target ROM;
- Android 9 r61 platform-header ARMv7 cross-compilation passes with
  -Wall -Wextra -Werror; the resulting ELF is 32-bit ARM and exports AELI;
- host smoke tests pass for descriptor/create/release, default-disabled bypass,
  PCM16 gain, DSP-off bypass, unsupported-rate/device bypass, offload and
  in-call bypass, plus the Float32 adapter;
- latest verified ARMv7 artifact including Bluetooth A2DP 44.1 kHz gating and
  the shared stereo core's native 32 kHz support (built from commit 706c023e)
  is available at
  https://github.com/14march93/mt8163_audio_engine/actions/runs/43169030035/artifacts/13921780142;
- no full Android platform-tree build has been run and no effect has been loaded
  on the head unit;
- target ROM factory behavior, loaded XML copy/schema and runtime stream
  classification still require device-specific validation.

## Target firmware effect configuration audit — 2026-11-12

The repository firmware archive audit completed successfully for `files.zip`,
`files2.zip.001`, `lib.zip.001` and `vendor_lib.zip.001`. It recovered
`files/vendor/audio_effects.xml` and the matching `files2/vendor/audio_effects.xml`
copy (identical content, 5,832 bytes).

Verified configuration facts:

- XML root declares Audio Effects configuration version 2.0.
- The active library list points to the standard stock wrappers under the
  vendor sound-effects directory: bundle, reverb, visualizer, downmix,
  loudness enhancer and dynamics processing.
- The active effects list contains only the corresponding stock effects.
- There is no active `<postprocess>` element. The Music postprocess example
  in lines 89–99 is XML-commented documentation, not an enabled chain.
- The file does not register `libmt8163_stereo_effect.so` or implementation
  UUID `8e5f3c12-7b4d-4a91-9c62-1f08a7d35e20`.

This is a confirmed firmware-archive baseline, not proof of the exact file
currently loaded by AudioPolicyService on a running unit. The target's runtime
configuration path and any overlay/vendor modifications still require a
read-only device diagnostic.

The audit artifact is available at:
https://github.com/14march93/mt8163_audio_engine/actions/runs/43167408077

## Effect identity

- Library module: libmt8163_stereo_effect
- Library filename: libmt8163_stereo_effect.so
- Implementation UUID: 8e5f3c12-7b4d-4a91-9c62-1f08a7d35e20
- Effect type: standard OpenSL ES Equalizer type UUID
  0bed4300-ddd6-11db-8f34-0002a5d5c51b
- Descriptor mode: software INSERT effect, placed last in its output-session
  chain, with direct buffers and device/audio-mode indication; no offload
  capability is advertised.

## Bluetooth A2DP policy correlation

The recovered vendor `a2dp_audio_policy_configuration.xml` declares a
separate A2DP HAL module with a PCM16, stereo, 44.1 kHz output mix port and
three output device types: A2DP, A2DP Headphones and A2DP Speaker. The vendor
fragment does not declare a compressed-offload profile. The Android effect
adapter therefore now allows those three A2DP output masks, while retaining
its explicit offload bypass and normal-audio-mode gate.

The adapter deliberately permits A2DP only at 44.1 kHz, matching the
recovered vendor profile; 48 kHz A2DP and Bluetooth SCO fail open to exact
bypass. The host effect smoke test covers all three A2DP masks at 44.1 kHz,
48 kHz A2DP bypass, exact bypass for A2DP when `EFFECT_CMD_OFFLOAD` is true,
and SCO bypass. This explicitly protects against the A2DP HAL's recovered
offload-control path even though the vendor A2DP policy fragment does not
declare a compressed-offload mix-port profile. These tests validate the
adapter's device-mask, sample-rate and offload gates, not the target's
AudioFlinger routing or runtime offload state. See [MT8163 audio source coverage](MT8163_AUDIO_SOURCE_COVERAGE.md).

## Supported processing contract

The DSP is enabled only when all of these conditions hold:

1. Framework effect instance is enabled.
2. DSP parameter enabled is true. It defaults to false.
3. Stream is stereo output.
4. Sample rate is 44.1 or 48 kHz.
5. Input and output formats match and are either PCM16 or PCM Float32.
6. Current output device is speaker, wired headset, wired headphones, or one of the three vendor-declared Bluetooth A2DP output devices.
7. Audio mode is AUDIO_MODE_NORMAL.
8. The output is not offloaded.

Unsupported but copy-compatible PCM layouts are passed through unchanged.
Unsupported device, mode, offload, or sample-rate states fail open to bypass.
Compressed formats are not a supported process() input; the framework must
keep offloaded streams out of this software effect path.

Float32 is adapted to the existing PCM16 stereo core using fixed-size,
per-instance scratch buffers. Processing chunks are bounded to 4096 frames.
No heap allocation, mutex, logging, file I/O, or blocking operation occurs
inside process().

## Parameter command ABI

All parameter commands use Android effect_param_t, with a 32-bit parameter
ID and the value beginning at the next 32-bit boundary.

| ID | Value type | Meaning |
|---:|---|---|
| 1 | uint32_t | DSP enabled (0/1; default 0) |
| 2 | float | Preamp dB |
| 3 | float | Stereo balance (-1..+1) |
| 4 | uint32_t | Limiter enabled (0/1) |
| 5 | float | Limiter threshold |
| 100..109 | BandParameter | One of the ten stereo EQ bands |

BandParameter is six 32-bit fields in this order:
frequency_hz, gain_db, q, enabled, type, shelf_slope.
Filter type values follow pcm_dsp::FilterType:
0 peaking, 1 low shelf, 2 high shelf, 3 high-pass, 4 low-pass.
Both immediate and deferred parameter commands are supported. A fixed
three-slot reader/writer snapshot handoff keeps the audio callback from
observing a partially written parameter structure; if no safe slot is
available, a control update is rejected with -ENOSPC.

## Build in an Android 9 platform tree

Place the repository under a path scanned by the Android build system, then
build the optional module from the platform tree:

```sh
source build/envsetup.sh
lunch <target>
mmm path/to/mt8163_audio_engine/pcm-dsp
```

The module is vendor-side and installs under vendor/lib/soundfx/ or the
corresponding 64-bit directory selected by the platform build.
Building this module alone does not register or activate it.

## AOSP Android 9 integration contract — statically verified

The Android 9 r61 build inputs used by the ABI workflow are the matching
platform headers. The framework-side integration contract was also checked
against AOSP:

- The v2.0 XML schema explicitly allows
  `<postprocess><stream type="music"><apply effect="..."/></stream></postprocess>`.
  The `effect/@uuid` field is the implementation UUID, not the generic type
  UUID. The schema therefore does not require the descriptor type to be
  POST_PROC.
- `AudioPolicyEffects::addOutputSessionEffects()` selects the postprocess
  list by legacy output stream, creates the effect on that audio session and
  output, then enables the processor. This is the framework attachment point
  for the Music mapping.
- `AudioFlinger::EffectChain` recognizes `EFFECT_FLAG_TYPE_INSERT` and
  `EFFECT_FLAG_INSERT_LAST` as a valid software-chain insertion preference.
  The effect is processed in the AudioFlinger chain; this is distinct from
  `EFFECT_FLAG_TYPE_POST_PROC`, which is the HAL post-processing path.
- With `EFFECT_FLAG_DEVICE_IND` and `EFFECT_FLAG_AUDIO_MODE_IND`, AudioFlinger
  forwards `EFFECT_CMD_SET_DEVICE` and `EFFECT_CMD_SET_AUDIO_MODE`. The
  adapter implements both commands and gates processing to the normal mode
  and supported output masks.
- `EFFECT_FLAG_INPUT_DIRECT` and `EFFECT_FLAG_OUTPUT_DIRECT` describe direct
  buffer access by the effect; they do not advertise hardware offload.

This confirms that the proposed XML Music attachment plus a TYPE_INSERT /
INSERT_LAST descriptor is coherent with the AOSP software effect architecture.
It does not prove that the target ROM retained unmodified AOSP behavior, loads
the expected XML file, classifies navigation prompts as a separate stream, or
delivers the expected device masks. Those remain target-specific gates.

AOSP references:
- https://android.googlesource.com/platform/hardware/interfaces/+/b5433136dd099d533e866028367ba4ed829fab02/audio/effect/2.0/xml/audio_effects_conf_V2_0.xsd
- https://android.googlesource.com/platform/frameworks/av/+/e3c46b0a49d045fa38d35e5cf7ad4becaacbbed6/services/audiopolicy/service/AudioPolicyEffects.cpp
- https://android.googlesource.com/platform/frameworks/av/+/725dfe9/services/audioflinger/AudioFlinger.cpp
- https://android.googlesource.com/platform/system/media/+/a8cff157ff0ed02fa7e29438f4889a9933c37768/audio/include/system/audio_effect.h

## Registration gate — intentionally not done

The intended policy entry is a stream postprocess mapping for Music, but the
library descriptor deliberately uses TYPE_INSERT. In Android 9,
AudioPolicyEffects::addOutputSessionEffects() creates the configured effect on
the stream's audio session. An INSERT descriptor is handled by AudioFlinger’s
software effect chain; a TYPE_POST_PROC descriptor can instead be forwarded
to the audio HAL through audio_stream_out::add_audio_effect. We need the
software insert path so the stereo effect runs before the MTK HAL's speaker
mono fold.

Before adding the effect to the target ROM's effect configuration:

1. Inspect the exact target audio_effects.xml loaded at runtime and confirm
   the framework accepts a postprocess effect for the music stream.
2. Confirm the ROM's EffectsFactory accepts the descriptor's
   EFFECT_FLAG_TYPE_INSERT and EFFECT_FLAG_INSERT_LAST combination.
3. Confirm AudioPolicyEffects creates the effect on the intended Music
   output session and AudioFlinger processes it in the software session chain
   (not through the HAL add_audio_effect callback). Confirm device and
   audio-mode updates reach the effect.
4. Confirm navigation prompts are not unintentionally classified into the
   same Music stream on this ROM.
5. Confirm speaker and wired output masks as delivered by the vendor framework.
6. Build for ARMv7, inspect ELF dependencies and exported AELI symbol, then
   test load/unload and bypass behavior before enabling DSP parameters.

Do not register the effect globally or modify the system image as part of this
scaffold. The target-specific config and runtime behavior are not yet known.

## Disable transition and dry-buffer contract — 2026-11-12

Android 9 AudioFlinger continues to invoke an enabled INSERT effect while its
state is STOPPING/STOPPED. After EFFECT_CMD_DISABLE, the effect must therefore
keep the final process callback safe: copy the input PCM bit-for-bit to a
distinct output buffer, then return `-ENODATA` to indicate that the effect tail
is complete. AudioFlinger uses that return value in STOPPED to force the
disable-wait counter to one and transition the effect chain to IDLE.

The adapter now implements this contract when its framework-enabled flag is
false. This is separate from the user DSP-enabled parameter: a framework-active
effect with DSP disabled continues to return success and dry-copy each block,
whereas a framework-disabled effect dry-copies the final block and returns
`-ENODATA`.

The host ABI smoke test covers the post-disable callback and checks both the
`-ENODATA` result and bit-exact PCM output. This validates the adapter contract
in the host test harness; it does not replace runtime testing on the target ROM.

AOSP reference implementation:
- https://android.googlesource.com/platform/frameworks/av/+/26965db50a617f69bdefca0d7533796c80374f2c/services/audioflinger/Effects.cpp

## Transactional configuration updates — 2026-11-12

`EFFECT_CMD_SET_CONFIG` is now treated as an atomic state update. The
adapter validates the required sample-rate, channel-mask and format fields,
requires matching input/output layouts, and verifies that the output layout
has a known PCM sample width before replacing the active configuration.
Malformed or non-copy-compatible configurations are rejected without
overwriting the previous known-good configuration.

Valid but unsupported PCM streams remain accepted and are passed through
unchanged. This preserves the fail-open behavior for unsupported sample rates,
formats and channel layouts that are still safe to copy.

The Android effect ABI smoke test now submits a mismatched input/output
sample-rate configuration while the effect is disabled, expects `-EINVAL`,
then re-enables processing and verifies that the previous 48 kHz PCM16
configuration still processes correctly. This is a host ABI regression test;
target framework behavior and device runtime remain unverified.
