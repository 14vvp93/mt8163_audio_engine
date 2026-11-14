#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "553704e5285c4ab1e505bab569ac261f40ce7e6aa8c0c9173e038acc59dc3be5"
FUNCTIONS = (
    "vendor::mediatek::hardware::audio::V4_1::BpHwAudioParameterChangedCallback::_hidl_audioParameterChangedCallback(android::hardware::IInterface*, android::hardware::details::HidlInstrumentor*, android::hardware::hidl_string const&)",
    "vendor::mediatek::hardware::audio::V4_1::BpHwAudioParameterChangedCallback::audioParameterChangedCallback(android::hardware::hidl_string const&)",
    "vendor::mediatek::hardware::audio::V4_1::BnHwAudioParameterChangedCallback::_hidl_audioParameterChangedCallback(android::hidl::base::V1_0::BnHwBase*, android::hardware::Parcel const&, android::hardware::Parcel*, std::__1::function<void (android::hardware::Parcel&)>)",
    "vendor::mediatek::hardware::audio::V4_1::BnHwAudioParameterChangedCallback::onTransact(unsigned int, android::hardware::Parcel const&, android::hardware::Parcel*, unsigned int, std::__1::function<void (android::hardware::Parcel&)>)",
    "vendor::mediatek::hardware::audio::V4_1::BsAudioParameterChangedCallback::addOnewayTask(std::__1::function<void ()>)",
)
CALL_TARGET = re.compile(
    r"\b(?:bl|blx)\s+[^<\n]*<([^>]+)>",
    re.IGNORECASE,
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
        timeout=60,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: audit_primary_hal_p2_hidl_callback.py <vendor.mediatek.hardware.audio@4.1.so>", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"ERROR: HIDL interface library not found: {binary}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f"ERROR: unexpected HIDL interface library SHA-256: {digest}", file=sys.stderr)
        return 1

    symbol_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols: dict[str, list[tuple[int, int]]] = {}
    for line in symbol_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if not match:
            continue
        address_text, size_text, name = match.groups()
        symbols.setdefault(name, []).append((int(address_text, 16), int(size_text, 10)))

    print(f"HIDL callback interface SHA-256: {digest}")
    print("Read-only HIDL callback proxy/stub transaction disassembly")

    missing = [name for name in FUNCTIONS if name not in symbols]
    if missing:
        for name in missing:
            print(f"ERROR: required symbol not found: {name}", file=sys.stderr)
        return 1

    for name in FUNCTIONS:
        for index, (address, size) in enumerate(symbols[name]):
            if size <= 0:
                continue
            code_address = address & ~1
            command = ["arm-linux-gnueabi-objdump", "-d", "-C"]
            if address & 1:
                command.extend(["-M", "force-thumb"])
            command.extend([
                f"--start-address=0x{code_address:x}",
                f"--stop-address=0x{code_address + size:x}",
                str(binary),
            ])
            disassembly = run(command)
            expected_address = rf"^\s*0*{code_address:x}(?:\s*<|:\s+)"
            if not re.search(expected_address, disassembly, re.MULTILINE):
                preview = "\n".join(disassembly.splitlines()[:8])
                raise RuntimeError(
                    f"disassembly did not contain the expected start address "
                    f"0x{code_address:x}: {name}\n{preview}"
                )
            calls = sorted(set(CALL_TARGET.findall(disassembly)), key=str.casefold)
            transaction_contract = None
            if name == FUNCTIONS[0]:
                synchronous_signature = (
                    re.search(r"\\bmovs\\s+r1,\\s*#1\\b", disassembly, re.IGNORECASE)
                    and re.search(r"\\bmov\\.w\\s+sl,\\s*#0\\b", disassembly, re.IGNORECASE)
                    and re.search(r"\\bstrd\\s+sl,\\s*r8,\\s*\\[sp\\]", disassembly, re.IGNORECASE)
                )
                if not synchronous_signature:
                    raise RuntimeError(
                        "pinned Bp proxy no longer matches the audited transaction "
                        "signature (transaction code 1, Binder flags 0)"
                    )
                transaction_contract = (
                    "Binder transaction: code=1, flags=0 (synchronous; "
                    "not FLAG_ONEWAY)"
                )
            isa = "Thumb" if address & 1 else "ARM"
            print(f"\n===== {name} [{index}] @ 0x{code_address:08x}, size={size}, ISA={isa} =====")
            print("Direct call targets: " + (", ".join(calls) if calls else "none detected"))
            if transaction_contract:
                print(transaction_contract)
            print(disassembly.rstrip())

    print("\nNo runtime Binder scheduling or callback completion behavior is inferred.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
