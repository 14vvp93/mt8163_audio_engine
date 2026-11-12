# MT8163 Audio Engine — Product Goal and Replacement Scope

## Product definition

**MT8163 Audio Engine is one integrated audio system intended to replace the stock vendor audio implementation on the target MT8163 / Android 9 head unit.**

It is not a post-processing add-on, an optional effect layered over the factory engine, or a collection of independently deployed products. The HAL, policy/routing, audio core, PCM/mixer processing, DSP controls, CD3313 driver interface, source management, and state management are internal subsystems of one product. They must share one coherent configuration, lifecycle, ownership model, and validation strategy.

The implementation may be split into libraries, services, kernel code, and build targets for engineering reasons. That split does not change the product boundary: together they form **MT8163 Audio Engine**.

## Primary objective

Replace the factory vendor audio functionality that owns or controls the head unit's audio behavior, while preserving the Android-facing contracts and low-level platform interfaces required for a stable Android 9 system.

The finished engine must own the audio behavior end to end:
- Android audio integration and device/stream policy;
- source selection and routing;
- PCM stream handling, mixing, and digital processing;
- volume, mute, channel and tone control;
- CD3313 hardware control and error handling;
- state transitions, synchronization, recovery, and diagnostics.

There must be no competing factory component and no double ownership of a control. For each route, stream, hardware control, and state variable, the new engine must have one clearly defined owner.

## Replacement boundary

The replacement target is the **factory vendor audio implementation**, not Android's public audio APIs as a whole and not every low-level kernel component.

The project must investigate and, where required, replace or take ownership of:
- the vendor Audio HAL implementation and its vendor-specific control paths;
- vendor audio policy extensions and routing decisions;
- factory audio services, JNI/Binder bridges, and hidden control APIs that participate in audio behavior;
- factory DSP/effect control and processing paths;
- factory CD3313 userspace control and the cd3313.ko driver;
- vendor mixer/ALSA configuration and route setup where evidence shows they are part of the factory behavior being replaced.

Android AudioFlinger, AudioPolicyManager framework contracts, ALSA, kernel audio drivers, and other platform facilities are not automatically rewrite targets. Retain and use these interfaces where appropriate. Replace a lower-level platform component only when repository evidence and target-device testing demonstrate that it is necessary and feasible.

This distinction is intentional: **the factory audio implementation is replaced; the Android platform interfaces and necessary hardware foundations are preserved unless a proven requirement says otherwise.**

## Unified internal architecture

The following are internal subsystems of the single MT8163 Audio Engine product:

1. **Android integration** — compatible HAL-facing and policy-facing interfaces for Android 9.
2. **Audio core and state manager** — the authoritative owner of global audio state, transitions, synchronization, and recovery.
3. **Source and route manager** — music, Bluetooth A2DP, FM/Radio, AUX and other verified sources; route selection and priorities.
4. **PCM engine** — format/rate/channel negotiation, stream lifecycle, safe buffering, and PCM processing.
5. **Mixer engine** — controlled mixing of concurrent audio sources where the platform's actual signal paths permit it.
6. **DSP engine** — equalization and other digital effects, with explicit bypass and format-aware processing.
7. **Audio controls** — master/output volume, mute reasons, balance, supported tone controls, and profiles.
8. **CD3313 hardware subsystem** — the rewritten kernel driver plus its single authoritative userspace control path.
9. **Diagnostics and validation** — structured error reporting, tracing, test interfaces, and regression coverage.

These are design boundaries, not separate products or independently competing control planes. Their final process/library placement must be decided from the recovered Android 9 implementation and verified call paths.

## Non-negotiable engineering rules

- Preserve the original firmware, binaries, and configuration as read-only reference and rollback material.
- Recover the actual factory behavior before replacing it; do not infer undocumented behavior from names alone.
- Preserve required Android 9 interfaces and existing application compatibility.
- Give each audio control and state variable one owner. Avoid factory/new-engine double writes and conflicting volume, mute, routing, or source decisions.
- Do not remove a factory component until its responsibilities have been identified, implemented by the new engine, and covered by regression tests.
- Do not guess CD3313 registers, GPIO polarity, board wiring, ALSA endpoints, ACF/HCF layouts, or undocumented ABI details.
- Propagate hardware and transport failures. Do not report success when only a queue submission or cached-state update has occurred.
- Keep unverified paths fail-safe; calls, microphone capture, Bluetooth SCO, navigation, FM, and A2DP must each be mapped and tested rather than assumed to share one path.
- Maintain a factory-defect register. Every discovered defect must be recorded with evidence, impact, root cause, proposed fix, and test; unrelated findings must not be discarded.
- Make changes incrementally, with focused commits and regression tests.
- Never claim target-device behavior from static analysis or host tests alone.

## Project priority order

### P0 — Factory system inventory and compatibility map
Map the complete vendor audio stack, binaries, services, JNI/Binder interfaces, HAL, policy, effects, ALSA routes, kernel interfaces, and all known clients. Establish the exact Android-facing and hardware-facing contracts.

### P1 — Factory defect audit and reference behavior
Analyze the original CD3313 module and all related userspace clients; record factory defects and reconstruct the behavior that must remain compatible. Extend the audit to the rest of the vendor audio path.

### P2 — Unified architecture and ownership model
Define the single source of truth for audio state, route ownership, volume/mute ownership, command sequencing, error propagation, and subsystem interfaces.

### P3 — CD3313 and hardware-control replacement
Implement a safe, compatible CD3313 driver/control path, preserving verified behavior and correcting proven defects. Confirm board-specific GPIO and I2C details before hardware changes.

### P4 — Audio core, PCM, mixer, and DSP completion
Build the unified processing and control engine, with explicit format negotiation, bypass behavior, latency bounds, and testable state transitions.

### P5 — Android HAL and policy integration
Integrate the complete engine with Android 9 so the engine, rather than the factory vendor implementation, owns the verified routes and controls. Preserve framework contracts and application compatibility.

### P6 — Progressive factory-component retirement
For each factory component, prove that all its responsibilities have a replacement, switch ownership without duplicate control, test all dependent scenarios, and retain a rollback route. Remove/disable only components proven redundant.

### P7 — End-to-end validation and release
Build against the exact target kernel/platform configuration; validate all audio sources, output routes, concurrent streams, calls, microphones, Bluetooth modes, suspend/resume, reboot, error recovery, sound quality, latency, and stability on the real MT8163 device.

## Definition of done

The project is complete only when all of the following are demonstrated:

1. The integrated MT8163 Audio Engine provides the required factory audio functionality through one coherent implementation.
2. The relevant factory vendor audio components no longer own or compete for the functions replaced by the engine.
3. Android 9 applications and framework clients continue to work through compatible interfaces.
4. All supported sources and routes have documented ownership and end-to-end test results.
5. CD3313 controls and PCM/DSP processing behave correctly, including error and recovery paths.
6. Known factory defects are fixed or explicitly documented as unresolved with evidence and a reason they cannot yet be closed.
7. Host tests, static checks, exact-target builds, and real-device tests are reported separately and accurately.
8. A tested recovery path to the original firmware behavior exists.

## Current status and evidence discipline

This document records the agreed product goal; it does not claim that the replacement is implemented.

Existing repository work includes static reverse-engineering of parts of the CD3313, MT8163 HAL, FM/A2DP paths, and host-side PCM DSP foundations. The root README and linked analysis documents describe the current evidence and limitations. In particular, host-side DSP tests do not prove that the engine owns the target device's complete system audio path. No full factory-stack replacement or end-to-end target-device validation is claimed by this goal document.

## Related work

- [CD3313 driver rewrite plan](../kernel/cd3313/README.md)
- [MT8163 primary Audio HAL analysis](MT8163_AUDIO_HAL_ANALYSIS.md)
- [Android audio integration gate](ANDROID_AUDIO_INTEGRATION_GATE.md)
- [PCM DSP core](../pcm-dsp/README.md)
- [Audio source coverage](MT8163_AUDIO_SOURCE_COVERAGE.md)
