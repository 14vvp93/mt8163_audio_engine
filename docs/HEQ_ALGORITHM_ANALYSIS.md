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

### CD3313 ASP path (distinct from the external-DSP adapters)

The same APK has a separate ASP path that explicitly recognizes CD3313:

- EqUtils.ASP_CHIP_3313 is the literal "cd3313".
- The chip name is selected from ro.hw.dsp.chip.
- hasAsp() is true for pt2348 and cd3313.
- MainActivity creates the ASP equalizer and balance UI for this path.
- AspSettings obtains android.audio.AudioEffect.getInstance() and calls the vendor methods setBalance(), setLoud(), setSubWoofer(), setBassBoost(), setTreble(), and setMasterVolume().

This is a high-level CD3313-aware client path, separate from the AK7604/FY7604/CSC37534/SI47925 adapters above. The APK does not contain the implementation of android.audio.AudioEffect, so the underlying native/service transport and exact ioctl mapping remain unavailable.

#### ASP equalizer is not a 12-band hardware PEQ

AspFragment exposes 12 seek bars, but each drag recalculates two averages:
- bass = integer average of UI bands 0..5;
- treble = integer average of UI bands 6..11.

It then calls AspSettings.setAspBassBoost(bass, false) and setAspTreble(treble, false). The 12 values are also stored as UI profile data. Therefore the visible 12-band UI does not prove that CD3313 receives 12 independent EQ bands; the observed ASP path reduces them to two coarse controls.

#### Four-channel balance path

AspSettings.setAspBalance() computes four integer output levels in FL, FR, RL, RR order from the balance/fader coordinates and calls AudioEffect.setBalance(FL, FR, RL, RR). The values are in a 0..15 range, matching the kernel's four-index 0x15 command and its 16-entry speaker attenuation table. This is a strong semantic correspondence, but the hidden framework method must be recovered before declaring a proven Java-method-to-ioctl mapping.

After setting the four speaker values, the app also calls setMasterVolume(left, right) with:
- left = max(FL, RL) / 15.0
- right = max(FR, RR) / 15.0

This appears intended to coordinate left/right master gain with the per-speaker balance values. The kernel module currently exposes one global volume_percentage variable, so the exact route for the two-float API is unresolved.

#### Other ASP calls and kernel correlations

| HEQ API call | Observed app arguments | Possible driver correspondence | Confidence |
|---|---|---|---|
| setBalance(FL, FR, RL, RR) | Four 0..15 levels | Command 0x15, four speaker indices | Strong semantic match; transport unverified |
| setLoud(value) | 0/1 | Command 0x56 toggles a control bit | Likely semantic match; transport unverified |
| setSubWoofer(value, 20) | Enable/value plus constant | Command 0xA3 reads one integer and changes GPIO 409 | Likely semantic match; transport unverified |
| setBassBoost(value, 200) | Value plus constant | Command 0x55 transition routine | Mapping unresolved; kernel uses second payload word |
| setTreble(value, Integer.MAX_VALUE) | Value plus sentinel | Command 0x5C transition routine | Mapping unresolved; kernel uses second payload word |
| setMasterVolume(left, right) | Two floats | Master-volume/percentage path | Unresolved; kernel has one named percentage global |

The 0x55 and 0x5C kernel paths consume the second payload word to calculate an internal table index. The Java calls pass constants in that position, so the hidden AudioEffect wrapper must transform/reorder arguments or use a different wire representation. Do not directly equate the Java parameters with raw ioctl payload words.

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
- Any assumption that the external-DSP adapters for other chips can control CD3313. The separate ASP path explicitly recognizes CD3313, but its hidden framework transport is still unknown.

### Project decision

Continue with our own independent PCM DSP and a separate CD3313 control layer. HEQ provides confirmation of useful automotive DSP concepts (RBJ peaking EQ, shelving, crossover sections, delay, channel attenuation, and smoothed transitions), but does not supply a compatible MT8163 integration route.

The HEQ APK is now confirmed as a high-level CD3313-aware ASP client, but no direct `/dev/audiomux` caller or method-to-ioctl transport has been verified. The matching android.audio.AudioEffect framework implementation is still missing. Do not connect the PCM engine or issue CD3313 writes until that integration point and the ioctl ABI are verified.

## 5. Verification limits

- Static decompilation only; the HEQ APK was not executed.
- No HEQ runtime, android.audio.AudioEffect framework implementation, native/service transport, or DSP firmware was available for inspection.
- No license/source provenance was established.
- No head-unit playback, coefficient response measurement, CPU/latency measurement, or audible test was performed.
- The HEQ APK targets multiple external DSP chip families; its presence alone does not establish which backend, if any, is active on the target MT8163 unit.
