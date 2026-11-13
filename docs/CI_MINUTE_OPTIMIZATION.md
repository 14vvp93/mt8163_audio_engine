# GitHub Actions minute optimization

Audit date: 2026-11-13

## Goals and billing boundary

Keep private product CI small, relevant and branch-deduplicated. Run expensive diagnostics only on demand. Keep public firmware analysis inside `14vvp93/public_calculations`.

A reusable workflow called by a private workflow is billed to the caller repository. Therefore, private workflows must not call public reusable workflows when the goal is to move compute away from the private Actions allowance. Public firmware jobs are dispatched directly in the public repository.

Public workers check out only `14vvp93/public_calculations` with `persist-credentials: false`. They do not check out private product source or receive private credentials. Public artifacts and logs must contain only approved public data.

## Automatic private CI

| Workflow | Trigger scope | Purpose |
|---|---|---|
| `test-pcm-dsp.yml` | `pcm-dsp/**`, `audio-core/**` | Host build and regression tests |
| `android-armv7.yml` | `pcm-dsp/**`, `audio-core/**` | Android 9 ARMv7 cross-build |
| `android-effect-library.yml` | Effect-library implementation and build inputs | Android effect ABI smoke test and ARMv7 build |
| `primary-hal-arm32-abi.yml` | `primary-hal/**` | Primary HAL ARM32 ABI bootstrap |
| `android9-hal-abi-layout.yml` | `audio-hal-abi/**` | Recovered ABI layout tests |
| `repository-integrity.yml` | Markdown, repository scripts, audit tools and workflow YAML | Documentation, scripts and workflow structure |
| `test-cd3313-emulator.yml` | `cd3313-emulator/**` | Host protocol and safety tests |

The PCM DSP host test suite is the default regression gate. The Android ARMv7 workflow no longer runs a second host benchmark. The Primary HAL ARM32 workflow does not start for AudioCore-only changes because the Android ARMv7 workflow already cross-compiles AudioCore.

The Android Effect Library workflow does not run for documentation-only changes. Workflow-file edits are validated by Repository Integrity rather than starting the build/test workflow being edited.

### Branch and concurrency behavior

Automatic private workflows are limited to maintained branches where applicable. Pull-request filters target `main` and `feature` (CD3313 also retains `integration/beta-0.1`).

Each automatic workflow uses a concurrency key based on `github.head_ref || github.ref`. A push and its pull-request event therefore share the same per-workflow branch key. A newer run cancels an obsolete in-progress run instead of allowing both to consume runner time. Different workflows and different branches remain independent.

## Manual-only private diagnostics

Run these only when the changed code warrants the additional validation:

- `audio-core-tsan.yml` — full AudioCore ThreadSanitizer suite.
- `pcm-stream-broker-tsan.yml` — focused PCM broker ThreadSanitizer suite.
- `pcm-dsp-stability-lab.yml` — extended stability run; overlaps the standard host test suite.
- `pcm-dsp-performance.yml` — performance benchmark.
- `build-android-test-apk.yml` — Android diagnostic APK.
- `primary-hal-binary-audit.yml` — expensive factory firmware binary audit.

CD3313 host tests remain automatic for relevant changes. ASan/UBSan and libFuzzer jobs run only when `test-cd3313-emulator.yml` is manually dispatched.

## Public firmware compute

Run compute directly in `14vvp93/public_calculations` → Actions:

- `Public firmware compute`: `inventory`, `verify`, `all-light` or `audio-inventory`.
- Nine firmware-analysis workers support direct `workflow_dispatch`.
- Each public worker has a per-ref concurrency group to cancel duplicate runs of the same worker.
- Public infrastructure validation also deduplicates push and pull-request runs by source branch.

The `inventory` and `all-light` task names currently execute the same inventory operation; `all-light` is retained as a compatibility alias. For `verify` and `audio-inventory`, inventory and manifest SHA-256 verification now happen in one file-reading pass instead of hashing the extracted dataset twice.

### Public worker I/O and artifact reductions

- The audio-HAL worker scans the canonical extracted tree in place and copies only the primary HAL binary needed as an artifact; it no longer duplicates the full extracted firmware tree.
- The audiomux-client scanner reads the canonical extracted tree directly instead of copying it into a second workspace directory.
- The firmware-libraries worker uses local symlinks to the canonical extracted firmware and dumps. Its artifact now contains analysis reports and focused outputs, not a second copy of the public firmware dataset.

The full-dataset integrity audit and the firmware-libraries analysis still perform independent integrity checks. They are separate manual tasks with different outputs; run only the one needed for a given investigation.

## Operational rules

1. Let the standard host regression suite run for PCM DSP/AudioCore changes.
2. Run Android cross-builds only when the corresponding Android-facing code changes.
3. Run TSAN, fuzzing, stability, performance and full binary audits on demand.
4. Prefer one coherent group of commits over a sequence of tiny commits.
5. Do not dispatch several full-dataset public workers when one focused worker answers the question.
6. Do not treat skipped/manual diagnostics as passing tests. Record which gates actually ran.
7. Do not copy public firmware into the private repository or private product source into public workflows.

## Audit findings and remaining limits

The workflow definitions were reviewed in both repositories. The changes reduce redundant push/PR runs, unrelated path-triggered jobs, duplicate SHA-256 reads in the public compute workflow, unnecessary full-tree copies, and oversized analysis artifacts.

This is not a claim that every possible minute has been eliminated:

- GitHub's account-level Actions billing/remaining-minute dashboard is not exposed through the repository workflow API used for this audit. Exact minute savings and remaining quota cannot be calculated here.
- At the audit time, private Actions jobs failed before runner allocation (`runner_id=0`, empty `runner_name`). This is a runner/quota availability problem, not evidence that builds or tests passed. Private code CI therefore remains unverified until a runner executes it.
- Public infrastructure validation passed on the final public workflow commit `865b6efaeff0c4547dab1f4456f2fa664b3611d1` (run `43461096014`, completed successfully). This validates public workflow YAML, Python scripts and public tool tests covered by that workflow.
- The ARMv7, Effect Library, Primary HAL and diagnostic APK workflows now use the Ubuntu 24.04 runner image's preinstalled Android NDK r27d (`27.3.13750724`) and install that exact version only as a fallback. The APK workflow also reuses the preinstalled Android 35 platform, Build Tools 35.0.0 and CMake 3.31.5, installing only missing packages. This removes recurring SDK Manager download/license/install work on the standard runner image while preserving pinned tool versions. AOSP headers are still shallow-cloned per run; caching them is deferred until its restore/save cost can be measured.
- Self-hosted runners or local CI can reduce dependence on GitHub-hosted private minutes, but require a maintained trusted machine and are not enabled by this change.

Private changes are on `feature`; private `main` was not changed. Public changes are on `main` at audit commit `865b6efaeff0c4547dab1f4456f2fa664b3611d1`.
