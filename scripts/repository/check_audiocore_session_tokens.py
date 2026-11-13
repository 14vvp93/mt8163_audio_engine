#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PUBLIC_HEADERS = {
    "session_registry": ROOT / "audio-core/include/mt8163_audio_core/session_registry.hpp",
    "audio_core": ROOT / "audio-core/include/mt8163_audio_core/audio_core.hpp",
    "pcm_stream_broker": ROOT / "audio-core/include/mt8163_audio_core/pcm_stream_broker.hpp",
    "primary_hal_adapter": ROOT / "audio-core/include/mt8163_audio_core/primary_hal_stream_adapter.hpp",
}

TEST_FILES = {
    "session_registry": ROOT / "audio-core/tests/session_registry_test.cpp",
    "audio_core": ROOT / "audio-core/tests/audio_core_test.cpp",
    "pcm_stream_broker": ROOT / "audio-core/tests/pcm_stream_broker_test.cpp",
    "primary_hal_adapter": ROOT / "audio-core/tests/primary_hal_stream_adapter_test.cpp",
}

IDENTITY_METHOD = re.compile(
    r"\b(?:transition|close|find|configure|start|write|read|"
    r"write_transfer|read_transfer|stop|get_record|"
    r"transition_session|close_session)\s*\(\s*"
    r"(?:const\s+)?(?:std::)?uint32_t\s+"
    r"(?:handle|session_handle)\b",
    re.MULTILINE,
)


def fail(message: str) -> None:
    print(f"::error::{message}")
    raise SystemExit(1)


def read_required(path: Path) -> str:
    if not path.is_file():
        fail(f"Required P1 file is missing: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    token_header = ROOT / "audio-core/include/mt8163_audio_core/session_token.hpp"
    if not token_header.exists():
        token_header = PUBLIC_HEADERS["session_registry"]
    token_source = read_required(token_header)

    if not re.search(r"struct\s+SessionToken\b", token_source):
        fail("SessionToken definition is missing from the public AudioCore headers.")
    if not re.search(r"(?:std::)?uint32_t\s+handle\b", token_source):
        fail("SessionToken must retain a uint32_t correlation handle.")
    if not re.search(r"(?:std::)?uint64_t\s+generation\b", token_source):
        fail("SessionToken must contain a uint64_t generation.")
    if not re.search(r"operator\s*==|operator\s*!=|equals\s*\(", token_source):
        fail("SessionToken must provide explicit value equality semantics.")

    for component, path in PUBLIC_HEADERS.items():
        source = read_required(path)
        if "SessionToken" not in source:
            fail(f"{component}: SessionToken is not propagated through its public API.")
        match = IDENTITY_METHOD.search(source)
        if match:
            fail(
                f"{component}: raw numeric session identity remains in a public "
                f"lifecycle/stream API: {match.group(0)!r}"
            )

        if component in {"session_registry", "audio_core", "primary_hal_adapter"}:
            open_signature = re.compile(
                r"\b(?:open_session|open)\s*\\(\s*std::uint32_t\s+handle"
                r"[\s\S]{0,240}SessionToken\s*\\*\s*out_token"
            )
            if not open_signature.search(source):
                fail(f"{component}: open must return the new SessionToken through out_token.")

        if component == "pcm_stream_broker" and not re.search(
            r"\bopen\s*\\(\s*SessionToken\s+session", source
        ):
            fail("pcm_stream_broker: open must accept SessionToken, never a raw handle.")

    registry = read_required(PUBLIC_HEADERS["session_registry"])
    if not re.search(r"struct\s+SessionRecord\b[\s\S]*?SessionToken", registry):
        fail("SessionRecord must publish the complete SessionToken.")

    broker = read_required(PUBLIC_HEADERS["pcm_stream_broker"])
    if not re.search(r"struct\s+PcmStreamRecord\b[\s\S]*?SessionToken", broker):
        fail("PcmStreamRecord must retain the complete SessionToken.")

    for component, path in TEST_FILES.items():
        source = read_required(path)
        if "SessionToken" not in source:
            fail(f"{component} tests do not exercise the SessionToken API.")
        if not re.search(r"stale|reopen|same.{0,24}handle|handle.{0,24}reuse", source, re.I):
            fail(f"{component} tests lack stale-token/same-handle-reuse coverage.")

    registry_tests = read_required(TEST_FILES["session_registry"])
    if not re.search(r"exhaust|numeric_limits|UINT64_MAX|uint64_t\s*\{?\s*[-]1", registry_tests, re.I):
        fail("SessionRegistry tests must cover generation exhaustion and fail-closed behavior.")

    print("P1 API contract checks passed: generation-bearing tokens are propagated,")
    print("raw-handle lifecycle APIs are absent, and component regression tests are present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
