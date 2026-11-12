# Repository structure and ownership

This repository is the private, canonical MT8163 Audio Engine project. It contains product code, build definitions, tests, engineering documentation and reviewed findings. Firmware data is maintained separately in the public compute/data repository.

## Private repository: `14vvp93/mt8163_audio_engine`

| Path | Purpose | Policy |
|---|---|---|
| `.github/workflows/` | Product CI, Android builds, host tests and entry points that invoke public compute workers | Keep product workflows here; firmware-heavy analysis runs in the public worker. |
| `android-test/` | Android-side probes and bring-up utilities | Test-only; not a production system component. |
| `cd3313-emulator/` | Host-side CD3313 protocol/register/state model | Keep independent of kernel code; share evidence through docs and tests. |
| `docs/` | Architecture, reverse-engineering evidence, audits, project status and public-compute contracts | Canonical documentation location. |
| `kernel/cd3313/` | CD3313 kernel-driver rewrite | Target-kernel code and driver-specific tests belong here. |
| `pcm-dsp/` | PCM/DSP algorithms, Android effect adapter, tests and benchmarks | Keep algorithms independent from Android-specific adapters. |
| `scripts/repository/` | Private repository maintenance utilities | Keep only tools that operate on project code or documentation. |
| `tools/` | Diagnostic and reverse-engineering utilities | State whether each utility reads or modifies a device. |

## Public firmware and compute repository

All firmware data and firmware-only heavy compute belong in `14vvp93/public_calculations`:

- `firmware/dumps/mt8163/` — standalone firmware images, configuration dumps and reference kernel module.
- `firmware/extracted/` — verified extracted System/Vendor filesystem and HEQ reference payload.
- `firmware/manifests/` — SHA-256 provenance and extracted-file inventories.
- `.github/workflows/` — reusable firmware analysis and compute workers.
- `scripts/firmware/` and `tests/` — worker-side processing and tests.

Do not copy product implementation code or project documentation into the public repository. Do not store firmware archives, dumps or extracted firmware under this private repository.

## Data flow and execution

1. The private repository invokes a reusable worker workflow in `public_calculations`.
2. The public worker checks out its own firmware dataset and analysis tools.
3. Firmware-only analysis reads public firmware data directly; no private firmware checkout or read token is required.
4. Results are uploaded as GitHub Actions artifacts attached to the caller's run.
5. Only reviewed findings are promoted into the private documentation.

## Naming and file placement

- Use lowercase kebab-case for new directory and file names, except where upstream names must be preserved for ABI or forensic accuracy.
- Preserve original binary names and source partition paths in the public firmware tree.
- Use descriptive Markdown names in `docs/`, grouped by subsystem prefix (for example `CD3313_` and `MT8163_`).
- Keep one authoritative documentation index at `docs/README.md` and one project entry point at the root `README.md`.

## Generated and temporary content

Do not commit build directories, object files, intermediate shared libraries, temporary unpack directories, caches, test logs or workflow artifacts. Large compute outputs belong in Actions artifacts. Commit only intentional test fixtures that are small, reproducible and necessary for offline tests.

## Duplicate and deletion policy

1. Compare SHA-256, not filenames, before classifying firmware files as duplicates.
2. Preserve different builds even when basenames match; System, VNDK and Vendor copies may be ABI- or version-distinct.
3. Preserve original path context for firmware files in the public data repository.
4. Delete a tracked item only when its redundancy or lack of project value is demonstrated and documented.
5. Keep manifests and verified extracted firmware as the canonical public dataset.
6. Never delete the only copy of a firmware binary, configuration, source file or test fixture.

## Historical archive handling

The original split ZIP volumes were removed from both repositories after the full 1,386-file extracted tree was copied to the public repository and SHA-256 verified. The raw standalone dumps remain public. See [firmware dataset migration](public-compute/FIRMWARE_DATASET_LAYOUT.md) for counts, sizes, source commit and integrity manifests.
