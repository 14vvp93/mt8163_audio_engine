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
    "RefBase::RefBase": "android::RefBase::RefBase(",
    "RefBase::~RefBase": "android::RefBase::~RefBase(",
}

OPTIONAL_TARGETS = {
    "RefBase::extendObjectLifetime": "android::RefBase::extendObjectLifetime(",
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
    for target, fragment in (*TARGETS.items(), *OPTIONAL_TARGETS.items()):
        matches = find_target_symbols(symbols, fragment)
        if not matches:
            if target in TARGETS:
                report.append(f"SYMBOL {target}: UNRESOLVED")
                missing.add(target)
            else:
                report.append(f"OPTIONAL_SYMBOL {target}: NOT EXPORTED")
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


def audit_hidl_library(binary: Path, label: str) -> list[str]:
    verify_arm32_elf(binary)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    symbol_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols = parse_function_symbols(symbol_output)
    report = [
        f"===== {label} =====",
        f"PATH: {binary}",
        f"SIZE: {binary.stat().st_size}",
        f"SHA256: {digest}",
        "ELF: ARM32 little-endian",
    ]

    extension_references = [
        line.strip() for line in symbol_output.splitlines()
        if "RefBase::extendObjectLifetime(" in line
    ]
    report.append(
        "REFBASE_EXTEND_OBJECT_LIFETIME_SYMBOL_REFERENCES: "
        + ("; ".join(extension_references) if extension_references else "none")
    )

    constructor_fragments = (
        "IBase::IBase(",
        "IDevice::IDevice(",
        "IBase::~IBase(",
        "IDevice::~IDevice(",
    )
    for fragment in constructor_fragments:
        matches = find_target_symbols(symbols, fragment)
        if not matches:
            report.append(f"CONSTRUCTOR {fragment}: NOT FOUND")
            continue
        for symbol in matches:
            body = disassemble_function(binary, symbol)
            report.extend([
                f"CONSTRUCTOR {fragment}: 0x{symbol.address:08x} "
                f"size={symbol.size} mode={'Thumb' if symbol.thumb else 'ARM'} "
                f"{symbol.name}",
                body.rstrip(),
            ])
            extension_calls = [
                line.strip() for line in body.splitlines()
                if "RefBase::extendObjectLifetime" in line
            ]
            report.append(
                "CONSTRUCTOR_EXTEND_OBJECT_LIFETIME_CALLS: "
                + ("; ".join(extension_calls) if extension_calls else "none")
            )
    return report


def refbase_implementation_status(
    bridge_uses_libutils: bool,
    missing_by_library: dict[str, set[str]],
) -> str:
    if bridge_uses_libutils and missing_by_library and not any(missing_by_library.values()):
        return "COMPLETE"
    return "INCOMPLETE"


def refbase_implementation_exit_code(status: str) -> int:
    return 0 if status == "COMPLETE" else 1


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
    bridge_symbols = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(bridge)])
    extension_refs = [
        line.strip() for line in bridge_symbols.splitlines()
        if "RefBase::extendObjectLifetime(" in line
    ]
    report.append(
        "BRIDGE_EXTEND_OBJECT_LIFETIME_SYMBOL_REFERENCES: "
        + ("; ".join(extension_refs) if extension_refs else "none")
    )
    return report, found


def main() -> int:
    if len(sys.argv) != 9:
        print(
            "usage: audit_android9_refbase_lifetime.py "
            "<hidl-bridge.so> <system-libutils.so> <vndk-sp-libutils.so> "
            "<system-libhidlbase.so> <vndk-sp-libhidlbase.so> "
            "<system-audio-v4.so> <vndk-audio-v4.so> <output.txt>",
            file=sys.stderr,
        )
        return 2

    (
        bridge,
        system_libutils,
        vndk_libutils,
        system_libhidlbase,
        vndk_libhidlbase,
        system_audio_v4,
        vndk_audio_v4,
        output,
    ) = map(Path, sys.argv[1:])
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

        for label, binary in (
            ("SYSTEM LIBHIDLBASE", system_libhidlbase),
            ("VNDK-SP-28 LIBHIDLBASE", vndk_libhidlbase),
            ("SYSTEM AUDIO V4 INTERFACE", system_audio_v4),
            ("VNDK-28 AUDIO V4 INTERFACE", vndk_audio_v4),
        ):
            report.extend(["", *audit_hidl_library(binary, label)])

        implementation_status = refbase_implementation_status(
            bridge_uses_libutils,
            all_missing,
        )
        report.extend([
            "",
            "===== LINKER-NAMESPACE LIMIT =====",
            "Both system/lib and VNDK candidates for libutils, libhidlbase, and the audio V4 interface were inspected.",
            "This report does not select the runtime library; the target vendor linker namespace "
            "configuration and actual namespace search order are not yet pinned.",
            "",
            f"BRIDGE_LIBUTILS_DEPENDENCY: {'CONFIRMED' if bridge_uses_libutils else 'UNRESOLVED'}",
            f"REFBASE_IMPLEMENTATION_STATUS: {implementation_status}",
        ])
        rendered = "\n".join(report) + "\n"
        output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
    except (OSError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return refbase_implementation_exit_code(implementation_status)


if __name__ == "__main__":
    raise SystemExit(main())
