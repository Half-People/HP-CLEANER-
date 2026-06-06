#!/usr/bin/env python3
"""Find translation rows where EN/JA printf specifiers differ from zh-TW key."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "Hi18nBuiltinPages.cpp"

ROW_RE = re.compile(
    r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'
)
SPEC_RE = re.compile(
    r"%(?!%)(?:\d+\$)?[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[hlLzjt]*[diuoxXfceEgGaAcspn]"
)


def specs(s: str) -> list[str]:
    return SPEC_RE.findall(s)


def main() -> int:
    text = CPP.read_text(encoding="utf-8")
    bad: list[tuple[str, str, list[str], list[str]]] = []
    for m in ROW_RE.finditer(text):
        tw, _cn, en, ja = m.groups()
        if "%" not in tw:
            continue
        st = specs(tw)
        for lang, val in (("en", en), ("ja", ja)):
            sv = specs(val)
            if st != sv:
                bad.append((tw, lang, st, sv))

    print(f"mismatches: {len(bad)}")
    for tw, lang, st, sv in bad:
        print("---")
        print(f"key: {tw[:100]}")
        print(f"{lang}: expected {st} got {sv}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
