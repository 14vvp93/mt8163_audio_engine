#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import lzma
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED_SHA256 = "58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc"
HELPER_TARGETS = (
    0x284DC,
    0x284EC,
    0x2817C,
    0x280EC,
    0x281CC,
    0x284FC,
    0x2850C,
    0x2820C,
    0x281EC,
    0x2851C,
    0x2811C,
)
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
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


def parse_functions(symbols_output: str) -> list[tuple[int, int, str, bool]]:
    functions: list[tuple[int, int, str, bool]] = []
    for line in symbols_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if not match:
            continue
        raw_address, raw_size, name = match.groups()
        address = int(raw_address, 16)
        size = int(raw_size, 10)
        if size > 0:
            functions.append((address & ~1, size, name, bool(address & 1)))
    return functions


def extract_gnu_debugdata(elf_data: bytes) -> bytes | None:
    if len(elf_data) < 52 or elf_data[:4] != b"\x7fELF":
        raise RuntimeError("invalid ELF header while inspecting .gnu_debugdata")
    if elf_data[4] != 1 or elf_data[5] != 1:
        raise RuntimeError(".gnu_debugdata extraction requires ELF32 little-endian input")

    section_offset = struct.unpack_from("<I", elf_data, 32)[0]
    section_entry_size, section_count, names_index = struct.unpack_from(
        "<HHH", elf_data, 46
    )
    if section_entry_size < 40 or section_count == 0 or names_index >= section_count:
        raise RuntimeError("invalid ELF section table while inspecting .gnu_debugdata")
    section_table_end = section_offset + section_entry_size * section_count
    if section_offset < 52 or section_table_end > len(elf_data):
        raise RuntimeError("ELF section table is outside the input file")

    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        offset = section_offset + index * section_entry_size
        name_offset, section_type, _, _, file_offset, size = struct.unpack_from(
            "<IIIIII", elf_data, offset
        )
        sections.append((name_offset, section_type, file_offset, size))

    names_offset, names_type, names_file_offset, names_size = sections[names_index]
    del names_offset
    if names_type != 3:
        raise RuntimeError("ELF section-name table is not a SHT_STRTAB section")
    if names_file_offset + names_size > len(elf_data):
        raise RuntimeError("ELF section-name table is outside the input file")
    names = elf_data[names_file_offset:names_file_offset + names_size]

    for name_offset, section_type, file_offset, size in sections:
        if name_offset >= len(names):
            continue
        end = names.find(b"\0", name_offset)
        if end < 0 or names[name_offset:end] != b".gnu_debugdata":
            continue
        if section_type == 8 or file_offset + size > len(elf_data):
            raise RuntimeError("invalid .gnu_debugdata section bounds")
        try:
            return lzma.decompress(elf_data[file_offset:file_offset + size])
        except lzma.LZMAError as error:
            raise RuntimeError(f"cannot decompress .gnu_debugdata: {error}") from error
    return None


def parse_debugdata_functions(elf_data: bytes) -> tuple[list[tuple[int, int, str, bool]], str]:
    debug_elf = extract_gnu_debugdata(elf_data)
    if debug_elf is None:
        return [], ".gnu_debugdata section is absent"
    if len(debug_elf) < 4 or debug_elf[:4] != b"\x7fELF":
        raise RuntimeError(".gnu_debugdata did not contain a valid ELF image")

    with tempfile.NamedTemporaryFile(prefix="mt8163-hidl-debug-", suffix=".elf") as stream:
        stream.write(debug_elf)
        stream.flush()
        output = run([
            "arm-linux-gnueabi-readelf", "-Ws", "-C", stream.name
        ])
    functions = parse_functions(output)
    return functions, f"loaded {len(functions)} sized FUNC symbols from .gnu_debugdata"


def resolve_function(
    target: int, functions: list[tuple[int, int, str, bool]]
) -> tuple[int, int, str, bool]:
    matches = [
        item for item in functions
        if item[0] <= target < item[0] + item[1]
    ]
    if not matches:
        raise RuntimeError(f"no FUNC symbol contains helper target 0x{target:08x}")
    matches.sort(key=lambda item: (item[1], item[0], item[2]))
    smallest = matches[0]
    same_extent = [item for item in matches if item[:2] == smallest[:2]]
    names = sorted({item[2] for item in same_extent}, key=str.casefold)
    return smallest[0], smallest[1], " | ".join(names), smallest[3]


def unresolved_report_lines(target: int, error: RuntimeError) -> list[str]:
    return [
        "",
        f"CALL_TARGET 0x{target:08x} -> UNRESOLVED",
        f"RESOLUTION_ERROR: {error}",
        "No guessed function boundary or truncated disassembly was emitted.",
    ]


def disassemble(binary: Path, address: int, size: int, thumb: bool) -> str:
    if address < 0 or size <= 0:
        raise ValueError("invalid function disassembly range")
    command = ["arm-linux-gnueabi-objdump", "-d", "-C"]
    if thumb:
        command.extend(["-M", "force-thumb"])
    command.extend([
        f"--start-address=0x{address:x}",
        f"--stop-address=0x{address + size:x}",
        str(binary),
    ])
    return run(command)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(
            "Usage: audit_primary_hal_p2_hidl_sync_helpers.py "
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
    functions = parse_functions(symbols_output)
    debugdata_status = ".gnu_debugdata not inspected"
    try:
        debug_functions, debugdata_status = parse_debugdata_functions(binary.read_bytes())
        functions.extend(debug_functions)
    except RuntimeError as error:
        debugdata_status = f".gnu_debugdata unavailable: {error}"
    if not functions:
        raise RuntimeError("no sized FUNC symbols found in HIDL bridge or .gnu_debugdata")

    reports: list[str] = [
        f"HIDL bridge SHA-256: {digest}",
        "Source: direct BL targets observed in Device::syncCallback at 0x0001cb4c",
        "Function symbols are collected from the ELF symbol tables and the optional "
        "GNU mini-debug ELF; no boundaries are inferred from arbitrary byte windows.",
        f"GNU mini-debug data: {debugdata_status}",
        "All helper inspection is read-only; no runtime behavior is inferred from names.",
    ]

    unresolved: list[int] = []
    for target in HELPER_TARGETS:
        try:
            address, size, name, thumb = resolve_function(target, functions)
        except RuntimeError as error:
            unresolved.append(target)
            reports.extend(unresolved_report_lines(target, error))
            continue

        reports.extend([
            "",
            f"CALL_TARGET 0x{target:08x} -> FUNC 0x{address:08x} "
            f"size={size} offset=0x{target - address:x} "
            f"mode={'Thumb' if thumb else 'ARM'} {name}",
            f"FULL_FUNCTION_RANGE 0x{address:08x}..0x{address + size:08x}",
            disassemble(binary, address, size, thumb).rstrip(),
        ])

    reports.extend([
        "",
        f"HELPER_RESOLUTION_STATUS: {'INCOMPLETE' if unresolved else 'COMPLETE'}",
        "UNRESOLVED_HELPER_TARGETS: "
        + (", ".join(f"0x{target:08x}" for target in unresolved) if unresolved else "none"),
        "An INCOMPLETE report is a successful evidence-generation result, not a lifetime "
        "or P2 pass. Unresolved helpers remain explicit P2 evidence gaps.",
        "Static helper disassembly only. Determine object pinning, reference ownership, "
        "and callback lifetime from complete instruction/dataflow evidence; helper names "
        "alone are not lifetime guarantees.",
    ])
    report = "\n".join(reports) + "\n"
    print(report, end="")
    if len(sys.argv) == 3:
        Path(sys.argv[2]).write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
