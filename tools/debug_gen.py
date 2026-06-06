from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"imgui", "HP_CLEANER++", "x64", "tools", "i18n_samples", "doc"}
count = 0
for p in ROOT.rglob("*.cpp"):
    if any(part in SKIP_DIRS for part in p.parts):
        continue
    count += 1
print("cpp files", count)
t = (ROOT / "ClearPageUI.cpp").read_text(encoding="utf-8")
print("I18N occurrences", t.count("I18N("))
