#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Remove I18N() from string storage (snap/status); keep keys as u8 literals."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    "OptimizeScan.cpp",
    "OptimizeNetworkScan.cpp",
    "MainPageDiskScan.cpp",
    "DiskHealthScan.cpp",
    "FileMapScan.cpp",
    "DiskHealthTest.cpp",
    "OptimizeStartupIcon.cpp",
    "FileMapTree.cpp",
    "MainPageMemory.cpp",
]

PATTERNS = [
    (re.compile(r"strncpy_s\(([^;]+),\s*I18N\((u8\"(?:[^\"\\]|\\.)*\")\)"), r"strncpy_s(\1, \2"),
    (re.compile(r"SetLastAction\(I18N\((u8\"(?:[^\"\\]|\\.)*\")\)\)"), r"SetLastAction(\1)"),
    (re.compile(r"SetLastAction\(I18N\((u8\"(?:[^\"\\]|\\.)*\")\),\s*"), r"SetLastAction(\1, "),
    (re.compile(r"StorageWorkSetLocked\(([^,]+),\s*I18N\((u8\"(?:[^\"\\]|\\.)*\")\)\)"),
     r"StorageWorkSetLocked(\1, \2)"),
    (re.compile(r"return I18N\((u8\"(?:[^\"\\]|\\.)*\")\);"), r"return \1;"),
]


def fix_file(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    original = text
    count = 0
    for rx, repl in PATTERNS:
        text, n = rx.subn(repl, text)
        count += n
    if text != original:
        path.write_text(text, encoding="utf-8")
    return count


def main() -> int:
    total = 0
    for name in FILES:
        p = ROOT / name
        if not p.exists():
            continue
        n = fix_file(p)
        if n:
            print(f"{name}: {n}")
            total += n
    print(f"storage fixes: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
