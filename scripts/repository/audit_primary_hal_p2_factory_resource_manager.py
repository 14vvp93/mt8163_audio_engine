#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.audit_android_packed_relocations import (
    R_ARM_ABS32,
    R_ARM_RELATIVE,
    decode_aps2,
    parse_elf32,
)

R_ARM_GLOB_DAT = 21

EXPECTED_SHA256 = "112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4"
RESOURCE_VTABLE = "_ZTVN7android22AudioALSAStreamManagerE"
CONSTRUCTOR_START = 0x000C5274
CONSTRUCTOR_END = 0x000C565C
DESTRUCTOR_START = 0x000C56E4
DESTRUCTOR_END = 0x000C57F8
GET_INSTANCE_START = 0x000D0EE0
GET_INSTANCE_END = 0x000D1244
ATEXIT_FUNCTION = 0x00073524
MANAGER_SINGLETON_POINTER_CELL = 0x001929A0
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


def read_elf_word(binary: Path, address: int) -> int:
    output = run(
        [
            "arm-linux-gnueabi-objdump",
            "-s",
            str(binary),
        ]
    )
    for line in output.splitlines():
        match = re.match(
            r"^\s*([0-9a-fA-F]{4,8})\s+((?:[0-9a-fA-F]{8}\s*){1,4})",
            line,
        )
        if match is None:
            continue
        row_address = int(match.group(1), 16)
        raw = "".join(match.group(2).split())
        row_bytes = bytes.fromhex(raw)
        offset = address - row_address
        if 0 <= offset and offset + 4 <= len(row_bytes):
            return int.from_bytes(row_bytes[offset : offset + 4], byteorder="little")
    raise RuntimeError(f"could not read ELF word at 0x{address:08x}")


def resolve_relocated_pointer(binary: Path, address: int) -> tuple[int, int, str]:
    data = binary.read_bytes()
    sections, symbols = parse_elf32(data)
    relocation_section = next((section for section in sections if section.name == ".rel.dyn"), None)
    require(relocation_section is not None, "factory ELF has no .rel.dyn section")
    relocation_data = data[
        relocation_section.offset : relocation_section.offset + relocation_section.size
    ]
    relocations, _ = decode_aps2(relocation_data)
    matches = [relocation for relocation in relocations if relocation.offset == address]
    require(
        len(matches) == 1,
        f"expected one dynamic relocation for pointer cell 0x{address:08x}, found {len(matches)}",
    )

    relocation = matches[0]
    relocation_type = relocation.info & 0xFF
    symbol_index = relocation.info >> 8
    implicit_addend = read_elf_word(binary, address)

    if relocation_type == R_ARM_RELATIVE:
        require(symbol_index == 0, "R_ARM_RELATIVE pointer relocation has a non-zero symbol index")
        target = implicit_addend
        description = f"R_ARM_RELATIVE(addend=0x{implicit_addend:08x})"
    elif relocation_type == R_ARM_ABS32:
        require(symbol_index < len(symbols), f"invalid relocation symbol index {symbol_index}")
        target_symbol = symbols[symbol_index]
        target = (target_symbol.value + implicit_addend) & 0xFFFFFFFF
        description = (
            f"R_ARM_ABS32(symbol={target_symbol.name or '<anonymous>'}, "
            f"symbol_value=0x{target_symbol.value:08x}, addend=0x{implicit_addend:08x})"
        )
    elif relocation_type == R_ARM_GLOB_DAT:
        require(symbol_index < len(symbols), f"invalid relocation symbol index {symbol_index}")
        target_symbol = symbols[symbol_index]
        require(target_symbol.value != 0, f"R_ARM_GLOB_DAT symbol {target_symbol.name!r} is undefined")
        target = target_symbol.value & 0xFFFFFFFF
        description = (
            f"R_ARM_GLOB_DAT(symbol={target_symbol.name or '<anonymous>'}, "
            f"symbol_value=0x{target_symbol.value:08x})"
        )
    else:
        raise RuntimeError(
            f"unsupported pointer relocation type {relocation_type} at 0x{address:08x}"
        )

    return implicit_addend, target, description


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
    require(
        "Static evidence only: HMI.close dispatches through the returned object's second vtable slot."
        in close_text,
        "factory close report does not confirm the HMI.close deleting-destructor contract",
    )

    manager_get_instance = next(
        (
            (address, size, name)
            for address, size, name in functions
            if "AudioALSAStreamManager::getInstance()" in name
        ),
        None,
    )
    require(manager_get_instance is not None, "AudioALSAStreamManager::getInstance() symbol missing")
    nondeleting_slot = re.search(
        r"^\+0x008\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+(.+)$",
        relocations,
        re.MULTILINE,
    )
    require(nondeleting_slot is not None, "resource-manager non-deleting-destructor slot +0x008 missing")
    nondeleting_destructor_address = int(nondeleting_slot.group(1), 16)
    require(
        "AudioALSAStreamManager::~AudioALSAStreamManager()" in nondeleting_slot.group(2),
        "unexpected resource-manager vtable slot +0x008",
    )
    manager_destructor = next(
        (
            (address, size, name)
            for address, size, name in functions
            if address == deleting_destructor_address
        ),
        None,
    )
    manager_nondeleting_destructor = next(
        (
            (address, size, name)
            for address, size, name in functions
            if address == nondeleting_destructor_address
        ),
        None,
    )
    require(manager_destructor is not None, "resource-manager deleting destructor symbol missing")
    require(manager_nondeleting_destructor is not None, "resource-manager non-deleting destructor symbol missing")
    manager_get_instance_body = disassemble(
        binary,
        manager_get_instance[0],
        manager_get_instance[0] + manager_get_instance[1],
    )
    hardware_get_instance_body = disassemble(
        binary,
        GET_INSTANCE_START,
        GET_INSTANCE_END,
    )
    manager_lock_failure_falls_through = (
        re.search(
            r"76674:.*bl\s+47da4 <alock_lock_ms@plt>\n"
            r"\s*76678:.*cmp\s+r0,\s*#0\n"
            r"\s*7667c:.*beq\s+766d8",
            manager_get_instance_body,
        )
        is not None
        and re.search(r"766d8:.*ldr\s+r0,.*\n\s*766dc:.*ldr\s+r0,\s*\[pc,\s*r0\]", manager_get_instance_body)
        is not None
    )
    require(
        manager_lock_failure_falls_through,
        "AudioALSAStreamManager::getInstance lock failure no longer falls through to singleton access",
    )
    hardware_lock_failure_falls_through = (
        re.search(
            r"d0f3c:.*bl\s+47da4 <alock_lock_ms@plt>\n"
            r"\s*d0f40:.*cmp\s+r0,\s*#0\n"
            r"\s*d0f44:.*beq\s+d0fa0",
            hardware_get_instance_body,
        )
        is not None
        and re.search(r"d0fa0:.*ldr\s+r0,.*\n\s*d0fa4:.*ldr\s+r0,\s*\[pc,\s*r0\]", hardware_get_instance_body)
        is not None
    )
    require(
        hardware_lock_failure_falls_through,
        "AudioALSAHardware::GetInstance lock failure no longer falls through to singleton access",
    )

    callback_functions = {
        "registration": "AudioALSAHardware::setAudioParameterChangedCallback(",
        "clear": "AudioALSAHardware::clearAudioParameterChangedCallback(void*)",
        "dispatch": "AudioALSAHardware::onAudioParameterChangedCallback(char const*)",
    }
    callback_bodies: dict[str, str] = {}
    for label, symbol_fragment in callback_functions.items():
        symbol = next(
            (
                (address, size, name)
                for address, size, name in functions
                if symbol_fragment in name
            ),
            None,
        )
        require(symbol is not None, f"callback {label} function symbol missing")
        callback_bodies[label] = disassemble(binary, symbol[0], symbol[0] + symbol[1])

    callback_lock_failure_patterns = {
        "registration": (
            r"cea38:.*bl\s+47da4 <alock_lock_ms@plt>\n"
            r"\s*cea3c:.*cmp\s+r0,\s*#0\n"
            r"\s*cea40:.*beq\s+cea9c",
            r"cea9c:.*ldr\s+r0,.*\n\s*ceaa0:.*add\s+r4,\s*r7,\s*#944",
        ),
        "clear": (
            r"ced08:.*bl\s+47da4 <alock_lock_ms@plt>\n"
            r"\s*ced0c:.*cmp\s+r0,\s*#0\n"
            r"\s*ced10:.*beq\s+ced6c",
            r"ced6c:.*ldr\s+fp,.*\n\s*ced70:.*add\s+r7,\s+r6,\s+#944",
        ),
        "dispatch": (
            r"cef60:.*bl\s+47da4 <alock_lock_ms@plt>\n"
            r"\s*cef64:.*cmp\s+r0,\s*#0\n"
            r"\s*cef68:.*beq\s+cefc4",
            r"cefc4:.*ldr\s+r0,.*\n\s*cefc8:.*cmp\s+r0,\s*#0",
        ),
    }
    for label, (lock_pattern, fallthrough_pattern) in callback_lock_failure_patterns.items():
        body = callback_bodies[label]
        require(
            re.search(lock_pattern, body) is not None
            and re.search(fallthrough_pattern, body) is not None,
            f"callback {label} lock failure no longer falls through to registry access",
        )
    dispatch_body = callback_bodies["dispatch"]
    callback_invoke = re.search(r"cf01c:.*mov\s+r0,\s*r9.*\n\s*cf020:.*blx\s+r2", dispatch_body)
    dispatch_unlock = re.search(
        r"cf044:.*mov\s+r0,\s*r8[\s\S]*?cf050:.*bl\s+47e04 <alock_unlock@plt>",
        dispatch_body,
    )
    require(
        callback_invoke is not None and dispatch_unlock is not None
        and callback_invoke.start() < dispatch_unlock.start(),
        "callback dispatch no longer invokes the callback before releasing the registry lock",
    )

    manager_destructor_body = disassemble(
        binary,
        manager_destructor[0],
        manager_destructor[0] + manager_destructor[1],
    )
    manager_nondeleting_destructor_body = disassemble(
        binary,
        manager_nondeleting_destructor[0],
        manager_nondeleting_destructor[0] + manager_nondeleting_destructor[1],
    )
    atexit_target_body = disassemble(binary, ATEXIT_FUNCTION, ATEXIT_FUNCTION + 0x80)
    require(
        "alock_free@plt" in atexit_target_body
        and re.search(r"str\s+r1,\s*\[r0\],\s*#4", atexit_target_body) is not None,
        "__cxa_atexit target no longer matches the static alock cleanup callback shape",
    )
    manager_singleton_reset = (
        re.search(r"77bac:.*ldr\s+r0,\s*\[pc,\s*#564\].*@ 77de8", manager_nondeleting_destructor_body) is not None
        and re.search(r"77bb0:.*mov\s+r1,\s*#0", manager_nondeleting_destructor_body) is not None
        and re.search(r"77bb4:.*ldr\s+r0,\s*\[pc,\s*r0\]", manager_nondeleting_destructor_body) is not None
        and re.search(r"77bb8:.*str\s+r1,\s*\[r0\]", manager_nondeleting_destructor_body) is not None
        and re.search(r"77de8:\s+0011ade4", manager_nondeleting_destructor_body) is not None
    )
    require(
        manager_singleton_reset,
        "AudioALSAStreamManager destructor no longer resets singleton pointer cell 0x001929A0",
    )

    print(f"Factory HAL SHA-256: {digest}")
    print(f"AudioALSAHardware constructor: 0x{CONSTRUCTOR_START:08x}..0x{CONSTRUCTOR_END:08x}")
    print("Constructor stores AudioALSAStreamManager::getInstance() result at AudioALSAHardware+0x04")
    print(f"AudioALSAHardware destructor: 0x{DESTRUCTOR_START:08x}..0x{DESTRUCTOR_END:08x}")
    print("Destructor calls the second virtual slot of the object at AudioALSAHardware+0x04")
    print(f"Resource-manager vtable: {RESOURCE_VTABLE}")
    print(
        f"AudioALSAStreamManager::getInstance(): "
        f"0x{manager_get_instance[0]:08x}, size={manager_get_instance[1]}"
    )
    print(manager_get_instance_body.rstrip())
    print(
        f"AudioALSAStreamManager deleting destructor: "
        f"0x{manager_destructor[0]:08x}, size={manager_destructor[1]}"
    )
    print(manager_destructor_body.rstrip())
    print(
        f"AudioALSAStreamManager non-deleting destructor: "
        f"0x{manager_nondeleting_destructor[0]:08x}, size={manager_nondeleting_destructor[1]}"
    )
    print(manager_nondeleting_destructor_body.rstrip())
    print(f"Resource-manager deleting destructor: 0x{deleting_destructor_address:08x} {deleting_destructor_name}")
    print(
        f"AudioALSAStreamManager singleton pointer cell derived from getInstance() PC-relative load/store: "
        f"0x{MANAGER_SINGLETON_POINTER_CELL:08x}"
    )
    print("CONTRACT VERIFIED: AudioALSAStreamManager::getInstance lock failure falls through to singleton pointer read/create.")
    print("CONTRACT VERIFIED: AudioALSAHardware::GetInstance lock failure falls through to singleton pointer read/create.")
    print("CONTRACT VERIFIED: callback registration, clear, and dispatch all continue into registry access after lock failure.")
    print("CONTRACT VERIFIED: parameter-change callbacks are invoked before the registry lock is released.")
    print(
        "CONTRACT VERIFIED: AudioALSAStreamManager destructor clears singleton pointer cell "
        f"0x{MANAGER_SINGLETON_POINTER_CELL:08x} to null."
    )
    print("__cxa_atexit callback target is an interior cleanup thunk in AudioALSAHardwareResourceManager::getInstance(); it rewrites the supplied lock object's vptr and calls alock_free.")
    print(f"__cxa_atexit callback target disassembly at 0x{ATEXIT_FUNCTION:08x}:")
    print(atexit_target_body.rstrip())
    print("STATIC CONTRACT: AudioALSAHardware destruction dispatches the resource-manager deleting-destructor slot.")

    event_thread_functions = {
        "manager_unregister_by_type": "AudioEventThreadManager::unregisterCallback(android::AudioEventType)",
        "manager_unregister_specific": "AudioEventThreadManager::unregisterCallback(android::AudioEventType, void (*)(int, void*), void*)",
        "manager_register": "AudioEventThreadManager::registerCallback(android::AudioEventType, void (*)(int, void*), void*)",
        "manager_get_thread": "AudioEventThreadManager::getAudioEventThread(android::AudioEventType)",
        "event_thread_destructor": "AudioEventThread::~AudioEventThread()",
        "event_thread_loop": "AudioEventThread::eventThread(void*)",
        "event_thread_signal": "AudioEventThread::signal(void*)",
        "event_thread_add_callback": "AudioEventThread::addCallback(void (*)(int, void*), void*)",
        "event_manager_destructor": "AudioEventThreadManager::~AudioEventThreadManager()",
        "event_manager_notify": "AudioEventThreadManager::notifyCallback(android::AudioEventType, void*)",
        "stream_manager_constructor": "AudioALSAStreamManager::AudioALSAStreamManager()",
    }
    event_thread_bodies: dict[str, str] = {}
    for label, fragment in event_thread_functions.items():
        matches = [
            (address, size, name)
            for address, size, name in functions
            if fragment in name
        ]
        require(matches, f"AudioEventThread lifecycle symbol missing: {label} ({fragment})")
        address, size, name = matches[0]
        body = disassemble(binary, address, address + size)
        event_thread_bodies[label] = body
        print(f"\n===== EVENT THREAD LIFECYCLE: {label} @ 0x{address:08x}, size={size}: {name} =====")
        print(body.rstrip())

    unregister_by_type = event_thread_bodies["manager_unregister_by_type"]
    unregister_stop = re.search(
        r"1361a8:.*mov\s+r1,\s*#0\n"
        r"\s*1361ac:.*strb\s+r1,\s*\[r0,\s*#28\]\n"
        r"\s*1361b0:.*mov\s+r1,\s*r5\n"
        r"\s*1361b4:.*bl\s+[0-9a-f]+ <android::AudioEventThread::signal\(void\*\)@plt>",
        unregister_by_type,
    )
    unregister_join = re.search(
        r"1361e0:.*ldr\s+r0,\s*\[r0,\s*#4\]\n"
        r"\s*1361e4:.*bl\s+[0-9a-f]+ <pthread_join@plt>",
        unregister_by_type,
    )
    unregister_remove = re.search(
        r"136208:.*add\s+r0,\s*r6,.*\n"
        r"\s*13620c:.*str\s+r0,\s*\[r5,\s*#8\]",
        unregister_by_type,
    )
    require(
        unregister_stop is not None
        and unregister_join is not None
        and unregister_remove is not None
        and unregister_stop.start() < unregister_join.start() < unregister_remove.start(),
        "AudioEventThreadManager::unregisterCallback(type) no longer stop/signals, joins, then removes worker",
    )

    stream_constructor = event_thread_bodies["stream_manager_constructor"]
    constructor_registration = [
        re.search(rf"^\s*{address}:.*{pattern}", stream_constructor, re.MULTILINE)
        for address, pattern in (
            ("7721c", r"\bbl\s+[0-9a-f]+\s+<android::AudioEventThreadManager::getInstance\(\)@plt>"),
            ("77224", r"\bmov\s+r3,\s*r4\b"),
            ("77228", r"\bldr\s+r2,"),
            ("7722c", r"\bmov\s+r1,\s*#0\b"),
            ("77230", r"\bbl\s+[0-9a-f]+\s+<android::AudioEventThreadManager::registerCallback\("),
        )
    ]
    require(
        all(match is not None for match in constructor_registration),
        "AudioALSAStreamManager constructor no longer registers event type 0 with this as callback cookie",
    )

    callback_literal = re.search(
        r"^\s*77220:.*ldr\s+r1,\s*\[pc,\s*#732\].*@\s*77504",
        stream_constructor,
        re.MULTILINE,
    )
    callback_cell_load = re.search(
        r"^\s*77228:.*ldr\s+r2,\s*\[pc,\s*r1\]",
        stream_constructor,
        re.MULTILINE,
    )
    require(
        callback_literal is not None and callback_cell_load is not None,
        "StreamManager event callback pointer load sequence changed",
    )
    callback_cell = 0x00077230 + read_elf_word(binary, 0x00077504)
    require(
        callback_cell == 0x001929C8,
        f"unexpected StreamManager event callback pointer cell: 0x{callback_cell:08x}",
    )
    callback_target_raw, callback_target_relocated, callback_relocation = resolve_relocated_pointer(
        binary, callback_cell
    )
    callback_target = callback_target_relocated & ~1
    if callback_target == 0:
        raise RuntimeError(
            "StreamManager event callback pointer resolves to null after applying its ELF relocation: "
            f"cell=0x{callback_cell:08x}, raw_addend=0x{callback_target_raw:08x}, "
            f"relocation={callback_relocation}, relocated=0x{callback_target_relocated:08x}"
        )
    callback_symbols = [
        (address, size, name)
        for address, size, name in functions
        if address <= callback_target < address + max(size, 1)
    ]
    require(
        len(callback_symbols) == 1,
        f"StreamManager event callback target 0x{callback_target:08x} does not resolve to exactly one FUNC symbol",
    )
    callback_symbol = callback_symbols[0]
    callback_body = disassemble(
        binary,
        callback_symbol[0],
        callback_symbol[0] + callback_symbol[1],
    )
    print(
        "StreamManager AudioEventType(0) callback pointer: "
        f"cell=0x{callback_cell:08x}, raw_addend=0x{callback_target_raw:08x}, "
        f"relocation={callback_relocation}, relocated=0x{callback_target_relocated:08x}, "
        f"target=0x{callback_target:08x}, symbol={callback_symbol[2]}"
    )
    print("===== STREAM MANAGER EVENT CALLBACK TARGET =====")
    print(callback_body.rstrip())

    manager_register_body = event_thread_bodies["manager_register"]
    manager_get_thread_body = event_thread_bodies["manager_get_thread"]
    require(
        "AudioEventThread::getAudioEventThread(android::AudioEventType)@plt" not in manager_register_body
        and "AudioEventThread::addCallback(void (*)(int, void*), void*)@plt" not in manager_register_body,
        "AudioEventThreadManager::registerCallback unexpectedly changed from its pinned inline registration path",
    )
    require(
        re.search(r"135bdc:.*ldr\s+r7,\s*\[r0,\s*r3,\s*lsl\s+#2\]\n\s*135be0:.*ldr\s+r7,\s*\[r7,\s*#8\]", manager_register_body) is not None
        and re.search(r"135bf8:.*mov\s+r0,\s*#40\n\s*135bfc:.*bl\s+[0-9a-f]+ <operator new\(unsigned int\)@plt>", manager_register_body) is not None
        and "AudioEventThread::AudioEventThread(android::AudioEventType)@plt" in manager_register_body
        and re.search(r"135d10:.*str\s+r1,\s*\[r0\]", manager_register_body) is not None,
        "AudioEventThreadManager::registerCallback no longer performs inline worker lookup, construction, and callback insertion",
    )
    require(
        "AudioEventThread::addCallback(void (*)(int, void*), void*)@plt" not in manager_get_thread_body,
        "getAudioEventThread unexpectedly owns callback insertion; review changed registration path",
    )
    print(
        "CONTRACT VERIFIED: event type 0 callback target resolves from the pinned ELF data cell "
        f"to {callback_symbol[2]} at 0x{callback_target:08x}."
    )

    worker_loop = event_thread_bodies["event_thread_loop"]
    require(
        re.search(r"13517c:.*ldrb\s+r0,\s*\[r4,\s*#28\]", worker_loop) is not None
        and re.search(r"1351f8:.*ldr\s+r0,\s*\[r4,\s*#8\]\n\s*1351fc:.*blx\s+r2", worker_loop) is not None
        and re.search(r"1352d8:.*mov\s+r0,\s*#0\n\s*1352dc:.*bl\s+[0-9a-f]+ <pthread_exit@plt>", worker_loop) is not None,
        "AudioEventThread worker loop no longer checks stop flag, dispatches callbacks, and exits via pthread_exit",
    )

    worker_destructor = event_thread_bodies["event_thread_destructor"]
    destructor_stop = re.search(
        r"1353f0:.*mov\s+r0,\s*#0\n"
        r"\s*1353f4:.*strb\s+r0,\s*\[r4,\s*#28\]\n"
        r"\s*1353f8:.*mov\s+r0,\s*r4\n"
        r"\s*1353fc:.*mov\s+r1,\s*r4\n"
        r"\s*135400:.*bl\s+[0-9a-f]+ <android::AudioEventThread::signal\(void\*\)@plt>",
        worker_destructor,
    )
    destructor_join = re.search(
        r"135404:.*ldr\s+r0,\s*\[r4,\s*#4\]\n"
        r"\s*135408:.*mov\s+r1,\s*#0\n"
        r"\s*13540c:.*mov\s+r5,\s*#0\n"
        r"\s*135410:.*bl\s+[0-9a-f]+ <pthread_join@plt>",
        worker_destructor,
    )
    require(
        destructor_stop is not None
        and destructor_join is not None
        and destructor_stop.start() < destructor_join.start(),
        "AudioEventThread destructor no longer stops/signals and joins its worker before teardown",
    )
    print("CONTRACT VERIFIED: StreamManager registers AudioEventType(0) with its own instance cookie.")
    print("CONTRACT VERIFIED: unregisterCallback(type) clears the worker run flag, signals, pthread_join()s, then removes the worker entry.")
    print("CONTRACT VERIFIED: AudioEventThread destructor stops/signals and joins the worker before destroying its synchronization state.")

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
