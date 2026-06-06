#!/usr/bin/env python3
"""Find zh-TW string literals in .cpp not passed through I18N/W18N/HTR."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"imgui", "x64", "tools", "i18n_samples", "doc", ".vs"}
SKIP_FILES = {"Hi18nBuiltinPages.cpp", "Hi18nBuiltin.cpp"}

LIT = re.compile(r'u8"((?:[^"\\]|\\.)*)"|"((?:[^"\\]|\\.)*)"')
I18N_CTX = re.compile(r"I18N\s*\(|W18N\s*\(|HTR\s*\(|I18N_JOIN\s*\(|I18NStr\s*\(|I18NF\s*\(")
CJK = re.compile(r"[\u4e00-\u9fff]")
LOG_CTX = re.compile(r"HLOG_|SPDLOG_|RegisterClean|RegistrationNav|RegistrationPage")


def decode(raw: str) -> str:
    return raw.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")


def main() -> None:
    hits: list[tuple[str, int, str, str]] = []
    for p in sorted(ROOT.rglob("*.cpp")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.name in SKIP_FILES:
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        for m in LIT.finditer(text):
            raw = m.group(1) if m.group(1) is not None else m.group(2)
            if raw is None:
                continue
            s = decode(raw)
            if not CJK.search(s):
                continue
            if len(s) > 200:
                continue
            ctx = text[max(0, m.start() - 64) : m.start()]
            if I18N_CTX.search(ctx):
                continue
            if LOG_CTX.search(ctx):
                continue
            if "REG_PAGEN" in ctx or "REG_CLEAN" in ctx:
                continue
            line = text.count("\n", 0, m.start()) + 1
            hits.append((str(p.relative_to(ROOT)), line, s[:100], ctx.strip()[-40:]))

    by_file: dict[str, list] = {}
    for f, line, s, ctx in hits:
        by_file.setdefault(f, []).append((line, s, ctx))

    print(f"hardcoded zh (no I18N): {len(hits)} in {len(by_file)} files\n")
    for f in sorted(by_file.keys()):
        items = by_file[f]
        print(f"=== {f} ({len(items)}) ===")
        for line, s, ctx in items[:12]:
            one = s.replace("\n", "\\n")
            print(f"  L{line}: {one[:80]}")
        if len(items) > 12:
            print(f"  ... +{len(items)-12} more")
        print()


if __name__ == "__main__":
    main()
