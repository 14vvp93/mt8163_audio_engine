# Firmware dataset migration and future updates

## Initial migration — completed

The one-time migration copied the complete extracted firmware dataset from the former private location into `14vvp93/public_calculations/firmware/extracted/`.

- 1,386 files
- 578,175,653 bytes
- Per-file size and SHA-256 verified before commit
- Source commit: `44e132c8ac299ebfd58831232ec6b6f64ae8f769`
- Extracted manifest: `firmware/manifests/mt8163-extracted.json`
- Standalone raw dumps: 22 files, 44,050,240 bytes
- HEQ APK: three chunks, reassembled and verified against the full APK SHA-256

After verification, the 26 original split ZIP volumes (349,781,724 bytes) and the private `reference-materials/` tree were removed from the current working trees. The original archive history remains in Git history; it is not part of the current branch contents.

See the private [firmware dataset migration report](FIRMWARE_DATASET_LAYOUT.md) for the complete inventory and provenance.

## Future updates

New approved firmware should be added directly to `public_calculations/firmware/`:

1. Place standalone images and dumps under `firmware/dumps/mt8163/`.
2. Place extracted filesystem content under `firmware/extracted/`, preserving original paths.
3. Generate/update a manifest with file sizes, SHA-256 values and source provenance.
4. Run `worker-audit-firmware-integrity.yml` and `compute.yml` with task `verify`.
5. Keep only one canonical copy of each dataset; do not mirror firmware into the private project repository.

No private-repository import token or cross-repository firmware importer is used by the current architecture.
