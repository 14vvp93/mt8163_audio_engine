# PCM DSP core

This library is an independent, host-buildable C++17 DSP core. It is not yet connected to AudioFlinger or the MTK HAL.

## Implemented cores

### Legacy stereo core

- interleaved signed PCM16 stereo;
- 44.1 kHz and 48 kHz;
- 10 configurable RBJ biquad bands: peaking, low/high shelf, second-order HPF/LPF;
- preamp, balance, linked stereo peak limiter;
- bit-exact bypass and no heap allocation in the processing method.

### Four-channel Float32 foundation

- interleaved Float32 FL/FR/RL/RR processing;
- 44.1 kHz and 48 kHz;
- 32 independent biquad slots per channel (128 possible channel/filter stages);
- per-channel gain and independent filter settings;
- peaking, low/high shelf, second-order HPF/LPF;
- per-channel peak envelope and configurable limiter ceiling;
- bit-exact Float32 bypass;
- no heap allocation or locks in the processing method;
- non-finite input samples are treated as silence while DSP is enabled;
- parameter changes use a dual-filter-bank state copy and output crossfade instead of linearly interpolating normalized denominator coefficients.

The dual-bank transition is intentionally conservative: the new filter bank starts from a copy of the active filter states, and old/new outputs are crossfaded over the supplied block. This avoids relying on linear interpolation of IIR denominator coefficients, which can produce unstable intermediate filters for some parameter changes. Callers should provide reasonably sized audio blocks; transition duration is one block.

## Real-time contract

Call configure() before processing. A sample-rate change resets filter and limiter state. Unsupported rates are rejected. Parameters must remain immutable for the duration of each process call. Parameter snapshots and thread-safe handoff belong in the future Android adapter, not in the audio callback.

The Float32 API expects normalized samples. When limiting is enabled, each channel is constrained to the configured ceiling. With limiting disabled, output is still hard-clamped to [-1, 1] before handing samples to a PCM sink.

## Current limits

- The Float32 four-channel core is a processing foundation, not an Android integration.
- The legacy stereo PCM16 API remains available and unchanged.
- No AudioFlinger effect registration, HAL adapter, global hook, SCO/call processing, microphone path, resampling, CD3313 control, crossover module, time alignment, dynamic EQ, or multiband compression is connected yet.
- The limiter is a safety limiter, not a mastering-grade loudness processor.
- No Android/head-unit runtime or audible validation has been performed.

## Host tests

The host workflow builds the library with CMake and runs tests for stereo behavior, all filter types, Float32 four-channel bypass, independent channel gains, all 32 filter slots, repeated extreme parameter transitions, finite output, and limiter ceiling.


## Four-channel utility modules

### Time alignment

\`QuadTimeAlignment\` provides independent FL/FR/RL/RR delays from 0 to 20 ms at 44.1/48 kHz. It uses fixed-size ring buffers, performs no allocation or locking in process(), and crossfades delay-tap changes over one block.

### Linkwitz-Riley crossover

\`LinkwitzRileyCrossover\` is a mono splitter; instantiate one per channel. It supports 12, 24, 36 and 48 dB/octave Linkwitz-Riley alignments built from cascaded Butterworth sections. The high branch polarity is corrected for odd prototype order. The branches are tested for a flat summed response at the crossover frequency.

Both modules are standalone building blocks at this stage; the QuadEngine pipeline integration is a subsequent step.
