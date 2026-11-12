# Historical firmware archive inventory

> Historical record of the source archive sets before the repository separation completed on 2026-11-12. The original split ZIP volumes and the former private extracted tree are no longer present in the current branch trees. The canonical firmware dataset is now in the public repository; see [firmware dataset migration](../public-compute/FIRMWARE_DATASET_LAYOUT.md) and the [final cleanup report](REPOSITORY_CLEANUP_REPORT.md).

This inventory preserves the pre-cleanup archive composition and extracted-file statistics. It is not a statement that those original ZIP archives remain in the current repositories.

| Archive set | Historical status | Members | Expanded size | Main file types |
|---|---:|---:|---:|---|
| `files2.zip` | Verified | 324 | 122.9 MiB | `.0-service-mediatek`: 1, `.apk`: 3, `.art`: 24, `.conf`: 2, `.jar`: 60, `.mp3`: 1, `.oat`: 23, `.odex`: 38, `.ogg`: 29, `.prof`: 3, `.rc`: 3, `.rel`: 23, `.so`: 32, `.vdex`: 61, `.xml`: 19, no extension: 2 |
| `HEQ.zip` | Verified | 1 | 161.0 MiB | `.apk`: 1 |
| `lib.zip` | Verified | 766 | 176.2 MiB | `.bc`: 5, `.so`: 761 |
| `vendor_lib.zip` | Verified | 325 | 99.9 MiB | `.cfg`: 1, `.ko`: 33, `.so`: 291 |

## Canonical extracted dataset

The original four archive sets contained 1,416 archive members and approximately 559.99 MiB uncompressed. The curated persistent extraction contained 1,386 files (578,175,653 bytes) after the HEQ APK was represented as three GitHub-safe chunks and duplicate library copies were represented at canonical System/Vendor paths.

The public dataset now stores:

- `firmware/extracted/system/` — System-side framework, audio policy, libraries and configuration.
- `firmware/extracted/vendor/` — Vendor audio libraries, modules and configuration.
- `firmware/extracted/heq/` — three HEQ APK chunks.
- `firmware/manifests/mt8163-extracted.json` — per-file size and SHA-256 inventory.
- `firmware/manifests/heq-apk.json` — full APK hash and per-chunk hashes.

All 32 shared-library members in the former `files2.zip` were verified by SHA-256 as exact copies of files in the library archive sets and were represented once at their canonical System/Vendor paths. The 26 original split ZIP volumes (349,781,724 bytes) were removed from the current branch trees only after the 1,386 extracted files had been copied and hash-verified in the public repository.

## Current handling policy

- Firmware data is stored only in `14vvp93/public_calculations/firmware/`.
- The extracted tree and SHA-256 manifests are the durable canonical dataset.
- Original ZIP archives are not retained when their complete extracted contents and provenance are already preserved.
- New approved firmware is added directly to the public repository with a new manifest and integrity-worker run.
- Project documentation and reviewed findings remain in the private `14vvp93/mt8163_audio_engine` repository.
