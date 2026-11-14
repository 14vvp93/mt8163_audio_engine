#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Iterable

PATTERN = re.compile(
    rb"xWayPlay|xWayRec|Play2Way|Record2Way|AudioVUnlockDL|VoiceUnlock|"
    rb"voice.?unlock|ReadRefFromRing|setupParametersCallback|"
    rb"audioParameterChanged|setAudioParameterChangedCallback|"
    rb"clearAudioParameterChangedCallback|MTKPrimaryDevicesHalClient|"
    rb"AudioALSAHardware",
    re.IGNORECASE,
)
ARCHIVE_MEMBER = re.compile(r"(^|/)(classes[0-9]*\.dex|[^/]+\.so)$", re.IGNORECASE)
CANDIDATE_SUFFIXES = {
    ".apk", ".jar", ".dex", ".odex", ".vdex", ".oat", ".art", ".so"
}
MAX_MEMBER_SIZE = 128 * 1024 * 1024


def printable_runs(data: bytes) -> Iterable[bytes]:
    for match in re.finditer(rb"[\x20-\x7e]{4,}", data):
        yield match.group(0)


def scan_bytes(data: bytes) -> list[str]:
    found: set[str] = set()
    for value in printable_runs(data):
        if PATTERN.search(value):
            found.add(value.decode("ascii", errors="replace"))
    return sorted(found, key=str.casefold)


def dynamic_symbols(path: Path, nm: str) -> list[str]:
    try:
        result = subprocess.run(
            [nm, "-D", "-C", str(path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=20,
        )
    except (OSError, subprocess.TimeoutExpired):
        return []
    if result.returncode not in (0, 1):
        return []
    return sorted(
        {line.strip() for line in result.stdout.splitlines() if PATTERN.search(line.encode())},
        key=str.casefold,
    )


def has_elf_magic(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    except OSError:
        return False


def candidate_files(root: Path) -> list[Path]:
    scan_roots = (
        root / "system/lib",
        root / "vendor/lib",
        root / "system/bin",
        root / "vendor/bin",
        root / "system/framework",
        root / "system/AudioInputSource",
    )
    explicit = (
        root / "system/audioserver",
        root / "system/AudioInputSource.apk",
        root / "vendor/android.hardware.audio@4.0-service-mediatek",
        root / "vendor/audiocmdservice_atci",
    )
    files: set[Path] = {p for p in explicit if p.is_file()}
    for scan_root in scan_roots:
        if not scan_root.is_dir():
            continue
        for directory, _, names in os.walk(scan_root, followlinks=False):
            for name in names:
                path = Path(directory) / name
                try:
                    if path.is_file() and (
                        path.suffix.lower() in CANDIDATE_SUFFIXES
                        or has_elf_magic(path)
                    ):
                        files.add(path)
                except OSError:
                    continue
    return sorted(files)


def scan_file(path: Path, root: Path, nm: str) -> tuple[list[str], list[tuple[str, list[str]]]]:
    try:
        data = path.read_bytes()
    except OSError:
        return [], []
    symbols = dynamic_symbols(path, nm) if data.startswith(b"\x7fELF") else []
    matches = scan_bytes(data)
    members: list[tuple[str, list[str]]] = []
    if zipfile.is_zipfile(path):
        try:
            with zipfile.ZipFile(path) as archive:
                for info in sorted(archive.infolist(), key=lambda item: item.filename):
                    if info.is_dir() or not ARCHIVE_MEMBER.search(info.filename):
                        continue
                    if info.file_size > MAX_MEMBER_SIZE:
                        continue
                    try:
                        member_matches = scan_bytes(archive.read(info))
                    except (OSError, RuntimeError, zipfile.BadZipFile):
                        continue
                    if member_matches:
                        members.append((info.filename, member_matches))
        except (OSError, zipfile.BadZipFile):
            pass
    return symbols + matches, members


def main() -> int:
    parser = argparse.ArgumentParser(description="Read-only MT8163 private audio callback client scan")
    parser.add_argument("root", type=Path, help="firmware/extracted directory")
    parser.add_argument("--nm", default="arm-linux-gnueabi-nm")
    args = parser.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        print(f"ERROR: firmware root does not exist: {root}", file=sys.stderr)
        return 2

    files = candidate_files(root)
    if not files:
        print("ERROR: no firmware candidates found", file=sys.stderr)
        return 2

    matched_files = 0
    matched_members = 0
    for path in files:
        hits, members = scan_file(path, root, args.nm)
        if not hits and not members:
            continue
        matched_files += 1
        relative = path.relative_to(root).as_posix()
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        print(f"\n===== {relative} =====")
        print(f"SHA-256: {digest}")
        for hit in hits:
            print(f"FILE: {hit}")
        for member, member_hits in members:
            matched_members += 1
            print(f"ARCHIVE MEMBER: {member}")
            for hit in member_hits:
                print(f"  {hit}")

    print(f"\nScanned firmware candidates: {len(files)}")
    print(f"Files with callback-related matches: {matched_files}")
    print(f"Archive members with callback-related matches: {matched_members}")
    print("LIMITATION: strings/symbol matches cannot prove absence of stripped or indirect numeric-offset callers.")
    print("LIMITATION: static firmware scanning cannot establish runtime callback ordering, linker namespace behavior, or device routing.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
