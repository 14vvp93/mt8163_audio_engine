# Android sound profiles

## Scope

The Android test bench now exposes named sound-profile save/load actions and automatically restores the last session. Profiles are stored locally in the application's private SharedPreferences file; no storage permission or network access is required.

This is a test-bench control feature. It does not route audio into Android's system mixer or the MT8163 vendor HAL.

## Persisted controls

The Android snapshot stores DSP enable state; four channel gains (FL, FR, RL, RR); 32 peaking-EQ gain values per channel; Balance/Fader; software bass-management enable, crossover frequency/order, subwoofer gain and phase; Bass enhancement enable/amount/focus frequency; Clarity enhancement enable/amount/focus frequency; multiband dynamics enable state, low/mid and mid/high crossover frequencies, and all seven compressor parameters for each of the 12 channel/band combinations.

The snapshot is applied to native code as one validated profile update, rather than issuing a sequence of independent control calls. This preserves the engine's single 50 ms profile crossfade and avoids partially applied profiles.

## Packed JNI layout (233 floats)

| Index | Contents |
|---:|---|
| 0 | DSP enabled (0 or 1) |
| 1–132 | Four repetitions of channel gain followed by 32 EQ gains |
| 133 | Balance (-1 left to +1 right) |
| 134 | Fader (-1 front to +1 rear) |
| 135 | Bass management enabled (0 or 1) |
| 136 | Crossover frequency in Hz |
| 137 | Linkwitz–Riley order (2, 4, 6 or 8) |
| 138 | Subwoofer gain in dB |
| 139 | Subwoofer phase inverted (0 or 1) |
| 140 | Bass enhancement enabled (0 or 1) |
| 141 | Bass enhancement amount (0–1) |
| 142 | Bass focus frequency in Hz |
| 143 | Clarity enhancement enabled (0 or 1) |
| 144 | Clarity enhancement amount (0–1) |
| 145 | Clarity focus frequency in Hz |
| 146 | Multiband dynamics enabled (0 or 1) |
| 147 | Low/mid crossover frequency in Hz |
| 148 | Mid/high crossover frequency in Hz |
| 149–232 | Four channels × three bands; each compressor stores enabled, threshold dB, ratio, attack ms, release ms, knee dB and makeup gain dB |

The compressor settings are independently selectable for FL, FR, RL and RR, with Low, Mid and High bands. The test-bench UI exposes master dynamics enable, both crossover frequencies, channel/band selection, per-band enable and threshold/ratio/attack/release/knee/makeup controls.

Native code rejects wrong-length arrays, non-finite values, invalid boolean encodings, out-of-range controls, invalid compressor parameters and unsupported crossover orders before queuing the profile.

## Compatibility

The persistent JSON array is private to the app. The native packed layout is explicitly versioned by UI_PROFILE_FLOAT_COUNT; any future layout change must increment the version or introduce a versioned envelope and migration path. Existing malformed or incompatible snapshots are ignored rather than passed to native DSP.

## Validation status

Android APK compilation (including the ARMv7 JNI/C++ build) passed in GitHub Actions run `42969841753` for commit `98b44dd2ea8c0852dd8c3f686adcc9b7e8c29fd4`. The generated debug APK artifact is `mt8163-audio-test-debug-apk` (artifact ID `13832503462`). This verifies compilation and packaging only. Runtime persistence, UI behavior, DSP output and audible behavior still require installation and testing on a compatible Android 9 device.
