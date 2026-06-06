# HP CLEANER++ — 安裝與首次使用

[English](INSTALL.md) | **繁體中文**

---

## 快速開始

1. 自 [Releases](https://github.com/Half-People/HP-CLEANER-/releases) 下載 **`HP_CLEANER++-1.0.0-win64.zip`**。
2. 解壓至任意資料夾（例如 `C:\Tools\HP_CLEANER++`）。
3. 執行 **`HP_CLEANER++.exe`**。
4. 請勿刪除與執行檔同目錄的 **`i18n/`** 資料夾（語言檔必需）。

## 建議

- 首次清理或硬碟測試前，可先建立**系統還原點**。
- 需要完整 SMART / 硬碟健康資料時，請**以系統管理員身分執行**。
- 可在程式底部切換介面語言（繁中、簡中、英文、日文）。

## 疑難排解

| 現象 | 處理方式 |
|------|----------|
| 無法啟動 | 確認 Windows 10+（64 位元）、顯示與 DirectX 9 正常 |
| 語言無法切換 | 確認 `i18n/` 與 `.exe` 在同一目錄 |
| SMART 資料不完整 | 以系統管理員身分重新執行 |
| SmartScreen 阻擋 | 點 **詳細資訊** → **仍要執行**（或未來使用已簽章版本） |

## 更新

至 [Releases](https://github.com/Half-People/HP-CLEANER-/releases) 下載新版 zip，解壓至新資料夾或覆蓋 `.exe` 與 `i18n/`。使用者設定通常不在安裝目錄內，覆蓋 exe 一般不會遺失設定。

## 授權

Copyright © HalfPeople Studio。第三方授權見 [`third_party/README.zh-TW.md`](../third_party/README.zh-TW.md)。
