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
    "AudioALSAHardware::AudioALSAHardware()",
    "AudioALSAHardware::GetInstance()",
    "AudioALSAHardware::setAudioParameterChangedCallback(",
    "AudioALSAHardware::clearAudioParameterChangedCallback(",
    "AudioALSAHardware::onAudioParameterChangedCallback(",
    "AudioMTKHardwareInterface::create()",
)
MODULE_OPEN_START = 0x0009B70C
MODULE_OPEN_END = 0x0009B994
MODULE_CLOSE_START = 0x0009BA48
MODULE_CLOSE_END = 0x0009BAE4


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


def disassemble_range(binary: Path, start: int, end: int, label: str) -> str:
    if start < 0 or end <= start:
        raise ValueError(f"invalid disassembly range: {start:#x}..{end:#x}")
    output = run(
        [
            "arm-linux-gnueabi-objdump",
            "-dr",
            "-C",
            f"--start-address=0x{start:x}",
            f"--stop-address=0x{end:x}",
            str(binary),
        ]
    )
    if not re.search(rf"^\s*0*{start:x}(?:\s|<)", output, re.MULTILINE):
        raise RuntimeError(f"{label}: start address is not present in disassembly")
    return output


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

    selected = [
        (address, size, name)
        for address, size, kind, name in symbols
        if kind == "FUNC" and any(target in name for target in TARGETS)
    ]
    missing = [
        target
        for target in TARGETS
        if not any(target in name for _, _, name in selected)
    ]
    if missing:
        print("ERROR: required symbols missing from pinned ELF:")
        for target in missing:
            print(f"- {target}")
        return 1

    print(f"Factory HAL SHA-256: {digest}")
    print(f"Selected function symbols: {len(selected)}")
    for address, size, name in sorted(selected):
        print(f"\n===== {name} @ 0x{address:08x}, size={size} =====")
        if size == 0:
            raise RuntimeError(f"zero-sized required function: {name}")
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
        body = run(command)
        calls = sorted(set(CALL_TARGET.findall(body)), key=str.casefold)
        print("Direct call targets: " + (", ".join(calls) if calls else "none detected"))
        print(body.rstrip())

    print("\n===== HMI.open wrapper allocation and callback table =====")
    print(disassemble_range(binary, MODULE_OPEN_START, MODULE_OPEN_END, "HMI.open").rstrip())

    print("\n===== HMI.close wrapper teardown =====")
    print(disassemble_range(binary, MODULE_CLOSE_START, MODULE_CLOSE_END, "HMI.close").rstrip())

    print(
        "\nStatic evidence only: HMI.close dispatches through the returned object's "
        "second vtable slot. The selected factory symbols and vtable layout must "
        "be correlated before classifying that slot as a device close or destructor; "
        "this audit does not prove callback-drain semantics or runtime ordering."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
