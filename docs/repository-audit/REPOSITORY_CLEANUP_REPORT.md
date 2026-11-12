# Repository cleanup and organization report

## Scope

Audit target: `14march93/mt8163_audio_engine`, working branch `feature/cd3313-driver-rewrite`.

The audit covers the tracked tree, project documentation, source/test layout, workflows, and the supplied ZIP archive sets. Original firmware inputs are treated as evidence and are not edited in place.

## Current tracked layout

| Area | Responsibility | Decision |
|---|---|---|
| `.github/workflows/` | CI, binary analysis, host tests, Android builds and archive workflows | Keep; obsolete single-archive extractor removed; stale archive triggers corrected |
| `android-test/` | Android bring-up and API probe app | Keep; test utility, not production engine |
| `cd3313-emulator/` | Host-side protocol/state emulator and tests | Keep; useful reference for the driver rewrite |
| `docs/` | Reverse-engineering, architecture, integration and test evidence | Keep; use `docs/README.md` as the navigation index |
| `kernel/cd3313/` | CD3313 rewrite plan and future implementation | Keep |
| `pcm-dsp/` | DSP library, Android JNI bridge, tests and benchmarks | Keep |
| `reference-materials/` | Original firmware archives and binary reference inputs | Keep originals immutable; curated extracted project inputs live in `reference-materials/extracted/` |
| `scripts/repository/` | Reproducible archive processing and curation scripts | Keep |
| `tools/` | ADB diagnostics and local firmware scanners | Keep |

## Archive extraction results

The four retained archive sets were assembled where split, CRC-tested, safely extracted and inventoried in GitHub Actions run [43198951304](https://github.com/14march93/mt8163_audio_engine/actions/runs/43198951304). The initial five-set run [43197649691](https://github.com/14march93/mt8163_audio_engine/actions/runs/43197649691) also verified the now-removed redundant `files.zip`.

- **1,416 files** extracted from the four retained archive sets.
- **Approximately 559.99 MiB** total uncompressed content.
- No retained archive set failed.
- The complete extraction is uploaded as workflow artifact `mt8163-reference-materials-expanded` with 30-day retention.
- The machine-readable inventory records each archive member path, byte size, SHA-256 and extraction destination.
- A permanent curated subset of **410 files (115.19 MiB)** is stored in `reference-materials/extracted/`. It includes the complete 325-file vendor library tree, all 33 vendor kernel modules, audio-related system libraries, audio configuration, AudioInputSource and carservices artifacts.

The full expansion is not committed indiscriminately. It includes unrelated Android system libraries and media assets, repeated copies, placeholder framework JARs, and a 161 MiB HEQ APK that exceeds GitHub's normal per-file limit. The curated audio-relevant subset is committed for direct project use. The original retained split archives remain available as immutable source evidence.

## Confirmed redundancy

- Every one of the 36 files in the former `files.zip` had a byte-identical SHA-256 match in the larger `files2.zip` set. It was a redundant subset, not a distinct firmware revision, and has been removed.
- `files2.zip` contains repeated copies of some files under alternate paths. Some are exact copies; some similarly named system/VNDK libraries are different builds and must not be collapsed by filename alone.
- Some vendor audio HAL libraries appear identically in `files2.zip` and `vendor_lib.zip`. The vendor library archive preserves their actual `lib/hw` or `lib` placement.
- Framework JAR entries in the partial `files2.zip` dump are tiny stubs (183–200 bytes); the meaningful compiled framework content is in OAT/VDEX/ART files.

## Cleanup decisions

1. Keep source code, tests, analysis documents, CI workflows and diagnostic tools.
2. Keep the original retained split archives and reference kernel module unchanged.
3. Promote the complete extracted `vendor_lib/lib` tree, audio-related system libraries, audio policy/configuration, AudioInputSource and carservices artifacts into a stable curated tree.
4. Keep the full HEQ APK in its split source archive and full-extraction artifact; do not try to commit a 161 MiB single file.
5. Do not delete binary libraries merely because their names look unrelated. They may be dependencies of the vendor HAL. Preserve the full vendor library set until dependency analysis proves an item can be dropped.
6. Remove only confirmed redundant repository-level files or obsolete workflows, and record each such removal here.
7. Removed the redundant `files.zip` archive (all 36 members byte-identical to files2.zip) and superseded `.github/workflows/unpack-files.yml` workflow.
8. Added a root `.gitignore` for generated build, analysis, extraction and editor files. The curated firmware references are not ignored.
9. Updated analysis workflows to use the curated extracted tree instead of the removed `files.zip` input.

## Safety and reproducibility

- Archive extraction rejects path traversal and symlink entries.
- Extraction is hash-aware and repeatable.
- Every promoted file is recorded with its source archive member and SHA-256 in `reference-materials/extracted/manifest.json`.
- Firmware inputs remain read-only research material; nothing in this cleanup process writes to a device.
- Deletion is based on content identity and documented use, not filename guesses.

## Remaining work

- Verify the final HAL analysis workflow against the curated reference tree.
- Add explicit dependency-closure analysis for the primary HAL and related audio services.
- Review remaining workflows for overlap, naming and trigger correctness.
- Run a repository-wide local Markdown-link check.
- Decide whether any remaining non-audio archive content warrants a separate long-term reference package.
