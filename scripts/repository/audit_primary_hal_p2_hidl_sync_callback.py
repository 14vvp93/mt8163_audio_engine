#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc"
FUNCTION_PREFIX = (
    "android::hardware::audio::V4_0::implementation::Device::syncCallback("
)
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
)
CALL_TARGET = re.compile(r"\b(?:bl|blx)\s+[^<\n]*<([^>]+)>", re.IGNORECASE)


def run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=60,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(
            "Usage: audit_primary_hal_p2_hidl_sync_callback.py "
            "<android.hardware.audio@4.0-impl-mediatek.so> [output.txt]",
            file=sys.stderr,
        )
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        raise RuntimeError(f"HIDL bridge not found: {binary}")

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(f"unexpected HIDL bridge SHA-256: {digest}")

    symbols_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    matches: list[tuple[int, int, str]] = []
    for line in symbols_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if not match:
            continue
        address, size, name = match.groups()
        if name.startswith(FUNCTION_PREFIX) and int(size, 10) > 0:
            matches.append((int(address, 16), int(size, 10), name))

    if len(matches) != 1:
        raise RuntimeError(
            f"expected exactly one syncCallback symbol, found {len(matches)}"
        )

    address, size, name = matches[0]
    code_address = address & ~1
    command = ["arm-linux-gnueabi-objdump", "-d", "-C"]
    if address & 1:
        command.extend(["-M", "force-thumb"])
    command.extend([
        f"--start-address=0x{code_address:x}",
        f"--stop-address=0x{code_address + size:x}",
        str(binary),
    ])
    disassembly = run(command)
    if not re.search(
        rf"^\s*0*{code_address:x}(?:\s*<|:\s+)",
        disassembly,
        re.MULTILINE,
    ):
        raise RuntimeError("syncCallback disassembly start address mismatch")

    calls = sorted(set(CALL_TARGET.findall(disassembly)), key=str.casefold)
    report = "\n".join([
        f"HIDL bridge SHA-256: {digest}",
        f"Function: {name}",
        f"Address: 0x{code_address:08x}",
        f"Size: {size}",
        "Direct call targets: " + (", ".join(calls) if calls else "none detected"),
        "Read-only syncCallback disassembly:",
        disassembly.rstrip(),
        "",
        "Static body evidence only; this report does not prove runtime callback lifetime or teardown quiescence.",
    ]) + "\n"

    print(report, end="")
    if len(sys.argv) == 3:
        Path(sys.argv[2]).write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
