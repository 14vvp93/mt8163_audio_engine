# GitHub Actions workflow audit

Audit branch: `feature/cd3313-driver-rewrite`

## Inventory

The repository currently contains 17 workflows:

| Workflow | Responsibility |
|---|---|
| `analyze-audio-hal.yml` | Primary audio HAL static analysis |
| `analyze-cd3313-ioctl.yml` | CD3313 ioctl analysis |
| `analyze-cd3313.yml` | CD3313 binary/protocol analysis |
| `analyze-firmware-images.yml` | Firmware image inspection |
| `analyze-firmware-libraries.yml` | Firmware library inspection |
| `android-armv7.yml` | ARMv7 Android native build |
| `android-effect-library.yml` | Android effect library build |
| `audit-reference-archives.yml` | Archive extraction, inventory and curation |
| `build-android-test-apk.yml` | Android probe APK build |
| `decompile-heq.yml` | HEQ APK decompilation |
| `firmware-audio-config-audit.yml` | Firmware audio configuration audit |
| `pcm-dsp-performance.yml` | DSP performance benchmark |
| `pcm-dsp-stability-lab.yml` | DSP stability tests |
| `reassemble-heq.yml` | HEQ split archive reassembly |
| `scan-audiomux-clients.yml` | Userspace audiomux client scan |
| `test-cd3313-emulator.yml` | CD3313 emulator host tests |
| `test-pcm-dsp.yml` | PCM DSP host tests |

## Findings

- All 17 workflow files were readable from the working branch.
- All 17 use explicit workflow triggers; the inspected set includes `push` and/or `workflow_dispatch`, while the build/test workflows also include `pull_request`.
- Sixteen workflows check out the repository. `decompile-heq.yml` does not use checkout; its steps must therefore be reviewed as a self-contained download/decompile job rather than assumed to read the current repository.
- Two workflows hard-code the working branch in their checkout configuration:
  - `audit-reference-archives.yml`
  - `firmware-audio-config-audit.yml`
  This is intentional for the current cleanup branch, but must be changed to a branch/ref input or the intended integration branch before the workflow is expected to run on other branches.
- The two archive/configuration audit workflows may both be triggered by repository changes. Their path filters and outputs should be compared before combining them; they perform different analyses and should not be merged solely because both inspect firmware material.
- No workflow was deleted during this audit. A workflow is not obsolete merely because another workflow has a similar name.

## Required follow-up

1. Inspect every workflow's complete path filters and shell commands.
2. Confirm whether the two branch-pinned audit workflows are intentionally branch-specific.
3. Review action versions, permissions, artifact retention and generated-file commits.
4. Add path filters to broad workflows only where they will not suppress required validation.
5. Keep binary-analysis workflows separate when their inputs or outputs differ.
6. Run the relevant workflows after any cleanup change and record actual run URLs and outcomes.

## Audit limitation

This is a structural inventory and trigger/checkout scan, not a complete security review of every third-party action or shell command. No claim is made here that all workflows have passed recently.
