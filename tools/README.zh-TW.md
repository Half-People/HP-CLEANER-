<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# tools — 開發者腳本

用於 **i18n**、**稽核** 與 **同步 GitHub 發佈目錄** 的 Python / PowerShell 工具。  
**執行程式不需要**這些腳本 — 僅供維護翻譯與目錄結構。

---

## 腳本對照表

| 腳本 | 用途 | 何時執行 |
|------|------|----------|
| `gen_i18n_pages.py` | 由 key 清單重新產生 `Hi18nBuiltinPages.cpp` | 新增 `I18N(u8"…")` 後 |
| `batch_translate_ja.py` | Google 批次翻譯 → 日文快取 JSON | 補日文缺口 |
| `batch_translate_en.py` | 批次翻譯 → 英文快取 JSON | 補英文缺口 |
| `apply_ja_manual.py` | 日文人工短語覆寫 | 修正機翻 |
| `audit_i18n_coverage.py` | EN/JA 對繁中覆蓋率 | 發佈前 |
| `audit_hardcoded_zh.py` | 找出未包 `I18N()` 的中文 | 修未譯 UI |
| `audit_fmt_i18n.py` | 檢查譯文 `%d`/`%s` 順序 | 避免切語言崩潰 |
| `audit_i18n.py` | 一般 i18n 一致性 | 維護 |
| `setup_github_folder.ps1` | 從開發樹重建整個 `Github/` | push GitHub 前 |
| `build.ps1` | 原 `.sln` 的 MSBuild 包裝 | 僅開發樹（非 Github/） |

---

## 典型 i18n 流程

```
1. 程式碼新增 I18N(u8"繁中 key")
           │
           ▼
2. python tools\gen_i18n_pages.py
           │
           ▼
3. python tools\batch_translate_ja.py   （可選）
   python tools\apply_ja_manual.py       （可選）
           │
           ▼
4. python tools\audit_i18n_coverage.py
   python tools\audit_hardcoded_zh.py
           │
           ▼
5. 重新建置 / 同步 Github 資料夾
```

---

## Git 忽略說明

以下快取**不建議提交**（見 [`.gitignore`](../.gitignore)）：

- `*_batch.json`
- `i18n_extracted.json`
- `ja_remaining.json` / `ja_remaining.txt`

clone 後需在本機重新產生。

---

## 同步 GitHub 資料夾

在**上層開發專案根目錄**：

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_github_folder.ps1
```

會清除並重建含 `src/<模組>/` 的 `Github/` 目錄。

---

## 延伸閱讀

- 執行期語言包：[../config/i18n/README.zh-TW.md](../config/i18n/README.zh-TW.md)
- i18n 原始碼：[../src/i18n/README.zh-TW.md](../src/i18n/README.zh-TW.md)
- English: [README.md](README.md)
