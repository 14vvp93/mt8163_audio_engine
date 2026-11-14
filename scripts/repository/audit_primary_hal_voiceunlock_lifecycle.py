#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4"
FUNCTIONS = (
    "android::ReadRoutine(void*)",
    "android::AudioVUnlockDL::startInput()",
    "android::AudioVUnlockDL::stopInput()",
    "android::AudioVUnlockDL::freeInstance()",
    "android::AudioVUnlockDL::ReadRefFromRing(void*, unsigned int, void*)",
    "android::AudioVUnlockDL::SetInputStandBy(bool)",
    "android::AudioALSAHardware::startVoiceUnlockDL()",
    "android::AudioALSAHardware::stopVoiceUnlockDL()",
    "android::AudioALSAHardware::freeVoiceUnlockDLInstance()",
)
THREAD_CALLS = re.compile(
    r"pthread_(?:create|detach|join|mutex_lock|mutex_unlock|cond_wait)|usleep"
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
        print("Usage: audit_primary_hal_voiceunlock_lifecycle.py <factory-hal.so>", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"ERROR: factory HAL not found: {binary}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f"ERROR: unexpected factory HAL SHA-256: {digest}", file=sys.stderr)
        return 1

    nm_output = run(["arm-linux-gnueabi-nm", "-S", "-C", "--defined-only", str(binary)])
    symbols: dict[str, tuple[int, int]] = {}
    for line in nm_output.splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) != 4:
            continue
        address_text, size_text, _, name = fields
        try:
            symbols[name] = (int(address_text, 16), int(size_text, 16))
        except ValueError:
            continue

    print(f"Factory SHA-256: {digest}")
    print("Read-only targeted lifecycle disassembly")
    missing = [name for name in FUNCTIONS if name not in symbols]
    if missing:
        for name in missing:
            print(f"ERROR: required symbol not found: {name}", file=sys.stderr)
        return 1

    for name in FUNCTIONS:
        address, size = symbols[name]
        if size <= 0:
            print(f"ERROR: zero-sized symbol: {name}", file=sys.stderr)
            return 1
        disassembly = run([
            "arm-linux-gnueabi-objdump",
            "-d",
            "-C",
            f"--start-address=0x{address:x}",
            f"--stop-address=0x{address + size:x}",
            str(binary),
        ])
        calls = sorted(set(THREAD_CALLS.findall(disassembly)))
        print(f"\n===== {name} @ 0x{address:08x}, size={size} =====")
        print("Thread/lifecycle references: " + (", ".join(calls) if calls else "none detected"))
        print(disassembly.rstrip())

    print("\nNo runtime or device behavior is inferred by this disassembly report.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
