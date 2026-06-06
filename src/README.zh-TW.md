<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# src — 應用程式原始碼

HP CLEANER++ 的全部邏輯在此，分成 **11 個功能子目錄**（65 個 `.cpp`）。  
CMake 會把各子目錄加入 include 路徑，因此仍可使用 `#include "HPage.h"` 這類扁平引用。

---

## 模組地圖（快速對照）

```
src/
├── application/   進入點、視窗 Shell、系統匣、設定、UAC
├── core/          HPage 框架、日誌、崩潰處理、嵌入資源
├── i18n/          約 3000 條內建字串 + 語言選單
├── mainpage/      首頁 — 儲存圖、記憶體整理
├── clean/         清理任務 + 清理分頁 UI
├── optimize/      啟動項 / 服務 / 網路 / 預設
├── diskhealth/    SMART、NVMe、診斷 UI
├── filemap/       Treemap + 目錄掃描
├── about/         關於分頁、裝置資訊、日誌
├── history/       清理紀錄時間軸
└── ui/            共用控件、解除安裝精靈
```

---

## 模組詳表

| 目錄 | 代表檔案 | 使用者看到的 | 背景工作 |
|------|----------|--------------|----------|
| [`application/`](application/README.zh-TW.md) | `main.cpp`、`HAppShell.*` | 視窗、系統匣、單實例 | 提升權限 |
| [`core/`](core/README.zh-TW.md) | `HPage.*`、`HLog*.*` | 所有分頁的基底 | spdlog、minidump |
| [`i18n/`](i18n/README.zh-TW.md) | `Hi18n*.cpp` | 語言選單 | JSON 語言包載入 |
| [`mainpage/`](mainpage/README.zh-TW.md) | `MainPageUI.*` | 首頁儀表板 | 磁碟分類掃描 |
| [`clean/`](clean/README.zh-TW.md) | `CleanTasks_*.cpp` | 清理卡片、詳情 | 各任務容量掃描 |
| [`optimize/`](optimize/README.zh-TW.md) | `OptimizePageUI.*` | 多分頁優化 | 啟動項/服務列舉 |
| [`diskhealth/`](diskhealth/README.zh-TW.md) | `DiskHealthUI.*` | SMART 卡片、圖表 | 背景磁碟掃描 |
| [`filemap/`](filemap/README.zh-TW.md) | `FileMapUI.*` | Treemap | 資料夾大小遍歷 |
| [`about/`](about/README.zh-TW.md) | `AboutPageUI.*` | 版本與日誌 | 裝置資訊查詢 |
| [`history/`](history/README.zh-TW.md) | `HistoryPage.cpp` | 清理紀錄列表 | 讀取 `CleanHistory` |
| [`ui/`](ui/README.zh-TW.md) | `HUiTheme.h` | 共用樣式、卸載 | — |

---

## 分頁如何串接

```
HAppShell (application/)
    │
    ├─► MainPage      (mainpage/)  ──► MainPageUI
    ├─► ClearPage     (clean/)     ──► ClearPageUI + HCleanTask*
    ├─► OptimizePage  (optimize/)  ──► OptimizePageUI + OptimizeScan
    ├─► DiskHealthPage(diskhealth/)──► DiskHealthUI + DiskHealthScan
    ├─► FileMapPage   (filemap/)   ──► FileMapUI + FileMapScan
    ├─► HistoryPage   (history/)
    └─► AboutPage     (about/)     ──► AboutPageUI
```

各 `*Page.cpp` 向 Shell 註冊；`*UI.cpp` 負責 ImGui 繪製。

---

## 編碼慣例

| 主題 | 規則 |
|------|------|
| **UI 字串** | 存繁中 key；顯示時才 `I18N()` / `I18NF()` |
| **格式化文字** | 用 `SnprintfI18n` 或 `I18NF("…%s…", arg)`；勿把 `I18N()` 直接當 ImGui 格式字串 |
| **快照** | 背景執行緒寫 **key** 或原始資料；UI 執行緒翻譯 |
| **日誌** | `HLOG_INFO(...)`（`HPage.h` / spdlog） |
| **新增清理任務** | 新增 `CleanTasks_*.cpp`、註冊、並加入 i18n 表 |

---

## 各目錄 .cpp 數量（約）

| 目錄 | 數量 |
|------|------|
| clean | 14 |
| core | 12 |
| application | 9 |
| optimize | 5 |
| diskhealth | 5 |
| i18n / filemap / mainpage / about | 各 4 |
| ui | 3 |
| history | 1 |

---

## 延伸閱讀

- 專案總覽：[../README.zh-TW.md](../README.zh-TW.md)
- 建置：[../cmake/README.zh-TW.md](../cmake/README.zh-TW.md)
- 翻譯：[../config/i18n/README.zh-TW.md](../config/i18n/README.zh-TW.md)
- English: [README.md](README.md)
