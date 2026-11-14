#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


TARGETS = {
    "RefBase::createWeak": "android::RefBase::createWeak(",
    "weakref_type::attemptIncStrong": "android::RefBase::weakref_type::attemptIncStrong(",
    "RefBase::decStrong": "android::RefBase::decStrong(",
    "weakref_type::decWeak": "android::RefBase::weakref_type::decWeak(",
}


@dataclass(frozen=True)
class FunctionSymbol:
    address: int
    size: int
    thumb: bool
    name: str


def run(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command,
            check=True,
            capture_output=True,
            text=True,
            timeout=90,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"command timed out: {' '.join(command)}") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip() or error.stdout.strip()
        raise RuntimeError(f"command failed: {' '.join(command)}: {detail}") from error
    return result.stdout


def parse_function_symbols(output: str) -> list[FunctionSymbol]:
    symbols: list[FunctionSymbol] = []
    pattern = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+"
        r"\S+\s+\S+\s+(\S+)\s+(.+?)\s*$"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        value, size_text, section_index, name = match.groups()
        size = int(size_text)
        if section_index == "UND" or size <= 0:
            continue
        raw_address = int(value, 16)
        symbols.append(
            FunctionSymbol(
                address=raw_address & ~1,
                size=size,
                thumb=bool(raw_address & 1),
                name=name,
            )
        )
    return symbols


def find_target_symbols(
    symbols: list[FunctionSymbol],
    fragment: str,
) -> list[FunctionSymbol]:
    matches = [symbol for symbol in symbols if fragment in symbol.name]
    return sorted(matches, key=lambda symbol: (symbol.address, symbol.size, symbol.name))


def disassemble_function(binary: Path, symbol: FunctionSymbol) -> str:
    command = [
        "arm-linux-gnueabi-objdump",
        "-d",
        "-C",
    ]
    if symbol.thumb:
        command.extend(["-M", "force-thumb"])
    command.extend([
        f"--start-address=0x{symbol.address:x}",
        f"--stop-address=0x{symbol.address + symbol.size:x}",
        str(binary),
    ])
    return run(command)


def verify_arm32_elf(binary: Path) -> str:
    if not binary.is_file() or binary.stat().st_size == 0:
        raise RuntimeError(f"missing or empty ELF: {binary}")
    header = run(["arm-linux-gnueabi-readelf", "-h", str(binary)])
    required = (
        "Class:                             ELF32",
        "Data:                              2's complement, little endian",
        "Machine:                           ARM",
    )
    missing = [item for item in required if item not in header]
    if missing:
        raise RuntimeError(f"{binary} is not the expected ARM32 little-endian ELF: {missing}")
    return header


def audit_library(binary: Path, label: str) -> tuple[list[str], set[str]]:
    header = verify_arm32_elf(binary)
    symbol_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols = parse_function_symbols(symbol_output)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    report = [
        f"===== {label} =====",
        f"PATH: {binary}",
        f"SIZE: {binary.stat().st_size}",
        f"SHA256: {digest}",
        "ELF: ARM32 little-endian",
        "SYMBOL_TABLE: readelf -Ws -C",
    ]
    missing: set[str] = set()
    for target, fragment in TARGETS.items():
        matches = find_target_symbols(symbols, fragment)
        if not matches:
            report.append(f"SYMBOL {target}: UNRESOLVED")
            missing.add(target)
            continue
        for symbol in matches:
            report.extend([
                f"SYMBOL {target}: 0x{symbol.address:08x} size={symbol.size} "
                f"mode={'Thumb' if symbol.thumb else 'ARM'} {symbol.name}",
                f"FUNCTION_RANGE: 0x{symbol.address:08x}.."
                f"0x{symbol.address + symbol.size:08x}",
                disassemble_function(binary, symbol).rstrip(),
            ])
    report.append(
        f"LIBRARY_SYMBOL_STATUS: {'INCOMPLETE' if missing else 'COMPLETE'}"
    )
    if missing:
        report.append("MISSING_TARGETS: " + ", ".join(sorted(missing)))
    return report, missing


def audit_bridge_dependencies(bridge: Path) -> tuple[list[str], bool]:
    if not bridge.is_file() or bridge.stat().st_size == 0:
        raise RuntimeError(f"missing or empty HIDL bridge: {bridge}")
    dynamic = run(["arm-linux-gnueabi-readelf", "-d", str(bridge)])
    needed = re.findall(r"Shared library: \[(.+?)\]", dynamic)
    report = [
        "===== HIDL BRIDGE DYNAMIC DEPENDENCIES =====",
        f"PATH: {bridge}",
        "DT_NEEDED:",
        *(f"  {name}" for name in needed),
    ]
    found = "libutils.so" in needed
    report.append(f"LIBUTILS_DT_NEEDED: {'PRESENT' if found else 'NOT FOUND'}")
    return report, found


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: audit_android9_refbase_lifetime.py "
            "<hidl-bridge.so> <system-libutils.so> <vndk-sp-libutils.so> <output.txt>",
            file=sys.stderr,
        )
        return 2

    bridge, system_libutils, vndk_libutils, output = map(Path, sys.argv[1:])
    try:
        report, bridge_uses_libutils = audit_bridge_dependencies(bridge)
        all_missing: dict[str, set[str]] = {}
        for label, binary in (
            ("SYSTEM LIBUTILS", system_libutils),
            ("VNDK-SP-28 LIBUTILS", vndk_libutils),
        ):
            library_report, missing = audit_library(binary, label)
            report.extend(["", *library_report])
            all_missing[label] = missing

        report.extend([
            "",
            "===== LINKER-NAMESPACE LIMIT =====",
            "Both system/lib and VNDK-SP-28 candidates were inspected.",
            "This report does not select the runtime library; the target vendor linker namespace "
            "configuration and actual namespace search order are not yet pinned.",
            "",
            f"BRIDGE_LIBUTILS_DEPENDENCY: {'CONFIRMED' if bridge_uses_libutils else 'UNRESOLVED'}",
            "REFBASE_IMPLEMENTATION_STATUS: "
            + (
                "COMPLETE"
                if bridge_uses_libutils and not any(all_missing.values())
                else "INCOMPLETE"
            ),
        ])
        rendered = "\n".join(report) + "\n"
        output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
    except (OSError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
