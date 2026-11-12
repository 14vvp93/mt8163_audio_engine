# Unified MT8163 Audio Core — architecture proposal

Status: approved product direction; architecture baseline, not implementation complete.
Target: MT8163 / Android 9 / ARM32 / vendor kernel 4.9.117.

## 1. Product invariant

MT8163 Audio Engine replaces the factory vendor audio implementation as one coherent system. It is not a global INSERT effect, an app-side DSP, or a second controller layered over the factory stack.

There is exactly one authoritative owner for:
- source/session state and source priority;
- route selection and route transitions;
- master and per-source mute arbitration;
- logical and hardware volume state;
- DSP profile and processing state;
- CD3313 command sequencing and completion/error tracking;
- vehicle events that affect audio behavior.

Android AudioFlinger, AudioPolicyService, ALSA and kernel drivers remain infrastructure/contracts where possible. They must not independently own a second copy of vendor audio policy.

## 2. Deployment boundary

### AudioCore runtime

Place the authoritative AudioCore state machine and PCM graph in the replacement primary audio HAL process. This keeps PCM buffers in-process and avoids Binder/IPC on the real-time path. The HAL owns one AudioCore instance for its lifetime.

Control clients must be thin proxies. They submit typed events to the HAL control interface; they do not maintain shadow source, mute, volume or DSP state. The exact Android 9-compatible control transport is an implementation gate: evaluate the existing HAL set_parameters/vendor extension path and a small vendor HIDL control interface against the captured framework and service call sites. Do not introduce a second stateful daemon.

### Android framework boundary

- Keep AudioFlinger and public Android audio APIs intact unless a proven vendor contract requires a narrow compatibility change.
- Replace factory-specific AudioPolicyManager extensions and service hooks progressively, preserving standard device/patch contracts.
- The replacement HAL must expose the required output/input streams and patch behavior to Android policy.
- Factory Java/Binder clients become migration inputs only. They must not continue sending CD3313 commands after the new broker takes ownership.

### Kernel boundary

Retain ALSA and the target kernel facilities where possible. The CD3313 driver is a transport/device layer, not the owner of audio policy. The new HAL hardware broker is the only userspace owner of CD3313 command sequencing.

## 3. Logical architecture

```text
Android framework / AudioPolicyService
              | stream, device, patch, mode events
              v
      Replacement Primary HAL
              |
       +------v-------------------------------+
       |              AudioCore              |
       |                                      |
       | SourceRegistry / SessionManager      |
       | RouteGraph / RouteTransition         |
       | MuteArbiter / VolumeModel             |
       | ProfileManager / DSP control          |
       | HardwareCommandBroker                 |
       |                                      |
       +-----------+--------------------------+
                   |
          +--------+---------+
          |                  |
          v                  v
    PCM graph/DSP       Hardware adapters
    mixer/resampler     CD3313 / vehicle state
          |                  |
          v                  v
     ALSA PCM/I2S       /dev/audiomux, /dev/carstatus
```

The diagram shows ownership boundaries, not a claim that every source is already a PCM node. FM direct I2S and some telephony routes require dedicated adapters until a verified PCM boundary is available.

## 4. Core modules and contracts

| Module | Sole responsibility | Must not do |
|---|---|---|
| SourceRegistry | Register source identity, capabilities, format and lifecycle | Write chip registers or make route decisions independently |
| SessionManager | Own active stream sessions, priorities and lifecycle | Duplicate AudioPolicyService's generic device model |
| RouteGraph | Select source-to-output path and perform atomic route transitions | Change CD3313 state outside HardwareCommandBroker |
| PcmGraph | Process PCM blocks, resample, mix and route channels | Allocate, block, perform Binder or issue I2C/ioctl in callback |
| DSP/ProfileManager | Own active profile, DSP parameters and transitions | Maintain a second profile copy in UI/service |
| MuteArbiter | Own named mute reasons for master and individual sources | Use one mutable boolean or clear another owner's reason |
| VolumeModel | Separate stream gain, master gain, analog output attenuation and source gain | Treat CD3313 FL/FR/RL/RR attenuation as four-channel PCM |
| HardwareCommandBroker | Serialize typed CD3313 operations, track queued/completed/failed state | Accept raw unvalidated command arrays from arbitrary clients |
| VehicleStateAdapter | Convert ACC/reverse/telephone/silence signals into typed events | Own audio mute policy or PCM route state |
| AndroidPolicyAdapter | Translate framework stream/device/patch/mode events | Become a second vendor source-priority controller |

### Snapshot publication implementation

The publisher uses three fixed slots and sequentially consistent 32-bit atomics. It is single-writer/multi-reader, allocation-free, has a non-blocking writer gate, and a fixed three-attempt reader acquisition. If all inactive slots are pinned, publication returns false; the control context retries later. If a reader cannot acquire a consistent slot within the bound, the PCM caller retains its previous snapshot for that block.

Host concurrency tests exercise two readers against 100,000 writer updates. ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer host runs pass. Android ARMv7 NDK CI verified the publisher instantiation, lock-free 32-bit atomic requirement and AudioCore object architecture as ELF32 ARM in run 43255493234.

### Initial SessionRegistry implementation

The host-side foundation now includes a fixed-capacity 16-slot SessionRegistry. It tracks opaque session handles, source identity, route type and lifecycle state (Requested, Active, Suspended, Stopping, Failed). It rejects duplicate/zero handles, invalid source/route identifiers and illegal lifecycle transitions. Closing a session releases its slot.

This registry is control-plane bookkeeping only. It does not choose priorities, open ALSA devices, execute routes or access hardware. Its capacity is an initial bound for tests, not a claim that Android will never expose more than 16 simultaneous logical sessions.

## 5. Event and real-time model

### Control plane

All state changes enter as typed events, for example:
- SourceRequested / SourceStarted / SourceStopped;
- OutputDeviceChanged / InputDeviceChanged / AudioModeChanged;
- AccChanged / ReverseChanged / TelephoneStateChanged;
- MuteOwnerAdded / MuteOwnerRemoved;
- VolumeChanged / ProfileChanged;
- HardwareCommandCompleted / HardwareCommandFailed.

A dedicated HAL control context validates events and updates AudioCore state. Route and chip operations are serialized there. UI/service proxies receive status snapshots, not authority to write the hardware.

### PCM plane

The audio callback consumes immutable, prevalidated processing/routing snapshots. MuteArbiter exposes a detached, read-only MuteSnapshot. A three-slot SnapshotPublisher<T> now provides single-writer/multi-reader publication: readers have a fixed retry bound, the writer never waits and returns false if every inactive slot is pinned. The caller must reuse its previous block snapshot after a failed acquire. The class statically requires trivially copyable snapshots and lock-free 32-bit atomics; Android ARMv7 NDK CI has verified this requirement. The callback performs bounded work only: no heap allocation, mutex, Binder, filesystem access, ioctl, I2C or logging that can block.

PCM processing failure must fail open to the last safe route where possible. Unsupported formats/routes are explicitly bypassed, never silently reinterpreted.

## 6. Source and route model

Do not collapse all audio sources into one enum or assume all sources enter the same PCM callback. Represent source identity separately from transport/route type.

| Source class | Factory evidence | Initial replacement handling |
|---|---|---|
| Local media / navigation / notifications | Android primary PCM stream; Normal HAL handler | PCM session adapters; distinguish media, navigation and notification policy |
| Bluetooth A2DP | Separate A2DP route; PCM16 stereo 44.1 kHz policy; offload control also exists | Dedicated adapter; process only when PCM is actually owned by the replacement and offload is false |
| HFP/SCO | Separate HFP client, SCO and Voice handlers; 8/16 kHz profiles; microphone and echo-reference path | Dedicated telephony adapter; initially preserve/bypass until downlink, uplink, clock and echo contracts are complete |
| FM direct | AudioPatch controls FM_I2S_Playback; no userspace PCM write loop in recovered direct path | Dedicated direct-route adapter or deliberate redesign to a verified PCM source; never claim Music INSERT coverage |
| FM capture | AUDIO_SOURCE_FM_TUNER → FM_I2S_Capture capture provider/fanout | Separate PCM input adapter if chosen as the audible path; prove how it reaches output |
| AUX / AUX2 / DAB / RADIO | CD3313 logical analog input selection and source mute | Hardware-source sessions controlled by AudioCore; distinguish analog mux selection from Android PCM sessions |
| Reverse / ONSTAR | Legacy source-priority and vehicle state | Safety-priority route policy; exact output signal and takeover behavior require target validation |

The recovered CD3313 source-priority order is retained only as a compatibility reference: HFP, reverse, ONSTAR, RADIO/RDS, AUX, A2DP, MUSIC, AUX2, DAB, fallback NONE. It is not automatically the new universal priority policy. AudioCore must model emergency/telephony priority separately from user-selected media and overlay streams.

## 7. Mute and volume semantics

### Mute

Internally, use named owners/tokens rather than vendor numeric reason values. Maintain separate domains:
- global master mute owners;
- per-source/per-input mute owners;
- stream-local mute/duck state;
- safety/emergency mute state.

Removing an owner clears only that owner's token. ACC, SilenceMode, HFP voice transition, radio retune/scan and radio packet mute must not accidentally clear one another.

Compatibility mapping to the paired CD3313 firmware:

| Legacy behavior | CD3313 reason index | Internal owner name |
|---|---:|---|
| ACC master mute | 2 | ACC_POWER_STATE |
| SilenceMode master mute | 1 | SILENCE_MODE |
| temporary SilenceMode transition mute | 24 | SILENCE_TRANSITION |
| Bluetooth voice master mute | 3 | BLUETOOTH_CALL_TRANSITION |
| selected-input effective mute reflected to master | 31 (reserved) | SELECTED_INPUT_MUTE_BRIDGE |

Radio input-path reasons are a separate per-input bitmask domain; they must not be confused with global master reasons using the same integer:

| AudioCore owner | Legacy radio input reason |
|---|---:|
| RadioRetune | 1 |
| RadioScan | 2 |
| RadioPacketMute | 2 |
| RadioUserMute | 4 |
| RadioStartupMute | 16 |

RadioScan and RadioPacketMute are separate internal owners but share legacy reason bit 2. The compatibility mapper must keep bit 2 set while either owner remains active, and clear it only after both owners release it. This avoids one caller prematurely unmuting the radio path while another caller still owns the mute.

### Volume

Keep at least four distinct controls:
1. Android stream/session gain;
2. AudioCore master digital gain/limiting;
3. CD3313 master analog volume;
4. CD3313 per-output analog attenuation FL/FR/RL/RR.

CD3313 does not expose verified readback for all these values. Hardware state therefore has explicit UNKNOWN / REQUESTED / CONFIRMED states. A cached register image is not physical readback.

## 8. PCM topology and four-channel gate

The captured Android policy declares primary PCM16 stereo at 44.1/48 kHz. The ordinary Normal HAL path also configures two-channel ALSA output. On speaker routes, the HAL may fold L/R to mono and duplicate it before its MTK post-processing callback.

The MTK post-process callback is only called from Normal playback and is after that speaker fold. Voice, HFP/SCO, HDMI, FM transmitter and capture are distinct paths. Therefore it is not the final integration point for a quad engine.

The DSP core may continue to support FL/FR/RL/RR internally, but do not synthesize four channels by duplicating stereo and call that hardware support. Before enabling quad output, prove the board's downstream PCM/TDM/I2S topology, actual ALSA channel count and physical speaker mapping. Until then the HAL-facing ingress is stereo and downstream output topology remains an explicit adapter gate.

## 9. Hardware broker contract

The broker accepts typed operations, validates all arguments against the exact paired CD3313 ABI, serializes them on a control thread, and reports distinct states:
- REQUESTED — accepted by AudioCore;
- QUEUED — accepted by kernel ioctl/event queue;
- COMPLETED — driver reports operation completion;
- FAILED — a confirmed transport/driver error;
- UNKNOWN — no reliable completion or readback is available.

The JNI extra-word discrepancy is unresolved. Do not reproduce the vendor JNI frame, invent padding or issue guessed requests. First replace the unsafe native transport with a safe typed ABI whose command-specific sizes are validated by host tests and a compatible kernel implementation.

## 10. Migration and cutover sequence

1. Finish static ownership map and exact Android 9/vendor compatibility contract.
2. Implement AudioCore state types, source/session registry, route model and mute arbitration as host-testable code.
3. Connect existing PCM/DSP core through a bounded PCM graph; keep current verified DSP tests intact.
4. Implement CD3313 broker/emulator contract and safe kernel/user ABI. Hardware writes remain disabled until reviewed.
5. Implement replacement primary HAL and Android policy adapter with explicit source-specific route handlers.
6. Integrate A2DP, FM direct/capture and HFP/SCO one route at a time with fallback and regression tests.
7. Retire factory service/JNI/AudioPolicy/AudioFlinger vendor hooks only when each behavior has a replacement owner and rollback.
8. Validate on the target head unit and compare every route against the factory behavior before final cutover.

## 11. Acceptance gates

- Exactly one source/route/mute/volume/DSP state authority.
- No factory process continues writing CD3313 after broker cutover.
- No raw chip command is issued from UI, service or PCM callback.
- PCM callback is bounded, allocation-free and free of blocking IPC/device I/O.
- Media, navigation, notification, A2DP, HFP/SCO, FM, AUX/DAB and vehicle events have explicit route behavior.
- Unsupported routes and offload fail open safely.
- Quad output is enabled only after board/ALSA topology is proven.
- Every route has host tests, Android build evidence and target-device evidence separately.
- Rollback restores the previous HAL/policy and hardware-safe state without concurrent factory/new ownership.

## Related documents

- [Factory audio ownership matrix](FACTORY_AUDIO_OWNERSHIP_MATRIX.md)
- [Phase 1 audit baseline](PHASE1_AUDIT_BASELINE.md)
- [Factory defect and risk register](FACTORY_DEFECTS.md)
- [Product goal](MT8163_AUDIO_ENGINE_GOAL.md)
- [Android integration gate](ANDROID_AUDIO_INTEGRATION_GATE.md)
- [PCM DSP core](../pcm-dsp/README.md)

## 12. Factory-compatible artifact identity and replacement packaging

The deliverable is a replacement firmware set, not a separately named application or parallel audio stack. Whenever a compiled artifact replaces a factory file, its installed filename, relative path, ELF SONAME (for shared libraries), architecture, ABI, exported symbols and relevant service/module identity must match the factory contract.

Rules for every build and packaging change:

1. **Preserve factory filenames.** A replacement for a factory `.so`, executable, kernel module, configuration file or service binary must be emitted under the exact original basename. Do not ship names such as `libmt8163_audio_engine.so` as a substitute for a factory library unless that exact name is itself present in the target firmware and is intentionally part of the replacement contract.
2. **Preserve factory paths.** Package each replacement at the original partition-relative path (for example, the exact `/system/lib`, `/vendor/lib`, `/vendor/bin`, `/vendor/etc` or module path established by the target image). Do not infer a path from a filename.
3. **Separate development targets from firmware outputs.** CMake target names, namespaces, source directories and intermediate static libraries may use project-specific names such as `mt8163_audio_core`. Those are build-internal artifacts; the final staged firmware artifact must use the factory identity it replaces.
4. **Record provenance before replacing.** Maintain a factory-artifact manifest containing original path, basename, SHA-256, ELF class/machine, SONAME, DT_NEEDED dependencies, exported/imported symbols, file mode/owner where recoverable, and the exact replacement target. Record unresolved fields rather than guessing.
5. **Match the target ABI.** A filename match is not sufficient. Shared libraries and executables must match the target's ARM32 EABI, Android API/platform assumptions, linker namespace, C++ runtime and symbol-version expectations. Kernel modules must match the exact target kernel build/configuration and module ABI. Android NDK host success alone does not establish firmware compatibility.
6. **Preserve contracts not intentionally replaced.** Keep required HAL entry points, framework-facing symbols, init/service names, property names, configuration schema and ioctl contracts. Changes to any of these require an explicit compatibility implementation and tests.
7. **Stage and audit before image assembly.** Generate a replacement manifest and compare the staged package against the factory inventory. Fail packaging on unexpected new runtime libraries/services, missing replacement files, filename/path drift, duplicate owners or unapproved factory-file deletion.
8. **Keep rollback artifacts.** Preserve the exact original files and hashes in the reference baseline. Never overwrite the only factory copy during development.

The exact factory basenames and installation paths must be populated from the recovered target firmware inventory and binary/service audits before the first replacement image is assembled. No placeholder or guessed filename is an acceptable final output name.

The AudioCore static library remains an internal implementation component unless the target firmware proves that a new standalone shared object is required. The default integration direction is to link AudioCore into the factory-named replacement primary HAL and any other explicitly identified factory replacement binaries, so Android continues loading the expected components without a parallel audio stack.

### Artifact-level acceptance gates

- Every staged replacement maps one-to-one to a known factory path and file identity.
- No replacement artifact is installed under a project-only name where Android or init expects the factory name.
- Shared-library SONAME and required symbol/dependency contracts are checked against the factory binary.
- ARM32 / Android 9 / vendor-kernel compatibility is checked independently of host tests.
- The package contains no unapproved extra audio daemon, HAL, library or kernel module.
- Factory originals and a verified rollback manifest remain available until target-device validation is complete.
