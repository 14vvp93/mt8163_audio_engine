# MT8163 firmware dataset and storage boundary

## Canonical location

Firmware is stored only in the public data repository:

- Repository: `14vvp93/public_calculations`
- Standalone raw inputs: `firmware/dumps/mt8163/`
- Extracted filesystem: `firmware/extracted/`
- Provenance and integrity manifests: `firmware/manifests/`

The private `14vvp93/mt8163_audio_engine` repository owns source code, build files, tests, engineering documentation and project decisions. It no longer stores firmware archives, raw dumps or extracted firmware.

## Verified dataset transfer

The one-time synchronization workflow copied all 1,386 entries from the former private extracted tree into the public repository. Each file was checked by byte size and SHA-256 before the Git commit.

| Dataset | Files | Bytes |
|---|---:|---:|
| Extracted firmware | 1,386 | 578,175,653 |
| Standalone raw dumps | 22 | 44,050,240 |
| Original split ZIP volumes removed after verification | 26 | 349,781,724 |

The extracted dataset includes System and Vendor filesystem content plus the HEQ APK split into three GitHub-safe chunks. The HEQ manifest preserves the complete APK SHA-256 and the size/hash of every chunk.

## Integrity records

- Extracted-file manifest: `public_calculations/firmware/manifests/mt8163-extracted.json`
- Raw dump provenance: `public_calculations/firmware/manifests/mt8163-source-import.json`
- HEQ APK manifest: `public_calculations/firmware/manifests/heq-apk.json`
- Source repository commit used for the transfer: `44e132c8ac299ebfd58831232ec6b6f64ae8f769`
- Original extracted manifest blob: `83ba8985a13a87576688061cfed7c930eefa1b05`

The public firmware integrity worker verifies all extracted files against the manifest and reassembles the HEQ APK to verify its full SHA-256.

## Archive deletion decision

The 26 split ZIP volumes were removed from both repositories only after the complete extracted tree had been transferred and all 1,386 file hashes had passed verification. The public extracted tree is now the durable canonical dataset. The raw boot/recovery/DTBO images, configuration dumps and CD3313 reference module remain under `firmware/dumps/mt8163/`.

The archive processing utilities remain available in the public compute scripts for future imports, but no current workflow depends on the deleted archive volumes.

## Worker data flow

Public workers check out `14vvp93/public_calculations` and read firmware directly from `firmware/`. They do not check out private firmware paths and no longer require `SOURCE_REPO_READ_TOKEN`. The private repository invokes the reusable workers; generated results return as GitHub Actions artifacts attached to the caller's run.
