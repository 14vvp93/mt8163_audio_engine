# Reference materials

This directory separates original firmware inputs from the complete extracted filesystem tree used by MT8163 Audio Engine research.

## Original inputs — preserve unchanged

- `firmware-dumps/` — firmware images and archive inputs. The retained multipart sets are `files2.zip.001-.003`, `lib.zip.001-.004`, and `vendor_lib.zip.001-.002`.
- `heq/` — original split HEQ archive parts `HEQ.zip.001` through `HEQ.zip.017`.
- `kernel/` — original reference `cd3313.ko` binary.

The former `files.zip` was removed after SHA-256 comparison proved that all 36 of its files were byte-identical to members of the larger `files2.zip` archive. It contained no distinct firmware revision.

## Permanent extracted filesystem

- `extracted/system/` — all 766 system library files from `lib.zip`, including the VNDK-28 variants, plus every non-.so member from `files2.zip` under system and framework.
- `extracted/vendor/` — the complete 325-file `vendor_lib.zip` tree under vendor/lib, plus every non-.so member from `files2.zip` under vendor.
- `extracted/heq/` — the extracted 161 MiB HEQ APK, stored as three chunks below GitHub's 100 MiB per-file limit. Reassemble using the command in `extracted/heq/README.md`.
- `extracted/manifest.json` — source archive/member, destination, size and SHA-256 for every extracted file.
- `extracted/README.md` — directory map and handling instructions.

All 32 shared-library files listed in `files2.zip` were verified by SHA-256 to be identical to files in `lib.zip` or `vendor_lib.zip`; the repository stores them once at their canonical filesystem paths. This avoids duplicate binaries while preserving the original firmware paths.

The full archive sets are safely assembled, CRC-tested and inventoried by the repository archive workflow. The expanded tree is now committed here, so normal project work does not need to unpack the archives again.

Original firmware images, split archive parts and reference kernel binaries are read-only research inputs. Do not install or load them without separate compatibility and safety verification.

## Project source and tools

- `../pcm-dsp/` — PCM DSP engine, JNI bridge, tests and benchmarks.
- `../android-test/` — Android bring-up/test application.
- `../cd3313-emulator/` — CD3313 protocol/state emulator and host tests.
- `../kernel/cd3313/` — CD3313 driver rewrite plan and implementation.
- `../docs/` — architecture, reverse-engineering, integration and validation documents.
- `../.github/workflows/` — CI, analysis and archive extraction workflows.
