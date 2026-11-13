# Repository Health Audit — 2026-11-13

## Scope and evidence

Repository: `14vvp93/mt8163_audio_engine`  
Audit refs observed: `main` = `0490b0be651e6e1fa8c7b6b8e6f56d9262479a26`; `feature` = `84cc9aecd2852e095545c0f7fb36f9a927d0391a`.  
This is a read-only audit snapshot. No branch was merged, reset, force-pushed or deleted.

## Executive assessment

The repository has substantial engineering foundations and useful factory-firmware reverse-engineering evidence, but it is not a deployable replacement audio HAL. The active `feature` branch contains the current work; `main` is not an integration target that can safely absorb the whole branch as a routine merge.

The current highest risks are:
1. Branch divergence and stale branch/status documentation.
2. Two failing CI checks on the current `feature` HEAD.
3. No concrete target ALSA/TinyALSA endpoint implementation or live device inventory.
4. The Primary HAL is an ABI/bootstrap shell that deliberately returns errors and does not expose working streams.
5. Physical hardware identity and hardware-control ownership must be treated as a strict evidence gate; no hardware writes are justified.

## Git topology

- Only two branch refs were returned by GitHub: `main` and `feature`.
- `feature` is 591 commits ahead of `main` and 2 commits behind it; GitHub reports the refs as diverged.
- The comparison contains a broad body of new implementation, tests, workflows and documentation, not a small isolated patch.
- Earlier `work/*` refs are not currently present in the branch list. Their historical CI runs do not prove that those refs still exist.
- The current PR listing contains PR #1 in a closed state; it is not an approved integration path.
- Do not merge all 591 commits, rebase/reset either branch, delete refs or rewrite history as a cleanup shortcut. First establish a reviewed integration baseline and identify the two main-only commits.

## CI state at the observed HEAD

Current `feature` HEAD: `84cc9aecd2852e095545c0f7fb36f9a927d0391a`.

Two latest workflow runs failed:
- PCM Stream Broker ThreadSanitizer, run 43428590121, job `pcm-broker-tsan`.
- Repository Integrity, run 43428590110, job `markdown-links`.

The job-log endpoint did not return retained logs during this audit, so the root causes are **unconfirmed**. Do not describe these as runner-allocation failures or code failures without the actual logs. Previous successful runs on earlier commits are historical evidence only and do not clear the current HEAD.

Required next CI action:
1. Re-run or inspect the two failed jobs with retained logs.
2. Fix the exact failing cause on `feature`.
3. Run the complete current workflow matrix on the resulting commit.
4. Keep host tests, sanitizer tests, Android cross-build, ABI layout checks and device validation as separate evidence classes.

## Code and architecture assessment

### AudioCore / PCM

Present:
- AudioCore state/control primitives, route policy, mute arbitration, snapshots and session registry.
- PCM backend contract, endpoint adapter, byte/frame adapter and fixed-capacity stream broker.
- Generation/admission gates and substantial lifecycle/concurrency regression coverage.
- Android Soong source-list integration for the AudioCore static library.

Not yet production-ready:
- `EndpointPcmBackend` is an injected-operations adapter. It is not itself a concrete TinyALSA/ALSA implementation.
- Endpoint card/device values must come from read-only target inventory, not guessed constants.
- HAL control calls still need a single-writer executor; the core primitives do not automatically serialize every control operation.
- Current CI failure means the latest PCM broker state is not currently verified by TSAN.

### Primary HAL

Present:
- Factory ARM32 ABI layout assertions and a non-installable bootstrap module with exported `HMI`.
- Factory binary/relocation audit tooling and documentation of the recovered module, device and stream layouts.

Blocking gaps:
- `primary-hal/src/primary_hal.cpp` returns `-ENODEV`/ `-ENOSYS` for initialization and stream opening; it is intentionally a bootstrap, not a functional HAL.
- No complete factory-compatible stream callback implementation or private-tail callback implementation.
- No exact full replacement artifact validated against all factory symbols, dependencies, linker namespace and runtime behavior.
- Keep `installable: false` until the complete ABI and runtime contract is proven.

### DSP and routes

- `pcm-dsp` has a meaningful host-tested DSP foundation and Android/JNI scaffolding.
- DSP is not yet wired into the actual Primary HAL PCM callback path.
- Media, navigation, FM, A2DP, HFP/SCO, voice capture and concurrent route transitions are not end-to-end complete.
- Preserve bit-transparent bypass for unsupported formats/routes and all DSP failures.

### Chip and hardware evidence

- The current roadmap names CD3313EO as the physical IC and CD3313KO as the driver, and explicitly keeps hardware transport disabled until runtime ownership is proven.
- A historical decision document contains a prior CD3310EO-only assertion, but its header marks it superseded. Keep that historical distinction explicit; do not use firmware filenames, DTBO declarations or module names as proof of the populated chip.
- Runtime I2C binding, device-node owner, GPIO polarity, mixer topology and actual board wiring remain unknown until collected from the target.
- No module load, ioctl write, I2C write, GPIO change, mixer write or firmware flash should be attempted as part of repository cleanup.

## Documentation and workflow hygiene

- `docs/PROJECT_STATUS.md` contains stale exact HEAD/test-baseline statements relative to the live `feature` ref.
- `docs/PROJECT_ROADMAP.md` is designated as the single authoritative plan; preserve that rule and update status from current evidence only.
- Historical Beta roadmap and superseded hardware decisions should remain clearly labeled as archive/history, not competing execution plans.
- Current repository tree contains 284 tracked entries on `feature` (including directories); the recursive file inventory is substantially larger than the older 209-file baseline recorded in the roadmap.
- The repository-integrity workflow currently fails at its Markdown-link job. Its cause must be determined from job logs before modifying the checker or link targets.

## Ordered recovery plan

### P0 — Stabilize repository and CI
1. Preserve current `main` and `feature` refs.
2. Obtain exact logs for the two failed workflows and fix only proven causes.
3. Refresh the live HEAD, branch, PR and workflow inventory in the authoritative status document.
4. Audit all local Markdown links and workflow sparse-checkout assumptions.
5. Record the two main-only commits and determine whether they are bookkeeping or contain required changes.

### P1 — Prove target runtime topology
1. Run the existing read-only collector on the actual head unit.
2. Capture build fingerprint, kernel identity, loaded modules, I2C bindings, ALSA cards/PCMs, PCM substream state, mixer controls, device-node metadata and open-FD owners.
3. Confirm physical IC and active driver ownership from runtime/board evidence.
4. Keep hardware transport disabled until this evidence is reviewed.

### P2 — Complete the real PCM backend
1. Implement a concrete target TinyALSA/ALSA operations adapter behind the existing injected interface.
2. Derive endpoint mappings from P1 evidence.
3. Add XRUN, suspend/resume, device-loss, partial transfer and recovery tests.
4. Validate callback lifetime and single-writer control serialization under sanitizers.

### P3 — Implement the factory-compatible Primary HAL
1. Reconcile exact AOSP/vendor headers and linker namespace.
2. Implement output/input stream objects and callbacks with factory-compatible negotiation and buffer semantics.
3. Integrate AudioCore in-process as the single state owner.
4. Preserve factory private callback ABI only where evidence proves it is required.
5. Keep the module non-installable until exact ARM32 ABI, symbol, dependency and runtime tests pass.

### P4 — Integrate routes and DSP
1. Wire DSP at the verified PCM insertion point with transparent bypass.
2. Add route adapters incrementally: media, navigation, FM, Bluetooth, voice and capture.
3. Test route transitions, latency, clipping, channel separation, suspend/resume and recovery.

### P5 — Release gate
- Exact factory path/name: `/vendor/lib/hw/audio.primary.mt8163.so`.
- ARM32/EABI5 and Android 9 ABI verified.
- Factory SONAME, required symbols, dependencies and linker namespace verified.
- Full host, sanitizer and Android build matrix green.
- Device inventory and route tests completed.
- Rollback package and hashes verified.
- No deployment until explicit hardware-validation approval.

## Change policy

This audit does not authorize destructive cleanup. Keep original firmware artifacts and rollback references immutable. Do not delete historical evidence, force-push, reset, merge to `main`, enable installation, or perform hardware writes without a separate explicit approval.
