#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
for p in ROOT.rglob('*.cpp'):
    if any(x in str(p) for x in ('imgui', 'implot', 'Hi18nBuiltinPages')):
        continue
    t = p.read_text(encoding='utf-8')
    n = t.replace('_(u8', 'I18N(u8')
    if n != t:
        p.write_text(n, encoding='utf-8')
        print(p.name)
