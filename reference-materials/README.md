# Reference materials

This directory keeps binary inputs and firmware artifacts out of the repository root and separate from buildable source code.

## Contents

- `firmware-dumps/` — original firmware/archive inputs (`files.zip` and split `files2.zip.*` parts). Preserve part names and ordering when reassembling.
- `heq/` — split HEQ archive parts (`HEQ.zip.001` onward).
- `kernel/` — reference CD3313 kernel module (`cd3313.ko`).

These files are retained for reverse engineering and reproducibility. They are not build outputs and must not be installed or loaded on a device without separate compatibility and safety verification. The source code, tests, workflows, and technical notes remain in their existing project directories.

## Repository organization

- `pcm-dsp/` — PCM DSP engine, JNI bridge, tests, and benchmarks.
- `android-test/` — Android test application.
- `cd3313-emulator/` — CD3313 protocol/emulation research.
- `docs/` — architecture, reverse-engineering, integration-gate, and test documentation.
- `.github/workflows/` — CI and analysis workflows.
- `reference-materials/` — original binary research inputs.
