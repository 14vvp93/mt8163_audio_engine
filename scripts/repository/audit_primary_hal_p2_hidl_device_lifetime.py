#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc"
SYMBOL_LINE = re.compile(
    r"^\\s*\\d+:\\s+([0-9a-fA-F]+)\\s+([0-9a-fA-F]+)\\s+(FUNC|OBJECT|NOTYPE)\\s+\\S+\\s+\\S+\\s+\\S+\\s+(.+?)\\s*$"
)
DEVICE_TYPE = re.compile(r"(?:^|::)Device(?:::|$)", re.IGNORECASE)
OWNER_HINT = re.compile(
    r"device|callback|owner|vector|list|weak|strong|sp<|wp<",
    re.IGNORECASE,
)
CALL_TARGET = re.compile(r"\\b(?:bl|blx)\\s+[^<\\n]*<([^>]+)>", re.IGNORECASE)


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
            f"command failed ({result.returncode}): {' '.join(command)}\\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "Usage: audit_primary_hal_p2_hidl_device_lifetime.py "
            "<android.hardware.audio@4.0-impl-mediatek.so>",
            file=sys.stderr,
        )
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"ERROR: bridge library not found: {binary}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f"ERROR: unexpected HIDL bridge SHA-256: {digest}", file=sys.stderr)
        return 1

    symbol_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols: list[tuple[int, int, str, str]] = []
    for line in symbol_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if match:
            address, size, kind, name = match.groups()
            symbols.append((int(address, 16), int(size, 10), kind, name))

    constructors = [
        item for item in symbols
        if item[2] == "FUNC" and re.search(r"(?:^|::)Device::Device\\(", item[3])
    ]
    destructors = [
        item for item in symbols
        if item[2] == "FUNC" and re.search(r"(?:^|::)Device::~Device\\(", item[3])
    ]
    owner_candidates = [
        item for item in symbols
        if item[2] in {"OBJECT", "NOTYPE"} and OWNER_HINT.search(item[3])
    ]

    print(f"HIDL bridge SHA-256: {digest}")
    print("Target Device owner/lifetime symbol inventory")
    print(f"Device constructors: {len(constructors)}")
    print(f"Device destructors: {len(destructors)}")

    for label, candidates in (("CONSTRUCTOR", constructors), ("DESTRUCTOR", destructors)):
        for index, (address, size, _, name) in enumerate(candidates):
            print(f"\\n===== {label} {index}: {name} @ 0x{address:08x}, size={size} =====")
            if size <= 0:
                print("No function size recorded; disassembly skipped.")
                continue
            code_address = address & ~1
            command = ["arm-linux-gnueabi-objdump", "-dr", "-C"]
            if address & 1:
                command.extend(["-M", "force-thumb"])
            command.extend([
                f"--start-address=0x{code_address:x}",
                f"--stop-address=0x{code_address + size:x}",
                str(binary),
            ])
            disassembly = run(command)
            calls = sorted(set(CALL_TARGET.findall(disassembly)), key=str.casefold)
            print("Direct call targets: " + (", ".join(calls) if calls else "none detected"))
            print(disassembly.rstrip())

    print("\\n===== GLOBAL / OWNER CANDIDATE SYMBOLS =====")
    if not owner_candidates:
        print("No named OBJECT/NOTYPE owner candidates found in the ELF symbol table.")
    for address, size, kind, name in owner_candidates:
        print(f"{kind} 0x{address:08x} size={size} {name}")

    print("\\n===== DEVICE-RELATED SYMBOLS =====")
    device_symbols = [
        item for item in symbols
        if DEVICE_TYPE.search(item[3]) or "Callback" in item[3]
    ]
    for address, size, kind, name in device_symbols:
        print(f"{kind} 0x{address:08x} size={size} {name}")

    if not constructors and not destructors:
        print(
            "WARNING: no named Device constructor/destructor symbols were retained; "
            "owner lifetime cannot be resolved from this symbol table."
        )
    elif not constructors or not destructors:
        print(
            "WARNING: constructor/destructor symbol coverage is incomplete; "
            "do not infer a complete owner lifecycle."
        )

    print(
        "\\nStatic symbol/disassembly inventory only. It does not prove runtime "
        "registration, owner pinning, destructor reachability, callback drain, "
        "or absence of stripped/indirect ownership code."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
