#!/usr/bin/env python3
"""Reassemble the repository's chunked HEQ APK and verify its full SHA-256."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("HEQ.apk"))
    parser.add_argument("--parts-dir", type=Path, default=Path("reference-materials/extracted/heq"))
    args = parser.parse_args()
    manifest_path = args.parts_dir / "HEQ_APK_MANIFEST.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    try:
        with temporary.open("wb") as output:
            for part in manifest["parts"]:
                source = Path(part["path"])
                if not source.is_absolute():
                    source = Path.cwd() / source
                if not source.is_file():
                    raise FileNotFoundError(source)
                if source.stat().st_size != part["size"] or sha256(source) != part["sha256"]:
                    raise ValueError(f"chunk verification failed: {source}")
                with source.open("rb") as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        output.write(chunk)
        if temporary.stat().st_size != manifest["size"]:
            raise ValueError("reassembled APK size mismatch")
        if sha256(temporary) != manifest["sha256"]:
            raise ValueError("reassembled APK SHA-256 mismatch")
        temporary.replace(args.output)
    except Exception:
        if temporary.exists():
            temporary.unlink()
        raise
    print(f"Verified HEQ APK: {args.output} ({args.output.stat().st_size} bytes, {manifest['sha256']})")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
