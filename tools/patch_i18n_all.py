#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wrap remaining CJK string literals with I18N(u8"...") for Hi18n::TrZh."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"imgui", "x64", "tools", "i18n_samples", "doc", ".vs"}
SKIP_FILES = {
    "Hi18nBuiltinPages.cpp",
    "Hi18nBuiltin.cpp",
    "Hi18n.cpp",
    "Hi18nLangPicker.cpp",
}

STR_RE = re.compile(r'u8"((?:[^"\\]|\\.)*)"|"((?:[^"\\]|\\.)*)"')
SKIP_CTX = (
    "HLOG_", "SPDLOG_", "I18N(", "HTR(", "W18N(", "I18N_JOIN(",
    "TrZh(", "Hi18n::Tr(", "REG_PAGEN", "REG_NAV_ITEM", "REG_PAGE",
    "RegisterCleanCategory", "RegisterCleanTask", "RegisterClean",
)
# 登錄／設定用識別碼，不作 UI 翻譯
SKIP_LITERAL_PREFIXES = ("##",)


def decode(raw: str) -> str:
    return (
        raw.replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace('\\"', '"')
        .replace("\\\\", "\\")
    )


def should_skip(text: str, start: int, decoded: str) -> bool:
    if not re.search(r"[\u4e00-\u9fff]", decoded):
        return True
    if decoded.startswith(SKIP_LITERAL_PREFIXES):
        return True
    if len(decoded) > 220:
        return True
    ctx = text[max(0, start - 72) : start]
    for mark in SKIP_CTX:
        if mark in ctx:
            return True
    # 寬字元字面量由 W18N 手動處理
    if start >= 2 and text[start - 1] == '"' and "L" in ctx[-4:]:
        return True
    return False


def ensure_hi18n_include(text: str) -> str:
    if '#include "Hi18n.h"' in text:
        return text
    lines = text.splitlines()
    insert_at = 0
    for i, line in enumerate(lines):
        if line.startswith("#include"):
            insert_at = i + 1
    lines.insert(insert_at, '#include "Hi18n.h"')
    return "\n".join(lines)


def patch_file(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    count = 0
    out = []
    pos = 0
    for m in STR_RE.finditer(text):
        out.append(text[pos:m.start()])
        raw_inner = m.group(1) if m.group(1) is not None else m.group(2)
        prefix = 'u8"' if m.group(1) is not None else '"'
        decoded = decode(raw_inner)
        if should_skip(text, m.start(), decoded):
            out.append(m.group(0))
        else:
            out.append(f'I18N(u8"{raw_inner}")')
            count += 1
        pos = m.end()
    out.append(text[pos:])
    new_text = "".join(out)
    if count > 0:
        new_text = ensure_hi18n_include(new_text)
        path.write_text(new_text, encoding="utf-8")
    return count


def main() -> int:
    total = 0
    files = 0
    for p in sorted(ROOT.rglob("*.cpp")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.name in SKIP_FILES:
            continue
        n = patch_file(p)
        if n:
            print(f"{p.relative_to(ROOT)}: {n}")
            total += n
            files += 1
    print(f"Patched {total} literals in {files} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
