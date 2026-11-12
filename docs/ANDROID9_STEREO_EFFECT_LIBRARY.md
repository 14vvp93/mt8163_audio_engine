# Android 9 per-stream stereo software insert effect

## Status

The first Android Effect Library adapter is implemented in
pcm-dsp/android/effect_library.cpp. The optional platform build recipe is
pcm-dsp/Android.mk.

This is an integration scaffold, not a device-ready installation:

- no audio_effects.xml or product makefile has been changed;
- the effect is not registered or loaded by the target ROM;
- the first Android 9 header/ARMv7 cross-compile exposed two bypass-call errors,
  which have been fixed; the corrected CI build is pending;
- no full Android platform-tree build has been run and no effect has been loaded
  on the head unit;
- the UUID and post-process descriptor still need validation against the
  target ROM's effect factory and exact audio_effects.xml schema.

## Effect identity

- Library module: libmt8163_stereo_effect
- Library filename: libmt8163_stereo_effect.so
- Implementation UUID: 8e5f3c12-7b4d-4a91-9c62-1f08a7d35e20
- Effect type: standard OpenSL ES Equalizer type UUID
  0bed4300-ddd6-11db-8f34-0002a5d5c51b
- Descriptor mode: software INSERT effect, placed last in its output-session
  chain, with direct buffers and device/audio-mode indication; no offload
  capability is advertised.

## Supported processing contract

The DSP is enabled only when all of these conditions hold:

1. Framework effect instance is enabled.
2. DSP parameter enabled is true. It defaults to false.
3. Stream is stereo output.
4. Sample rate is 44.1 or 48 kHz.
5. Input and output formats match and are either PCM16 or PCM Float32.
6. Current output device is speaker, wired headset, or wired headphones only.
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

1. Inspect the exact target audio_effects.xml and confirm the framework
   accepts a postprocess effect for the music stream.
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
