<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# config

與可執行檔一併部署的執行期設定（非編譯進二進位）。

## 子目錄

| 路徑 | 說明 |
|------|------|
| [`i18n/`](i18n/README.zh-TW.md) | 可選 JSON 語言包與 `languages.json` 清單 |

當 `HP_CLEANER_COPY_RUNTIME_ASSETS=ON` 時，CMake 建置後會複製 `config/i18n` → `<exe-dir>/i18n/`。

即使無這些檔案，[`src/i18n`](../src/i18n/README.zh-TW.md) 的內建翻譯仍可用。
