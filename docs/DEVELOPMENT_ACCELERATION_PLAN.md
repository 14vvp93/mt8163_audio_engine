# Development Acceleration Plan — Beta 0.1

## Purpose

Reduce parallel-branch overhead and move toward one testable Android 9 / MT8163 HAL beta without weakening hardware-safety gates.

## Integration lane

- Integration branch: `integration/beta-0.1`.
- Starting point: `feature/profile-transition-benchmark`.
- `main` remains untouched.
- No force-pushes, history rewrites, branch deletions, or automatic merges.
- Existing work branches remain intact as evidence and source branches until their changes are reviewed.

## Workstream policy

Only these workstreams should receive new implementation work:

1. **PCM/backend:** `work/pcm-backend` — PCM endpoint, stream broker, DSP and concurrency correctness.
2. **HAL contract:** `work/android9-hal-contract` — Android 9 primary HAL ABI, factory artifact inventory and binary compatibility.
3. **Hardware evidence:** `work/hardware-route-evidence` — read-only device diagnostics and route evidence.
4. **Virtual Audio:** existing `work/virtual-audio-*` branches are test fixtures and contract experiments. Do not add more parallel branches. Promote only reviewed commits into the integration lane.

Before promotion, compare each source branch with the integration branch, inspect the exact changed files, and run its focused CI. Do not merge whole sibling branches blindly.

## CI policy

- Run focused module tests for changes limited to one module.
- Run the complete integration matrix on integration-lane changes affecting public interfaces, lifecycle, routing, or build configuration.
- Require host tests, Android ARMv7 build, static ELF/ABI checks, and repository-integrity checks before calling Beta 0.1 build-ready.
- Treat CI success as host/build evidence only. It is not proof of operation on the target head unit.
- Keep failures visible; diagnose the failing assertion/log before retrying.

## Beta 0.1 acceptance gate

- Android 9 primary HAL entry points and ABI are confirmed against the target factory binary.
- ARM32 library builds with the required Android toolchain and expected SONAME, symbols and dependencies.
- PCM open/configure/start/transfer/stop/close lifecycle passes deterministic host tests.
- No unresolved race or ownership issue remains in the PCM path.
- Device diagnostics identify the actual ALSA card, PCM nodes, mixer controls and route ownership.
- No factory audio component is removed or overwritten during development.

## Current verified facts (2026-11-12)

- `work/pcm-backend` commit `68a92261988e0d19c5149282af81042d7cb5b0db`: five CI checks passed.
- `work/virtual-audio-pcm-contract` commit `1ba65fecd3354e47f7e87e2f0036bf35ddcd979d`: the test suite failed because a lifecycle test expected `close()` to fail on an opened, idle stream, contradicting the implementation and other tests.
- Corrected the test to assert that duplicate `open()` preserves the existing stream and that `close()` is rejected while running. Fix commit: `e1841aff9fd5bc9067b60acd5014619cbbc746b7`.
- CI for the fix passed: run `43295081606`, job `host-build-and-test`.
- None of these CI results constitutes target-device validation.

## Immediate sequence

1. Keep `integration/beta-0.1` as the sole integration destination.
2. Review the four unique concurrency commits and the HAL/route evidence deltas against the current PCM branch; promote only changes that are still needed.
3. Align Virtual Audio contract tests with the PCM lifecycle semantics and add deterministic regression coverage.
4. Integrate the PCM backend into the Android 9 HAL skeleton.
5. Collect target-device diagnostics and validate the exact factory ABI before producing a flashable package.
