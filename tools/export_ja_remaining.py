#!/usr/bin/env python3
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROW_RE = re.compile(
    r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'
)
cpp = (ROOT / "Hi18nBuiltinPages.cpp").read_text(encoding="utf-8")
items = []
for m in ROW_RE.finditer(cpp):
    tw, cn, en, ja = m.groups()
    if tw == ja:
        items.append({"tw": tw, "en": en})
(ROOT / "tools" / "ja_remaining.json").write_text(
    json.dumps(items, ensure_ascii=False, indent=2), encoding="utf-8"
)
print(len(items))
