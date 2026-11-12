# Repository cleanup and organization report

## Scope

Audit target: `14march93/mt8163_audio_engine`, working branch `feature/cd3313-driver-rewrite`.

The audit covers the tracked tree, project documentation, source/test layout, workflows, and all supplied ZIP archive sets. Original firmware inputs are treated as evidence and are not edited in place.

## Current tracked layout

| Area | Responsibility | Decision |
|---|---|---|
| `.github/workflows/` | CI, binary analysis, host tests, Android builds and archive workflows | Keep; obsolete single-archive extractor removed; stale archive triggers corrected |
| `android-test/` | Android bring-up and API probe app | Keep; test utility, not production engine |
| `cd3313-emulator/` | Host-side protocol/state emulator and tests | Keep; useful reference for the driver rewrite |
| `docs/` | Reverse-engineering, architecture, integration and test evidence | Keep; use `docs/README.md` as the navigation index |
| `kernel/cd3313/` | CD3313 rewrite plan and future implementation | Keep |
| `pcm-dsp/` | DSP library, Android JNI bridge, tests and benchmarks | Keep |
| `reference-materials/` | Original firmware archives and complete extracted filesystem tree | Keep original inputs immutable; use `reference-materials/extracted/` for direct analysis |
| `scripts/repository/` | Reproducible archive processing and curation scripts | Keep |
| `tools/` | ADB diagnostics and local firmware scanners | Keep |

## Archive extraction results

The four retained archive sets were assembled where split, CRC-tested, safely extracted and inventoried in GitHub Actions run [43198951304](https://github.com/14march93/mt8163_audio_engine/actions/runs/43198951304). The current full-tree promotion was completed in [43200636495](https://github.com/14march93/mt8163_audio_engine/actions/runs/43200636495).

- **1,416 archive members** extracted from the four retained archive sets.
- **Approximately 559.99 MiB** total uncompressed archive content.
- No retained archive set failed CRC or path-safety checks.
- The persistent filesystem tree contains **1,386 files (551.39 MiB)** after removing 32 byte-identical .so copies from the flattened files2 listing and representing the 161 MiB HEQ APK as three GitHub-safe chunks.
- The persistent tree contains 1,041 system files, 342 vendor files and 3 HEQ APK chunks.
- The machine-readable manifest records each destination, source archive member, byte size and SHA-256.
- The complete raw extraction is also uploaded as workflow artifact `mt8163-reference-materials-expanded` with 30-day retention.

The extracted tree is committed under `reference-materials/extracted/`. It includes all 766 files from `lib.zip`, all 325 files from `vendor_lib.zip`, every non-.so member from `files2.zip`, and the complete HEQ APK bytes split into three chunks. The 32 shared libraries from files2.zip were verified byte-identical to copies in the two library archives and are represented at their canonical system/vendor paths instead of duplicated.

## Confirmed redundancy

- Every one of the 36 files in the former `files.zip` had a byte-identical SHA-256 match in the larger `files2.zip` set. It was a redundant subset, not a distinct firmware revision, and has been removed.
- `files2.zip` contains repeated copies of some files under alternate paths. Some are exact copies; some similarly named system/VNDK libraries are different builds and must not be collapsed by filename alone.
- Some vendor audio HAL libraries appear identically in `files2.zip` and `vendor_lib.zip`. The vendor library archive preserves their actual `lib/hw` or `lib` placement.
- Framework JAR entries in the partial `files2.zip` dump are tiny stubs (183–200 bytes); they are retained as evidence, while the meaningful compiled framework content is in OAT/VDEX/ART files.

## Cleanup decisions

1. Keep source code, tests, analysis documents, CI workflows and diagnostic tools.
2. Keep the original retained split archives and reference kernel module unchanged.
3. Persist the full extracted system and vendor library trees and all remaining files2 members in filesystem layout.
4. Split the 161 MiB HEQ APK into three files below GitHub's 100 MiB per-file limit; keep its original 17 archive volumes unchanged.
5. Do not delete binary libraries merely because their names look unrelated. They may be dependencies of the vendor HAL.
6. Remove only confirmed redundant repository-level files or obsolete workflows, and record each such removal here.
7. Removed the redundant `files.zip` archive (all 36 members byte-identical to files2.zip) and superseded `.github/workflows/unpack-files.yml` workflow.
8. Added a root `.gitignore` for generated build, analysis, extraction and editor files. The committed firmware reference tree is not ignored.
9. Updated audio HAL and CD3313 client analysis workflows to use the extracted tree instead of the removed `files.zip` input.

## Safety and reproducibility

- Archive extraction rejects path traversal and symlink entries.
- Extraction is hash-aware and repeatable.
- Every committed extracted file is recorded with its source archive member and SHA-256 in `reference-materials/extracted/manifest.json`.
- The three HEQ APK chunks have individual hashes and a full APK hash in `reference-materials/extracted/heq/HEQ_APK_MANIFEST.json`.
- Firmware inputs remain read-only research material; nothing in this cleanup process writes to a device.
- Deletion is based on content identity and documented use, not filename guesses.

## Additional firmware findings

The full binary scan covered 1,117 native ARM objects. It identified direct factory `/dev/audiomux` and `/dev/carstatus` references inside `libcarservices_jni.so`, `libaudiopolicymanagerdefault.so` and `libaudioflinger.so`; three alternative DSP modules (`cd3313.ko`, `fy7604.ko`, `ak7604.ko`) expose an audiomux misc-device design; `carstatus.ko` has an unresolved audiomux interaction; and the primary HAL contains a substantial HFP call-audio path with NVRAM-backed parameters. These findings are recorded in [FIRMWARE_ANALYSIS_SUMMARY.md](FIRMWARE_ANALYSIS_SUMMARY.md) and [FACTORY_DEFECTS.md](../FACTORY_DEFECTS.md).

## Remaining work

- Repository-wide Markdown link check passed: 173 local links across 61 Markdown files (sparse-checkout/Git-tree run 43203308565).
- Continue call-graph and dependency-closure analysis, especially framework-level audiomux access and the primary HAL dependency graph.
- Confirm which alternative DSP kernel module is selected on the target board.
- Keep target-device validation separate from static and host-side evidence.
