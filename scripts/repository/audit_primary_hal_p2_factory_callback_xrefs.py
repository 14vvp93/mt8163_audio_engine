#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

EXPECTED_SHA256 = "112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4"
TARGETS = (
    "AudioALSAHardware::onAudioParameterChangedCallback(",
    "AudioALSAHardware::setParametersByCallback(",
    "AudioALSAHardware::setupParametersCallback(",
    "AudioALSAHardware::setAudioParameterChangedCallback(",
    "AudioALSAHardware::clearAudioParameterChangedCallback(",
)
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(FUNC|OBJECT|NOTYPE)\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
)
FUNCTION_HEADER = re.compile(r"^\s*([0-9a-fA-F]+) <(.+)>:$")
INSTRUCTION = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+(?:(?:[0-9a-fA-F]{4,8})\s+)+([a-zA-Z][a-zA-Z0-9.]*)\s*(.*?)\s*$"
)
DIRECT_BRANCH = re.compile(
    r"^\s*(?:0x)?([0-9a-fA-F]+)(?:\s+<([^>]+)>)?\s*$"
)
INDIRECT_BRANCH = re.compile(r"^\s*(?:r(?:1[0-2]|[0-9])|ip|lr)\s*$", re.IGNORECASE)


@dataclass(frozen=True)
class Symbol:
    address: int
    size: int
    name: str


@dataclass(frozen=True)
class CallSite:
    address: int
    caller: str
    mnemonic: str
    operand: str
    target: int | None
    target_name: str | None


def run(command: list[str], timeout: int = 180) -> str:
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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)



@dataclass(frozen=True)
class InstructionRecord:
    address: int
    mnemonic: str
    operand: str


def parse_function_instructions(
    disassembly: str,
    function_prefix: str,
) -> list[InstructionRecord]:
    records: list[InstructionRecord] = []
    current_function = ""
    matched_function = False
    matched_address: int | None = None
    for line in disassembly.splitlines():
        header = FUNCTION_HEADER.match(line)
        if header is not None:
            header_address = int(header.group(1), 16)
            if matched_function:
                if records or header_address != matched_address:
                    break
                current_function = header.group(2)
                matched_function = (
                    matched_function or current_function.startswith(function_prefix)
                )
                continue
            current_function = header.group(2)
            matched_function = current_function.startswith(function_prefix)
            if matched_function:
                matched_address = header_address
            continue
        if not matched_function:
            continue
        match = INSTRUCTION.match(line)
        if match is None:
            continue
        address_text, mnemonic, operand = match.groups()
        records.append(
            InstructionRecord(int(address_text, 16), mnemonic.lower(), operand.strip())
        )
    return records


def normalize_operand(operand: str) -> str:
    operand = re.sub(r"\s+@\s.*$", "", operand.lower())
    return re.sub(r"\s+", "", operand)


def require_instruction(
    records: list[InstructionRecord],
    address: int,
    mnemonic: str,
    operand: str,
) -> int:
    matches = [index for index, item in enumerate(records) if item.address == address]
    require(len(matches) == 1, f"expected one instruction at 0x{address:08x}")
    index = matches[0]
    instruction = records[index]
    require(
        instruction.mnemonic == mnemonic
        and normalize_operand(instruction.operand) == normalize_operand(operand),
        f"unexpected instruction at 0x{address:08x}: "
        f"{instruction.mnemonic} {instruction.operand}",
    )
    return index


def verify_callback_dataflow(disassembly: str) -> None:
    dispatch = parse_function_instructions(
        disassembly, "android::AudioALSAHardware::onAudioParameterChangedCallback("
    )
    require(dispatch, "parameter callback dispatcher instructions missing")
    dispatch_indices = [
        require_instruction(dispatch, address, mnemonic, operand)
        for address, mnemonic, operand in (
            (0x000CF00C, "ldr", "r0, [sl, #948]"),
            (0x000CF010, "ldr", "r0, [r0, r7, lsl #2]"),
            (0x000CF014, "ldr", "r1, [r0, #4]"),
            (0x000CF018, "ldr", "r2, [r0]"),
            (0x000CF01C, "mov", "r0, r9"),
            (0x000CF020, "blx", "r2"),
            (0x000CF050, "bl", "47e04 <alock_unlock@plt>"),
        )
    ]
    require(
        dispatch_indices == sorted(dispatch_indices),
        "parameter callback dispatch order or unlock ordering changed",
    )
    require_instruction(dispatch, 0x000CEF60, "bl", "47da4 <alock_lock_ms@plt>")
    require_instruction(dispatch, 0x000CEF64, "cmp", "r0, #0")
    require_instruction(
        dispatch,
        0x000CEF68,
        "beq",
        "cefc4 <android::AudioALSAHardware::onAudioParameterChangedCallback(char const*)@@Base+0xac>",
    )

    global_callback = parse_function_instructions(
        disassembly, "android::AudioALSAHardware::setParametersByCallback("
    )
    require(global_callback, "global parameter callback instructions missing")
    global_indices = [
        require_instruction(global_callback, address, mnemonic, operand)
        for address, mnemonic, operand in (
            (0x000CF1F8, "ldr", "r0, [pc, #108]"),
            (0x000CF200, "ldr", "r0, [pc, r0]"),
            (0x000CF204, "ldr", "r3, [r0]"),
            (0x000CF208, "ldr", "r0, [pc, #96]"),
            (0x000CF20C, "ldr", "r0, [pc, r0]"),
            (0x000CF210, "ldr", "r2, [r0]"),
            (0x000CF214, "mov", "r0, #0"),
            (0x000CF218, "blx", "r3"),
        )
    ]
    require(
        global_indices == sorted(global_indices),
        "global callback/cookie load or BLX order changed",
    )
    require_instruction(global_callback, 0x000CF1FC, "mov", "r1, r8")

    setup_callback = parse_function_instructions(
        disassembly, "android::AudioALSAHardware::setupParametersCallback("
    )
    require(setup_callback, "setup-parameters callback registration instructions missing")
    setup_indices = [
        require_instruction(setup_callback, address, mnemonic, operand)
        for address, mnemonic, operand in (
            (0x000CE904, "mov", "r5, r1"),
            (0x000CE908, "mov", "r4, r2"),
            (0x000CE928, "ldr", "r0, [pc, #136]"),
            (0x000CE930, "ldr", "r0, [pc, r0]"),
            (0x000CE934, "str", "r5, [r0]"),
            (0x000CE938, "ldr", "r0, [pc, #124]"),
            (0x000CE93C, "ldr", "r0, [pc, r0]"),
            (0x000CE940, "str", "r4, [r0]"),
        )
    ]
    require(
        setup_indices == sorted(setup_indices),
        "setup-parameters callback/cookie registration dataflow changed",
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "Usage: audit_primary_hal_p2_factory_callback_xrefs.py <audio.primary.mt8163.so>",
            file=sys.stderr,
        )
        return 2

    binary = Path(sys.argv[1])
    require(binary.is_file(), f"factory HAL not found: {binary}")
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    require(digest == EXPECTED_SHA256, f"unexpected factory HAL SHA-256: {digest}")

    readelf = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols: list[Symbol] = []
    for line in readelf.splitlines():
        match = SYMBOL_LINE.match(line)
        if match is None:
            continue
        address, size, kind, name = match.groups()
        if kind == "FUNC" and int(size, 16) > 0:
            symbols.append(Symbol(int(address, 16) & ~1, int(size, 16), name))

    selected: dict[str, list[Symbol]] = {}
    for target in TARGETS:
        matches = [symbol for symbol in symbols if target in symbol.name]
        require(matches, f"required target symbol missing: {target}")
        selected[target] = matches

    disassembly = run(["arm-linux-gnueabi-objdump", "-dr", "-C", str(binary)], timeout=240)
    verify_callback_dataflow(disassembly)
    print("CALLBACK_DATAFLOW_ASSERTIONS: PASS")

    ordered_symbols = sorted(symbols, key=lambda symbol: (symbol.address, -symbol.size))
    callsites: list[CallSite] = []
    current_function = "<unknown>"
    for line in disassembly.splitlines():
        header = FUNCTION_HEADER.match(line)
        if header:
            current_function = header.group(2)
            continue
        instruction = INSTRUCTION.match(line)
        if instruction is None:
            continue
        address_text, mnemonic, operand = instruction.groups()
        normalized = mnemonic.lower()
        if normalized not in {"bl", "blx"}:
            continue

        target_match = DIRECT_BRANCH.match(operand)
        if target_match is not None:
            target = int(target_match.group(1), 16) & ~1
            target_name = target_match.group(2)
            callsites.append(
                CallSite(int(address_text, 16), current_function, normalized, operand, target, target_name)
            )
        elif INDIRECT_BRANCH.match(operand):
            callsites.append(
                CallSite(int(address_text, 16), current_function, normalized, operand, None, None)
            )

    print(f"Factory HAL SHA-256: {digest}")
    print("Analysis: complete ARM/Thumb disassembly; direct BL/BLX and register-indirect BLX inventory")
    print("Limit: indirect calls are enumerated but cannot be attributed to a target without data-flow proof.")

    for target_name, target_symbols in selected.items():
        target_addresses = {symbol.address for symbol in target_symbols}
        direct = [site for site in callsites if site.target in target_addresses]
        print(f"\n===== TARGET {target_name} =====")
        for symbol in target_symbols:
            print(f"SYMBOL 0x{symbol.address:08x} size={symbol.size} {symbol.name}")
        if direct:
            for site in direct:
                print(
                    f"DIRECT_CALL 0x{site.address:08x} caller={site.caller} "
                    f"mnemonic={site.mnemonic} operand={site.operand}"
                )
        else:
            print("DIRECT_CALLS: none resolved by direct branch target address")

    for target_name in (
        "AudioALSAHardware::onAudioParameterChangedCallback(",
        "AudioALSAHardware::setParametersByCallback(",
        "AudioALSAHardware::setupParametersCallback(",
    ):
        print(f"\n===== BODY {target_name} =====")
        for symbol in selected[target_name]:
            start = symbol.address
            end = start + symbol.size
            body = run([
                "arm-linux-gnueabi-objdump", "-dr", "-C",
                f"--start-address=0x{start:x}",
                f"--stop-address=0x{end:x}",
                str(binary),
            ])
            print(body.rstrip())

    print("\n===== INDIRECT CALLSITE SUMMARY =====")
    indirect = [site for site in callsites if site.target is None]
    print(f"REGISTER_INDIRECT_BLX_COUNT={len(indirect)}")
    for site in indirect:
        print(
            f"INDIRECT_BLX 0x{site.address:08x} caller={site.caller} "
            f"operand={site.operand}"
        )

    print(
        "\nStatic-only result. Direct branch xrefs are exhaustive for the pinned disassembly; "
        "register-indirect calls, data-driven dispatch, runtime-loaded callers and "
        "stripped external clients require separate data-flow/runtime evidence."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
