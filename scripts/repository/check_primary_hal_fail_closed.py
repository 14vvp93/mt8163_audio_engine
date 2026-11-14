#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "primary-hal/src/primary_hal.cpp"
BUILD = ROOT / "primary-hal/Android.bp"


def extract_function(source: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\s*\(", source)
    if match is None:
        raise ValueError(f"missing function: {name}")

    opening = source.find("{", match.end())
    if opening < 0:
        raise ValueError(f"missing function body: {name}")

    depth = 0
    for index in range(opening, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]

    raise ValueError(f"unterminated function body: {name}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def main() -> int:
    try:
        source = SOURCE.read_text(encoding="utf-8")
        build = BUILD.read_text(encoding="utf-8")

        for function_name in ("open_output_stream", "open_input_stream"):
            body = extract_function(source, function_name)
            require(
                re.search(r"\*stream\s*=\s*nullptr\s*;", body) is not None,
                f"{function_name} must clear the output stream pointer",
            )
            require(
                re.search(r"return\s+-ENODEV\s*;", body) is not None,
                f"{function_name} must fail closed with -ENODEV",
            )

        require(
            re.search(r"\binstallable\s*:\s*false\s*,", build) is not None,
            "Primary HAL bootstrap must remain non-installable",
        )

        print("Primary HAL safety gate: PASS (stream opens fail closed; artifact non-installable)")
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Primary HAL safety gate: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
