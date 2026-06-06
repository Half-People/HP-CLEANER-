#!/usr/bin/env python3
"""Report i18n rows where EN/JA still equals zh-TW (likely untranslated)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "Hi18nBuiltinPages.cpp"
ROW_RE = re.compile(
    r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'
)


def main() -> int:
    text = CPP.read_text(encoding="utf-8")
    total = en_same = ja_same = 0
    samples_ja: list[str] = []
    for m in ROW_RE.finditer(text):
        tw, _cn, en, ja = m.groups()
        total += 1
        if en == tw:
            en_same += 1
        if ja == tw:
            ja_same += 1
            if len(samples_ja) < 12:
                samples_ja.append(tw[:70])
    print(f"total rows: {total}")
    print(f"EN same as zh-TW: {en_same} ({100*en_same/max(total,1):.1f}%)")
    print(f"JA same as zh-TW: {ja_same} ({100*ja_same/max(total,1):.1f}%)")
    if samples_ja:
        print("JA untranslated samples:")
        for s in samples_ja:
            print(f"  - {s}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
