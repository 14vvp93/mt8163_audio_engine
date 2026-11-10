# PCM DSP Stability Lab

The Stability Lab is a host-side validation suite for the four-channel Float32
DSP core. It runs in GitHub Actions and does not require an MT8163 head unit.

## Coverage

- Frequency-response checks for peaking, low-shelf, high-shelf, high-pass and
  low-pass biquads.
- Passband/stopband checks for high-pass and low-pass filters.
- Repeated 32-slot, four-channel EQ reconfiguration with high-Q and maximum
  supported gain values.
- Abrupt parameter transitions across 600 processing blocks.
- Output finiteness and limiter-ceiling checks.
- Rejection of non-finite output-matrix coefficients.
- Sanitization of NaN and infinite PCM input samples.

## Run locally

```sh
cmake -S pcm-dsp -B build -DCMAKE_BUILD_TYPE=Release -DPCM_DSP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Interpretation and limits

A passing run establishes that the tested host build compiles and meets these
deterministic synthetic-signal assertions. It is not a measurement of sound
quality, THD+N, real-time deadline compliance, Android AudioFlinger latency,
MT8163 CPU load, or the behavior of the device's analog output stage.

The current tests also do not prove stability for every possible sequence of
user parameter changes. They exercise representative extreme transitions;
additional randomized/property-based testing and target-device profiling remain
planned.

## CI

Workflow: [PCM DSP Stability Lab](../.github/workflows/pcm-dsp-stability-lab.yml)
