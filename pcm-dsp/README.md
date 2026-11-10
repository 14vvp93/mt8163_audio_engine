# PCM DSP core (initial stage)

This library is an independent, host-buildable C++17 DSP core. It is not yet connected to AudioFlinger or the MTK HAL.

## Implemented

- interleaved signed PCM16 stereo processing;
- 44.1 kHz and 48 kHz configuration;
- 10 parametric peaking EQ bands (RBJ biquad coefficients);
- preamp gain;
- stereo balance;
- linked stereo peak limiter with attack/release envelope and hard safety ceiling;
- exact bit-preserving bypass when disabled;
- fixed filter state and no heap allocation in the processing method.

## Real-time contract

The processing method does not allocate memory or acquire locks. The caller must pass an immutable Parameters snapshot for the duration of each call. Parameter ownership/snapshot transfer will be implemented in the Android adapter, not hidden inside the audio callback.

Call configure() before processing. A sample-rate change resets all filter and limiter state. Unsupported rates are rejected.

## Current limits

- Only PCM16 stereo is supported.
- The first integration target is primary media/music output.
- No AudioFlinger effect registration, HAL patch, global hook, SCO/call processing, microphone path, resampling, multichannel routing, or CD3313 control is implemented here.
- The limiter is an initial safety-oriented peak limiter, not a final mastering limiter.
- EQ coefficient changes currently take effect at a block boundary; parameter ramping/click-free transitions are a later task.

## Host tests

The test-pcm-dsp.yml workflow builds the library with CMake and runs host-side tests for bypass, preamp, EQ, limiter ceiling and supported sample rates.
