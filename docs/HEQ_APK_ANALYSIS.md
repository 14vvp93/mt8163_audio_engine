# HEQ APK static analysis

## Source and integrity

- Source: split archive in `HEQ/HEQ.zip.001` through `HEQ.zip.017`.
- GitHub Actions run: [42812564361](https://github.com/14march93/mt8163_audio_engine/actions/runs/42812564361) — completed successfully.
- Artifact: `heq-reassembled-and-extracted` (artifact ID `13763051324`).
- Reassembled `HEQ.zip` SHA-256: `6d98a4e6594fa65ed8f046c34a2f3e41f3a09be505f93600d38bc8b5cb13e5a4`.
- Extracted APK: `HEQ.apk`, 168,811,733 bytes.
- APK SHA-256: `a7c6e1163b56270b506a4b8b6f1ae763a4c4ecc9fae5b0a498aeea46968759b3`.
- APK package string found in DEX: `com.auto.hequalizer`.
- DEX: one `classes.dex`, 5,413,032 bytes.
- APK ZIP contains 6,115 entries. No `lib/` native libraries and no `assets/` or `res/raw/` payloads were found in the APK listing.

## What HEQ contains

Static DEX string/class inspection identifies a fairly broad head-unit equalizer/control application:

- Internal software EQ implementation classes: `com/auto/hequalizer/Biquad`, `Complex`, `EqualizerController`, `EqualizerSurface`, `EqualizerTransaction`.
- Standard Android effects are referenced: `android.media.audiofx.Equalizer`, `BassBoost`, and `Virtualizer`.
- EQ preset/frequency constants include 14-, 32-, and 48-band layouts, with named frequency constants from 20 Hz to 20 kHz.
- Bass/treble controls and bass boost are present.
- External DSP feature families include band EQ, balance, attenuation, delay, surround, reverb, DBB/bass boost, and high/low-pass filtering (HLPF).
- DSP-specific Java adapter classes are present for `Ak7604`, `CSC37534`, `FY7604`, and `SI47925`, plus `NativeHelper` and `IEq`.
- The DEX contains chip-specific command identifiers and settings/file-key constants such as `FY_CMD_SUB_ID_SET_EQ_EFECT`, `FY_CMD_SUB_ID_SET_PEQ_32ALL`, `FY_CMD_SUB_ID_SET_PEQ_SINGLE`, `FY_DSP_BAND_FILE`, `EXT_DSP_BAND_FILE`, and corresponding balance/delay/attenuation/surround/HLPF keys.

## CD3313 support discovered in the decompiled application

The full JADX output contains a concrete CD3313 selection path that was not visible in the earlier string-only scan:

- EqUtils defines ASP_CHIP_3313 = "cd3313".
- EqUtils.getEqChipType() reads the Android system property ro.hw.dsp.chip.
- EqUtils.hasAsp() returns true for either pt2348 or cd3313.
- MainActivity uses the ASP UI path when hasAsp() is true and creates the ASP equalizer and balance fragments.
- AspSettings obtains android.audio.AudioEffect.getInstance() and calls the vendor methods setBalance(FL, FR, RL, RR), setLoud(), setSubWoofer(), setBassBoost(), setTreble(), and setMasterVolume(left, right).

This is strong evidence that HEQ was designed to support a device configured as CD3313 through a vendor Android framework API. It is not merely a generic EQ app with unrelated DSP backends.

The ASP equalizer presents 12 UI bands, but AspFragment does not send 12 independent bands to the chip. It averages bands 0..5 into one bass value and bands 6..11 into one treble value, then calls setAspBassBoost() and setAspTreble(). The 12-band values are also persisted as UI state. This indicates that the CD3313 ASP path exposes coarse bass/treble hardware controls rather than a true 12-band hardware PEQ.

The balance path computes four integer speaker levels in FL/FR/RL/RR order and passes them to setBalance(). This closely matches the kernel driver's 0x15 four-speaker command shape and 0..15 speaker attenuation lookup. The semantic match is strong, but the actual mapping through android.audio.AudioEffect has not yet been proven.

The APK does not contain the implementation of android.audio.AudioEffect. The vendor framework class and its native/service transport are outside the decompiled APK. In particular:
- No direct /dev/audiomux open or ioctl call appears in the HEQ Java code.
- The framework implementation must be recovered before equating each Java method with a specific kernel command.
- setBassBoost(value, 200) and setTreble(value, Integer.MAX_VALUE) pass two arguments; the optimized kernel 0x55/0x5C handlers use the second payload word for their transition index. The wrapper may transform or reorder values, but that behavior is not available here.
- setMasterVolume(left, right) receives two floats, while the recovered kernel state currently exposes one volume-percentage global. Do not assume a direct one-to-one ioctl mapping.

### Useful design references

1. The app separates a generic EQ UI/data model from chip-specific adapters. This supports keeping our PCM DSP independent from the CD3313 controller.
2. Its Biquad/Complex classes are a lead for studying coefficient calculation and EQ-band handling. Our current PCM DSP already uses RBJ biquads, so we should compare formulas and stability/normalization choices rather than copy opaque implementation.
3. Its 14/32/48-band presets and 20 Hz–20 kHz frequency coverage are useful UI/product references. They do not mean our MT8163 path should immediately expose 48 bands; CPU cost, filter interaction, headroom and tuning must be measured.
4. Balance, attenuation, delay and crossover/HLPF are useful feature ideas. Our CD3313 analysis already establishes hardware four-channel attenuation-based balance/fader support, while PCM DSP can host software filters and time alignment if the final routing point is proven.
5. The app references Android's standard Equalizer/BassBoost/Virtualizer APIs. These APIs are not proof of whole-system processing; their session scope and actual effect implementation must be checked on our Android 9 image.

### What it does not prove

- The APK explicitly supports the CD3313 chip family through the hidden android.audio.AudioEffect API. However, it does not expose the implementation or prove a direct /dev/audiomux/ioctl transport, nor does it prove compatibility with every MT8163 firmware build.
- The identified external chip adapters target other DSP families (AK7604, CSC37534, FY7604, SI47925); they are not drop-in CD3313 drivers.
- No native `lib/` payload was found, but that does not rule out vendor services, framework APIs, shell/root file access, or external system components.
- Static DEX decompilation reveals the high-level CD3313 ASP call sites, but not the hidden framework implementation, exact file paths, ioctl wire format, coefficient scaling, or runtime behavior. Those require the matching system framework/native service.
- The APK's signing/redistribution rights and source provenance have not been established. Do not copy code or assets into our project until licensing is verified.

## Recommended next steps

1. Decompile `classes.dex` for the exact implementations of `Biquad`, `EqualizerController`, `EqualizerTransaction`, `NativeHelper`, and the four chip adapters.
2. Trace the command/data serialization for FY7604 and the other external DSP backends; compare their parameter semantics with our own PCM DSP and CD3313.
3. Extract default EQ frequency/Q/gain tables and preset curves for analysis only, recording their provenance and license status.
4. Keep the current architecture: independent PCM DSP plus a separately verified CD3313 transport. Do not integrate HEQ's chip adapters or claim CD3313 compatibility based only on this APK.

This is static APK inspection, not execution on the head unit. No device compatibility or audible improvement has been validated.
