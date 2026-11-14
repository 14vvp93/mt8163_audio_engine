#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4"
RESOURCE_VTABLE = "_ZTVN7android22AudioALSAStreamManagerE"
RESOURCE_VTABLE_NAME = "vtable for android::AudioALSAStreamManager"
CONSTRUCTOR_START = 0x000C5274
CONSTRUCTOR_END = 0x000C565C
DESTRUCTOR_START = 0x000C56E4
DESTRUCTOR_END = 0x000C57F8
GET_INSTANCE_START = 0x000D0EE0
GET_INSTANCE_END = 0x000D1244
ATEXIT_FUNCTION = 0x00073524
GLOBAL_POINTER_CELLS = (0x00192B78, 0x00192B7C, 0x00192B80)


def run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=90,
    )
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr}"
        )
    return result.stdout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def disassemble(binary: Path, start: int, end: int) -> str:
    return run(
        [
            "arm-linux-gnueabi-objdump",
            "-dr",
            "-C",
            f"--start-address=0x{start:x}",
            f"--stop-address=0x{end:x}",
            str(binary),
        ]
    )


def parse_function_symbols(readelf_output: str) -> list[tuple[int, int, str]]:
    symbols: list[tuple[int, int, str]] = []
    pattern = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9]+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
    )
    for line in readelf_output.splitlines():
        match = pattern.match(line)
        if match:
            address, size, name = match.groups()
            symbols.append((int(address, 16), int(size, 10), name))
    return symbols


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "Usage: audit_primary_hal_p2_factory_resource_manager.py "
            "<audio.primary.mt8163.so> <resource-manager-vtable-report> "
            "<factory-device-close-report>",
            file=sys.stderr,
        )
        return 2

    binary = Path(sys.argv[1])
    vtable_report = Path(sys.argv[2])
    close_report = Path(sys.argv[3])
    require(binary.is_file(), f"factory HAL not found: {binary}")
    require(vtable_report.is_file(), f"resource-manager vtable report not found: {vtable_report}")
    require(close_report.is_file(), f"factory close report not found: {close_report}")

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    require(digest == EXPECTED_SHA256, f"unexpected factory HAL SHA-256: {digest}")

    relocations = vtable_report.read_text(encoding="utf-8", errors="replace")
    require(f"Vtable symbol: {RESOURCE_VTABLE}" in relocations, "wrong resource-manager vtable symbol")
    require(f"Vtable symbol: {RESOURCE_VTABLE}" in relocations, "resource-manager vtable report missing")
    require(
        f"vtable for android::AudioALSAStreamManager" in relocations
        or RESOURCE_VTABLE in relocations,
        "resource-manager vtable identity missing",
    )
    destructor_slot = re.search(
        r"^\+0x00c\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+(.+)$",
        relocations,
        re.MULTILINE,
    )
    require(destructor_slot is not None, "resource-manager deleting-destructor slot +0x00c missing")
    deleting_destructor_address = int(destructor_slot.group(1), 16)
    deleting_destructor_name = destructor_slot.group(2).strip()
    require(
        "AudioALSAStreamManager::~AudioALSAStreamManager()" in deleting_destructor_name,
        f"unexpected resource-manager vtable slot +0x00c: {deleting_destructor_name}",
    )

    constructor = disassemble(binary, CONSTRUCTOR_START, CONSTRUCTOR_END)
    require(
        "AudioALSAStreamManager::getInstance()@plt" in constructor,
        "AudioALSAHardware constructor no longer calls AudioALSAStreamManager::getInstance@plt",
    )
    require(
        re.search(r"str\s+r0,\s*\[r4,\s*#4\]", constructor) is not None,
        "AudioALSAHardware constructor no longer stores the resource-manager pointer at this+4",
    )

    destructor = disassemble(binary, DESTRUCTOR_START, DESTRUCTOR_END)
    require(
        re.search(r"ldr\s+r0,\s*\[r4,\s*#4\]", destructor) is not None
        and re.search(r"ldr\s+r1,\s*\[r0\]", destructor) is not None
        and re.search(r"ldr\s+r1,\s*\[r1,\s*#4\]", destructor) is not None
        and re.search(r"blx\s+r1", destructor) is not None,
        "AudioALSAHardware destructor virtual cleanup sequence changed",
    )

    readelf_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    functions = parse_function_symbols(readelf_output)
    atexit_candidates = [
        (address, size, name)
        for address, size, name in functions
        if address <= ATEXIT_FUNCTION < address + max(size, 1)
    ]

    object_pattern = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9]+)\s+OBJECT\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
    )
    object_symbols: list[tuple[int, int, str]] = []
    for line in readelf_output.splitlines():
        match = object_pattern.match(line)
        if match:
            address, size, name = match.groups()
            object_symbols.append((int(address, 16), int(size, 10), name))

    close_text = close_report.read_text(encoding="utf-8", errors="replace")
    require("0x00192b78" in close_text, "factory close report does not confirm singleton pointer cell")

    print(f"Factory HAL SHA-256: {digest}")
    print(f"AudioALSAHardware constructor: 0x{CONSTRUCTOR_START:08x}..0x{CONSTRUCTOR_END:08x}")
    print("Constructor stores AudioALSAStreamManager::getInstance() result at AudioALSAHardware+0x04")
    print(f"AudioALSAHardware destructor: 0x{DESTRUCTOR_START:08x}..0x{DESTRUCTOR_END:08x}")
    print("Destructor calls the second virtual slot of the object at AudioALSAHardware+0x04")
    print(f"Resource-manager vtable: {RESOURCE_VTABLE}")
    print(f"Resource-manager deleting destructor: 0x{deleting_destructor_address:08x} {deleting_destructor_name}")
    print("STATIC CONTRACT: AudioALSAHardware destruction dispatches the resource-manager deleting-destructor slot.")

    if atexit_candidates:
        print(f"GetInstance __cxa_atexit function pointer 0x{ATEXIT_FUNCTION:08x} resolves to:")
        for address, size, name in atexit_candidates:
            print(f"  0x{address:08x} size={size}: {name}")
    else:
        print(f"GetInstance __cxa_atexit function pointer 0x{ATEXIT_FUNCTION:08x}: no containing dynamic FUNC symbol")

    print("Symbols overlapping adjacent destructor-cleared global pointer cells:")
    for cell in GLOBAL_POINTER_CELLS:
        matches = [
            (address, size, name)
            for address, size, name in object_symbols
            if address <= cell < address + max(size, 1)
        ]
        if matches:
            for address, size, name in matches:
                print(f"  0x{cell:08x}: 0x{address:08x} size={size}: {name}")
        else:
            print(f"  0x{cell:08x}: no matching OBJECT symbol in ELF symbol tables")

    print("Static evidence only; singleton ownership, destructor reentrancy, and callback quiescence remain unproven.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
