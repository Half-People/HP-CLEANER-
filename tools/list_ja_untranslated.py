#!/usr/bin/env python3
"""List rows where JA still equals zh-TW."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROW_RE = re.compile(
    r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'
)


def main() -> int:
    cpp = (ROOT / "Hi18nBuiltinPages.cpp").read_text(encoding="utf-8")
    items: list[str] = []
    for m in ROW_RE.finditer(cpp):
        tw, _cn, _en, ja = m.groups()
        if tw == ja:
            items.append(tw)
    print(f"count: {len(items)}")
    for s in items:
        print(s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
