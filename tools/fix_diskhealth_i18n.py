#!/usr/bin/env python3
"""Fix unsafe ImGui/snprintf + I18N usage in DiskHealthUI.cpp."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "DiskHealthUI.cpp"


def fix_plain_imgui_text(line: str) -> str:
    """TextDisabled/TextColored/Text with I18N only, no format args."""
    for fn in ("TextDisabled", "TextColored", "Text"):
        pat = rf'(ImGui::{fn}\([^;]*?,?\s*)I18N\((u8"[^"]*")\)\);'
        m = re.search(pat, line)
        if m and "%s" not in m.group(1):
            # skip if already has format string before I18N
            prefix = m.group(1)
            if re.search(r'"%s"', prefix):
                continue
            key = m.group(2)
            line = re.sub(
                rf'{re.escape(prefix)}I18N\({re.escape(key)}\)\);',
                f'{prefix}"%s", I18N({key}));',
                line,
                count=1,
            )
    # TextColored with single arg before I18N e.g. TextColored(Theme::cyan_neon, I18N(...));
    pat = r'(ImGui::TextColored\(\s*[^,]+,\s*)I18N\((u8"[^"]*")\)\);'
    if re.search(pat, line) and '"%s"' not in line:
        line = re.sub(pat, r'\1"%s", I18N(\2));', line, count=1)
    pat2 = r'(ImGui::TextDisabled\(\s*)I18N\((u8"[^"]*")\)\);'
    if re.search(pat2, line) and '"%s"' not in line:
        line = re.sub(pat2, r'\1"%s", I18N(\2));', line, count=1)
    return line


def fix_imgui_format(line: str) -> str:
    """I18N(u8"...%...") as format -> I18NF(u8"...")"""
    return re.sub(
        r'ImGui::(TextColored|TextDisabled|Text|TextWrapped)\(([^)]*?)I18N\((u8"[^"]*%[^"]*")\)',
        r'ImGui::\1(\2I18NF(\3)',
        line,
    )


def fix_snprintf(line: str) -> str:
    line = re.sub(
        r'snprintf\((\w+),\s*sizeof\(\1\),\s*I18N\((u8"[^"]*")\)',
        r'SnprintfI18n(\1, sizeof(\1), \2',
        line,
    )
    line = re.sub(
        r'snprintf\((\w+),\s*(\w+),\s*I18N\((u8"[^"]*")\)',
        r'SnprintfI18n(\1, \2, \3',
        line,
    )
    return line


def fix_status_and_notes(content: str) -> str:
    content = content.replace(
        'ImGui::ProgressBar(snap.progress, ImVec2(-1, 18), I18N(snap.status_text));',
        'ImGui::ProgressBar(snap.progress, ImVec2(-1, 18), snap.status_text);',
    )
    content = content.replace(
        'ImGui::TextDisabled("%s%s%s", I18N(snap.status_text),',
        'ImGui::TextDisabled("%s%s%s", snap.status_text,',
    )
    content = re.sub(
        r'I18N\(drive\.status_note\)',
        'drive.status_note',
        content,
    )
    return content


def main() -> None:
    text = TARGET.read_text(encoding="utf-8")
    text = fix_status_and_notes(text)

    lines = text.splitlines(keepends=True)
    out: list[str] = []
    for line in lines:
        line = fix_snprintf(line)
        line = fix_imgui_format(line)
        line = fix_plain_imgui_text(line)
        out.append(line)

    text = "".join(out)
    # second pass: plain TextDisabled still missed (no comma before I18N)
    text = re.sub(
        r'ImGui::TextDisabled\(I18N\((u8"[^"]*")\)\);',
        r'ImGui::TextDisabled("%s", I18N(\1));',
        text,
    )
    text = re.sub(
        r'ImGui::TextColored\((Theme::[^,]+),\s*I18N\((u8"[^"]*")\)\);',
        r'ImGui::TextColored(\1, "%s", I18N(\2));',
        text,
    )
    text = re.sub(
        r'ImGui::TextColored\((ImVec4\([^)]+\)),\s*I18N\((u8"[^"]*")\)\);',
        r'ImGui::TextColored(\1, "%s", I18N(\2));',
        text,
    )
    # TextWrapped plain
    text = re.sub(
        r'ImGui::TextWrapped\(\s*I18N\((u8"[^"]*")\)\s*\);',
        lambda m: f'ImGui::TextWrapped("%s", I18N({m.group(1)}));'
        if "%" not in m.group(1)
        else m.group(0),
        text,
    )

    TARGET.write_text(text, encoding="utf-8")
    print(f"Patched {TARGET}")


if __name__ == "__main__":
    main()
