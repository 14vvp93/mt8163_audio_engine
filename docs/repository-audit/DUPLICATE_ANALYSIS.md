# Duplicate and redundancy analysis — 2026-11-12

## Current repository separation

The 48 identical firmware-related Git blobs previously present in both repositories have been consolidated into the public firmware repository. The current private main tree contains no firmware dumps, extracted firmware, ZIP archives or firmware binaries.

The public repository is now the sole current source of firmware data:
- 1,386 extracted files (578,175,653 bytes);
- 22 standalone raw dumps (44,050,240 bytes);
- three manifests, including the full HEQ APK chunk manifest.

## Confirmed archive-level redundancy and deletion

The earlier archive audit found that all 36 files in the former `files.zip` were byte-identical to members in `files2.zip`. That archive was a redundant subset, not a distinct firmware revision.

After the complete extracted dataset was copied to the public repository and all 1,386 file sizes and SHA-256 values were verified, the 26 original split ZIP volumes (349,781,724 bytes) were removed from the current working trees of both repositories.

The public extracted tree is the canonical durable representation. The HEQ APK remains as three verified chunks, with a manifest containing the full APK SHA-256 and individual chunk hashes.

## Same name does not mean duplicate

Different builds of similarly named libraries must remain separate where their SHA-256 or original System/Vendor/VNDK path differs. The extracted manifest preserves source archive member names and canonical destination paths. No files were deduplicated solely by basename.

## History and storage caveat

This cleanup changes current branch trees but does not rewrite Git history. Old firmware blobs remain reachable through historical commits. A history rewrite would require a separately approved force-push and coordinated recloning; it has not been performed.
