# GitHub Actions minute optimization

## Policy

Keep the private repository's automatic CI small and relevant. Run expensive diagnostics only on demand. Do not call reusable workflows hosted in the public repository from a private workflow: GitHub bills reusable-workflow runs to the caller repository.

## Automatic private CI

| Workflow | Trigger scope | Purpose |
|---|---|---|
| `test-pcm-dsp.yml` | `pcm-dsp/**`, `audio-core/**` | Primary host build and regression suite |
| `android-armv7.yml` | `pcm-dsp/**`, `audio-core/**` | Android 9 ARMv7 cross-build only; host benchmark removed |
| `android-effect-library.yml` | Effect-library implementation and build inputs | Android 9 effect ABI smoke test and ARMv7 build |
| `primary-hal-arm32-abi.yml` | `primary-hal/**` | Primary HAL ARM32 ABI bootstrap |
| `android9-hal-abi-layout.yml` | `audio-hal-abi/**` | Recovered ABI layout tests |
| `repository-integrity.yml` | Markdown, repository scripts and audit tools | Documentation and script integrity |
| `test-cd3313-emulator.yml` | `cd3313-emulator/**` | Host protocol and safety tests |

The PCM DSP host test suite is the default regression gate. The Android ARMv7 workflow no longer runs a second host benchmark. The Primary HAL ARM32 workflow no longer starts for AudioCore-only changes because the Android ARMv7 workflow already cross-compiles AudioCore.

## Manual-only private diagnostics

Run these only when the changed code warrants the additional validation:

- `audio-core-tsan.yml` — full AudioCore ThreadSanitizer suite.
- `pcm-stream-broker-tsan.yml` — focused PCM broker ThreadSanitizer suite.
- `pcm-dsp-stability-lab.yml` — extended stability run; overlaps the standard host test suite.
- `pcm-dsp-performance.yml` — performance benchmark.
- `build-android-test-apk.yml` — Android diagnostic APK.
- `primary-hal-binary-audit.yml` — expensive factory firmware binary audit.

CD3313 host tests remain automatic for relevant changes. ASan/UBSan and libFuzzer jobs run only when that workflow is manually dispatched.

## Public firmware compute

Run compute directly in `14vvp93/public_calculations` → Actions:

- `Public firmware compute`: choose `inventory`, `verify`, `all-light` or `audio-inventory`.
- The nine firmware-analysis workers now support direct `workflow_dispatch` in addition to `workflow_call`.

Do not run the old private caller workflows: they were removed from this branch because reusable workflow billing follows the private caller. Direct runs in the public repository use standard public-repository runners, which do not consume the private repository's included compute allowance. Public artifacts and logs must contain only approved public data.

## Cancellation and validation

Pushes to the same branch cancel stale in-progress runs within each workflow. This avoids spending time finishing obsolete commits. Workflow path filters prevent unrelated changes from starting build jobs.

No private product source is checked out by public workers. Private builds, tests and ABI checks remain in the private repository.

## Operational rules

1. Let the standard host regression suite run for PCM DSP/AudioCore changes.
2. Run ARM32 cross-builds only when the relevant Android-facing code changes.
3. Run TSAN, fuzzing, performance and full binary audits on demand.
4. Prefer one run after a coherent group of commits over one run per tiny commit.
5. Do not treat skipped/manual diagnostics as passing tests. Record which gates actually ran.
