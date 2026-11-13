# MT8163 AudioCore foundation

This directory contains the first host-testable primitives for the unified MT8163 Audio Engine. It is deliberately separate from the experimental Android effect adapter.

## Current scope

- Session identity is now generation-bearing across SessionRegistry, AudioCore snapshots, PcmStreamBroker, PrimaryHalStreamAdapter and PcmByteStreamAdapter. The external 32-bit handle is accepted only at open; every subsequent mutation and transfer requires the complete `SessionToken`. Same-handle reopen, stale-token, foreign-registry and generation-exhaustion regressions are present. The implementation contract is [AudioCore Session Token Contract](../docs/AUDIOCORE_SESSION_TOKEN_CONTRACT.md). HAL integration remains blocked until the [AudioCore P1 Session Identity Gate](../.github/workflows/audiocore-p1-session-token.yml) passes on the exact final commit.

- Stable source and route identifiers.
- Fixed-capacity multi-session registry with an explicit lifecycle state machine.
- Named mute owners independent from legacy CD3313 reason numbers.
- Separate master and per-source mute domains.
- Detached, read-only MuteSnapshot values for publishing state toward the PCM plane.
- Three-slot, single-writer/multi-reader SnapshotPublisher with bounded reader retries and non-blocking writer backpressure.
- Explicit mapping from AudioCore mute owners to the paired CD3313 legacy reason masks.
- Shared legacy radio reason 2 remains asserted until both RadioScan and RadioPacketMute owners release it.
- Fixed-size state with no dynamic allocation in the mute arbiter.
- Initial AudioCore coordinator that owns session lifecycle, mute state, audio mode and the single hardware-command broker, then publishes a fixed-size immutable snapshot to the PCM plane, including every active session's complete token, source, route and lifecycle state.
- Semantic hardware commands for source selection and master/per-source mute, with strict input validation, one non-owning backend attachment and explicit error propagation.
- An evidence-gated source/route policy distinguishes supported, unverified and invalid pairs. It keeps primary PCM, A2DP, HFP/SCO, FM direct I2S, FM capture PCM and CD3313 analog inputs as distinct paths; unverified combinations cannot be opened as sessions. The broker has no CD3313 ioctl numbers or guessed wire format; it is callable only from the serialized control context.
- A separate DSP-route visibility classifier distinguishes transport validity from software sample visibility: primary playback PCM, A2DP with offload control, HFP/SCO telephony bridge, microphone capture with synchronized echo reference, FM capture PCM, FM direct-I2S digital tap, and analog-only CD3313 inputs. `SystemSound` is a first-class source. An analog route being valid does not imply it is software-DSP-visible.
- Snapshot generation tracking and a dirty/retry contract when all inactive publisher slots are pinned.

The mute arbiter is a control-plane object. One serialized AudioCore control context owns mutations. It creates immutable snapshots and publishes them through SnapshotPublisher. The publisher uses three fixed slots and 32-bit atomics; a non-blocking writer gate rejects concurrent publishers, readers have a fixed retry bound, the writer never waits, and a failed acquisition must reuse the previous block snapshot. Android ARMv7 CI verifies the actual MuteSnapshot publisher instantiation and the lock-free 32-bit atomic requirement. The publisher is single-writer and multi-reader; it does not make MuteArbiter itself thread-safe.

## Validation

The CMake targets `mt8163_audio_core_tests`, `mt8163_audio_core_session_tests`, `mt8163_audio_core_coordinator_tests`, `mt8163_audio_core_hardware_tests` and `mt8163_audio_core_snapshot_tests` cover mute ownership, CD3313 reason aggregation, session lifecycle, fixed capacity, snapshot slot pinning and concurrent readers. The snapshot test runs 100,000 writer updates with two readers. They are included in the `pcm-dsp` host CMake test suite.

## Not implemented yet

No factory HAL, AudioPolicy, CD3313 transport backend, FM, A2DP, HFP/SCO or vehicle adapter is connected here. The hardware broker is an interface and host-tested fake backend only; it does not open `/dev/audiomux`, issue ioctls or touch hardware. AudioCore is currently a host-testable control-state coordinator, not yet a complete route engine or firmware replacement. The library does not issue device I/O and does not change any live audio route.
