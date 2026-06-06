<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# i18n

從 `<exe-dir>/i18n/` 載入的可選執行期語言包。

## 檔案

| 檔案 | 用途 |
|------|------|
| `languages.json` | 可用語言代碼與顯示名稱 |
| `en-US.json`、`zh-CN.json`、`ja-JP.json`… | 覆寫 / 擴充內建字串 |

格式範例：

```json
{
  "language": "ja-JP",
  "name": "日本語",
  "strings": {
    "繁中原文 key": "譯文"
  }
}
```

缺少的 key 會回退至 `Hi18nBuiltinPages.cpp` 內建表。

可用 [`tools/`](../../tools/README.zh-TW.md) 腳本產生或稽核語言包。
