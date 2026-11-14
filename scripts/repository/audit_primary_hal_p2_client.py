#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "b5e805b6cf20d995babb730f70b6287d4d4ff0c7168c189de326d05e8d58c22c"
FUNCTIONS = (
    "android::MTKPrimaryDevicesHalClient::getInstance()",
    "android::MTKPrimaryDevicesHalClient::destroyInstance()",
    "android::MTKPrimaryDevicesHalClientImpl::MTKPrimaryDevicesHalClientImpl()",
    "android::MTKPrimaryDevicesHalClientImpl::~MTKPrimaryDevicesHalClientImpl()",
    "android::MTKPrimaryDevicesHalClientImpl::setParameters(android::String8 const&)",
    "android::MTKPrimaryDevicesHalClientImpl::getParameters(android::String8 const&)",
)
CALL_TARGET = re.compile(
    r"\b(?:bl|blx)\s+[^<\n]*<([^>]+)>",
    re.IGNORECASE,
)
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+FUNC\s+\\S+\s+\\S+\s+\\S+\s+(.+?)\s*$"
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
        timeout=60,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: audit_primary_hal_p2_client.py <libaudioprimarydevicehalifclient.so>", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"ERROR: client library not found: {binary}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f"ERROR: unexpected client library SHA-256: {digest}", file=sys.stderr)
        return 1

    symbol_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols: dict[str, list[tuple[int, int]]] = {}
    for line in symbol_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if not match:
            continue
        address_text, size_text, name = match.groups()
        symbols.setdefault(name, []).append((int(address_text, 16), int(size_text, 10)))

    print(f"Client library SHA-256: {digest}")
    print("Read-only MTKPrimaryDevicesHalClient lifecycle and dispatch disassembly")

    missing = [name for name in FUNCTIONS if name not in symbols]
    if missing:
        for name in missing:
            print(f"ERROR: required symbol not found: {name}", file=sys.stderr)
        return 1

    for name in FUNCTIONS:
        for index, (address, size) in enumerate(symbols[name]):
            if size <= 0:
                continue
            disassembly = run([
                "arm-linux-gnueabi-objdump",
                "-d",
                "-C",
                f"--start-address=0x{address:x}",
                f"--stop-address=0x{address + size:x}",
                str(binary),
            ])
            calls = sorted(set(CALL_TARGET.findall(disassembly)), key=str.casefold)
            print(f"\n===== {name} [{index}] @ 0x{address:08x}, size={size} =====")
            print("Direct call targets: " + (", ".join(calls) if calls else "none detected"))
            print(disassembly.rstrip())

    print("\nNo runtime service registration, callback ordering or device behavior is inferred.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
