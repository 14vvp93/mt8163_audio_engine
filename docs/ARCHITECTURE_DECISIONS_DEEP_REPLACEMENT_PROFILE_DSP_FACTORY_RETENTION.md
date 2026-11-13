# Architecture Decision Record — Deep Replacement, Profile-Based DSP, Factory Retention

**Status:** Accepted  
**Date:** 2026-11-13  
**Applies to:** MT8163 / Android 9 / ARM32 / vendor Linux 4.9.117  
**Working branch:** `feature`  
**Stable branch:** `main`

## 1. Owner-approved decisions

The project owner selected:

1. **Replacement depth: `deep`.** Replace selected factory audio processing and control layers where the recovered contracts and target evidence justify it. Do not rewrite or replace the entire factory vendor audio stack by default. Preserve compatible factory HAL, policy, routing, stream lifecycle and platform integrations unless a specific defect, limitation or required capability justifies replacing that boundary.
2. **DSP: `profiles`.** Use a profile-driven DSP architecture. Each supported profile defines its processing chain, parameters, format/rate/channel constraints, route applicability, enable/bypass behavior and transition policy. Do not impose one universal processing chain or claim every source and direction is DSP-covered. Expand profile coverage deliberately as each route is verified.
3. **Factory components: `retain`.** Preserve factory binaries, libraries, services, kernel drivers, configuration and original firmware artifacts. Keep compatible factory components in service where they remain the correct owner and meet requirements. Reuse them as adapters/transports only after their contracts are verified. Preserve immutable rollback copies and hashes.

These decisions supersede the previous interim direction of full vendor-audio replacement and mandatory all-source DSP coverage.

## 2. Meaning of deep replacement

Deep replacement targets the audio-engine internals that need improvement while minimizing unnecessary disruption to the recovered Android 9 / MT8163 integration.

Candidate replacement boundaries include:
- PCM DSP and profile execution;
- source/route-aware processing orchestration;
- selected factory post-processing stages where a verified insertion point exists;
- CD3313EO control arbitration and safe command scheduling;
- narrowly scoped HAL adapters or compatibility shims where factory behavior is defective or cannot expose a required capability.

The factory primary HAL remains the baseline integration owner unless evidence demonstrates that a narrower replacement or wrapper is required. Do not replace `/vendor/lib/hw/audio.primary.mt8163.so` wholesale merely because it is the final system integration point. Preserve its exact factory bytes, ABI, symbols, dependencies and path as a rollback/reference artifact.

Android AudioFlinger, AudioPolicyService, Bluetooth services, ALSA/ASoC, kernel drivers and vendor services remain in place unless an individually justified change is approved by evidence and covered by rollback.

## 3. Factory retention and runtime ownership

Factory retention means preservation and selective continued use, not blindly trusting factory behavior.

- Keep original factory artifacts immutable and record hashes/provenance.
- Retain active factory HAL/services/drivers when they provide required, verified functionality.
- A retained factory component may remain the owner of a route or control domain that the new engine does not take over.
- When the new engine takes over a specific route or hardware-control domain, exactly one component must own its mutable state and hardware writes.
- Do not allow simultaneous factory and replacement writes to the same CD3313EO registers, mixer controls, route, mute or gain state.
- Keep the factory primary HAL at its original loader path unless a separately reviewed cutover explicitly stages a compatible replacement.
- Do not delete, overwrite, uninstall or disable factory components during development. Any future change in runtime ownership requires a tested rollback procedure.

## 4. Profile-driven DSP model

The DSP library is shared infrastructure; profiles determine how and where it is applied.

A profile must describe at least:
- stable profile ID and version;
- source class and eligible route(s);
- playback, capture or telephony direction;
- supported sample formats, rates and channel layouts;
- ordered processing stages and validated parameter ranges;
- explicit enabled/disabled state and bit-transparent bypass behavior;
- gain ownership and interaction with CD3313EO analog controls;
- latency/CPU budget and transition/crossfade policy;
- fallback behavior for unsupported formats, missing resources or processing failure.

Profile selection and parameter changes occur on the control plane. PCM callbacks consume immutable, prevalidated profile snapshots and perform bounded real-time work only: no heap allocation, blocking mutex, Binder/IPC, filesystem access, synchronous logging, device open, ioctl, I2C or GPIO operations.

Profiles may be shared across sources when their requirements match, but source identity alone must not silently select an unverified profile. Unknown or unsupported routes remain on their verified factory path or use an explicit bit-transparent fallback; they must not be falsely reported as DSP-processed.

## 5. Initial profile and route strategy

Initial implementation should prioritize verified PCM routes and add profiles incrementally:

| Profile family | Candidate sources/routes | Initial rule |
|---|---|---|
| Music playback | Local media / primary PCM | First DSP integration target; preserve stereo before factory mono fold |
| Navigation and system overlays | Navigation, notifications, system sounds | Separate profile or explicit shared profile with validated ducking/gain policy |
| Bluetooth A2DP | A2DP PCM route | Enable only when DSP-visible PCM is proven; offload path remains factory-managed or bypassed until a safe integration exists |
| FM | FM capture PCM or verified digital insertion | Do not claim DSP coverage for direct I2S until a real digital tap/insertion boundary is proven |
| HFP/SCO and voice | Telephony downlink/uplink | Dedicated telephony profile only after PCM bridge, timing, call lifecycle and echo-reference contracts are recovered |
| Microphone capture | Capture PCM | Separate capture profile; AEC requires synchronized far-end reference |
| AUX/AUX2/DAB | Verified ADC/PCM capture only | CD3313 analog input selection alone is not a software-DSP input |
| CD3313EO analog controls | Analog input/master/output control | Separate analog profile mapping; avoid duplicate software and analog gain/EQ |

This is a staged target matrix, not a claim that all listed profiles are implemented or that every source must be processed by DSP. Factory handling remains available for routes not yet integrated.

## 6. CD3313EO and gain ownership

CD3313EO is a digitally controlled analog audio processor, not a PCM/I2S DSP. Its input selection, input gain, master volume, bass/treble, loudness, output attenuation and mute are analog-domain controls.

AudioCore must define explicit gain ownership across:
1. Android stream/session gain;
2. optional profile-level digital gain/EQ/limiting;
3. CD3313EO master analog volume;
4. CD3313EO per-output analog attenuation.

Do not apply the same EQ or gain stage in both software and CD3313EO unintentionally. All chip commands are serialized through a control-plane broker. No I2C or ioctl operation may run from a PCM callback.

The kernel driver artifact must be named `cd3313.ko`. Do not infer board-specific I2C bus, supply, reset, GPIO polarity, analog wiring or active device ownership without target evidence.

## 7. Migration and safety rules

1. Preserve and hash all factory artifacts before any replacement or runtime change.
2. Complete a route/control ownership map and mark each boundary as verified, unverified or unknown.
3. Integrate the first profile at the recovered stereo-preserving PCM insertion point; retain the factory path as a safe fallback.
4. Add one source/profile family at a time with exact-format validation and bit-transparent bypass tests.
5. Keep A2DP offload, FM direct I2S, HFP/SCO, microphone capture and analog-only inputs on their existing verified factory paths until a DSP-visible integration is proven.
6. Prevent duplicate CD3313EO, mixer, mute, route and gain ownership during every transition.
7. Test suspend/resume, XRUN, device loss, Bluetooth reconnect, call transitions, FM transitions, profile switching and rollback.
8. Do not flash, load experimental modules or perform hardware writes during host-only development.

## 8. Verification status

This record captures the owner's accepted product decisions and engineering constraints. It does not claim that any new profile is connected to the target audio path, that a factory component is defect-free, or that the system has been physically validated. Host tests, Android ARM32 builds, ABI audits and physical-device validation must be reported separately against exact commits and artifacts.
