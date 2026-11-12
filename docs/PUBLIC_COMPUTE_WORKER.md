# Public compute worker

## Repository ownership

- `14vvp93/mt8163_audio_engine` is the canonical product repository. It owns all
  product source code, tests, build definitions, engineering decisions and authoritative
  product documentation.
- `14vvp93/public_calculations` is the public compute/data repository. It owns
  approved public firmware archives, large non-secret reference files, heavy analysis
  scripts, extracted data, reports and compute artifacts.

Do not create new work in repositories under `14march93`. Those repositories are
historical references only and are not part of the active workflow.

## Run heavy compute from the primary repository

Open **Actions → Run public firmware compute → Run workflow** in this repository.
Choose one bounded task:

- `inventory` — inventory and hash the worker's approved firmware archives.
- `extract` — safely reassemble and extract split ZIP archives.
- `audio-inventory` — extract archives and index the Android audio stack and DSP
  modules.
- `all-light` — run the lightweight archive inventory task.

The primary workflow calls the reusable worker workflow at
`14vvp93/public_calculations/.github/workflows/compute.yml@main`. The worker checks
out its own scripts and public data in an isolated `worker/` directory. It does not
need a cross-repository write token, and it does not copy product source code into the
public repository.

The output is uploaded as a GitHub Actions artifact attached to the caller's run in
this primary repository. Use the run's **Artifacts** section to retrieve the reports.

## Run firmware analysis

Open **Actions → Run firmware analysis on public compute → Run workflow**.
Choose one workload and a source revision (normally `main`). The nine workloads are:

- Audio HAL ELF, symbols, relocations, disassembly and XML checks.
- CD3313 ioctl ABI and event-queue analysis.
- CD3313 kernel-module static analysis.
- Boot, recovery and DTBO image inspection.
- Firmware library and vendor-stack analysis.
- Reference archive audit, split-volume reassembly and curated-tree generation.
- HEQ APK reassembly, decompilation and DSP reference extraction.
- Extracted firmware audio-configuration validation.
- CD3313/audiomux userspace-client scan.

These jobs execute in the public worker repository but check out the selected primary
repository revision read-only. Reports are uploaded as artifacts to the caller's run.
The archive audit no longer commits generated inventory or extracted files back into
the primary repository.

The former product-side workflow files are retained temporarily as migration backups.
They should be disabled or archived only after the corresponding worker task has passed
an end-to-end run.

## Firmware import

Approved public firmware is imported separately by the worker's
`Import approved public MT8163 firmware` workflow. Its
`SOURCE_REPO_READ_TOKEN` secret must have read-only Contents access to
`14vvp93/mt8163_audio_engine`. If that secret was created for the former account,
replace it with a token scoped to the current primary repository.

## Separation and security rules

- Never put credentials, private keys, passwords, tokens or private signing material
  into `public_calculations`.
- Assume all public worker files, logs and artifacts are public.
- Do not automatically push generated reports or extracted trees back into the
  product repository.
- Promote only reviewed, compact technical findings with source hashes and worker run
  references.
- Keep Android product builds and implementation tests in the primary repository.
- Move firmware-only static analysis to the worker in separately reviewed stages; do
  not delete or disable existing analysis workflows until each replacement is verified.
