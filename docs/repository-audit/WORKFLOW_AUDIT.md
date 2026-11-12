# GitHub Actions workflow audit

Audit branch: `feature/cd3313-driver-rewrite`

## Current inventory

The repository now contains 16 retained workflows:

| Workflow | Responsibility |
|---|---|
| `analyze-audio-hal.yml` | Primary MT8163 Audio HAL ELF/symbol/relocation/disassembly and XML analysis |
| `analyze-cd3313-ioctl.yml` | Focused CD3313 ioctl ABI analysis |
| `analyze-cd3313.yml` | Full CD3313 module static analysis |
| `analyze-firmware-images.yml` | Firmware image and device-tree inspection |
| `analyze-firmware-libraries.yml` | Full library, VDEX/carservices, CD3313 and FM/WMT analysis |
| `android-armv7.yml` | ARMv7 Android native build and DSP benchmark |
| `android-effect-library.yml` | Android 9 effect library ABI build |
| `audit-reference-archives.yml` | Archive integrity, full extraction, persistent filesystem tree and HEQ chunk verification |
| `build-android-test-apk.yml` | Android bring-up/test APK build |
| `decompile-heq.yml` | HEQ APK decompilation directly from committed, hash-verified APK chunks |
| `firmware-audio-config-audit.yml` | Extracted firmware manifest/hash/XML and audio configuration validation |
| `pcm-dsp-performance.yml` | DSP performance benchmark |
| `pcm-dsp-stability-lab.yml` | DSP stability and stress tests |
| `scan-audiomux-clients.yml` | Search extracted firmware for CD3313 userspace clients |
| `test-cd3313-emulator.yml` | CD3313 emulator host and sanitizer tests |
| `test-pcm-dsp.yml` | PCM DSP host tests |

## Confirmed cleanup

- Removed `unpack-files.yml`: it only extracted the redundant `files.zip` archive and was superseded by the full archive pipeline.
- Removed `reassemble-heq.yml`: HEQ is now committed as three verified APK chunks; decompilation reads those chunks directly and no longer needs to reassemble/extract the source ZIP.
- Updated the primary HAL analysis and CD3313 client scan to read `reference-materials/extracted/`, not the deleted `files.zip`.
- Reworked the firmware audio configuration audit to validate the committed manifest and XML directly instead of partially extracting split archives.
- Removed all stale `files.zip` references from workflow files.
- Corrected the archive audit's branch handling: checkout, concurrency, rebase and push now use the triggering ref rather than always writing to the feature branch.
- The remaining workflows have distinct build, test, reverse-engineering or validation purposes. No workflow installs firmware or writes to the target device.

## Workflow verification

The following runs completed successfully against the full extracted tree:

- [Archive extraction, tree promotion and HEQ chunk verification — 43201056884](https://github.com/14march93/mt8163_audio_engine/actions/runs/43201056884)
- [Firmware manifest/hash/XML audit — 43201262083](https://github.com/14march93/mt8163_audio_engine/actions/runs/43201262083)
- [Primary HAL analysis — 43201246997](https://github.com/14march93/mt8163_audio_engine/actions/runs/43201246997)
- [CD3313 userspace client scan — 43201256048](https://github.com/14march93/mt8163_audio_engine/actions/runs/43201256048)

A deeper full-library/VDEX/FM analysis is running separately; its result must be recorded only after completion.

## Branch and permission notes

- Archive audit requires `contents: write` because it commits the generated manifest and extracted tree. All analysis-only workflows should use read-only permissions.
- The archive audit now uses the triggering Git ref, so a run on main does not accidentally write to the feature branch.
- Firmware/HAL analysis workflows are enabled on main and the current feature branch where useful. Build/test workflows retain their pull-request triggers where applicable.
- Some specialized CD3313 and firmware-image workflows remain main/manual-triggered; they are not needed for this cleanup validation.

## Scope limitation

This is a structural and execution audit of the retained workflows, their inputs and their latest cleanup-related runs. It is not a complete third-party action supply-chain audit or a guarantee that every workflow has passed on every branch.


## Fresh portability pass — 2026-11-12

A file-level pass was repeated across the 16 retained workflow files on
`feature/cd3313-driver-rewrite`.

- No workflow contains a hard-coded `14march93/mt8163_audio_engine` or
  `14vvp93/mt8163_audio_engine` repository target in its executable workflow logic.
- No workflow pins checkout or push operations to the old feature branch.
- `audit-reference-archives.yml` is the only retained workflow found to push commits.
  It checks out `${{ github.ref }}`, scopes concurrency to that ref, and pushes to
  `${GITHUB_REF_NAME}`; it is therefore branch-portable. It requires
  `contents: write`; analysis-only workflows should remain read-only.
- Branch filters intentionally limit automatic runs. The current feature branch is
  explicitly included in `analyze-audio-hal.yml`,
  `analyze-firmware-libraries.yml`, `decompile-heq.yml`,
  `firmware-audio-config-audit.yml`, and `scan-audiomux-clients.yml`.
  `analyze-firmware-images.yml` and `test-cd3313-emulator.yml` are main-only.
  These are trigger filters, not hard-coded checkout targets; manual dispatch remains
  available where declared.
- `android-effect-library.yml` pins its AOSP source checkout to
  `android-9.0.0_r61`. `analyze-firmware-images.yml` clones mkbootimg without a
  pinned commit, and `analyze-firmware-libraries.yml` clones vdexExtractor without
  a pinned commit; these are reproducibility follow-ups.
- Retained workflows use major-version action tags such as `actions/checkout@v5`.
  Full-SHA action pinning and a third-party supply-chain review remain separate work.

## Separation plan for the public compute repository

The public compute worker is initialized at
`14vvp93/public_calculations`. The following reverse-engineering and firmware-data
jobs are compute candidates: HAL/CD3313/firmware-image/library analysis, archive
inventory and extraction, HEQ decompilation, firmware audio-config audit, and
audiomux-client scanning. Product build/release jobs and product-facing tests should
remain indexed by the canonical product repository until their source checkout and
artifact contracts are deliberately moved. Do not duplicate the authoritative
product implementation in the worker repository.

Moving a job is not complete merely because its YAML is copied. Its input paths,
source ref, tool versions, artifact schema, permissions, output retention, and
report destination must be adapted and tested in the worker repository.
