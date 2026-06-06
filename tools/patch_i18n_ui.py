#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wrap CJK string literals with _(u8"...") for Hi18n::TrZh."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    "ClearPageUI.cpp", "OptimizePageUI.cpp", "DiskHealthUI.cpp", "FileMapUI.cpp",
    "AboutPageUI.cpp", "HistoryPage.cpp", "HAdminPrompt.cpp", "HUninstallUI.cpp",
    "HCrashReportUI.cpp", "ConfirmDeleteDirectorys_PopupPage.cpp",
    "ClearPage.cpp", "OptimizePage.cpp", "DiskHealthPage.cpp", "FileMapPage.cpp",
    "AboutPage.cpp",
]

STR_RE = re.compile(r'"(?:[^"\\]|\\.)*"')

def should_skip_context(text: str, start: int) -> bool:
    ctx = text[max(0, start - 48):start]
    if 'HLOG_' in ctx or 'SPDLOG_' in ctx or 'HTRS(' in ctx or 'HTR(' in ctx:
        return True
    if '_(' in ctx and ctx.rstrip().endswith('_('):
        return True
    return False

def patch_file(path: Path) -> int:
    text = path.read_text(encoding='utf-8')
    if '#include "Hi18n.h"' not in text:
        text = text.replace('#include "HPage.h"', '#include "HPage.h"\n#include "Hi18n.h"', 1)
        if '#include "Hi18n.h"' not in text:
            lines = text.splitlines()
            for i, line in enumerate(lines):
                if line.startswith('#include'):
                    lines.insert(i + 1, '#include "Hi18n.h"')
                    break
            text = '\n'.join(lines)

    count = 0
    out = []
    pos = 0
    for m in STR_RE.finditer(text):
        out.append(text[pos:m.start()])
        raw = m.group(0)
        inner = raw[1:-1]
        decoded = inner.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace('\\\\', '\\')
        if (re.search(r'[\u4e00-\u9fff]', decoded)
                and not should_skip_context(text, m.start())
                and not raw.startswith('_(u8"')
                and not decoded.startswith('##')):
            out.append(f'_(u8{raw})')
            count += 1
        else:
            out.append(raw)
        pos = m.end()
    out.append(text[pos:])
    new_text = ''.join(out)
    if new_text != text:
        path.write_text(new_text, encoding='utf-8')
    return count

def main():
    total = 0
    for fname in FILES:
        p = ROOT / fname
        if p.exists():
            n = patch_file(p)
            print(f'{fname}: {n} wrapped')
            total += n
    print(f'Total: {total}')

if __name__ == '__main__':
    main()
