# Reference materials

This directory separates original firmware inputs from the curated files used directly by the MT8163 Audio Engine research.

## Original inputs — preserve unchanged

- `firmware-dumps/` — firmware images and archive inputs. The multipart sets are `files2.zip.001-.003`, `lib.zip.001-.004`, and `vendor_lib.zip.001-.002`.
- `heq/` — split HEQ archive parts `HEQ.zip.001` through `HEQ.zip.017`.
- `kernel/` — original reference `cd3313.ko` binary.

The former `files.zip` was removed after SHA-256 comparison proved that all 36 of its files were byte-identical to members of the larger `files2.zip` archive. It contained no distinct firmware revision.

Original firmware images, split archive parts and reference kernel binaries are read-only research inputs. Do not install or load them without separate compatibility and safety verification.

## Curated extracted inputs

- `extracted/system/` — audio-related Android system libraries, audio policy/configuration, AudioInputSource and carservices compiled artifacts.
- `extracted/vendor/` — complete extracted vendor library tree, including HAL dependencies and kernel modules, plus audio service/configuration files.
- `extracted/manifest.json` — source archive/member, destination, size and SHA-256 for every promoted file.
- `extracted/README.md` — curation scope and handling notes.

The full archive sets are safely extracted and inventoried by the repository archive workflow. Its full extraction is a temporary CI artifact; the curated audio-relevant subset is committed here so project work does not need to unpack the archives repeatedly. The 161 MiB HEQ APK remains inside its split source archive and the full-extraction artifact because GitHub rejects individual normal Git files above 100 MiB.

## Project source and tools

- `../pcm-dsp/` — PCM DSP engine, JNI bridge, tests and benchmarks.
- `../android-test/` — Android bring-up/test application.
- `../cd3313-emulator/` — CD3313 protocol/state emulator and host tests.
- `../kernel/cd3313/` — CD3313 driver rewrite plan and implementation.
- `../docs/` — architecture, reverse-engineering, integration and validation documents.
- `../.github/workflows/` — CI, analysis and archive extraction workflows.
