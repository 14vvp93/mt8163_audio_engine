#!/usr/bin/env python3
"""Check local Markdown links using the Git tree, without checking out firmware binaries."""
from __future__ import annotations
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
LINK_RE = re.compile(r"!?(?:\[[^\]]*\])\(([^)]+)\)")

def tracked_paths() -> set[str]:
    result = subprocess.run(
        ["git", "ls-tree", "-r", "--name-only", "HEAD"],
        cwd=ROOT, check=True, text=True, capture_output=True,
    )
    return set(result.stdout.splitlines())

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

def normalize_target(source_path: str, target: str) -> str | None:
    if not target or target.startswith("#") or re.match(r"^(https?:|mailto:|data:|tel:|javascript:)", target, re.I):
        return None
    path_part = unquote(target.split("#", 1)[0].split("?", 1)[0])
    if not path_part:
        return None
    if path_part.startswith("/"):
        candidate = PurePosixPath(path_part.lstrip("/"))
    else:
        candidate = PurePosixPath(source_path).parent / path_part
    parts = []
    for part in candidate.parts:
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                return "../" + candidate.as_posix()
            parts.pop()
        else:
            parts.append(part)
    return "/".join(parts)

def main() -> int:
    paths = tracked_paths()
    markdown = sorted(p for p in paths if p.lower().endswith(".md"))
    failures = []
    checked = 0
    for source in markdown:
        source_file = ROOT / source
        if not source_file.is_file():
            failures.append((source, "<markdown source>", "not present in sparse checkout"))
            continue
        text = strip_fenced_code(source_file.read_text(encoding="utf-8"))
        for match in LINK_RE.finditer(text):
            target = target_token(match.group(1))
            resolved = normalize_target(source, target)
            if resolved is None:
                continue
            checked += 1
            if resolved.startswith("../") or (resolved not in paths and not any(p.startswith(resolved.rstrip("/") + "/") for p in paths)):
                failures.append((source, target, f"missing repository path: {resolved}"))
    print(f"Checked {checked} local Markdown links across {len(markdown)} tracked Markdown files.")
    if failures:
        for source, target, reason in failures:
            print(f"ERROR: {source} -> {target}: {reason}")
        return 1
    print("All local Markdown links resolve in the Git tree.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
