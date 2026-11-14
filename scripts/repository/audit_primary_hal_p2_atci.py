#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "302eee8562067a54d4b9e65ef982a100b94b36be6f73a993b9e01e059421a566"
CLIENT_SYMBOL = re.compile(r"MTKPrimaryDevicesHalClient", re.IGNORECASE)
CALL_TARGET = re.compile(
    r"\b(?:bl|blx)\s+[^<\n]*<([^>]*MTKPrimaryDevicesHalClient[^>]*)>",
    re.IGNORECASE,
)
FUNCTION_LABEL = re.compile(r"^([0-9a-fA-F]+) <(.+)>:$")


def run(command: list[str], timeout: int = 120) -> str:
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: audit_primary_hal_p2_atci.py <audiocmdservice_atci>", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"ERROR: ATCI service binary not found: {binary}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f"ERROR: unexpected ATCI service SHA-256: {digest}", file=sys.stderr)
        return 1

    symbols = run(["arm-linux-gnueabi-nm", "-D", "-C", str(binary)])
    relevant_symbols = [
        line.strip() for line in symbols.splitlines()
        if CLIENT_SYMBOL.search(line)
    ]
    relocations = run(["arm-linux-gnueabi-readelf", "-rW", str(binary)])
    relevant_relocations = [
        line.strip() for line in relocations.splitlines()
        if CLIENT_SYMBOL.search(line)
    ]

    disassembly = run(["arm-linux-gnueabi-objdump", "-d", "-C", str(binary)])
    blocks: list[tuple[str, str]] = []
    current_name = ""
    current_lines: list[str] = []
    for line in disassembly.splitlines():
        label = FUNCTION_LABEL.match(line)
        if label:
            if current_name:
                blocks.append((current_name, "\n".join(current_lines)))
            current_name = label.group(2)
            current_lines = [line]
        elif current_name:
            current_lines.append(line)
    if current_name:
        blocks.append((current_name, "\n".join(current_lines)))

    callers = [
        (name, block)
        for name, block in blocks
        if CALL_TARGET.search(block)
    ]

    print(f"ATCI service SHA-256: {digest}")
    print("Dynamic MTKPrimaryDevicesHalClient symbols:")
    for symbol in relevant_symbols:
        print(symbol)
    print("Dynamic relocations for MTKPrimaryDevicesHalClient:")
    for relocation in relevant_relocations:
        print(relocation)

    if not relevant_symbols:
        print("ERROR: expected MTKPrimaryDevicesHalClient import not found", file=sys.stderr)
        return 1

    if not callers:
        print(
            "WARNING: no direct symbolic call site was identified. The import/relocation "
            "does not prove an invocation; indirect or unused references remain unresolved."
        )
        print("\\nNo caller function is attributed to the MTK client from this binary.")
        return 0

    print(f"Direct caller functions found: {len(callers)}")
    for name, block in callers:
        print(f"\n===== ATCI caller: {name} =====")
        print(block)

    print("\nNo runtime ATCI service invocation or device behavior is inferred.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
