#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc"
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(FUNC|OBJECT|NOTYPE)\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
)
DEVICE_TYPE = re.compile(r"(?:^|::)Device(?:::|$)", re.IGNORECASE)
OWNER_HINT = re.compile(
    r"device|callback|owner|vector|list|weak|strong|sp<|wp<",
    re.IGNORECASE,
)
CALL_TARGET = re.compile(r"\b(?:bl|blx)\s+[^<\n]*<([^>]+)>", re.IGNORECASE)
DIRECT_CALL_PATTERN = re.compile(
    r"^\s*([0-9a-fA-F]+):.*\bblx?\s+([0-9a-fA-F]+)\s+<([^>]+)>",
    re.IGNORECASE,
)
RELOCATION_OFFSET_PATTERN = re.compile(r"^\s*([0-9a-fA-F]+)\s+")


def parse_direct_call(line: str) -> tuple[int, int, str] | None:
    match = DIRECT_CALL_PATTERN.match(line)
    if match is None:
        return None
    callsite, target, target_name = match.groups()
    return int(callsite, 16), int(target, 16) & ~1, target_name


def parse_relocation_offset(line: str) -> int | None:
    match = RELOCATION_OFFSET_PATTERN.match(line)
    return int(match.group(1), 16) if match else None


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
    raw_symbol_output = run(["arm-linux-gnueabi-readelf", "-Ws", str(binary)])
    symbols: list[tuple[int, int, str, str]] = []
    for line in symbol_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if match:
            address, size, kind, name = match.groups()
            symbols.append((int(address, 16), int(size, 10), kind, name))

    constructors = [
        item for item in symbols
        if item[2] == "FUNC" and re.search(r"(?:^|::)Device::Device\(", item[3])
    ]
    destructors = [
        item for item in symbols
        if item[2] == "FUNC" and re.search(r"(?:^|::)Device::~Device\(", item[3])
    ]
    owner_candidates = [
        item for item in symbols
        if item[2] in {"OBJECT", "NOTYPE"} and OWNER_HINT.search(item[3])
    ]

    refbase_constructor_calls: list[tuple[int, int, str, str]] = []
    refbase_constructor_veneer = 0x281DC

    vtables = [
        item for item in symbols
        if item[2] == "OBJECT"
        and "vtable for android::hardware::audio::V4_0::implementation::Device" in item[3]
        and item[1] > 0
    ]
    relocations = run(["arm-linux-gnueabi-readelf", "-r", "-C", str(binary)])

    raw_constructor_symbols = [
        line.strip() for line in raw_symbol_output.splitlines()
        if re.search(
            r"_ZN7android8hardware5audio4V4_014implementation6DeviceC[12]E",
            line,
        )
    ]
    constructor_call_sites: list[tuple[int, int, str, str]] = []
    constructor_addresses = {item[0] & ~1 for item in constructors}
    derived_constructors = [
        item for item in symbols
        if item[2] == "FUNC"
        and (
            "PrimaryDevice::PrimaryDevice(" in item[3]
            or "MTKPrimaryDevice::MTKPrimaryDevice(" in item[3]
        )
        and item[1] > 0
    ]
    for address, size, _, name in [*constructors, *derived_constructors]:
        code_address = address & ~1
        command = ["arm-linux-gnueabi-objdump", "-dr", "-C"]
        if address & 1:
            command.extend(["-M", "force-thumb"])
        command.extend([
            f"--start-address=0x{code_address:x}",
            f"--stop-address=0x{code_address + size:x}",
            str(binary),
        ])
        body = run(command)
        for line in body.splitlines():
            parsed_call = parse_direct_call(line)
            if parsed_call is None:
                continue
            callsite, target, target_name = parsed_call
            if target == refbase_constructor_veneer or "RefBase::RefBase" in target_name:
                callers = [
                    item for item in symbols
                    if item[2] == "FUNC" and item[0] <= callsite < item[0] + item[1]
                ]
                callers.sort(key=lambda item: (item[1], item[0], item[3]))
                caller = callers[0][3] if callers else name
                refbase_constructor_calls.append(
                    (callsite, target, caller, target_name)
                )
            if target in constructor_addresses:
                constructor_call_sites.append(
                    (callsite, target, name, target_name)
                )

    print(f"HIDL bridge SHA-256: {digest}")
    print("Target Device owner/lifetime symbol inventory")
    print(f"Device constructors: {len(constructors)}")
    print(f"Device destructors: {len(destructors)}")
    print("Raw Device constructor ABI symbols:")
    print("\n".join(raw_constructor_symbols) if raw_constructor_symbols else "none")
    print(f"Direct Device constructor callsites: {len(constructor_call_sites)}")
    for callsite, target, caller, target_name in constructor_call_sites:
        print(
            f"DEVICE_CTOR_CALLSITE 0x{callsite:08x} -> 0x{target:08x} "
            f"caller={caller} target={target_name}"
        )

    print(
        "RefBase constructor / interworking-veneer callsites: "
        f"{len(refbase_constructor_calls)}"
    )
    if not refbase_constructor_calls:
        print(
            "No direct BL/BLX call to the RefBase constructor or its known "
            "interworking veneer was found in the pinned HIDL bridge."
        )
    for callsite, target, caller, target_name in refbase_constructor_calls:
        print(
            f"REFBASE_CTOR_CALLSITE 0x{callsite:08x} -> 0x{target:08x} "
            f"caller={caller} target={target_name}"
        )

    print(f"Device vtable groups: {len(vtables)}")
    for address, size, _, name in vtables:
        print(f"\n===== VTABLE {name} @ 0x{address:08x}, size={size} =====")
        print(run([
            "arm-linux-gnueabi-objdump", "-s", "-C",
            f"--start-address=0x{address:x}",
            f"--stop-address=0x{address + size:x}",
            str(binary),
        ]).rstrip())
        vtable_relocations = []
        for line in relocations.splitlines():
            offset = parse_relocation_offset(line)
            if offset is None:
                continue
            if address <= offset < address + size:
                vtable_relocations.append(line.strip())
        print("VTABLE_RELOCATIONS:")
        print("\n".join(vtable_relocations) if vtable_relocations else "none")

    for label, candidates in (("CONSTRUCTOR", constructors), ("DESTRUCTOR", destructors)):
        for index, (address, size, _, name) in enumerate(candidates):
            print(f"\n===== {label} {index}: {name} @ 0x{address:08x}, size={size} =====")
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

    print("\n===== GLOBAL / OWNER CANDIDATE SYMBOLS =====")
    if not owner_candidates:
        print("No named OBJECT/NOTYPE owner candidates found in the ELF symbol table.")
    for address, size, kind, name in owner_candidates:
        print(f"{kind} 0x{address:08x} size={size} {name}")

    print("\n===== DEVICE-RELATED SYMBOLS =====")
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
        "\nStatic symbol/disassembly inventory only. It does not prove runtime "
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
