# Architecture Decision Record — Full Replacement, Unified DSP, Factory Retention

**Status:** Accepted  
**Date:** 2026-11-13  
**Applies to:** MT8163 / Android 9 / ARM32 / vendor Linux 4.9.117  
**Working branch:** `feature`  
**Stable branch:** `main`

## 1. Accepted decisions

The project owner selected:

1. **Replacement depth: `full`.** Replace the complete factory vendor audio implementation at the vendor-audio ownership boundary. Preserve Android 9 public framework contracts and required kernel/platform interfaces unless target evidence proves a narrow change is necessary.
2. **DSP scope: `all`.** Every supported audio source and direction must be covered by the unified DSP architecture. There are no permanent source-class exclusions for media, navigation, notifications, system sounds, Bluetooth A2DP, Bluetooth HFP/SCO, voice calls, microphone capture, FM, AUX/AUX2, DAB or other verified inputs.
3. **Factory components: `retain`.** Preserve factory binaries, libraries, services, kernel drivers, configuration and original firmware artifacts. Do not delete or overwrite factory components as part of development. Retention does not permit a second active owner of route, volume, mute, DSP or hardware state.

These decisions supersede any interim plan that permanently bypasses SCO, calls, microphone capture or another source class.

## 2. Meaning of full replacement

The replacement product is the factory-named primary HAL:

`/vendor/lib/hw/audio.primary.mt8163.so`

It must satisfy the recovered Android 9 ARM32 ABI, module identity, symbols, dependencies, public callback contracts and required vendor-private callback contracts.

The replacement boundary includes vendor audio policy/routing, stream/session lifecycle, source arbitration, PCM graph/mixer, DSP orchestration, volume/mute ownership, capture and telephony handling, FM/input adapters and CD3313EO control ownership.

Android AudioFlinger, AudioPolicyService, Bluetooth framework/profile services, ALSA/ASoC, kernel drivers and required vendor transports remain system infrastructure where compatible. Replacing the vendor audio implementation does not mean rewriting Android framework or Bluetooth protocol stacks without evidence.

## 3. Meaning of factory retention

Factory components and original artifacts are retained as:

- immutable rollback inputs with recorded hashes;
- compatibility references for recovered ABI and behavior;
- low-level transports or platform adapters only where their contract is verified and their use does not create a competing policy/state owner;
- diagnostic references for comparing factory and replacement behavior.

The replacement AudioCore is the only authoritative owner of logical source/session state, route decisions, mute arbitration, logical volume, DSP profile state and CD3313 command scheduling after cutover.

Factory services or clients that can issue audio or CD3313 commands must be migrated to typed proxy requests, disabled from conflicting writes, or isolated behind a compatibility adapter. Merely leaving a factory process active is not acceptable if it can mutate the same hardware state independently.

Do not delete factory files, uninstall factory packages, or disable factory modules during development. Any eventual runtime cutover requires a tested rollback procedure and explicit evidence. The old factory primary HAL cannot simultaneously occupy the same loader path as the replacement; preserve its exact original bytes in the immutable firmware archive/rollback package before staging the replacement.

## 4. One DSP engine, multiple processing graphs

"All sources" means one shared DSP implementation and control/profile model, not one identical filter chain applied indiscriminately to every buffer.

The DSP architecture must expose independently configured, bounded processing graphs for:

- playback render: media, navigation, notifications, system sounds, A2DP and other PCM playback sources;
- radio/external input: FM, AUX/AUX2, DAB and other verified inputs;
- telephony downlink: HFP/SCO received voice;
- capture/uplink: microphone and voice-call transmit path;
- echo-reference path: synchronized render reference for AEC;
- route-specific output mapping and channel topology.

Each graph uses a validated immutable profile and explicit enable/bypass state. Profiles may differ by source, route, sample rate, channel count and direction, but all processing is implemented and coordinated by the same DSP engine. Source priority, ducking, mixing, mute and gain ownership remain in AudioCore.

The real-time path remains bounded: no heap allocation, blocking mutex, Binder/IPC, device open, filesystem access, synchronous logging, I2C, GPIO or ioctl from PCM callbacks. Control-plane work prepares profiles and route snapshots before publication to audio threads.

## 5. Source coverage requirements and physical constraints

| Source/path | Required final behavior | Critical integration condition |
|---|---|---|
| Local media / navigation / notifications / system | DSP and mix/duck policy | Verified PCM insertion before any irreversible channel fold |
| Bluetooth A2DP | DSP on every supported active PCM route | Offload must be disabled or redirected to a DSP-visible PCM path when it would bypass processing |
| Bluetooth HFP/SCO downlink | Telephony DSP and call routing | Recover SCO transport, clocking, format, latency and call-state transitions |
| Microphone / HFP uplink | Capture DSP including AEC/NS/AGC as supported | Recover capture endpoint, echo reference, synchronization and microphone topology |
| FM direct I2S | DSP coverage | Direct route must be brought through a verified DSP-visible path or use a proven digital insertion point |
| FM capture PCM | DSP coverage | Prove actual consumer and audible route before enabling processing |
| AUX/AUX2/DAB | DSP coverage when digitized | Analog-only inputs need a verified ADC/PCM capture path; CD3313EO analog input selection alone cannot feed software DSP |
| CD3313EO analog processing | Unified control/profile ownership | Analog control is separate from PCM DSP; avoid duplicate gain/EQ and preserve electrical limits |

A route that cannot yet be observed as PCM is not silently declared DSP-covered. It remains an explicit integration blocker until its digital capture/insertion path is recovered and validated. Do not invent an ADC, loopback, I2S route, SCO PCM tap or board connection that has not been evidenced.

AEC requires a correctly synchronized far-end render reference and microphone capture. NS, AGC, sidetone and call gain are separate stages with explicit ownership and bounded latency. They must not be treated as generic media EQ.

## 6. Cutover and compatibility rules

1. Keep factory artifacts immutable and hash-verified.
2. Build and test the replacement HAL off-device first.
3. Verify exact Android 9 ARM32 ABI and linker namespace/dependency visibility.
4. Map every stream, patch, source, capture and telephony route to one owner and a documented endpoint contract.
5. Integrate one route at a time with a safe temporary fallback until replacement validation passes.
6. Prevent simultaneous factory/replacement writes to CD3313EO, mixer controls, mute, gain or route state.
7. Test suspend/resume, device loss, XRUN, Bluetooth reconnect, call transitions, FM transitions, profile changes and rollback.
8. Only then stage the factory-named HAL replacement. Do not flash or perform hardware writes during host-only development.

A temporary bit-transparent fallback is a migration technique only. It is not a permanent exclusion from final DSP/source coverage.

## 7. Immediate engineering consequences

- Reconcile roadmap and source-coverage documents so no permanent SCO/call/capture bypass remains.
- Audit A2DP offload policy and establish a DSP-visible PCM path for all supported A2DP playback.
- Recover HFP/SCO downlink/uplink, capture, echo-reference and call lifecycle contracts before implementing telephony DSP.
- Resolve FM direct-I2S versus capture topology and determine how to expose the audible route to DSP.
- Determine whether AUX/AUX2/DAB are analog-only or have a real ADC/PCM capture route on the target board.
- Extend the unified route/profile model to distinguish playback, capture, telephony and echo-reference graphs without duplicating DSP implementations.
- Preserve all factory components and keep exactly one active runtime state/hardware owner after cutover.

## 8. Verification status

This record captures the owner's accepted product decisions and engineering requirements. It does not claim that any newly required route is implemented, DSP-connected, built for the target, or device-verified. Existing host tests, Android cross-builds, ABI audits and physical-device validation must continue to be reported separately against exact commits and artifacts.
