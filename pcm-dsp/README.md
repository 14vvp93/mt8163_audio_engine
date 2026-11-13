# PCM DSP core

This library is an independent, host-buildable C++17 DSP core. It is not yet connected to AudioFlinger or the MTK HAL.

## Implemented cores

### Legacy stereo core

- interleaved signed PCM16 stereo;
- 32 kHz, 44.1 kHz and 48 kHz (32 kHz added for the MT6627 FM PCM rate);
- 10 configurable RBJ biquad bands: peaking, low/high shelf, second-order HPF/LPF;
- preamp, balance, linked stereo peak limiter;
- bit-exact bypass and no heap allocation in the processing method;
- stable parameter changes use parallel old/new IIR banks with an output crossfade; normalized denominator coefficients are never linearly interpolated.

### MT6627 FM capture adapter scaffold

`FmPcmAdapter` wraps the shared stereo engine for the recovered FM input
profile. It deliberately accepts only 32 kHz PCM16 stereo, and only processes
when the caller explicitly confirms that the FM route is active and Android
is in normal audio mode. Disabled DSP, inactive routes, non-normal modes,
unsupported sample rates and unsupported channel layouts are bit-exact
bypass. The adapter is a platform-independent PCM boundary component; it is
not yet called by the target's vendor HAL or the direct FM_I2S AudioPatch
playback path.

### Normal playback pre-fold adapter scaffold

`NormalPlaybackPcmAdapter` is a host-side fail-open harness for the recovered
Normal playback insertion boundary after DC removal and before the factory's
conditional stereo-to-mono fold.

It accepts only interleaved PCM16 stereo at sample rates accepted by
`Engine::configure()`. Processing requires an explicit caller-supplied
`PrimaryMedia` route, normal audio mode, matching sample rate/channel/format,
DSP enabled, and APDMNR tuning disabled. Voice call, Bluetooth SCO, FM,
HDMI, direct/offload, unknown routes, unsupported formats, mismatched
configurations and disabled DSP are bit-exact bypass.

The route enum is a policy input, not an automatic detector. No Android HAL
callback or AudioFlinger effect currently supplies it. The caller must not
label a route `PrimaryMedia` until the target route and stream contract are
verified. PCM32 remains bypassed even though the factory mono-fold helper
recognizes PCM32; the existing stereo DSP engine is PCM16-only.

The adapter uses no locks or dynamic allocation in `process()`, resets DSP
state when leaving an eligible route, supports exact in-place processing,
and uses overlap-safe bypass copying. Partial-overlap buffers bypass rather
than entering the DSP. This is a host integration scaffold, not a device
integration or an approval to patch the factory HAL.

### Four-channel Float32 foundation

- interleaved Float32 FL/FR/RL/RR processing;
- 44.1 kHz and 48 kHz;
- 32 independent biquad slots per channel (128 possible channel/filter stages);
- per-channel gain, independent filter settings, and integrated 0–20 ms time alignment;
- optional 4×4 output matrix with explicit row-major routing coefficients;
- optional four-way Linkwitz-Riley bass management (12/24/36/48 dB/oct) with four high-passed speaker outputs and a separate mono low-frequency bus;
- independent safety limiting for the four main outputs and the mono low-frequency bus;
- peaking, low/high shelf, second-order HPF/LPF;
- per-channel peak envelope and configurable limiter ceiling;
- bit-exact Float32 bypass;
- no heap allocation or locks in the processing method;
- non-finite input samples are treated as silence while DSP is enabled;
- parameter changes use a dual-filter-bank state copy and output crossfade instead of linearly interpolating normalized denominator coefficients.

The dual-bank transition is intentionally conservative: the new filter bank starts from a copy of the active filter states, and old/new outputs are crossfaded over the supplied block. This avoids relying on linear interpolation of IIR denominator coefficients, which can produce unstable intermediate filters for some parameter changes. Callers should provide reasonably sized audio blocks; transition duration is one block.

## Real-time contract

Call configure() before processing. A sample-rate change resets filter and limiter state. Unsupported rates are rejected. Parameters must remain immutable for the duration of each process call. Parameter snapshots and thread-safe handoff belong in the future Android adapter, not in the audio callback.

The Float32 API expects normalized samples. Per-channel delay is configured in milliseconds and is applied after EQ and before the output limiter. Delay-tap changes crossfade over one process block. When limiting is enabled, each channel is constrained to the configured ceiling. With limiting disabled, output is still hard-clamped to [-1, 1] before handing samples to a PCM sink.

## Current limits

- The Float32 four-channel core is a processing foundation, not an Android integration.
- The legacy stereo PCM16 API remains available and unchanged.
- No AudioFlinger effect registration, HAL adapter, global hook, SCO/call processing, microphone path, resampling, or CD3313 control is connected yet.
- The mono low-frequency bus is a software output only. Its physical CD3313/amplifier route has not been identified, so it is deliberately not connected to hardware.
- The limiter is a safety limiter, not a mastering-grade loudness processor.
- No Android/head-unit runtime or audible validation has been performed.

## Host tests

The host workflow builds the library with CMake and runs tests for the FM adapter's 32 kHz stereo processing and bit-exact fail-open gates, 32 kHz FM-rate engine processing, 44.1/48 kHz stereo behavior, all filter types, Float32 four-channel bypass, independent channel gains, all 32 filter slots, repeated extreme parameter transitions, finite output, and limiter ceiling. Multiband tests additionally verify bit-exact bypass, independent low/mid/high compression on separate channels, preservation of unselected bands, in-place equivalence, invalid crossover rejection, and finite output for non-finite input.


## Four-channel utility modules

### Time alignment

`QuadTimeAlignment` provides independent FL/FR/RL/RR delays from 0 to 20 ms at 44.1/48 kHz. It uses fixed-size ring buffers, performs no allocation or locking in process(), and crossfades delay-tap changes over one block.

### Linkwitz-Riley crossover

`LinkwitzRileyCrossover` is a mono splitter; instantiate one per channel. It supports 12, 24, 36 and 48 dB/octave Linkwitz-Riley alignments built from cascaded Butterworth sections. The high branch polarity is corrected for odd prototype order. The branches are tested for a flat summed response at the crossover frequency.

The crossover is integrated into the optional QuadEngine bass-management stage. The output matrix is applied after crossover splitting and before the final output limiters. Configure routing outside the real-time process callback with `configure_routing()`. When bass management is enabled, pass a separate mono `sub_output` buffer to `process()`; the call fails rather than silently discarding low-frequency content if that buffer is missing. The default routing remains disabled and therefore preserves the four-channel signal path.

### Multiband dynamics

`MultibandDynamicsProcessor` splits each FL/FR/RL/RR channel into low, mid and high bands using two cascaded Linkwitz-Riley 24 dB/octave crossovers. Each of the 12 channel/band compressors has independent threshold, ratio, attack, release, soft-knee and makeup-gain settings. The defaults are bypassed. Processing is interleaved Float32, supports in-place operation, sanitizes non-finite input, and performs no heap allocation or locking. Configure outside the audio callback; reconfiguration resets crossover and detector state. `QuadEngine::configure_dynamics()` configures this stage; when enabled, it runs after the 32-band channel EQ and before time alignment, output routing, and the final safety limiter. The front and rear pairs, as well as individual channels, can be tuned independently. The Android audio path is not connected yet.


## Psychoacoustic Bass and Clarity enhancement

The optional four-channel enhancement stage provides two independent harmonic
exciters inspired by common psychoacoustic bass and clarity processors:

- **Bass Enhancement** applies a low-pass analysis path (40–160 Hz) and a
  restrained cubic harmonic generator. It adds upper harmonics that can make
  bass notes more perceptible on small automotive speakers without simply
  boosting the lowest frequencies.
- **Clarity Enhancement** extracts content above a selectable 1.5–8 kHz focus
  point and adds a low-level nonlinear harmonic component to increase presence.
  It is deliberately conservative to reduce harshness and listening fatigue.

Both controls are independently enabled, have normalized 0–100% intensity,
are configured as part of the validated AudioProfile, and participate in the
existing profile crossfade. The processor uses fixed per-channel state, no
heap allocation, and no locks in process(). It is an original lightweight
implementation, not copied ViPER4Android code. These are early DSP controls:
the default is OFF, and final tuning requires listening tests and measurement
on the actual head unit and speakers. The stage is not yet connected to the
Android system audio path.

## Unified audio profile API

`audio_profile.hpp` defines a versioned, platform-independent `AudioProfile`
snapshot that groups the four-channel EQ/gain/time-alignment/limiter parameters,
output routing and bass-management settings, and all 12 multiband compressor
configurations. `validate_audio_profile(profile, sample_rate_hz)` validates
the complete snapshot before an adapter applies any part of it. It rejects
unknown schema versions, unsupported sample rates, out-of-range values,
non-finite values, invalid filter/crossover settings, and delays beyond the
fixed time-alignment buffer.

The profile is a data contract, not a persistence format or a real-time
thread-safe handoff. The future Android adapter must publish immutable
snapshots safely and apply configuration outside the audio callback. Profile
validation deliberately does not mutate the engine, so a rejected profile
cannot leave the DSP partially reconfigured.

## Safe profile transitions

`ProfiledQuadEngine` prepares a complete profile on a second DSP instance,
then crossfades the old and new processing paths over a configurable interval
(default 50 ms, accepted range 1–1000 ms). The control thread calls
`apply_profile()`; one audio thread calls `process()`. The audio callback
uses fixed scratch buffers, performs no heap allocation, and takes no locks.
A second update is rejected while the transition is pending or running, and
the retired engine slot becomes reusable only after the crossfade completes.
Both main and optional subwoofer outputs are crossfaded. Processing accepts
blocks larger than the internal scratch size by splitting them into bounded
chunks.

This API is a single-control-producer / single-audio-consumer contract.
Configure the engine before starting audio processing. Do not call
`configure()` concurrently with `process()`, and do not use multiple audio
threads on one instance. This is an in-process transition mechanism; persistent
profile storage and JNI synchronization are separate integration tasks.

## Android JNI bring-up bridge

Enable `PCM_DSP_BUILD_ANDROID_JNI=ON` in an Android NDK CMake build to produce `libmt8163_audio_jni.so` alongside the static DSP library. The Java wrapper is in `android/java/com/mt8163/audioengine/NativeDsp.java`; include it in a test APK and package the generated ARMv7 shared library under `jniLibs/armeabi-v7a/`.

The bridge exposes engine creation at 44.1/48 kHz, enable/disable, per-channel gain, and interleaved FL/FR/RL/RR Float32 processing through `ProfiledQuadEngine`. Every control change is submitted as a complete validated `AudioProfile` snapshot and crossfaded over the default 50 ms transition. A new control update is rejected while the previous transition is pending or running; the test UI therefore commits channel gain when the user releases the slider instead of flooding the control thread during a drag. It is an offline/bring-up interface only: Java array pinning/copying is not a real-time audio callback strategy. Production integration must call the C++ engine directly from a verified native AudioFlinger/HAL integration point, preserving the stock routing and bypassing calls, microphone, and Bluetooth SCO until those paths are separately mapped.

## Validation

The release benchmark exercises the full QuadEngine chain at 44.1/48 kHz and 128/256/512-frame blocks, including all 32 EQ slots per channel, all 12 multiband compressor instances, time alignment, bass management, and output limiting. It reports average, p95, maximum block time, and average one-core real-time budget usage on the host runner. Host timing is a regression indicator, not a substitute for measurement on the MT8163 target.

See [PCM DSP Stability Lab](../docs/PCM_DSP_STABILITY_LAB.md) for the host-side frequency-response, extreme-transition, and input-sanitization tests.


### 32-band logarithmic PEQ layout

`make_logarithmic_eq_layout_32()` creates 32 center frequencies spaced evenly
on a logarithmic scale from 20 Hz to 20 kHz by default. The generated bands
start disabled with 0 dB gain, peaking type, and Q=1.0. Copy the layout to each
of the four channel parameter arrays, then edit each channel independently.
The helper validates the frequency range and Q before changing the output
array. This is a parameter-layout utility; it does not add processing stages
beyond the 32 biquad slots already present in each channel.


## MT8163 / CD3313EO hardware boundary

The production integration must respect the actual division between the SoC
PCM path and the external analog processor:

- The recovered MT8163 Normal playback path is PCM16 stereo at the policy
  boundary and remains a two-channel ALSA path. The verified stereo-preserving
  DSP candidate is before the factory speaker mono-fold.
- CD3313EO is a digitally controlled **analog** audio processor. Its
  datasheet specifies three stereo analog input pairs, four analog outputs,
  input selection/gain, master volume, bass, treble, loudness and four
  independent output attenuators. It is not a four-channel PCM/I2S DSP.
- The CD3313EO control interface is I2C, 7-bit address 0x44, up to 100 kbit/s.
  Its recommended supply is 6–10 V (typical 9 V; absolute maximum 10 V).
  These electrical values apply to the chip supply, not to the MT8163 SoC.
- The CD3313 four-output attenuation command controls analog output levels;
  it does not establish four independent digital PCM/EQ channels.
- Keep PCM processing and CD3313 control separate. The PCM callback must
  never issue I2C, ioctl, GPIO, Binder or device-open operations.
- Avoid applying the same tone/volume function twice. The profile/control
  layer must define whether a given EQ, bass, treble, loudness or gain stage
  belongs to software DSP or to CD3313EO for each route.

For the exact register encoding, package pins, electrical limits and
command ranges, use
[CD3313EO hardware and driver reference](../docs/CD3313EO_HARDWARE_AND_DRIVER_REFERENCE.md).
Do not infer board wiring, amplifier topology, analog rail sequencing or
subwoofer connectivity from the chip datasheet.
