# HEQ decompiled algorithm analysis

## Decompilation provenance

- Source APK: `HEQ.apk`, SHA-256 `a7c6e1163b56270b506a4b8b6f1ae763a4c4ecc9fae5b0a498aeea46968759b3`.
- JADX: 1.5.1, Java decompilation completed in GitHub Actions.
- Workflow run: [42823440097](https://github.com/14march93/mt8163_audio_engine/actions/runs/42823440097).
- Artifact: `heq-decompiled-analysis`, artifact ID `13768013154`, SHA-256 `14691efeec4ad8f62610dd3b4ed5656408509ba811392cee32ba1b6e684331f6`.
- This report records behavior visible in decompiled Java. It is not evidence of runtime behavior on MT8163.

## 1. The app has two distinct EQ paths

### UI response-curve calculation

`com.auto.hequalizer.Biquad` and `Complex` calculate a frequency response used by `EqualizerSurface` to draw the EQ curve.

Observed flow:

1. `EqualizerSurface` takes the gain difference between adjacent EQ control points.
2. It configures one high-shelf biquad for each adjacent interval, at the interval's center frequency.
3. It evaluates the cascaded complex transfer functions at sampled frequencies and converts the magnitude to dB.
4. This calculation updates the displayed response curve.

Important: this Biquad implementation is a graph/response-visualization helper. The decompiled call sites show it being used by the drawing/curve path; they do not show it processing PCM samples. It must not be treated as HEQ's actual audio processing engine or copied as our PCM DSP implementation.

The shelf calculation uses a fixed 44.1 kHz reference in the curve renderer. Its `setHighShelf` formula has the shelf-slope term fixed by a zero multiplier, effectively selecting a fixed shape rather than exposing a user Q/slope parameter. This is suitable as a display approximation, not a general-purpose filter API.

### Actual external-DSP control path

The external DSP adapters send integer parameter arrays through:

`android.audio.AudioEffect.getInstance().doExtAudioEffect(statusId, int[])`

The class is a vendor-specific `android.audio.AudioEffect` extension, not the ordinary public `android.media.audiofx.AudioEffect` session API. The APK contains Java-side protocol adapters but no native `lib/` payload. The implementation of the vendor framework endpoint and DSP firmware is outside this APK.

The adapters select different command/status IDs:

| Backend | Status ID | Observed behavior |
|---|---:|---|
| AK7604 | 136 | Calculates biquad coefficients in Java, then sends fixed-point words |
| FY7604 | 137 | Sends command arrays directly to the vendor effect endpoint |
| CSC37534 | 138 | Sends chip-specific PEQ/balance/loudness/surround arrays |
| SI47925 | 139 | Calculates 24-bit fixed-point biquad parameters and sends them |

These are separate chip backends, not interchangeable transports. None is a CD3313 implementation.

## 2. Coefficient and parameter findings

### AK7604

- Sampling rate constant: 48,000 Hz.
- Peaking EQ uses the standard RBJ-style peaking-EQ relationship:
  - (A = 10^{gainDb/40})
  - (omega = 2pi f/F_s)
  - (alpha = sin(omega)/(2Q))
  - coefficients are normalized by (a_0 = 1 + alpha/A).
- The adapter accepts the frequency, gain and Q in its input array. Its caller converts the Q-like integer to a floating value by dividing by 2, and gain/frequency scaling is determined by the calling settings class.
- It serializes five coefficients into a seven-integer command: subcommand, band index, then coefficient words and a final default/shift word.
- Coefficient conversion is signed 24-bit-style fixed-point with saturation and a selectable scaling exponent. The adapter also contains low-shelf, high-pass and low-pass coefficient generators.

This confirms that our existing RBJ peaking filter is aligned with a real automotive-DSP coefficient family. It does not justify using AK7604's integer packing on MT8163/CD3313.

### SI47925

- Sampling rate constant: 48,000 Hz.
- Supports multiple filter types in its coefficient utility: low-pass, high-pass, band-pass, all-pass, notch, peaking EQ, high-shelf and low-shelf.
- The utility supports Q, S and bandwidth parameter modes.
- It normalizes the denominator coefficients, scales numerator coefficients when their magnitude exceeds the supported range, and carries a separate shift value.
- Coefficients are converted to signed 24-bit fixed point with 22 fractional bits.
- The adapter serializes per-band data as command ID, band index, A1, A2, B0, B1, B2 and shift.
- Its crossover command can serialize up to 12 biquad sections for low-pass and high-pass processing. The source builds the remaining sections as identity filters.
- Speaker delay uses a separate fixed-point conversion path.

This is a useful architecture reference for future crossover/delay features, but the SI47925 word format and scaling are chip-specific.

### FY7604

The settings caller constructs a single-band command with fields equivalent to:

`[subcommand, bandIndex, gainTimes10, Q, frequencyHz, filterType, bypass]`

Bulk EQ uses a different command beginning with subcommand 2 and repeated band parameter tuples. The FY adapter forwards these arrays to status ID 137 without calculating coefficients in Java. Therefore, coefficient generation/interpretation occurs downstream in the vendor effect implementation or DSP firmware, which is not included in this APK.

### CSC37534

The adapter uses status ID 138 and sends chip-specific arrays for PEQ, loudness, balance and surround. The PEQ command contains multiple integer parameters per band. It is not compatible with the CD3313 four-channel attenuation command.

## 3. Gain transition behavior

`EqualizerController` maintains target band levels and supports:
- immediate synchronization;
- delayed commit;
- stepwise fade-in using a configurable step size and interval;
- interruption/cancellation of pending transitions.

This is a valuable UX/control concept. Our PCM engine should apply parameter changes without abrupt clicks. A DSP-side coefficient/state transition or short gain ramp is preferable to doing repeated Android UI-thread writes. The decompiled fade method is not a complete reference implementation to transplant; its decompilation contains control-flow reconstruction warnings.

## 4. Implications for MT8163 Audio Engine

### Reuse as engineering concepts

1. Keep the EQ model separate from each hardware transport.
2. Retain RBJ peaking coefficients as the baseline for the software PCM DSP.
3. Add low-shelf/high-shelf and high-pass/low-pass filter types as explicit, tested filter designs when the product requirements call for them.
4. Add safe coefficient normalization, headroom checks and parameter ramping.
5. Consider a future multi-section crossover and per-channel delay model, but only after the Android audio routing and output-channel map are verified.
6. Keep 44.1 kHz and 48 kHz coefficient generation tied to the actual negotiated sample rate. Do not reuse coefficients generated for a different rate.

### Do not reuse

- AK7604/FY7604/CSC37534/SI47925 command IDs or integer payload layouts.
- Their 24-bit coefficient packing, shift rules or vendor framework calls.
- Their preset curves or APK resources without establishing licensing and provenance.
- Any assumption that HEQ's external-DSP backend can control CD3313.

### Project decision

Continue with our own independent PCM DSP and a separate CD3313 control layer. HEQ provides confirmation of useful automotive DSP concepts (RBJ peaking EQ, shelving, crossover sections, delay, channel attenuation, and smoothed transitions), but does not supply a compatible MT8163 integration route.

The current MT8163 evidence still shows no verified userspace caller for `/dev/audiomux`, and the primary HAL does not expose a proven CD3313 control interface. Do not connect the PCM engine or issue CD3313 writes until the relevant Android integration point and ioctl ABI are verified.

## 5. Verification limits

- Static decompilation only; the HEQ APK was not executed.
- No HEQ runtime, vendor effect implementation, or DSP firmware was available for inspection.
- No license/source provenance was established.
- No head-unit playback, coefficient response measurement, CPU/latency measurement, or audible test was performed.
- The HEQ APK targets multiple external DSP chip families; its presence alone does not establish which backend, if any, is active on the target MT8163 unit.
