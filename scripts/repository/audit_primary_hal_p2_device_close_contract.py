#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4"
VTABLE_NAME = "vtable for android::AudioALSAHardware"
VTABLE_ADDRESS = 0x00190A84
VTABLE_SIZE = 236
COMPLETE_DTOR = ("0x000c56e4", "android::AudioALSAHardware::~AudioALSAHardware()")
DELETING_DTOR = ("0x000c5874", "android::AudioALSAHardware::~AudioALSAHardware()")
HMI_CLOSE_START = 0x0009BA48
HMI_CLOSE_END = 0x0009BAE4
CREATE_THUNK_START = 0x000D127C
CREATE_THUNK_END = 0x000D1280


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


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "Usage: audit_primary_hal_p2_device_close_contract.py "
            "<audio.primary.mt8163.so> <p2-factory-relocations.txt>",
            file=sys.stderr,
        )
        return 2

    binary = Path(sys.argv[1])
    relocation_report = Path(sys.argv[2])
    require(binary.is_file(), f"factory HAL not found: {binary}")
    require(relocation_report.is_file(), f"vtable relocation report not found: {relocation_report}")

    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    require(digest == EXPECTED_SHA256, f"unexpected factory HAL SHA-256: {digest}")

    symbols = run(["arm-linux-gnueabi-readelf", "-Ws", "-C", str(binary)])
    escaped_name = re.escape(VTABLE_NAME)
    matches = re.findall(
        rf"^\s*\d+:\s+([0-9a-fA-F]+)\s+([0-9]+)\s+OBJECT\s+\S+\s+\S+\s+\S+\s+{escaped_name}\s*$",
        symbols,
        re.MULTILINE,
    )
    require(len(matches) == 1, f"expected one {VTABLE_NAME!r} symbol, found {len(matches)}")
    vtable_address, vtable_size = (int(value, 16 if index == 0 else 10) for index, value in enumerate(matches[0]))
    require(vtable_address == VTABLE_ADDRESS, f"unexpected vtable address: 0x{vtable_address:08x}")
    require(vtable_size == VTABLE_SIZE, f"unexpected vtable size: {vtable_size}")

    relocations = relocation_report.read_text(encoding="utf-8", errors="replace")
    require(
        "Vtable symbol: _ZTVN7android17AudioALSAHardwareE" in relocations,
        "relocation report does not identify the expected AudioALSAHardware vtable",
    )
    require(
        "Vtable address: 0x00190a84" in relocations,
        "relocation report vtable address does not match the factory symbol table",
    )
    require(
        "Vtable size: 236 bytes" in relocations,
        "relocation report vtable size does not match the factory symbol table",
    )

    def require_relocation(offset: str, target: str, name: str) -> None:
        pattern = rf"^{re.escape(offset)}\s+0x[0-9a-fA-F]+\s+{re.escape(target)}\s+{re.escape(name)}\s*$"
        require(
            re.search(pattern, relocations, re.MULTILINE) is not None,
            f"vtable relocation {offset} does not resolve to {name} at {target}",
        )

    require_relocation("+0x008", COMPLETE_DTOR[0], COMPLETE_DTOR[1])
    require_relocation("+0x00c", DELETING_DTOR[0], DELETING_DTOR[1])

    close = run(
        [
            "arm-linux-gnueabi-objdump",
            "-dr",
            "-C",
            f"--start-address=0x{HMI_CLOSE_START:x}",
            f"--stop-address=0x{HMI_CLOSE_END:x}",
            str(binary),
        ]
    )
    require(
        re.search(r"ldr\s+r0,\s*\[r4,\s*#220\]", close) is not None
        and re.search(r"ldr\s+r1,\s*\[r0\]", close) is not None
        and re.search(r"ldr\s+r1,\s*\[r1,\s*#4\]", close) is not None
        and re.search(r"blx\s+r1", close) is not None,
        "HMI.close no longer matches the audited indirect vtable+4 dispatch",
    )

    create = run(
        [
            "arm-linux-gnueabi-objdump",
            "-dr",
            "-C",
            f"--start-address=0x{CREATE_THUNK_START:x}",
            f"--stop-address=0x{CREATE_THUNK_END:x}",
            str(binary),
        ]
    )
    require(
        "android::AudioALSAHardware::GetInstance()@plt" in create,
        "AudioMTKHardwareInterface::create() no longer branches to GetInstance@plt",
    )

    get_instance = run(
        [
            "arm-linux-gnueabi-objdump",
            "-dr",
            "-C",
            "--start-address=0x000d0ee0",
            "--stop-address=0x000d1244",
            str(binary),
        ]
    )
    destructor = run(
        [
            "arm-linux-gnueabi-objdump",
            "-dr",
            "-C",
            "--start-address=0x000c56e4",
            "--stop-address=0x000c57f4",
            str(binary),
        ]
    )
    require(
        re.search(r"^\s*d1200:\s+000c1bcc\b", get_instance, re.MULTILINE) is not None
        and re.search(r"^\s*d120c:\s+000c1b8c\b", get_instance, re.MULTILINE) is not None
        and re.search(r"^\s*c5858:\s+000cd414\b", destructor, re.MULTILINE) is not None,
        "singleton pointer PC-relative literal constants changed",
    )
    singleton_read_cell = 0x000D0FAC + 0x000C1BCC
    singleton_write_cell = 0x000D0FEC + 0x000C1B8C
    destructor_clear_cell = 0x000C5764 + 0x000CD414
    require(
        singleton_read_cell == singleton_write_cell == destructor_clear_cell == 0x0019A178,
        "GetInstance/destructor singleton pointer cell correlation failed",
    )
    require(
        re.search(r"^\s*c5760:\s+e5806000\s+str\s+r6,\s*\[r0\]", destructor, re.MULTILINE)
        is not None,
        "AudioALSAHardware destructor no longer clears the singleton pointer cell",
    )

    print(f"Factory HAL SHA-256: {digest}")
    print(f"AudioALSAHardware vtable: 0x{vtable_address:08x}, {vtable_size} bytes")
    print("Itanium vtable address point: vtable + 0x008 = 0x00190a8c")
    print("HMI.close dispatch: object+0xdc -> vptr -> vptr+0x04")
    print("Resolved vptr+0x04 slot: vtable+0x00c -> deleting destructor 0x000c5874")
    print("Complete destructor slot: vtable+0x008 -> 0x000c56e4")
    print("AudioMTKHardwareInterface::create(): thunk -> AudioALSAHardware::GetInstance@plt")
    print("GetInstance/destructor singleton pointer cell: 0x0019a178 (read/write/reset correlated)")
    print("CONTRACT VERIFIED: HMI.close invokes the AudioALSAHardware deleting destructor.")
    print("Static evidence only; this does not prove safe concurrent singleton access or callback drain.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
