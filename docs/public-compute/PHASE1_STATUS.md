# Phase 1 — infrastructure and firmware baseline

Last updated: 2026-11-12

**Status: Complete**

## Completed

- Initialized and documented the public compute repository. The canonical product implementation remains in the private `14vvp93/mt8163_audio_engine`.
- Added the firmware archive/dump/extraction/manifests layout and analysis areas for ELF, Audio HAL, CD3313, PCM/DSP and configuration.
- Added streaming SHA-256 inventory, safe extraction, split-ZIP reassembly, CRC verification, duplicate destination checks, symlink rejection and source provenance manifests.
- Added automated tests for hashing, extraction safety, path traversal, duplicate paths, split-volume reassembly, missing volumes and importer provenance.
- Added a controlled private-source-to-public-worker importer. It preserves source bytes, verifies source/destination SHA-256, rejects symlinked inputs and enforces a conservative 95 MiB Git blob ceiling.
- Imported all 48 approved firmware inputs from source commit `44e132c8ac299ebfd58831232ec6b6f64ae8f769`: 26 split archive volumes, 21 standalone firmware files and one standalone kernel-module reference.
- Verified all 48 imported files against the source-to-destination manifest: no missing files or size mismatches.
- Reassembled, CRC-checked and extracted all four archive sets successfully: 1,416 files and 587,184,709 expanded bytes.
- Audited the retained source workflows for portability and recorded the migration map.
- Added the first-pass audio-stack report and a repeatable ELF/module/XML inventory script. The script compares both CD3313 builds, scans `/dev/audiomux` references, parses DTBO DSP nodes and checks AudioPolicy/init configuration. Follow-up disassembly reports document the shared misc-device registration conflict, I2C retry improvements, kernel payload validation gaps and the JNI one-word over-read.

## Verification runs

- [Infrastructure validation — run 43209938648](https://github.com/14vvp93/public_calculations/actions/runs/43209938648) — success.
- [Firmware import — run 43204393764](https://github.com/14vvp93/public_calculations/actions/runs/43204393764) — success.
- [Archive inventory and extraction — run 43205114889](https://github.com/14vvp93/public_calculations/actions/runs/43205114889) — success.
- [Audio-stack, base-DTB and DTBO static inventory — run 43210027838](https://github.com/14vvp93/public_calculations/actions/runs/43210027838) — success.

The latest analysis run produced artifact `public-compute-extract-43210027838`, containing:
- `archive-inventory.json` — SHA-256 inventory of the 26 original split volumes;
- `archive-manifest.json` — per-member paths, sizes and SHA-256 values;
- `archive-inventory.md` — human-readable archive summary;
- `extracted/` — extracted archive trees.

The artifact has 14-day retention and expires on 2026-11-26. It also contains both CD3313 module variants, `boot.img`, `dtbo.img`, ELF metadata, DTBO fixups and the parsed base I2C-controller state. Original source archives remain committed in the repository; the expanded tree is a reproducible temporary artifact.

## Extraction totals

| Archive set | Files | Expanded bytes |
|---|---:|---:|
| `files2.zip` | 324 | 128,875,776 |
| `HEQ.zip` | 1 | 168,811,733 |
| `lib.zip` | 766 | 184,791,172 |
| `vendor_lib.zip` | 325 | 104,706,028 |
| **Total** | **1,416** | **587,184,709** |

## First-pass audio findings

See [MT8163 audio control architecture](AUDIO_CONTROL_ARCHITECTURE.md), [MT8163 audio stack — first-pass findings](../reports/MT8163_AUDIO_STACK_FIRST_PASS.md), [DSP driver ownership analysis](../reports/DSP_DRIVER_OWNERSHIP_ANALYSIS.md), [CD3313 module variant comparison](../reports/CD3313_MODULE_VARIANT_DIFF.md), [CD3313 ioctl ABI audit](../reports/CD3313_IOCTL_ABI_AUDIT.md) and [ExtAudioMuxer JNI ABI audit](../reports/EXT_AUDIO_MUXER_JNI_ABI_AUDIT.md).

Highest-priority follow-ups:
1. Confirm actual DSP probe success/failure from read-only target logs; the DTBO enables three nodes that share one `audiomux` misc-device name.
2. Reconcile the JNI one-word over-read and CD3313 short-payload validation gaps with all userspace callers.
3. Resolve the shared `audiomux` misc-device registration conflict against actual I2C probe logs.\n4. Reconcile the ExtAudioMuxer JNI one-word over-read with kernel payload validation and command-specific ABI lengths.\n5. Trace all `/dev/audiomux` clients across AudioFlinger, AudioPolicy, ExtAudioMuxer JNI and kernel drivers.
4. Validate the Bluetooth SCO input channel mask and missing AudioPolicy include against the active runtime configuration.
5. Map primary HAL PCM/mixer controls to the actual sound-card topology.

## Scope and limitations

All verification so far is host-side static analysis and GitHub Actions processing. No live MT8163 device was accessed, no kernel module was loaded, and no hardware state was changed. The canonical product repository has not yet received an implementation migration; that remains a separate project step.

## Phase 1 exit criteria

- [x] Compute repository initialized and documented.
- [x] Safe, reproducible archive processing tools and CI tests.
- [x] Compute and import workflows prepared and verified.
- [x] Public firmware import completed and SHA-256 manifest checked.
- [x] All split archives reassembled, CRC-checked and extracted.
- [x] Source workflow portability audit recorded.
- [x] First-pass audio baseline report committed.
