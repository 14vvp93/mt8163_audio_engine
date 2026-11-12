#!/usr/bin/env python3
"""Promote audio-relevant firmware files from the full extraction to a stable tree."""
from __future__ import annotations
import hashlib, json, shutil
from pathlib import Path

AUDIO_SYSTEM_CONFIG = {
    "a2dp_audio_policy_configuration.xml", "audio_effects.conf",
    "audio_policy_configuration.xml", "audio_policy_configuration_stub.xml",
    "audio_policy_volumes.xml", "hearing_aid_audio_policy_configuration.xml",
    "r_submix_audio_policy_configuration.xml", "usb_audio_policy_configuration.xml",
    "audioserver", "audioserver.rc", "libem_audio_jni.so",
}
AUDIO_VENDOR_CONFIG = {
    "AudioParamOptions.xml", "a2dp_audio_policy_configuration.xml",
    "audio_device.xml", "audio_effects.xml", "audio_em.xml", "audio_policy.conf",
    "audio_policy_configuration.xml", "audio_policy_volumes.xml",
    "audiocmdservice_atci", "audiocmdservice_atci.rc",
    "android.hardware.audio@4.0-service-mediatek", "android.hardware.audio@4.0-service-mediatek.rc",
    "media_codecs_google_audio.xml", "media_codecs_mediatek_audio.xml",
}

def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def copy_one(source: Path, destination: Path, source_label: str, rows: list[dict]) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    source_hash = digest(source)
    if destination.exists() and digest(destination) != source_hash:
        raise RuntimeError(f"refusing to overwrite different file: {destination}")
    if not destination.exists():
        shutil.copy2(source, destination)
    rows.append({"path": destination.relative_to(Path.cwd()).as_posix(), "size": destination.stat().st_size,
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

    # Keep the complete vendor/lib tree as the HAL/vendor dependency reference.
    vendor_root = root / "vendor_lib" / "lib"
    if not vendor_root.is_dir():
        raise SystemExit(f"missing extracted vendor library tree: {vendor_root}")
    for source in sorted(p for p in vendor_root.rglob("*") if p.is_file()):
        rel = source.relative_to(vendor_root)
        copy_one(source, dest / "vendor" / "lib" / rel,
                 f"vendor_lib.zip:lib/{rel.as_posix()}", rows)

    # Keep audio-related system libraries and VNDK copies at their original paths.
    system_lib_root = root / "lib" / "lib"
    for source in sorted(p for p in system_lib_root.rglob("*") if p.is_file()):
        rel = source.relative_to(system_lib_root)
        name = source.name.lower()
        if any(token in name for token in ("audio", "carservices", "em_audio")):
            copy_one(source, dest / "system" / "lib" / rel,
                     f"lib.zip:lib/{rel.as_posix()}", rows)

    # Keep audio configuration, server/JNI bridge, AudioInputSource and carservices artifacts.
    files2 = root / "files2" / "files2"
    system_root = files2 / "system"
    for source in sorted(p for p in system_root.rglob("*") if p.is_file()):
        rel = source.relative_to(system_root)
        if source.name in AUDIO_SYSTEM_CONFIG or (rel.parts and rel.parts[0] == "AudioInputSource"):
            copy_one(source, dest / "system" / rel,
                     f"files2.zip:system/{rel.as_posix()}", rows)

    framework_root = files2 / "framework"
    if framework_root.is_dir():
        for source in sorted(p for p in framework_root.rglob("*") if p.is_file()):
            if source.name.startswith("boot-carservices."):
                rel = source.relative_to(framework_root)
                copy_one(source, dest / "system" / "framework" / rel,
                         f"files2.zip:framework/{rel.as_posix()}", rows)

    vendor_root2 = files2 / "vendor"
    for source in sorted(p for p in vendor_root2.rglob("*") if p.is_file()):
        if source.name in AUDIO_VENDOR_CONFIG:
            rel = source.relative_to(vendor_root2)
            copy_one(source, dest / "vendor" / rel,
                     f"files2.zip:vendor/{rel.as_posix()}", rows)

    rows.sort(key=lambda row: row["path"])
    manifest = {
        "purpose": "Persistent audio-relevant extraction from the complete archive set.",
        "source_archives": [
            "reference-materials/firmware-dumps/files2.zip.001-.003",
            "reference-materials/firmware-dumps/lib.zip.001-.004",
            "reference-materials/firmware-dumps/vendor_lib.zip.001-.002",
            "reference-materials/heq/HEQ.zip.001-.017 (161 MiB APK is retained in the source archive and CI extraction artifact, not Git)",
        ],
        "file_count": len(rows), "total_bytes": sum(row["size"] for row in rows), "files": rows,
    }
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    readme = """# Curated extracted firmware references

This directory contains the persistent, audio-relevant subset promoted from the complete archive extraction.

- system/ — Android system audio policy/configuration, audio-related system libraries, AudioInputSource and carservices compiled artifacts.
- vendor/ — the complete extracted vendor/lib tree, including HAL libraries, vendor dependencies, kernel modules, plus audio service/configuration files.
- manifest.json — source archive/member, destination, size and SHA-256 for every promoted file.

All archive sets were extracted and CRC-checked in GitHub Actions. The full extraction is available as a 30-day workflow artifact. This curated tree avoids committing unrelated Android system libraries, generic notification/ringtone assets, framework stub JARs, and the 161 MiB HEQ APK. Original archives remain the source of truth and are not modified.

Do not install or load these reference binaries on a device. They are read-only reverse-engineering inputs.
"""
    (dest / "README.md").write_text(readme, encoding="utf-8")
    print(f"Curated {len(rows)} files ({sum(row['size'] for row in rows) / 1048576:.2f} MiB) into {dest}")

if __name__ == "__main__":
    main()
