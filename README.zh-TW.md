<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# HP CLEANER++

**HP CLEANER++** 是一款 Windows 桌面工具，協助您**釋放儲存空間**、**調校系統效能**並**監控硬碟健康**，操作清晰、功能完整。本軟體面向日常重度使用電腦的族群：**擁有大型遊戲庫與啟動器的遊戲玩家**，以及**使用完整開發工具鏈的軟體開發者**。

---

## 適用對象

| 對象 | 常見需求 | HP CLEANER++ 提供的協助 |
|------|----------|-------------------------|
| **遊戲玩家** | 腾出空間裝新遊戲、縮短開機時間、確保硬碟可靠 | 清理啟動器與著色器快取、管理開機項、SMART 健康檢查 |
| **開發者** | 保持 SSD 效能、清除 IDE/建置快取、穩定網路環境 | 清理開發工具暫存、檢視啟動項與服務、DNS 與介面卡工具 |
| **進階使用者** | 完整掌握系統狀態 | 檔案 Treemap、詳細日誌、多語言介面、系統匣常駐 |

---

## 主要功能

| 領域 | 說明 |
|------|------|
| **儲存總覽** | 首頁呈現磁碟使用概況與分類統計 |
| **智慧清理** | 系統、瀏覽器、遊戲、開發工具等分類任務；刪除前可掃描預覽 |
| **效能優化** | 啟動項、背景服務、電源配置、一鍵預設方案 |
| **硬碟健康** | SMART / NVMe 屬性、健康評分、可選速度與壞軌測試 |
| **檔案地圖** | Treemap 與目錄樹，快速定位大型資料夾 |
| **紀錄與日誌** | 清理紀錄與應用內診斷日誌 |
| **多語言** | 內建 **zh-TW**、**zh-CN**、**en-US**、**ja-JP**；可選 JSON 語言包擴充 |

---

## 系統需求

| 項目 | 需求 |
|------|------|
| 作業系統 | Windows 10 或以上（建議 64 位元） |
| 記憶體 | 至少 4 GB；大型掃描建議 8 GB 以上 |
| 螢幕 | 1280×720 或更高解析度 |
| 權限 | 一般使用者即可；完整 SMART 資料建議**以系統管理員身分執行** |

---

## 開始使用

### 方式 A — 從原始碼建置（本儲存庫）

**環境：** Visual Studio 2022（使用 C++ 的桌面開發）、CMake 3.20+、Windows SDK。

```powershell
cd path\to\HP_CLEANER++\Github
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\HP_CLEANER++.exe
```

建置完成後，語言檔會自動複製至 `build/Release/i18n/`。

### 方式 B — 正式版本

自 **[Releases](https://github.com/Half-People/HP-CLEANER-/releases)** 下載最新可攜版（`HP_CLEANER++-*-win64.zip`）。

安裝說明：[INSTALL.zh-TW.md](docs/INSTALL.zh-TW.md) · [English](docs/INSTALL.md) · [變更紀錄](CHANGELOG.md)

---

## 功能一覽

```
┌──────────────────────────────────────────────────────────────────┐
│                        HP CLEANER++                              │
├─────────┬─────────┬─────────┬─────────┬──────────────────────────┤
│  首頁   │  清理   │  優化   │  硬碟   │  檔案地圖                │
│  儲存   │  安全   │  啟動項 │  健康   │  視覺化容量分析          │
│  記憶體 │  清理   │  網路   │  SMART  │                          │
├─────────┴─────────┴─────────┴─────────┴──────────────────────────┤
│  清理紀錄 · 關於 · 解除安裝輔助 · 系統匣                          │
└──────────────────────────────────────────────────────────────────┘
```

---

## 儲存庫導覽（貢獻者）

| 資料夾 | 內容 |
|--------|------|
| [`src/`](src/README.zh-TW.md) | 依功能分類的應用程式原始碼 |
| [`assets/`](assets/README.zh-TW.md) | Logo、圖示、字型（嵌入執行檔） |
| [`config/i18n/`](config/i18n/README.zh-TW.md) | 可選執行期翻譯 JSON |
| [`cmake/`](cmake/README.zh-TW.md) | 建置設定 |
| [`tools/`](tools/README.zh-TW.md) | 翻譯與維護腳本 |
| [`third_party/`](third_party/README.zh-TW.md) | 內嵌函式庫 |

詳細建置選項、架構說明與疑難排解，請參閱下方「技術文件」章節。

---

## 技術文件

<details>
<summary><strong>建置選項與常見問題（點擊展開）</strong></summary>

### CMake 選項

| 選項 | 預設 | 說明 |
|------|------|------|
| `HP_CLEANER_COPY_RUNTIME_ASSETS` | ON | 建置後複製 `config/i18n` 至執行檔目錄 |

### 常見問題

| 現象 | 處理方式 |
|------|----------|
| 資源編譯失敗 | 確認 [`assets/`](assets/README.zh-TW.md) 含 `Icon.ico`、`Logo.png`、`HP_Logo.png`、`kaiu.ttf` |
| 介面語言未切換 | 確認 `i18n/` 資料夾與執行檔在同一目錄 |
| SMART 資料不完整 | 以系統管理員身分重新執行程式 |

完整技術索引見 [`src/README.zh-TW.md`](src/README.zh-TW.md)、[`cmake/README.zh-TW.md`](cmake/README.zh-TW.md)。

</details>

---

## 授權

Copyright © 2026 **HalfPeople Studio**. 保留所有權利。

第三方授權見 [`third_party/README.zh-TW.md`](third_party/README.zh-TW.md)。

---

<p align="center">
  <sub>HalfPeople Studio · HP CLEANER++ · 為 Windows 上的遊戲玩家與開發者而生</sub>
</p>
