<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# tools — Developer scripts

Python and PowerShell utilities for **i18n**, **audits**, and **syncing** the GitHub publish folder.  
Not required to **run** the app — only for maintaining translations and the repo layout.

---

## Script reference

| Script | Purpose | When to run |
|--------|---------|-------------|
| `gen_i18n_pages.py` | Regenerate `src/i18n/Hi18nBuiltinPages.cpp` from key list | After adding new `I18N(u8"…")` keys |
| `batch_translate_ja.py` | Google Translate batch → JA cache JSON | Filling Japanese gaps |
| `batch_translate_en.py` | Batch translate → EN cache JSON | Filling English gaps |
| `apply_ja_manual.py` | Manual JA phrase overrides | Fix bad machine translations |
| `audit_i18n_coverage.py` | Report EN/JA vs zh-TW coverage | Before release |
| `audit_hardcoded_zh.py` | Find Chinese literals not in `I18N()` | Fix untranslated UI |
| `audit_fmt_i18n.py` | Check `%d`/`%s` order in translations | Prevent crash on locale switch |
| `audit_i18n.py` | General i18n consistency checks | Maintenance |
| `setup_github_folder.ps1` | Rebuild entire `Github/` tree from dev sources | Before git push to GitHub |
| `build.ps1` | MSBuild wrapper for original `.sln` | Dev tree (not Github/) only |

---

## Typical i18n workflow

```
1. Add UI strings in code as I18N(u8"繁中 key")
           │
           ▼
2. python tools\gen_i18n_pages.py
           │
           ▼
3. python tools\batch_translate_ja.py   (optional)
   python tools\apply_ja_manual.py       (optional)
           │
           ▼
4. python tools\audit_i18n_coverage.py
   python tools\audit_hardcoded_zh.py
           │
           ▼
5. Rebuild app / sync Github folder
```

---

## Git ignore note

These cache files are **not** meant for GitHub (listed in [`.gitignore`](../.gitignore)):

- `*_batch.json` — translation API caches
- `i18n_extracted.json` — intermediate key list
- `ja_remaining.json` / `ja_remaining.txt`

Regenerate locally after clone.

---

## Sync GitHub folder

From **parent dev project root**:

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_github_folder.ps1
```

This wipes and recreates `Github/` with organized `src/<module>/` layout.

---

## See also

- i18n runtime packs: [../config/i18n/README.md](../config/i18n/README.md)
- i18n source: [../src/i18n/README.md](../src/i18n/README.md)
- 中文說明: [README.zh-TW.md](README.zh-TW.md)
