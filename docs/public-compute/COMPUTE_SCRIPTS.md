# Public firmware compute scripts

The executable firmware-processing tools live in `14vvp93/public_calculations/scripts/firmware/`, alongside the GitHub Actions workers that use them.

- `archive_inventory.py` — streaming SHA-256 inventory for ZIP/TAR archives and split-volume inputs.
- `safe_extract.py` — safe extraction of a single ZIP/TAR archive with path traversal and duplicate-destination checks.
- `process_archive_sets.py` — numeric reassembly of split ZIP volumes, ZIP CRC verification, path/duplicate rejection, extraction and inventory reports. Retained as an import utility; current workers consume the verified extracted tree.
- `build_source_manifest.py` — provenance manifest for a firmware dataset.
- `audio_stack_inventory.py` — host-side inventory of Android audio stack configuration and DSP modules.
- `reassemble_heq_apk.py` — reassembles the three public HEQ chunks and verifies the full APK SHA-256.

The tools use Python standard library components and are intended for offline/CI analysis. They do not use ADB, install firmware or write to a target device. The obsolete private-repository firmware importer has been removed; firmware is maintained directly in the public data repository.
