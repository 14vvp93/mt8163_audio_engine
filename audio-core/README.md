# MT8163 AudioCore foundation

This directory contains the first host-testable primitives for the unified MT8163 Audio Engine. It is deliberately separate from the experimental Android effect adapter.

## Current scope

- Stable source and route identifiers.
- Named mute owners independent from legacy CD3313 reason numbers.
- Separate master and per-source mute domains.
- Explicit mapping from AudioCore mute owners to the paired CD3313 legacy reason masks.
- Shared legacy radio reason 2 remains asserted until both RadioScan and RadioPacketMute owners release it.
- Fixed-size state with no dynamic allocation in the mute arbiter.

The mute arbiter is a control-plane object. A future AudioCore control context will own mutations and publish an immutable snapshot to the PCM callback. This class is not a concurrent multi-writer object and must not be accessed unsafely from the real-time thread.

## Validation

The CMake target `mt8163_audio_core_tests` covers independent mute owners, master/source scope, idempotent set/clear behavior and invalid identifiers. It is included in the `pcm-dsp` host CMake test suite.

## Not implemented yet

No HAL, AudioPolicy, CD3313, FM, A2DP, HFP/SCO or vehicle adapter is connected here. The library does not issue device I/O and does not change any live audio route.
