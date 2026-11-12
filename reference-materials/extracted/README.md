# Extracted MT8163 firmware reference tree

This is the persistent, organized extraction of the retained firmware archives.

- system/ — all 766 files from lib.zip at their original system/lib and system/lib/vndk-28 paths, plus every non-shared-library member from files2.zip under system/, including framework OAT/VDEX/ART files, AudioInputSource, audio policy/configuration, audio media and service files.
- vendor/ — the complete 325-file vendor_lib.zip tree under vendor/lib, plus every non-shared-library member from files2.zip under vendor/.
- manifest.json — source archive member, destination path, size and SHA-256 for every committed extracted file.

The 32 shared-library files listed in files2.zip were byte-identical to files already represented by lib.zip or vendor_lib.zip. They are stored once at their canonical system/lib, system/lib/vndk-28 or vendor/lib paths. The former files.zip was a fully identical 36-file subset of files2.zip and was removed.

The 161 MiB HEQ.apk is committed as three GitHub-safe chunks under heq/. Reassemble with the command in heq/README.md. The original 17 split volumes remain unchanged in reference-materials/heq/.

All original archive parts and the reference kernel module remain unchanged. These files are offline reverse-engineering inputs; do not install or load them on a device.
