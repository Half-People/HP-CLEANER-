<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# optimize — 系統優化

**優化**分頁：啟動項、服務、電源、一鍵預設與網路工具。

---

## 結構

| 檔案 | 角色 |
|------|------|
| `OptimizeScan.*` | 主快照：啟動項、服務、電源、磁碟預設 |
| `OptimizeNetworkScan.*` | 介面卡、DNS、測速、連線、ARP |
| `OptimizePage.cpp` | 分頁註冊 |
| `OptimizePageUI.*` | 大型多分頁 ImGui UI（約 6000 行） |
| `OptimizeStartupIcon.*` | 啟動項 exe 圖示 |

---

## OptimizePageUI 子分頁

| 子分頁 | 資料來源 | 使用者操作 |
|--------|----------|------------|
| 概覽 | `OptimizeScan` 快照 | 快速指標、套用預設 |
| 啟動項 | `snap.startups` | 啟用/停用、tooltip |
| 服務 | `snap.services` | 啟動類型說明 |
| 系統效能 | 電源、視覺效果 | 效能選項 |
| 網路 | `OptimizeNetworkScan` | DNS 測試、路由、釋放 |
| 儲存設定 | 磁碟最佳化 | 分析 / 整理 |

顯示 helper：`UiTxt(zh_key)` 包一層 `I18N()` 處理快照字串。

---

## 背景掃描流程

```
OptimizeScan::RequestScan()
        │
        ▼ 工作執行緒
列舉 Run 登錄 + 啟動資料夾
查詢服務 · 讀取電源 GUID
        │
        ▼
快照 (mutex) ──► OptimizePageUI 每幀讀取
```

---

## 延伸閱讀

- 網路邏輯：`OptimizeNetworkScan.cpp`
- 模組地圖：[../README.zh-TW.md](../README.zh-TW.md)
- English: [README.md](README.md)
