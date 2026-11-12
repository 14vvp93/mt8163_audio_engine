#!/usr/bin/env python3
"""Inventory and safely extract repository reference ZIP archives.

Split archives named *.zip.001, *.zip.002, ... are concatenated in numeric
order before opening. Extraction is idempotent: existing files are replaced
only when their content differs. Archive path traversal and symlink entries
are rejected. This tool is intended for CI/offline research, never device I/O.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import zipfile
from collections import Counter
from pathlib import Path, PurePosixPath


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_target(root: Path, member: str) -> Path:
    normalized = member.replace("\\", "/")
    relative = PurePosixPath(normalized)
    if relative.is_absolute() or any(part in ("", ".", "..") for part in relative.parts):
        raise ValueError(f"unsafe archive path: {member!r}")
    target = (root / Path(*relative.parts)).resolve()
    if target != root and root not in target.parents:
        raise ValueError(f"archive path escapes destination: {member!r}")
    return target


def archive_groups(root: Path):
    split_pattern = re.compile(r"^(.*\.zip)\.(\d{3})$")
    split = {}
    regular = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        match = split_pattern.match(path.name)
        if match:
            split.setdefault((path.parent, match.group(1)), []).append(
                (int(match.group(2)), path)
            )
        elif path.suffix.lower() == ".zip":
            regular.append((path.name, [path]))
    groups = [(name, [p for _, p in sorted(parts)])
              for (_, name), parts in split.items()]
    groups.extend(regular)
    return sorted(groups, key=lambda item: (item[0].lower(), str(item[1][0])))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--extract-root", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    args = parser.parse_args()

    repo_root = args.root.resolve()
    extract_root = args.extract_root.resolve()
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    extract_root.mkdir(parents=True, exist_ok=True)

    reports = []
    for archive_name, parts in archive_groups(repo_root / "reference-materials"):
        key = re.sub(r"[^A-Za-z0-9._-]+", "-", archive_name.removesuffix(".zip")).strip("-")
        destination = extract_root / key
        joined = None
        try:
            if len(parts) == 1:
                archive_path = parts[0]
            else:
                joined = extract_root / f".{key}.assembled.zip"
                with joined.open("wb") as output:
                    for index, part in enumerate(parts, start=1):
                        expected = index
                        actual = int(re.search(r"\.(\d{3})$", part.name).group(1))
                        if actual != expected:
                            raise ValueError(f"missing/out-of-order volume: expected {expected:03d}, got {actual:03d}")
                        with part.open("rb") as source:
                            shutil.copyfileobj(source, output)
                archive_path = joined

            entry_rows = []
            extension_counts = Counter()
            total_uncompressed = 0
            total_compressed = 0
            with zipfile.ZipFile(archive_path) as zf:
                bad_member = zf.testzip()
                if bad_member:
                    raise ValueError(f"CRC failure in {bad_member}")
                for info in zf.infolist():
                    if info.is_dir():
                        continue
                    target = safe_target(destination, info.filename)
                    mode = (info.external_attr >> 16) & 0o170000
                    if mode == 0o120000:
                        raise ValueError(f"symlink entry rejected: {info.filename}")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    temporary = target.with_name(target.name + ".extract-tmp")
                    digest = hashlib.sha256()
                    with zf.open(info) as source, temporary.open("wb") as output:
                        for chunk in iter(lambda: source.read(1024 * 1024), b""):
                            output.write(chunk)
                            digest.update(chunk)
                    if target.exists() and target.is_file() and sha256_file(target) == digest.hexdigest():
                        temporary.unlink()
                    else:
                        temporary.replace(target)
                    suffix = Path(info.filename).suffix.lower() or "[no extension]"
                    extension_counts[suffix] += 1
                    total_uncompressed += info.file_size
                    total_compressed += info.compress_size
                    entry_rows.append({
                        "path": info.filename,
                        "size": info.file_size,
                        "compressed_size": info.compress_size,
                        "sha256": digest.hexdigest(),
                        "extracted_path": str(target.relative_to(extract_root)),
                    })
            reports.append({
                "archive": archive_name,
                "parts": [str(p.relative_to(repo_root)) for p in parts],
                "status": "ok",
                "file_count": len(entry_rows),
                "uncompressed_bytes": total_uncompressed,
                "archive_bytes": sum(p.stat().st_size for p in parts),
                "extensions": dict(sorted(extension_counts.items())),
                "files": entry_rows,
            })
        except Exception as exc:  # keep processing other archive sets
            reports.append({
                "archive": archive_name,
                "parts": [str(p.relative_to(repo_root)) for p in parts],
                "status": "error",
                "error": str(exc),
            })
        finally:
            if joined and joined.exists():
                joined.unlink()

    args.json.write_text(json.dumps(reports, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    lines = [
        "# Reference archive extraction inventory",
        "",
        "Generated by `scripts/repository/prepare_reference_archives.py`.",
        "Extracted files are placed in the CI workspace under `reference-materials-expanded/`.",
        "Original archives and split parts remain unchanged in `reference-materials/`.",
        "",
        "| Archive set | Status | Files | Expanded size | Main file types |",
        "|---|---:|---:|---:|---|",
    ]
    for report in reports:
        if report["status"] != "ok":
            lines.append(f"| `{report['archive']}` | ERROR | — | — | {report['error']} |")
            continue
        size = report["uncompressed_bytes"]
        human = f"{size / (1024**2):.1f} MiB"
        types = ", ".join(f"`{k}`: {v}" for k, v in report["extensions"].items())
        lines.append(f"| `{report['archive']}` | OK | {report['file_count']} | {human} | {types} |")
    lines += [
        "",
        "## Handling policy",
        "",
        "- Source archives are preserved as immutable acquisition evidence.",
        "- Extraction is path-safe and content-hash-aware; reruns do not rewrite identical files.",
        "- Do not commit every extracted binary blindly. Review the JSON inventory, identify canonical inputs and useful firmware subsets, then promote only those into curated repository directories.",
        "- Duplicate files should be compared by SHA-256 before any deletion.",
        "- Failed or incomplete multipart sets must be repaired before their contents are relied upon.",
        "",
    ]
    args.markdown.write_text("\n".join(lines), encoding="utf-8")
    failures = [r for r in reports if r["status"] != "ok"]
    print(f"Archive sets: {len(reports)}; successful: {len(reports)-len(failures)}; failed: {len(failures)}")
    for report in reports:
        print(f"{report['status'].upper()}: {report['archive']} ({report.get('file_count', 0)} files)")
        if report.get("error"):
            print(f"  {report['error']}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
