#!/usr/bin/env python3
import re
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "OptimizePageUI.cpp"
text = path.read_text(encoding="utf-8")
new_text, n = re.subn(
    r'TagIf\(([^,]+),\s*I18N\(u8"((?:[^"\\]|\\.)*)"\)\)',
    r'TagIf(\1, u8"\2")',
    text,
)
path.write_text(new_text, encoding="utf-8")
print(f"Fixed {n} TagIf I18N calls")
