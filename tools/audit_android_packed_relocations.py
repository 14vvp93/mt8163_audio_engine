#!/usr/bin/env python3
"""Decode Android APS2 packed REL relocations and inspect an ELF vtable."""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ELF32_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
ELF32_SECTION = struct.Struct("<IIIIIIIIII")
ELF32_SYMBOL = struct.Struct("<IIIBBH")
APS2_MAGIC = b"APS2"
R_ARM_ABS32 = 2
R_ARM_RELATIVE = 23


@dataclass(frozen=True)
class Section:
    name: str
    section_type: int
    address: int
    offset: int
    size: int
    link: int
    entry_size: int


@dataclass(frozen=True)
class Symbol:
    name: str
    value: int
    size: int
    symbol_type: int
    binding: int
    visibility: int
    section_index: int


@dataclass(frozen=True)
class Relocation:
    offset: int
    info: int
    addend: int | None


def read_sleb128(data: bytes, offset: int) -> tuple[int, int]:
    result = 0
    shift = 0

    while True:
        if offset >= len(data):
            raise ValueError("truncated SLEB128 value")

        byte = data[offset]
        offset += 1
        result |= (byte & 0x7F) << shift
        shift += 7

        if not byte & 0x80:
            if byte & 0x40:
                result -= 1 << shift
            return result, offset

        if shift >= 70:
            raise ValueError("SLEB128 value is too large")


def decode_aps2(data: bytes) -> tuple[list[Relocation], int]:
    if not data.startswith(APS2_MAGIC):
        raise ValueError("Android packed relocation section has no APS2 header")

    offset = len(APS2_MAGIC)
    relocation_count, offset = read_sleb128(data, offset)
    relocation_offset, offset = read_sleb128(data, offset)

    if relocation_count < 0 or relocation_count > 10_000_000:
        raise ValueError(f"invalid relocation count: {relocation_count}")

    relocations: list[Relocation] = []

    while len(relocations) < relocation_count:
        group_size, offset = read_sleb128(data, offset)
        group_flags, offset = read_sleb128(data, offset)

        if group_size <= 0 or len(relocations) + group_size > relocation_count:
            raise ValueError(f"invalid APS2 group size: {group_size}")

        grouped_by_info = bool(group_flags & 1)
        grouped_by_offset = bool(group_flags & 2)
        grouped_by_addend = bool(group_flags & 4)
        has_addend = bool(group_flags & 8)

        if grouped_by_offset:
            group_offset_delta, offset = read_sleb128(data, offset)
        else:
            group_offset_delta = 0

        if grouped_by_info:
            group_info, offset = read_sleb128(data, offset)
        else:
            group_info = 0

        if has_addend and grouped_by_addend:
            group_addend, offset = read_sleb128(data, offset)
        else:
            group_addend = 0

        for _ in range(group_size):
            if not grouped_by_offset:
                offset_delta, offset = read_sleb128(data, offset)
            else:
                offset_delta = group_offset_delta

            if not grouped_by_info:
                info, offset = read_sleb128(data, offset)
            else:
                info = group_info

            if has_addend and not grouped_by_addend:
                addend_delta, offset = read_sleb128(data, offset)
            else:
                addend_delta = group_addend

            relocation_offset += offset_delta
            if relocation_offset < 0:
                raise ValueError("negative relocation offset")

            relocations.append(
                Relocation(
                    offset=relocation_offset,
                    info=info,
                    addend=addend_delta if has_addend else None,
                )
            )

    if any(data[offset:]):
        raise ValueError("non-zero trailing data after APS2 relocation stream")

    return relocations, relocation_count


def parse_elf32(data: bytes) -> tuple[list[Section], list[Symbol]]:
    if len(data) < ELF32_HEADER.size:
        raise ValueError("file is too small to contain an ELF header")

    header = ELF32_HEADER.unpack_from(data)
    ident = header[0]

    if ident[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    if ident[4] != 1 or ident[5] != 1:
        raise ValueError("expected ELF32 little-endian")
    if header[2] != 40:
        raise ValueError(f"expected EM_ARM (40), got {header[2]}")

    section_offset = header[6]
    section_entry_size = header[11]
    section_count = header[12]
    string_section_index = header[13]

    if section_entry_size < ELF32_SECTION.size or section_count == 0:
        raise ValueError("invalid ELF section header table")
    if string_section_index >= section_count:
        raise ValueError("invalid section-name string table index")

    raw_sections = []
    for index in range(section_count):
        start = section_offset + index * section_entry_size
        end = start + ELF32_SECTION.size
        if end > len(data):
            raise ValueError("section header table exceeds file size")
        raw_sections.append(ELF32_SECTION.unpack_from(data, start))

    string_header = raw_sections[string_section_index]
    string_data = data[string_header[4]:string_header[4] + string_header[5]]

    def get_string(table: bytes, index: int) -> str:
        if index < 0 or index >= len(table):
            raise ValueError("string-table offset out of range")
        end = table.find(b"\0", index)
        if end < 0:
            raise ValueError("unterminated ELF string")
        return table[index:end].decode("utf-8", errors="replace")

    sections: list[Section] = []
    for raw in raw_sections:
        sections.append(
            Section(
                name=get_string(string_data, raw[0]),
                section_type=raw[1],
                address=raw[3],
                offset=raw[4],
                size=raw[5],
                link=raw[6],
                entry_size=raw[9],
            )
        )

    dynsym_section = next((s for s in sections if s.name == ".dynsym"), None)
    if dynsym_section is None:
        raise ValueError("missing .dynsym section")
    if dynsym_section.link >= len(sections):
        raise ValueError("invalid .dynsym string-table link")

    dynstr_section = sections[dynsym_section.link]
    dynstr = data[dynstr_section.offset:dynstr_section.offset + dynstr_section.size]
    entry_size = dynsym_section.entry_size or ELF32_SYMBOL.size

    if entry_size < ELF32_SYMBOL.size:
        raise ValueError("invalid .dynsym entry size")

    symbols: list[Symbol] = []
    for start in range(
        dynsym_section.offset,
        dynsym_section.offset + dynsym_section.size,
        entry_size,
    ):
        if start + ELF32_SYMBOL.size > len(data):
            raise ValueError(".dynsym exceeds file size")

        name_offset, value, size, info, other, section_index = ELF32_SYMBOL.unpack_from(
            data, start
        )
        symbols.append(
            Symbol(
                name=get_string(dynstr, name_offset),
                value=value,
                size=size,
                symbol_type=info & 0x0F,
                binding=info >> 4,
                visibility=other & 0x03,
                section_index=section_index,
            )
        )

    return sections, symbols


def demangle_names(names: list[str]) -> list[str]:
    tool = shutil.which("c++filt")
    if not tool or not names:
        return names

    result = subprocess.run(
        [tool, *names],
        check=True,
        capture_output=True,
        text=True,
    )
    demangled = result.stdout.splitlines()
    return demangled if len(demangled) == len(names) else names


def inspect_vtable(binary: Path, symbol_name: str) -> int:
    data = binary.read_bytes()
    sections, symbols = parse_elf32(data)

    packed_relocations = next((s for s in sections if s.name == ".rel.dyn"), None)
    if packed_relocations is None:
        raise ValueError("missing .rel.dyn section")

    relocation_data = data[
        packed_relocations.offset:packed_relocations.offset + packed_relocations.size
    ]
    relocations, relocation_count = decode_aps2(relocation_data)

    matches = [s for s in symbols if s.name == symbol_name]
    if len(matches) != 1:
        raise ValueError(
            f"expected one dynamic symbol {symbol_name!r}, found {len(matches)}"
        )

    vtable = matches[0]
    if vtable.size < 8 or vtable.size % 4:
        raise ValueError(f"invalid vtable size: {vtable.size}")

    symbol_by_index = symbols
    selected = [
        relocation
        for relocation in relocations
        if vtable.value <= relocation.offset < vtable.value + vtable.size
    ]
    selected.sort(key=lambda relocation: relocation.offset)

    if not selected:
        raise ValueError("no packed relocations target the selected vtable")

    names = []
    rows = []
    for relocation in selected:
        symbol_index = relocation.info >> 8
        relocation_type = relocation.info & 0xFF
        place_offset = relocation.offset - vtable.value

        if place_offset % 4:
            raise ValueError(f"unaligned vtable relocation at 0x{relocation.offset:x}")

        if relocation_type == R_ARM_ABS32:
            if symbol_index >= len(symbol_by_index):
                raise ValueError(f"invalid dynamic symbol index: {symbol_index}")
            target_symbol = symbol_by_index[symbol_index]
            target_name = target_symbol.name
            target_value = target_symbol.value
        elif relocation_type == R_ARM_RELATIVE:
            if symbol_index != 0:
                raise ValueError(
                    f"R_ARM_RELATIVE has non-zero symbol index {symbol_index}"
                )
            target_section = next(
                (
                    section
                    for section in sections
                    if section.address <= relocation.offset
                    < section.address + section.size
                ),
                None,
            )
            if target_section is None:
                raise ValueError(
                    f"no section contains relative relocation at 0x{relocation.offset:x}"
                )
            file_offset = (
                target_section.offset + relocation.offset - target_section.address
            )
            if file_offset + 4 > len(data):
                raise ValueError("relative relocation addend exceeds file size")
            target_value = struct.unpack_from("<I", data, file_offset)[0]
            target_name = f"R_ARM_RELATIVE(addend=0x{target_value:08x})"
        else:
            raise ValueError(
                f"unexpected vtable relocation type {relocation_type} "
                f"at 0x{relocation.offset:x}"
            )

        names.append(target_name)
        rows.append((place_offset, relocation, target_value))

    pretty_names = demangle_names(names)

    print(f"ELF: {binary}")
    print(f"Vtable symbol: {vtable.name}")
    print(f"Vtable address: 0x{vtable.value:08x}")
    print(f"Vtable size: {vtable.size} bytes")
    print(f"APS2 relocation count: {relocation_count}")
    print(f"Relocations targeting vtable: {len(rows)}")
    print("Offset  Address     Symbol value  Method")
    print("------  ----------  ------------  ------")

    for (place_offset, relocation, target_value), pretty_name in zip(rows, pretty_names):
        print(
            f"+0x{place_offset:03x}   "
            f"0x{relocation.offset:08x}  "
            f"0x{target_value:08x}    "
            f"{pretty_name}"
        )

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode Android APS2 relocations and recover an ELF32 ARM vtable."
    )
    parser.add_argument("binary", type=Path)
    parser.add_argument(
        "--symbol",
        default="_ZTVN7android17AudioALSAHardwareE",
        help="exact dynamic symbol name (default: AudioALSAHardware vtable)",
    )
    args = parser.parse_args()

    try:
        if not args.binary.is_file():
            raise ValueError(f"file not found: {args.binary}")
        return inspect_vtable(args.binary, args.symbol)
    except (OSError, ValueError, struct.error, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
