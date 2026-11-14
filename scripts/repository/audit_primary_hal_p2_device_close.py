#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4"
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(FUNC|OBJECT|NOTYPE)\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
)
CALL_TARGET = re.compile(r"\b(?:bl|blx)\s+[^<\n]*<([^>]+)>", re.IGNORECASE)
TARGETS = (
    "AudioALSAHardware::~AudioALSAHardware()",
    "AudioALSAHardware::setAudioParameterChangedCallback(",
    "AudioALSAHardware::clearAudioParameterChangedCallback(",
    "AudioALSAHardware::onAudioParameterChangedCallback(",
)


def run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=90,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "Usage: audit_primary_hal_p2_device_close.py <audio.primary.mt8163.so>",
            file=sys.stderr,
        )
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"ERROR: factory HAL not found: {binary}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f"ERROR: unexpected factory HAL SHA-256: {digest}", file=sys.stderr)
        return 1

    output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols: list[tuple[int, int, str, str]] = []
    for line in output.splitlines():
        match = SYMBOL_LINE.match(line)
        if match:
            address, size, kind, name = match.groups()
            symbols.append((int(address, 16), int(size, 10), kind, name))

    selected: list[tuple[int, int, str]] = []
    for address, size, kind, name in symbols:
        if kind == "FUNC" and any(target in name for target in TARGETS):
            selected.append((address, size, name))

    print(f"Factory HAL SHA-256: {digest}")
    print("Target legacy device-close/destruction audit")
    print(f"Selected function symbols: {len(selected)}")

    for address, size, name in sorted(selected):
        print(f"\n===== {name} @ 0x{address:08x}, size={size} =====")
        if size <= 0:
            print("No function size recorded; disassembly skipped.")
            continue

        code_address = address & ~1
        command = ["arm-linux-gnueabi-objdump", "-dr", "-C"]
        if address & 1:
            command.extend(["-M", "force-thumb"])
        command.extend(
            [
                f"--start-address=0x{code_address:x}",
                f"--stop-address=0x{code_address + size:x}",
                str(binary),
            ]
        )
        disassembly = run(command)
        calls = sorted(set(CALL_TARGET.findall(disassembly)), key=str.casefold)
        print("Direct call targets: " + (", ".join(calls) if calls else "none detected"))
        print(disassembly.rstrip())

    missing = [
        target
        for target in TARGETS
        if not any(target in name for _, _, name in selected)
    ]
    if missing:
        print("\nERROR: required symbols not retained in the pinned ELF symbol table:")
        for target in missing:
            print(f"- {target}")
        return 1

    print(
        "\nStatic disassembly only. It does not prove that the HIDL destructor "
        "reaches this path at runtime, that close drains callback dispatch, or "
        "that indirect/stripped teardown paths are absent."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
