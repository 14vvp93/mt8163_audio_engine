#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
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
    if not functions:
        raise RuntimeError("no sized FUNC symbols found in HIDL bridge symbol table")

    reports: list[str] = [
        f"HIDL bridge SHA-256: {digest}",
        "Source: direct BL targets observed in Device::syncCallback at 0x0001cb4c",
        "Each report contains the complete containing FUNC symbol range, not a fixed-size excerpt.",
        "All helper inspection is read-only; no runtime behavior is inferred from names.",
    ]

    unresolved: list[int] = []
    for target in HELPER_TARGETS:
        try:
            address, size, name, thumb = resolve_function(target, functions)
        except RuntimeError as error:
            unresolved.append(target)
            reports.extend([
                "",
                f"CALL_TARGET 0x{target:08x} -> UNRESOLVED",
                f"RESOLUTION_ERROR: {error}",
                "No guessed function boundary or truncated disassembly was emitted.",
            ])
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
