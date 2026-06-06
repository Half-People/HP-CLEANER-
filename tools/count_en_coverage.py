#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_i18n_pages import to_en  # noqa: E402

text = (ROOT / "Hi18nBuiltinPages.cpp").read_text(encoding="utf-8")
pat = re.compile(
    r'\{ "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)" \}'
)


def dec(s: str) -> str:
    return s.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")


rows = pat.findall(text)
full = partial = same = ascii_only = 0
need = []
for zt, zc, en, ja in rows:
    zt_d = dec(zt)
    en_d = dec(en)
    if not re.search(r"[\u4e00-\u9fff]", zt_d):
        ascii_only += 1
        continue
    if not re.search(r"[\u4e00-\u9fff]", en_d):
        full += 1
    elif en_d == zt_d:
        same += 1
        need.append(zt_d)
    else:
        partial += 1

# verify with to_en on extracted list
extracted = __import__("json").loads((ROOT / "tools" / "i18n_extracted.json").read_text(encoding="utf-8"))
gen_full = sum(1 for s in extracted if re.search(r"[\u4e00-\u9fff]", s) and not re.search(r"[\u4e00-\u9fff]", to_en(s)))

print(f"builtin_rows={len(rows)} ascii_only={ascii_only}")
print(f"cjk_full_en={full} cjk_partial={partial} cjk_untranslated={same}")
print(f"gen_to_en_full={gen_full}/{sum(1 for s in extracted if re.search(r'[\\u4e00-\\u9fff]', s))}")
if need:
    print("samples:")
    for s in need[:8]:
        print(f"  - {s[:72]}")
