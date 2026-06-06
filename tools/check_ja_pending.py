#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from batch_translate_ja import load_existing, needs_translation

ROW_RE = re.compile(
    r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'
)


def main() -> None:
    cpp = (ROOT / "Hi18nBuiltinPages.cpp").read_text(encoding="utf-8")
    phrases = load_existing()
    strings = json.loads((ROOT / "tools" / "i18n_extracted.json").read_text(encoding="utf-8"))

    same_tw: list[str] = []
    for m in ROW_RE.finditer(cpp):
        tw, _cn, _en, ja = m.groups()
        if ja == tw and re.search(r"[\u4e00-\u9fff]", tw):
            same_tw.append(tw)

    pending = [s for s in strings if needs_translation(s) and s not in phrases]
    cache_same = [s for s in same_tw if s in phrases and phrases[s] == s]

    print(f"ja==tw in cpp: {len(same_tw)}")
    print(f"pending not in cache: {len(pending)}")
    print(f"in cache but value unchanged: {len(cache_same)}")
    print("pending samples:")
    for s in pending[:10]:
        print(f"  {s[:72]}")


if __name__ == "__main__":
    main()
