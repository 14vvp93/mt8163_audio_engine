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
    "android::AudioVUnlockDL::getInstance()",
    "android::AudioVUnlockDL::~AudioVUnlockDL()",
    "android::AudioVUnlockDL::ReadRefFromRing(void*, unsigned int, void*)",
    "android::AudioVUnlockDL::SetInputStandBy(bool)",
    "android::AudioALSAHardware::startVoiceUnlockDL()",
    "android::AudioALSAHardware::stopVoiceUnlockDL()",
    "android::AudioALSAHardware::freeVoiceUnlockDLInstance()",
    "android::AudioALSAHardware::getVoiceUnlockDLInstance()",
    "android::AudioALSAHardware::setupParametersCallback(int (*)(device_parameters_callback_event_t, audio_hw_device_set_parameters_callback*, void*), void*)",
    "android::AudioALSAHardware::setAudioParameterChangedCallback(int (*)(char const*, void*), void*)",
    "android::AudioALSAHardware::clearAudioParameterChangedCallback(void*)",
    "android::AudioALSAHardware::onAudioParameterChangedCallback(char const*)",
    "android::AudioALSAHardware::setParametersByCallback(android::String8 const&)",
    "android::AudioALSAHardware::xWayPlay_Start(int)",
    "android::AudioALSAHardware::xWayPlay_Stop()",
    "android::AudioALSAHardware::xWayPlay_Write(void*, int)",
    "android::AudioALSAHardware::xWayPlay_GetFreeBufferCount()",
    "android::AudioALSAHardware::xWayRec_Start(int)",
    "android::AudioALSAHardware::xWayRec_Stop()",
    "android::AudioALSAHardware::xWayRec_Read(void*, int)",
    "android::AudioALSAHardware::ReadRefFromRing(void*, unsigned int, void*)",
)
THREAD_CALLS = re.compile(
    r"pthread_(?:create|self|detach|join|mutex_lock|mutex_unlock|cond_wait|cond_timedwait|cond_broadcast|cond_signal)|"
    r"alock_(?:lock_ms|unlock)|usleep"
)
REQUIRED_LOCK_CALLS = {
    "android::AudioALSAHardware::setAudioParameterChangedCallback(int (*)(char const*, void*), void*)": {
        "alock_lock_ms",
        "alock_unlock",
    },
    "android::AudioALSAHardware::clearAudioParameterChangedCallback(void*)": {
        "alock_lock_ms",
        "alock_unlock",
    },
    "android::AudioALSAHardware::onAudioParameterChangedCallback(char const*)": {
        "alock_lock_ms",
        "alock_unlock",
    },
}
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

    symbol_output = run([
        "arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)
    ])
    symbols: dict[str, tuple[int, int]] = {}
    for line in symbol_output.splitlines():
        match = SYMBOL_LINE.match(line)
        if not match:
            continue
        address_text, size_text, name = match.groups()
        symbols[name] = (int(address_text, 16), int(size_text, 10))

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
        print("Synchronization/lifecycle references: " + (", ".join(calls) if calls else "none detected"))
        required_calls = REQUIRED_LOCK_CALLS.get(name, set())
        missing_calls = sorted(required_calls.difference(calls))
        if missing_calls:
            print(
                f"ERROR: {name} is missing expected synchronization calls: "
                + ", ".join(missing_calls),
                file=sys.stderr,
            )
            return 1
        print(disassembly.rstrip())

    print("\nNo runtime or device behavior is inferred by this disassembly report.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
