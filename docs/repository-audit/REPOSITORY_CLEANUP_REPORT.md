# Repository cleanup and organization report

## Scope

Audit target: `14march93/mt8163_audio_engine`, working branch `feature/cd3313-driver-rewrite`.

The audit covers the tracked tree, project documentation, source/test layout, workflows, and all five supplied ZIP archive sets. Original firmware inputs are treated as evidence and are not edited in place.

## Current tracked layout

| Area | Responsibility | Decision |
|---|---|---|
| `.github/workflows/` | CI, binary analysis, host tests, Android builds and archive workflows | Keep; remove obsolete single-archive extractor after replacement is verified |
| `android-test/` | Android bring-up and API probe app | Keep; test utility, not production engine |
| `cd3313-emulator/` | Host-side protocol/state emulator and tests | Keep; useful reference for the driver rewrite |
| `docs/` | Reverse-engineering, architecture, integration and test evidence | Keep; use `docs/README.md` as the navigation index |
| `kernel/cd3313/` | Driver rewrite plan and future implementation | Keep |
| `pcm-dsp/` | DSP library, Android JNI bridge, tests and benchmarks | Keep |
| `reference-materials/` | Original firmware archives and binary reference inputs | Keep originals immutable; put curated extracted project inputs in `reference-materials/extracted/` |
| `scripts/repository/` | Reproducible archive processing and curation scripts | Keep |
| `tools/` | ADB diagnostics and local firmware scanners | Keep |

## Archive extraction results

All five archive sets were assembled where split, CRC-tested, safely extracted and inventoried in GitHub Actions run [43197649691](https://github.com/14march93/mt8163_audio_engine/actions/runs/43197649691).

- 1,452 files extracted.
- 563.23 MiB total uncompressed content.
- No archive set failed.
- Full extraction is uploaded as workflow artifact `mt8163-reference-materials-expanded` with 30-day retention.
- The machine-readable file inventory records each member path, byte size, SHA-256 and extraction destination.

The full expansion is intentionally not committed indiscriminately. It includes unrelated Android system libraries and media assets, repeated copies, placeholder framework JARs, and a 161 MiB HEQ APK that exceeds GitHub's normal per-file limit. A curated, audio-relevant subset is promoted into `reference-materials/extracted/` for permanent, direct use. The original split archives remain available as immutable source evidence.

## Confirmed redundancy

- Every one of the 36 files in `files.zip` has a byte-identical SHA-256 match in the larger `files2.zip` set. The smaller archive is therefore a redundant subset, not a distinct firmware revision.
- `files2.zip` contains repeated copies of some files under alternate paths. Some are exact copies; some similarly named system/VNDK libraries are different builds and must not be collapsed by filename alone.
- Some vendor audio HAL libraries appear identically in `files2.zip` and `vendor_lib.zip`. The vendor library archive preserves their actual `lib/hw` or `lib` placement.
- Framework JAR entries in the partial `files2.zip` dump are tiny stubs (183–200 bytes); the meaningful compiled framework content is in OAT/VDEX/ART files.

## Cleanup decisions

1. Keep source code, tests, analysis documents, CI workflows and diagnostic tools.
2. Keep the original split archives and reference kernel module unchanged.
3. Promote the complete extracted `vendor_lib/lib` tree, audio-related system libraries, audio policy/configuration, AudioInputSource and carservices artifacts into a stable curated tree.
4. Keep the full HEQ APK in the source archive and full-extraction artifact; do not try to commit a 161 MiB single file.
5. Do not delete binary libraries merely because their names look unrelated. They may be dependencies of the vendor HAL. Preserve the full vendor library set until dependency analysis proves an item can be dropped.
6. Remove only confirmed redundant repository-level files or obsolete workflows, and record each such removal here.
7. Do not commit build outputs, temporary extraction directories, generated APKs, or CI caches unless they are intentional release artifacts.

## Safety and reproducibility

- Archive extraction rejects path traversal and symlink entries.
- Extraction is hash-aware and repeatable.
- Every promoted file is recorded with its source archive member and SHA-256 in `reference-materials/extracted/manifest.json`.
- Firmware inputs remain read-only research material; nothing in this cleanup process writes to a device.
- Deletion is based on content identity and documented use, not filename guesses.

## Remaining work

- Verify the curated tree and its manifest after the archive workflow completes.
- Add explicit dependency-closure analysis for the primary HAL and related audio services.
- Review each workflow for overlap, naming and trigger correctness.
- Check all Markdown links and workflow path references after cleanup.
- Decide whether any remaining non-audio archive content warrants a separate, long-term reference package.
