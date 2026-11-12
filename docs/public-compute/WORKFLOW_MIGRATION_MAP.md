# Workflow migration map

Canonical product repository: `14vvp93/mt8163_audio_engine`.
Public compute repository: `14vvp93/public_calculations`.

The product repository owns implementation, source code, tests, product build outputs,
and authoritative engineering documentation. The public compute repository owns
approved public firmware inputs, large non-secret reference data, expensive static
analysis, extraction, reproducible compute scripts, and generated reports.

## Move/adapt to the public compute worker

These jobs analyze public firmware evidence and are natural worker workloads:

| Product workflow | Worker responsibility | Required adaptation |
|---|---|---|
| `analyze-audio-hal.yml` | HAL ELF, symbols, relocations, disassembly, XML | Read imported firmware tree; publish report/artifact only |
| `analyze-cd3313-ioctl.yml` | CD3313 ioctl ABI analysis | Read `cd3313.ko`; keep static evidence separate from hardware claims |
| `analyze-cd3313.yml` | CD3313 module static analysis | Same immutable input/hash contract |
| `analyze-firmware-images.yml` | boot/recovery/DTBO inspection | Pin mkbootimg tool revision |
| `analyze-firmware-libraries.yml` | ELF/library/dependency and vendor analysis | Pin vdexExtractor and all external tools |
| `audit-reference-archives.yml` | Archive integrity and full extraction | Use split-aware processor and SHA-256 manifests; never auto-commit expanded trees |
| `decompile-heq.yml` | HEQ archive/APK decompilation | Reassemble verified HEQ parts; record APK and tool hashes |
| `firmware-audio-config-audit.yml` | Audio configuration and manifest audit | Read imported extracted tree and report evidence |
| `scan-audiomux-clients.yml` | Find userspace CD3313 clients | Search extracted firmware and emit machine-readable findings |

## Keep product-owned

| Product workflow | Reason |
|---|---|
| `android-armv7.yml` | Builds product binaries; release artifacts belong to the canonical product repo |
| `android-effect-library.yml` | Product Android 9 ABI build |
| `build-android-test-apk.yml` | Product-facing diagnostic APK |
| `test-cd3313-emulator.yml` | Tests canonical emulator implementation |
| `test-pcm-dsp.yml` | Tests canonical PCM/DSP implementation |
| `pcm-dsp-performance.yml` | Product performance regression baseline |
| `pcm-dsp-stability-lab.yml` | Product stability and stress gate |
| `repository-integrity.yml` | Validates canonical product documentation and links |

Product tests may later be executed on the compute runner through a protected read-only
checkout of the private product repository. Do not copy product source code into this
public repository. If such a bridge is added, logs/artifacts must contain only approved
test output and no credentials or private source bundles.

## Cross-repository contract

1. The product repository is the source of truth for code and approved source firmware.
2. The worker repository is the source of truth for public reference archives and heavy
   compute scripts; it is not a second product source repository.
3. Every worker run must identify the product commit SHA, worker commit SHA, input
   SHA-256 values, task, tool versions, and output schema.
4. The product repository should invoke expensive jobs explicitly or through narrowly
   filtered path triggers. Do not run a full firmware analysis for ordinary source-code
   edits.
5. Worker outputs are artifacts/reports. Promote only reviewed, compact findings to the
   product repository; never automatically push generated trees or binaries into it.
6. Keep tokens in Actions secrets only. The worker is public, so never print secrets or
   expose them to untrusted pull-request code.
7. Do not install firmware or write to a physical MT8163 device from these workflows.

## Current status

The repository migration and public firmware import infrastructure exist. The firmware
import workflow now targets `14vvp93/mt8163_audio_engine`. Archive inventory and
split-ZIP extraction are implemented in the worker. The nine firmware-analysis
workflows above remain product-side and are not yet fully ported to the worker; migrate
them task-by-task while preserving their current reports and validation behavior.
