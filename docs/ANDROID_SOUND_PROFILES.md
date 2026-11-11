# Android sound profiles

## Scope

The Android test bench now exposes named sound-profile save/load actions and automatically restores the last session. Profiles are stored locally in the application's private SharedPreferences file; no storage permission or network access is required.

This is a test-bench control feature. It does not route audio into Android's system mixer or the MT8163 vendor HAL.

## Persisted controls

The Android snapshot stores DSP enable state; four channel gains (FL, FR, RL, RR); 32 peaking-EQ gain values per channel; Balance/Fader; software bass-management enable, crossover frequency/order, subwoofer gain and phase; Bass enhancement enable/amount/focus frequency; and Clarity enhancement enable/amount/focus frequency.

The snapshot is applied to native code as one validated profile update, rather than issuing a sequence of independent control calls. This preserves the engine's single 50 ms profile crossfade and avoids partially applied profiles.

## Packed JNI layout (146 floats)

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

Native code rejects wrong-length arrays, non-finite values, invalid boolean encodings, out-of-range controls and unsupported crossover orders before queuing the profile.

## Compatibility

The persistent JSON array is private to the app. The native packed layout is explicitly versioned by UI_PROFILE_FLOAT_COUNT; any future layout change must increment the version or introduce a versioned envelope and migration path. Existing malformed or incompatible snapshots are ignored rather than passed to native DSP.

## Validation status

CI must pass Android APK compilation and the native ARMv7 build before this feature is considered build-verified. Runtime persistence and audible behavior still require installation and testing on a compatible Android 9 device.
