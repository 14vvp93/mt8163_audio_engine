# Duplicate and redundancy analysis

## Confirmed archive-level redundancy

The archive inventory records 36 files in `files.zip` and 324 files in `files2.zip`. Comparing SHA-256 hashes of every extracted member shows that **all 36 files from `files.zip` occur byte-for-byte in `files2.zip`**.

Therefore:
- `files2.zip` is the canonical, larger reference set.
- `files.zip` does not provide a distinct binary or configuration revision.
- Its contents must not be treated as a second firmware version.
- The smaller archive may be removed from the tracked repository to avoid retaining a redundant 3.2 MiB source package; the matching content remains available in `files2.zip`.

## Exact duplicate groups inside the archives

The inventory identified repeated content in these categories:

- System/vendor copies of `r_submix_audio_policy_configuration.xml` and `usb_audio_policy_configuration.xml` are byte-identical.
- `AudioParamOptions.xml` at the vendor root and under `vendor/audio_param/` is byte-identical.
- `AudioInputSource.apk`, `.odex` and `.vdex` appear both at the system root and under the nested AudioInputSource directory with identical hashes.
- Several framework JAR stubs share identical tiny contents (183 or 200 bytes). These are not complete framework implementations.
- Many audio HAL/effect binaries in `files2.zip` are byte-identical to the copies in `files.zip` and/or `vendor_lib.zip`.

## Same name does not mean duplicate

The archive contains different builds of similarly named libraries. In particular, the files with a literal ` (2)` suffix in the flattened `files2.zip` system listing correspond to the non-VNDK system library copies, while the unsuffixed files correspond to VNDK-28 copies. Their hashes and sizes differ. They must be placed into their correct system/lib versus system/lib/vndk-28 locations, not deleted or overwritten based on basename.

The curated extraction uses the original directory structure from `lib.zip` for these system and VNDK variants.

## Deletion policy

Only a complete archive-level subset with a matching SHA-256 for every member is eligible for removal. Individual same-hash files in different firmware paths are retained in the archive inventory, because their original locations may be meaningful. Original archives are retained unless a separate cleanup decision is documented.
