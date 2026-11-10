# PCM DSP core (initial stage)

This library is an independent, host-buildable C++17 DSP core. It is not yet connected to AudioFlinger or the MTK HAL.

## Implemented

- interleaved signed PCM16 stereo processing;
- 44.1 kHz and 48 kHz configuration;
- 10 configurable biquad bands:
  - RBJ peaking EQ;
  - low-shelf and high-shelf;
  - second-order high-pass and low-pass;
- shelf slope control (RBJ S parameter, clamped to 0.1..1.0);
- preamp gain;
- stereo balance;
- linked stereo peak limiter with attack/release envelope and hard safety ceiling;
- exact bit-preserving bypass when disabled;
- fixed filter state and no heap allocation in the processing method;
- linear coefficient transitions across each processing block when EQ parameters change.

## Real-time contract

The processing method does not allocate memory or acquire locks. The caller must pass an immutable Parameters snapshot for the duration of each call. Parameter ownership/snapshot transfer will be implemented in the Android adapter, not hidden inside the audio callback.

Call configure() before processing. A sample-rate change resets all filter and limiter state. Unsupported rates are rejected.

Filter coefficients are normalized before processing. Filter frequency is constrained below Nyquist, Q and gain are clamped, and shelf slope is constrained to the RBJ-supported range. Peaking and shelf filters with zero gain reduce to identity; HPF/LPF do not use gain_db.

When parameters change, the current normalized biquad coefficients are linearly interpolated to the new target over the next supplied processing block. This avoids an instantaneous coefficient step. It is a first-stage transition strategy; device listening and numerical stress tests are still required before production use.

## Current limits

- Only PCM16 stereo is supported.
- The first integration target is primary media/music output.
- No AudioFlinger effect registration, HAL patch, global hook, SCO/call processing, microphone path, resampling, multichannel routing, or CD3313 control is implemented here.
- The limiter is an initial safety-oriented peak limiter, not a final mastering limiter.
- Preamp and balance currently update at block boundaries; smoothing them is a separate follow-up.
- No Android/head-unit runtime or audible validation has been performed.

## Host tests

The test-pcm-dsp.yml workflow builds the library with CMake and runs host-side tests for bypass, preamp, EQ, limiter ceiling, supported sample rates, all five filter types, and coefficient transitions.
