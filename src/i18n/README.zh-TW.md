<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# i18n

國際化層：內建字串表 + 可選 JSON 覆寫。

## 主要檔案

| 檔案 | 用途 |
|------|------|
| `Hi18n.h` / `Hi18n.cpp` | `I18N()`、語言切換、JSON 語言包載入 |
| `Hi18nBuiltin.*` | 固定 enum key（導覽、首頁文案） |
| `Hi18nBuiltinPages.*` | 大型分頁字串表（約 3000 條） |
| `Hi18nLangPicker.*` | 應用內語言選單 UI |

## 執行期語言包

可選 JSON 位於 [`config/i18n`](../../config/i18n/README.zh-TW.md)，建置後複製至執行檔旁的 `i18n/`。

內建表可由 [`tools/`](../../tools/README.zh-TW.md) 腳本重新產生。
