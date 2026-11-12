#!/usr/bin/env python3
"""Check local Markdown links without requiring network access."""
from __future__ import annotations
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
LINK_RE = re.compile(r"!?(?:\[[^\]]*\])\(([^)]+)\)")
FENCE_RE = re.compile(r"^\s*(`{3,}|~{3,}).*$", re.MULTILINE)

def strip_fenced_code(text: str) -> str:
    lines = text.splitlines()
    output = []
    fence_char = None
    fence_len = 0
    for line in lines:
        match = re.match(r"^\s*(`{3,}|~{3,})", line)
        if match:
            token = match.group(1)
            if fence_char is None:
                fence_char, fence_len = token[0], len(token)
            elif token[0] == fence_char and len(token) >= fence_len:
                fence_char, fence_len = None, 0
            output.append("")
        elif fence_char is None:
            output.append(line)
        else:
            output.append("")
    return "\n".join(output)

def target_token(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1:value.index(">")]
    return value.split()[0] if value else ""

def resolve_target(source: Path, target: str) -> Path | None:
    if not target or target.startswith("#") or re.match(r"^(https?:|mailto:|data:|tel:|javascript:)", target, re.I):
        return None
    path_part = unquote(target.split("#", 1)[0].split("?", 1)[0])
    if not path_part:
        return None
    if path_part.startswith("/"):
        candidate = ROOT / path_part.lstrip("/")
    else:
        candidate = source.parent / path_part
    candidate = candidate.resolve()
    try:
        candidate.relative_to(ROOT)
    except ValueError:
        return candidate
    if candidate.is_dir():
        for index in ("README.md", "index.md"):
            if (candidate / index).is_file():
                return candidate / index
    return candidate

def main() -> int:
    failures = []
    checked = 0
    for source in sorted(ROOT.rglob("*.md")):
        if any(part in {".git", ".gradle", "build"} for part in source.parts):
            continue
        try:
            text = strip_fenced_code(source.read_text(encoding="utf-8"))
        except UnicodeDecodeError as exc:
            failures.append((source, "<file>", f"not UTF-8: {exc}"))
            continue
        for match in LINK_RE.finditer(text):
            target = target_token(match.group(1))
            resolved = resolve_target(source, target)
            if resolved is None:
                continue
            checked += 1
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                failures.append((source, target, "path escapes repository root"))
                continue
            if not resolved.is_file():
                failures.append((source, target, f"missing: {resolved.relative_to(ROOT)}"))
    print(f"Checked {checked} local Markdown links across {len(list(ROOT.rglob('*.md')))} Markdown files.")
    if failures:
        for source, target, reason in failures:
            print(f"ERROR: {source.relative_to(ROOT)} -> {target}: {reason}")
        return 1
    print("All local Markdown links resolve.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
