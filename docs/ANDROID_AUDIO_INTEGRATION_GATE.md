# Android audio integration gate: channel topology and real-time contract

## Status

**Integration remains blocked at the PCM boundary.** The DSP core is buildable and host-tested, but the available static evidence does not yet establish that the MTK HAL filter callback carries four-channel PCM in the format required by the current engine.

This document is a safety gate, not a claim that Android integration is complete.

## 1. Verified interfaces

### Android policy side

The supplied Android 9 primary audio policy declares the primary output as PCM16, stereo, at 44.1/48 kHz. It also declares other routes and formats (including SCO, A2DP, USB, FM and voice-call paths). Policy declarations are not proof of the actual buffer layout at every internal HAL stage.

### MTK HAL side

The primary HAL exports:

- `AudioMTKFilterManager::process(void*, unsigned int, void*, unsigned int)`
- `AudioMTKFilterManager::setParameter(unsigned int, void*)`
- `AudioMTKFilter::setFilterParam(unsigned int, unsigned int, int)`
- ACF/HCF update and preview functions.

The process symbol is a candidate boundary, but its four arguments' semantic types, PCM sample format, channel count, frame count, stride, and call-site ownership are not yet fully recovered. The ACF/HCF functions are parameter-update paths; they must not be mistaken for the PCM processing callback.

### DSP side

The production core currently exposes:

- `QuadEngine`: interleaved normalized Float32 FL/FR/RL/RR (4 channels).
- `ProfiledQuadEngine`: two preconfigured engines and a crossfade, with one control producer and one audio consumer.
- The audio processing path uses fixed-size scratch buffers, no locks, and no heap allocation.
- `AudioProfile` validation occurs before an entire profile is prepared and published.
- The JNI bridge is an offline/bring-up interface. Java array pinning/copying and synchronized calls make it unsuitable for a real-time HAL callback.

## 2. The unresolved topology mismatch

There is currently a mismatch between the known outer policy and the DSP core:

| Layer | Known representation |
|---|---|
| Android primary output policy | PCM16 stereo, 44.1/48 kHz |
| Candidate MTK filter process function | Opaque pointers and lengths; actual buffer layout unresolved |
| Current four-channel DSP core | Float32 interleaved FL/FR/RL/RR |
| CD3313 control path | Separate device/control ABI; not the PCM stream itself |

Do **not** solve this by blindly duplicating stereo L/R into FL/FR/RL/RR, downmixing the four-channel result back to stereo, or assuming the CD3313 receives a particular bus layout. Any of those could silently remove rear-channel independence, alter gain, or process the wrong stream.

The four-channel requirement can only be connected safely after one of these is demonstrated from the target HAL/bus:

1. The MTK filter boundary really carries four independent channels and its sample format/stride are recovered; or
2. A documented downstream four-channel PCM boundary exists before the CD3313/amplifier; or
3. The architecture is deliberately changed to a stereo DSP stage plus separately validated CD3313 per-speaker controls. This third option is not equivalent to four independent software EQ channels and must be treated as a product decision, not a transparent adapter.

## 3. Required reverse-engineering evidence before coding the HAL adapter

1. Recover all call sites of `AudioMTKFilterManager::process`, not just its symbol body.
2. Trace the producer of each input buffer and consumer of each output buffer.
3. Identify the actual sample type (PCM16, packed PCM, Float32, or another format), channel count, frame count, interleaving, and byte lengths.
4. Identify whether the function runs on the primary music path, and whether it is invoked for notifications, FM, USB, HDMI, or other routes.
5. Recover filter-manager construction/registration and the lifetime/locking rules for each filter instance.
6. Confirm whether the output buffer is in-place, separate, or partially overlapping.
7. Establish a bypass condition for SCO, voice calls, microphone capture, and all unsupported formats/routes.
8. Only then implement a thin adapter with explicit format conversion, bounded block sizes, preallocated scratch, no callback locks/allocations/logging, and exact bit-transparent bypass.

## 4. Runtime and failure-safety requirements

The eventual adapter must:

- leave the stock MTK HAL and its routing/device lifecycle intact;
- default to bypass until explicitly enabled;
- reject unsupported formats without modifying the input buffer;
- preserve the original output on DSP failure (or use a proven fail-open copy path);
- never process capture/microphone or call/SCO streams in the first release;
- avoid loading or mutating CD3313 register state from the PCM callback;
- use a control-thread-to-audio-thread immutable profile handoff;
- expose a hard disable/recovery mechanism that does not require replacing the vendor HAL.

No HAL replacement, AudioFlinger hook, global effect registration, or live CD3313 write should be attempted on the strength of the current static evidence alone.

## 5. ProfiledQuadEngine concurrency contract

The current implementation uses a single-control-producer/single-audio-consumer ownership model:

- Call `configure()` before starting audio processing.
- Call `apply_profile()` from one control thread only.
- Call `process()` from exactly one audio thread.
- Do not call `configure()` concurrently with processing.
- Do not destroy the engine while the audio thread can still access it.
- A profile update is rejected while the second engine slot is pending or transitioning.
- Keep processing blocks flowing until a transition completes; otherwise the retired engine slot cannot be reused.

The Java test wrapper serializes its methods and is not a model for the production callback. The production adapter must call the C++ engine directly and implement its own lifecycle-safe control handoff.

## 6. Current decision

Proceed with static call-site and buffer-layout recovery. Do not create a guessed HAL hook or claim four-channel system integration yet. The next implementation should be the smallest adapter that matches a proven native buffer contract, not a speculative replacement for the vendor audio stack.
