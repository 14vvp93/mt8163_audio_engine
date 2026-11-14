#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "docs/ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md"
ROADMAP = ROOT / "docs/PROJECT_ROADMAP.md"
HAL_README = ROOT / "primary-hal/README.md"
HAL_BUILD = ROOT / "primary-hal/Android.bp"
HAL_SOURCE = ROOT / "primary-hal/src/primary_hal.cpp"
EVIDENCE_MATRIX = ROOT / "docs/P2_PRIVATE_HAL_CALLBACK_EVIDENCE_MATRIX.md"

CALLBACKS = (
    "xWayPlay_Start", "xWayPlay_Stop", "xWayPlay_Write",
    "xWayPlay_GetFreeBufferCount", "xWayRec_Start", "xWayRec_Stop",
    "xWayRec_Read", "ReadRefFromRing", "GetVoiceUnlockULTime",
    "SetVoiceUnlockSRC", "startVoiceUnlockDL", "stopVoiceUnlockDL",
    "getVoiceUnlockDLInstance", "setupParametersCallback",
    "setAudioParameterChangedCallback", "clearAudioParameterChangedCallback",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def callback_body(source: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\s*\(", source)
    require(match is not None, f"missing callback: {name}")
    opening = source.find("{", match.end())
    require(opening >= 0, f"missing callback body: {name}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise ValueError(f"unterminated callback body: {name}")


def main() -> int:
    try:
        baseline = BASELINE.read_text(encoding="utf-8")
        roadmap = ROADMAP.read_text(encoding="utf-8")
        readme = HAL_README.read_text(encoding="utf-8")
        build = HAL_BUILD.read_text(encoding="utf-8")
        source = HAL_SOURCE.read_text(encoding="utf-8")
        matrix = EVIDENCE_MATRIX.read_text(encoding="utf-8")

        required_offsets = set(range(156, 220, 4))
        table_rows = re.findall(
            r"^\|\s*(156|160|164|168|172|176|180|184|188|192|196|200|204|208|212|216)\s*\|",
            baseline,
            flags=re.MULTILINE,
        )
        present_offsets = {int(value) for value in table_rows}
        missing = sorted(required_offsets - present_offsets)
        require(not missing, f"private callback offset inventory incomplete: {missing}")

        missing_callbacks = [name for name in CALLBACKS if f"`{name}`" not in matrix]
        require(not missing_callbacks, f"evidence matrix callback inventory incomplete: {missing_callbacks}")
        require("Status: **OPEN" in matrix, "P2 evidence matrix must remain explicitly open")

        for gate in (
            "external xWay caller",
            "callback admission",
            "worker-exit observation",
            "linker namespace configuration",
            "matching ALPS/MediaTek vendor headers",
        ):
            require(gate.lower() in matrix.lower(), f"missing unresolved P2 evidence gate: {gate}")

        require("pthread_detach" in baseline and "pthread_join" in baseline,
                "voice-unlock detached-worker evidence must retain detach/join distinction")
        require("stopInput()" in baseline and "freeInstance()" in baseline,
                "voice-unlock stop/free separation must remain documented")
        require("external callers" in baseline.lower(), "baseline must retain external-caller evidence gap")
        require("callback-in-flight" in baseline.lower(), "baseline must retain callback-drain evidence gap")
        require("linker namespace" in baseline.lower(), "baseline must retain linker-namespace evidence gap")

        require(
            re.search(r"\binstallable\s*:\s*false\s*,", build) is not None,
            "Primary HAL bootstrap must remain non-installable",
        )
        require("installable: false" in readme, "Primary HAL README must state non-installable status")

        for callback in ("open_output_stream", "open_input_stream"):
            body = callback_body(source, callback)
            require(re.search(r"\*stream\s*=\s*nullptr\s*;", body) is not None,
                    f"{callback} must clear its output stream pointer")
            require(re.search(r"return\s+-ENODEV\s*;", body) is not None,
                    f"{callback} must fail closed with -ENODEV")

        p2_section = roadmap.split("### P2 —", 1)
        require(len(p2_section) == 2, "P2 section missing from authoritative roadmap")
        p2_text = p2_section[1].split("### P3 —", 1)[0]
        require("[ ]" in p2_text, "P2 must remain open while required evidence gates are unresolved")

        print(
            "P2 contract gate: PASS "
            "(16 slots inventoried; evidence matrix/lifetime gaps retained; "
            "HAL opens fail closed; module non-installable; P2 remains open)"
        )
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print(f"P2 contract gate: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
