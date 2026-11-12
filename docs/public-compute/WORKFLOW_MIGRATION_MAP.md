# Workflow migration map — completed

## Repository responsibilities

- Private 14vvp93/mt8163_audio_engine: product implementation, Android and host builds, tests, engineering documentation and reviewed findings.
- Public 14vvp93/public_calculations: approved firmware data, integrity manifests, reusable heavy-analysis workflows, compute scripts and worker tests.

## Firmware workers migrated and verified

All nine firmware-analysis workflows now read their inputs directly from public_calculations/firmware/. They no longer check out the private repository or require SOURCE_REPO_READ_TOKEN.

| Worker | Public data used | End-to-end result |
|---|---|---|
| Audio HAL | Extracted Vendor HAL | Passed |
| CD3313 ioctl ABI | Standalone reference cd3313.ko | Passed |
| CD3313 static analysis | Standalone reference cd3313.ko | Passed |
| Firmware images | Boot, recovery and DTBO dumps | Passed |
| Firmware libraries | Extracted System/Vendor tree and raw dumps | Passed |
| Firmware integrity | Extracted manifest and HEQ chunks | Passed |
| HEQ decompile | HEQ APK chunks and manifest | Passed |
| Audio configuration audit | Extracted System/Vendor tree | Passed |
| Audiomux client scan | Extracted System/Vendor tree | Passed |

The one-time end-to-end run completed all nine jobs successfully. A separate compute validation run passed both verify and audio-inventory tasks.

## Private workflows retained

Android builds, CD3313 emulator tests, PCM/DSP tests, stability/performance tests and repository-integrity checks remain private because they validate or build product-owned code.

## Cross-repository data contract

1. The public repository is the sole current location for firmware data.
2. The private repository invokes public reusable workflows.
3. The public worker reads firmware from its own checkout; it does not read or write private project files.
4. Results are attached to the caller's Actions run as artifacts.
5. Only reviewed compact findings are promoted to private documentation.
6. No worker installs firmware or writes to a physical MT8163 device.
