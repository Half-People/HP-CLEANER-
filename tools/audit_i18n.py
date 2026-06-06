#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find CJK string literals not wrapped with I18N/HTR/W18N."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"imgui", "x64", "tools", "i18n_samples", "doc", ".vs"}
SKIP_FILES = {"Hi18nBuiltinPages.cpp", "Hi18nBuiltin.cpp", "Hi18n.cpp"}
# 儲存層：繁中 key，顯示時再 I18N()
STORAGE_FILE_PREFIXES = ("CleanTasks_",)
STORAGE_FILE_NAMES = {
    "DiskHealthScan.cpp",
    "OptimizeScan.cpp",
    "OptimizeNetworkScan.cpp",
    "FileMapScan.cpp",
    "FileMapTree.cpp",
    "OptimizeStartupIcon.cpp",
    "DiskHealthTest.cpp",
    "MainPageDiskScan.cpp",
    "HCleanTask.cpp",
}

STR_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
WRAP_MARKERS = ("I18N(", "HTR(", "W18N(", "I18N_JOIN(", "TrZh(", "Hi18n::Tr(")


def decode_literal(raw: str) -> str:
    inner = raw[1:-1]
    return (
        inner.replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace('\\"', '"')
        .replace("\\\\", "\\")
    )


def should_skip_context(ctx: str) -> bool:
    if "HLOG_" in ctx or "SPDLOG_" in ctx:
        return True
    if "REG_PAGEN" in ctx or "REG_NAV_ITEM" in ctx or "RegisterClean" in ctx:
        return True
    if "RegistrationNavItem_internal" in ctx:
        return True
    if "I18N_JOIN(" in ctx or "W18N(" in ctx or "TrZhWide(" in ctx:
        return True
    if "RegisterCleanCategory" in ctx or "RegisterCleanTask" in ctx:
        return True
    if "kLabels[" in ctx or "kStatusLabels[" in ctx or "kThirdPartyLibs" in ctx:
        return True
    if "source_label =" in ctx or "g_view.source_label" in ctx:
        return True
    if 'u8"' in ctx[-8:] or "strcmp(" in ctx or "strstr(" in ctx:
        return True
    for mark in WRAP_MARKERS:
        if mark in ctx[-48:]:
            return True
    return False


def is_storage_file(path: Path) -> bool:
    if path.name.startswith(STORAGE_FILE_PREFIXES):
        return True
    return path.name in STORAGE_FILE_NAMES


def audit_file(path: Path) -> list[tuple[int, str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    issues: list[tuple[int, str]] = []
    for m in STR_RE.finditer(text):
        decoded = decode_literal(m.group(0))
        if not re.search(r"[\u4e00-\u9fff]", decoded):
            continue
        if decoded.startswith("##"):
            continue
        if len(decoded) > 220:
            continue
        ctx = text[max(0, m.start() - 72) : m.start()]
        if should_skip_context(ctx):
            continue
        line = text[: m.start()].count("\n") + 1
        issues.append((line, decoded[:72]))
    return issues


def safe_print(text: str) -> None:
    enc = getattr(sys.stdout, "encoding", None) or "utf-8"
    try:
        print(text)
    except UnicodeEncodeError:
        print(text.encode(enc, errors="replace").decode(enc, errors="replace"))


def main() -> int:
    rows: list[tuple[int, Path, list[tuple[int, str]]]] = []
    storage_count = 0
    for p in sorted(ROOT.rglob("*.cpp")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.name in SKIP_FILES:
            continue
        if is_storage_file(p):
            storage_count += 1
            continue
        issues = audit_file(p)
        if issues:
            rows.append((len(issues), p, issues))

    rows.sort(key=lambda x: x[0], reverse=True)
    total = sum(n for n, _, _ in rows)
    safe_print(f"Display-layer literals needing review: {total} in {len(rows)} files")
    safe_print(f"(Skipped {storage_count} storage-key files: CleanTasks_*, *Scan.cpp, etc.)\n")
    for n, path, samples in rows:
        rel = path.relative_to(ROOT)
        safe_print(f"{n:4d}  {rel}")
        for line, sample in samples[:3]:
            safe_print(f"        L{line}: {sample}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
