#!/usr/bin/env python3
"""Inject HalfPeople Studio + HP CLEANER++ logo header into Github README files."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS_DIR = "assets"
SKIP_PARTS = {"build", ".git", "node_modules"}

LOGO_BLOCK = """\
<p align="center">
  <img src="{rel}head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
"""

LANG_EN = """\
<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
"""

LANG_ZH = """\
<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
"""

TAGLINE_EN = "<p align=\"center\"><sub>Windows system management for gamers and developers</sub></p>\n"
TAGLINE_ZH = "<p align=\"center\"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>\n"

HEADER_EN = LANG_EN + LOGO_BLOCK + TAGLINE_EN + "\n---\n\n"
HEADER_ZH = LANG_ZH + LOGO_BLOCK + TAGLINE_ZH + "\n---\n\n"

# Strip any existing injected header (language bar + logos through first ---)
STRIP_RE = re.compile(
    r"^(?:<p align=\"center\">.*?</p>\s*\n)+---\s*\n",
    re.DOTALL,
)


def rel_to_assets(md_path: Path) -> str:
    if md_path.parent.resolve() == (ROOT / ASSETS_DIR).resolve():
        return "./"
    depth = len(md_path.relative_to(ROOT).parts) - 1
    return "../" * depth + f"{ASSETS_DIR}/"


def process_file(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = STRIP_RE.sub("", text)
    rel = rel_to_assets(path)
    header = HEADER_ZH if path.name.endswith(".zh-TW.md") else HEADER_EN
    path.write_text(header.format(rel=rel) + text.lstrip(), encoding="utf-8")


def main() -> None:
    count = 0
    for md in sorted(ROOT.rglob("*.md")):
        if any(part in SKIP_PARTS for part in md.parts):
            continue
        if md.name not in ("README.md", "README.zh-TW.md"):
            continue
        process_file(md)
        count += 1
    print(f"Branded {count} README files under {ROOT}")


if __name__ == "__main__":
    main()
