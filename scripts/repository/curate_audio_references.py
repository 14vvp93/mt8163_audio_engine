#!/usr/bin/env python3
"""Promote the complete non-HEQ firmware extraction into a stable filesystem tree.

The 32 shared-library files in files2.zip are exact SHA-256 duplicates of
libraries in lib.zip or vendor_lib.zip. They are represented at their proper
system/lib, system/lib/vndk-28, or vendor/lib paths rather than copied from
the flattened files2 listing. All other files2 members are preserved.
"""
from __future__ import annotations
import hashlib
import json
import shutil
from pathlib import Path

def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def copy_one(source: Path, destination: Path, source_label: str, rows: list[dict], seen: set[str]) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    source_hash = digest(source)
    rel = destination.relative_to(Path.cwd()).as_posix()
    if rel in seen:
        if not destination.is_file() or digest(destination) != source_hash:
            raise RuntimeError(f"conflicting duplicate destination: {rel}")
        return
    if destination.exists() and digest(destination) != source_hash:
        raise RuntimeError(f"refusing to overwrite different file: {destination}")
    if not destination.exists():
        shutil.copy2(source, destination)
    seen.add(rel)
    rows.append({"path": rel, "size": destination.stat().st_size,
                 "sha256": source_hash, "source": source_label})

def main() -> None:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--expanded", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    root, dest = args.expanded.resolve(), args.destination.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []
    seen: set[str] = set()

    # Preserve every file from the vendor library archive in its vendor path.
    vendor_root = root / "vendor_lib" / "lib"
    if not vendor_root.is_dir():
        raise SystemExit(f"missing extracted vendor library tree: {vendor_root}")
    for source in sorted(p for p in vendor_root.rglob("*") if p.is_file()):
        rel = source.relative_to(vendor_root)
        copy_one(source, dest / "vendor" / "lib" / rel,
                 f"vendor_lib.zip:lib/{rel.as_posix()}", rows, seen)

    # Preserve the complete system library archive, including VNDK-28 variants.
    system_lib_root = root / "lib" / "lib"
    if not system_lib_root.is_dir():
        raise SystemExit(f"missing extracted system library tree: {system_lib_root}")
    for source in sorted(p for p in system_lib_root.rglob("*") if p.is_file()):
        rel = source.relative_to(system_lib_root)
        copy_one(source, dest / "system" / "lib" / rel,
                 f"lib.zip:lib/{rel.as_posix()}", rows, seen)

    # All files2 shared libraries must be exact copies represented by the two
    # canonical library archives above. Fail closed if a new unmatched .so appears.
    canonical_hashes = {row["sha256"] for row in rows}
    files2 = root / "files2" / "files2"
    if not files2.is_dir():
        raise SystemExit(f"missing extracted files2 tree: {files2}")
    files2_so = [p for p in files2.rglob("*.so") if p.is_file()]
    unmatched = [p for p in files2_so if digest(p) not in canonical_hashes]
    if unmatched:
        raise SystemExit("files2 contains shared libraries not represented by canonical lib archives: " +
                         ", ".join(str(p.relative_to(files2)) for p in unmatched))

    # Preserve every non-.so member from files2. Its paths are relative to the
    # original Android filesystem root; framework/ belongs under system/framework.
    for source in sorted(p for p in files2.rglob("*") if p.is_file() and p.suffix.lower() != ".so"):
        rel = source.relative_to(files2)
        if rel.parts[0] == "framework":
            destination = dest / "system" / "framework" / Path(*rel.parts[1:])
        else:
            destination = dest / Path(*rel.parts)
        copy_one(source, destination, f"files2.zip:{rel.as_posix()}", rows, seen)

    rows.sort(key=lambda row: row["path"])
    manifest = {
        "purpose": "Complete persistent extraction of all retained archive sets except the 161 MiB HEQ APK, which exceeds GitHub's 100 MiB per-file limit.",
        "source_archives": [
            "reference-materials/firmware-dumps/files2.zip.001-.003",
            "reference-materials/firmware-dumps/lib.zip.001-.004",
            "reference-materials/firmware-dumps/vendor_lib.zip.001-.002",
            "reference-materials/heq/HEQ.zip.001-.017 (HEQ.apk remains in the source archive and full extraction artifact)",
        ],
        "files2_shared_library_deduplication": {
            "files2_shared_library_count": len(files2_so),
            "unmatched_shared_libraries": 0,
            "note": "All files2 .so entries were SHA-256 verified against lib.zip or vendor_lib.zip and are represented at their canonical filesystem paths."
        },
        "file_count": len(rows),
        "total_bytes": sum(row["size"] for row in rows),
        "files": rows,
    }
    (dest / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    readme = """# Extracted MT8163 firmware reference tree

This is the persistent, organized extraction of the retained firmware archives.

- system/ — all 766 files from lib.zip at their original system/lib and system/lib/vndk-28 paths, plus every non-shared-library member from files2.zip under system/, including framework OAT/VDEX/ART files, AudioInputSource, audio policy/configuration, audio media and service files.
- vendor/ — the complete 325-file vendor_lib.zip tree under vendor/lib, plus every non-shared-library member from files2.zip under vendor/.
- manifest.json — source archive member, destination path, size and SHA-256 for every committed extracted file.

The 32 shared-library files listed in files2.zip were byte-identical to files already represented by lib.zip or vendor_lib.zip. They are stored once at their canonical system/lib, system/lib/vndk-28 or vendor/lib paths. The former files.zip was a fully identical 36-file subset of files2.zip and was removed.

The 161 MiB HEQ.apk is the only archive member not committed as an ordinary Git file because it exceeds GitHub's 100 MiB per-file limit. Its 17 original split volumes remain in reference-materials/heq/, and the extracted APK is available in the full-extraction CI artifact.

All original archive parts and the reference kernel module remain unchanged. These files are offline reverse-engineering inputs; do not install or load them on a device.
"""
    (dest / "README.md").write_text(readme, encoding="utf-8")
    print(f"Persisted {len(rows)} files ({sum(row['size'] for row in rows) / 1048576:.2f} MiB); "
          f"verified and deduplicated {len(files2_so)} files2 shared libraries.")

if __name__ == "__main__":
    main()
