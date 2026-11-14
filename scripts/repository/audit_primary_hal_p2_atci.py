#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "302eee8562067a54d4b9e65ef982a100b94b36be6f73a993b9e01e059421a566"
CLIENT_SYMBOL = re.compile(r"MTKPrimaryDevicesHalClient", re.IGNORECASE)


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

    if not relevant_symbols:
        print("ERROR: expected MTKPrimaryDevicesHalClient import not found", file=sys.stderr)
        return 1

    if not relevant_relocations:
        print(f"ATCI service SHA-256: {digest}")
        print("Dynamic MTKPrimaryDevicesHalClient symbols:")
        for symbol in relevant_symbols:
            print(symbol)
        print("Dynamic relocations for MTKPrimaryDevicesHalClient: none")
        print(
            "WARNING: the undefined import has no matching dynamic relocation; "
            "no executable call site is established by this ELF."
        )
        return 0

    plt = run(["arm-linux-gnueabi-objdump", "-d", "-C", "-j", ".plt", str(binary)])
    relevant_plt = [
        line.rstrip() for line in plt.splitlines()
        if CLIENT_SYMBOL.search(line)
    ]

    print(f"ATCI service SHA-256: {digest}")
    print("Dynamic MTKPrimaryDevicesHalClient symbols:")
    for symbol in relevant_symbols:
        print(symbol)
    print("Dynamic relocations for MTKPrimaryDevicesHalClient:")
    for relocation in relevant_relocations:
        print(relocation)
    print("PLT entries for MTKPrimaryDevicesHalClient:")
    for entry in relevant_plt:
        print(entry)

    print(
        "WARNING: a JUMP_SLOT relocation exists, but this audit does not prove "
        "that the ATCI code executes the corresponding PLT entry."
    )
    print("No direct caller function is claimed from symbol/relocation evidence alone.")
    print("No runtime ATCI service invocation or device behavior is inferred.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
