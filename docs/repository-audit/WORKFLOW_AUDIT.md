# GitHub Actions workflow audit — 2026-11-12

## Repository ownership

- Private `14vvp93/mt8163_audio_engine`: product builds, host tests, Android tests, repository integrity and operator-facing entry points.
- Public `14vvp93/public_calculations`: firmware-only heavy analysis and compute workflows.

## Public reusable workers

The nine public firmware workers read inputs from the public repository's own checkout. They do not check out private source code and do not require `SOURCE_REPO_READ_TOKEN`.

| Workflow | Responsibility | Validation |
|---|---|---|
| `worker-analyze-audio-hal.yml` | HAL ELF, symbols, relocations, disassembly | Passed |
| `worker-analyze-cd3313-ioctl.yml` | CD3313 ioctl ABI | Passed |
| `worker-analyze-cd3313.yml` | CD3313 module static analysis | Passed |
| `worker-analyze-firmware-images.yml` | Boot, recovery and DTBO inspection | Passed |
| `worker-analyze-firmware-libraries.yml` | Extracted libraries, VDEX, CD3313 and FM/WMT analysis | Passed |
| `worker-audit-firmware-integrity.yml` | SHA-256 manifest and HEQ APK verification | Passed |
| `worker-decompile-heq.yml` | HEQ APK reassembly and JADX analysis | Passed |
| `worker-firmware-audio-config-audit.yml` | Firmware manifest, XML and audio config audit | Passed |
| `worker-scan-audiomux-clients.yml` | CD3313/audiomux userspace-client scan | Passed |

The nine-job end-to-end validation completed successfully in [run 43238133570](https://github.com/14vvp93/public_calculations/actions/runs/43238133570).

## Public compute entry point

`compute.yml` exposes bounded tasks:

- `inventory`
- `verify`
- `all-light`
- `audio-inventory`

The two full compute validation tasks passed in [run 43239612147](https://github.com/14vvp93/public_calculations/actions/runs/43239612147).

## Infrastructure checks

- Public YAML parsing, Python compilation, firmware-tool tests and CLI help checks passed in [run 43239765157](https://github.com/14vvp93/public_calculations/actions/runs/43239765157).
- Private Markdown link integrity passed in [run 43240367918](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43240367918).

## Removed from private project

The nine duplicated firmware-analysis workflows were removed from the private repository after their public equivalents passed end-to-end. The private repository retains product-owned Android builds, DSP/emulator tests, stability/performance tests, repository integrity and the two manual entry points that call public workers.

## Removed from public compute repository

- The one-time private-firmware import workflow and its approval marker were removed after successful synchronization.
- The obsolete archive-audit worker was replaced by the extracted-firmware integrity worker.
- Temporary worker-validation harnesses and trigger files were removed after the successful test run.
- Public project docs, analysis reports and placeholder analysis directories were moved to or consolidated in the private documentation tree.

## Security and operating notes

- Public workers require only read access to their own public repository checkout.
- No current worker references `SOURCE_REPO_READ_TOKEN`.
- Keep public workflow permissions read-only unless a specific reviewed workflow needs a write permission.
- Generated reports and large intermediate files are Actions artifacts; neither repository is automatically modified by analysis jobs.
- The unused `SOURCE_REPO_READ_TOKEN` Actions secret should be manually removed from both repositories.
