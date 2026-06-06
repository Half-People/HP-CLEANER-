#!/usr/bin/env python3
"""Fix EN/JA rows where printf specifiers order/count differs from zh-TW key."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "Hi18nBuiltinPages.cpp"

ROW_RE = re.compile(
    r'(\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\})'
)
SPEC_RE = re.compile(
    r"%(?!%)(?:\d+\$)?[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[hlLzjt]*[diuoxXfceEgGaAcspn]"
)


def specs(s: str) -> list[str]:
    return SPEC_RE.findall(s)


def fix_row(tw: str, cn: str, en: str, ja: str) -> tuple[str, str, bool]:
    changed = False
    st = specs(tw)
    if not st:
        return en, ja, False
    if specs(en) != st:
        en = tw  # fallback: keep zh key format until manual EN rewrite
        changed = True
    if specs(ja) != st:
        ja = tw
        changed = True
    return en, ja, changed


def main() -> int:
    text = CPP.read_text(encoding="utf-8")
    n = 0

    def repl(m: re.Match) -> str:
        nonlocal n
        full, tw, cn, en, ja = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
        if "%" not in tw:
            return full
        new_en, new_ja, changed = fix_row(tw, cn, en, ja)
        if not changed:
            return full
        n += 1
        def esc(s: str) -> str:
            return s.replace("\\", "\\\\").replace('"', '\\"')
        return (
            f'{{ "{esc(tw)}", "{esc(cn)}", "{esc(new_en)}", "{esc(new_ja)}" }}'
        )

    out = ROW_RE.sub(repl, text)
    if n:
        CPP.write_text(out, encoding="utf-8")
    print(f"fixed {n} rows (EN/JA reverted to zh-TW format where specs mismatched)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
