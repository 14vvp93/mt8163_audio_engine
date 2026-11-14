#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc"
FUNCTIONS = (
    "android::hardware::audio::V4_0::implementation::Device::audioParameterChangedCallback(char const*, void*)",
    "android::hardware::audio::V4_0::implementation::Device::setAudioParameterChangedCallback(android::sp<vendor::mediatek::hardware::audio::V4_1::IAudioParameterChangedCallback> const&)",
    "android::hardware::audio::V4_0::implementation::Device::clearAudioParameterChangedCallback()",
)
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
)
CALL_TARGET = re.compile(r"\b(?:bl|blx)\s+[^<\n]*<([^>]+)>", re.IGNORECASE)
LOCK_CALL = re.compile(
    r"\b(?:alock_lock_ms|alock_unlock|pthread_mutex_lock|pthread_mutex_unlock|"
    r"pthread_rwlock_rdlock|pthread_rwlock_wrlock)\b",
    re.IGNORECASE,
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


def disassemble(binary: Path, address: int, size: int) -> str:
    code_address = address & ~1
    command = ["arm-linux-gnueabi-objdump", "-d", "-C"]
    if address & 1:
        command.extend(["-M", "force-thumb"])
    command.extend([
        f"--start-address=0x{code_address:x}",
        f"--stop-address=0x{code_address + size:x}",
        str(binary),
    ])
    output = run(command)
    if not re.search(rf"^\s*0*{code_address:x}(?:\s*<|:\s+)", output, re.MULTILINE):
        raise RuntimeError(f"invalid disassembly start for 0x{code_address:x}")
    return output


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "Usage: audit_primary_hal_p2_hidl_bridge.py "
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

    symbols_output = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    symbols: dict[str, list[tuple[int, int]]] = {}
    for line in symbols_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if match:
            address, size, name = match.groups()
            symbols.setdefault(name, []).append((int(address, 16), int(size, 10)))

    missing = [name for name in FUNCTIONS if name not in symbols]
    if missing:
        for name in missing:
            print(f"ERROR: required symbol not found: {name}", file=sys.stderr)
        return 1

    print(f"HIDL bridge SHA-256: {digest}")
    print("Read-only Device callback bridge lifecycle disassembly")

    reports: dict[str, str] = {}
    for name in FUNCTIONS:
        for index, (address, size) in enumerate(symbols[name]):
            if size <= 0:
                continue
            output = disassemble(binary, address, size)
            reports[name] = output
            calls = sorted(set(CALL_TARGET.findall(output)), key=str.casefold)
            locks = sorted(set(LOCK_CALL.findall(output)), key=str.casefold)
            print(f"\n===== {name} [{index}] @ 0x{address & ~1:08x}, size={size} =====")
            print("Direct call targets: " + (", ".join(calls) if calls else "none detected"))
            print("Visible lock calls: " + (", ".join(locks) if locks else "none detected"))
            print(output.rstrip())

    thunk = reports[FUNCTIONS[0]]
    setter = reports[FUNCTIONS[1]]
    clearer = reports[FUNCTIONS[2]]

    if not re.search(r"\[r1,\s*#40\]", thunk, re.IGNORECASE):
        raise RuntimeError("callback thunk no longer accesses the expected Device callback member at +0x28")
    if not re.search(r"\[r0,\s*#52\]", thunk, re.IGNORECASE):
        raise RuntimeError("callback thunk no longer invokes the expected callback vtable slot at +0x34")
    if not re.search(r"#212", setter) or not re.search(r"\bblx\s+r3\b", setter):
        raise RuntimeError("callback setter no longer dispatches the legacy set slot at device offset 0xd4")
    if not re.search(r"#216", clearer) or not re.search(r"\bblx\s+r2\b", clearer):
        raise RuntimeError("callback clearer no longer dispatches the legacy clear slot at device offset 0xd8")

    print("\nVerified bridge contract:")
    print("- callback thunk reads the stored HIDL callback member at Device+0x28")
    print("- callback thunk invokes the callback interface through vtable byte offset 0x34")
    print("- setter invokes the legacy HAL registration slot at audio_hw_device+0xd4")
    print("- clearer invokes the legacy HAL clear slot at audio_hw_device+0xd8")
    print("- no runtime Device destruction or callback quiescence is inferred")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
