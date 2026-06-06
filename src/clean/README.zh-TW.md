<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# clean — 磁碟清理功能

**清理**分頁協助**遊戲玩家**釋出啟動器與快取占用的空間，並協助**開發者**清除 IDE 與建置殘留；所有項目均支援刪除前掃描確認。

---

## 結構

```
CleanTasks_*.cpp     ← 任務定義（每檔一類）
HCleanTaskCommon.*   ← 共用掃描/刪除/容量
HCleanTask.*         ← 抽象介面（在 core/）
ClearPage.cpp        ← 分頁控制器
ClearPageUI.*        ← ImGui 卡片、篩選、詳情
CleanHistory.*       ← 完成紀錄持久化
```

---

## 任務分類（`CleanTasks_*.cpp`）

| 檔案 | 類別示例 |
|------|----------|
| `CleanTasks_System.cpp` | Windows 更新快取、系統 temp |
| `CleanTasks_Browser.cpp` | 瀏覽器快取 |
| `CleanTasks_User.cpp` | 使用者 temp、最近文件 |
| `CleanTasks_AppData.cpp` | AppData 日誌與快取 |
| `CleanTasks_Developer.cpp` | IDE / SDK 快取 |
| `CleanTasks_Game.cpp` | 遊戲平台快取 |
| `CleanTasks_Software.cpp` | 第三方軟體 temp |
| `CleanTasks_Advanced.cpp` | 進階 / 風險項 |
| `CleanTasks_Deep.cpp` | 深度掃描 |
| `CleanTasks_Discovery.cpp` | 探索到的路徑 |
| `CleanTasks_Communication.cpp` | 通訊軟體快取 |

---

## 資料流

```
ClearPageUI
    │  I18N(task->GetName())
    ▼
HCleanTask 子類別
    │  Scan() → GetSize() → Execute()
    ▼
HCleanTaskCommon
    └──► CleanHistory（成功後）
```

**重點：** `GetName()` 回傳 **繁中 key**；翻譯在 `ClearPageUI` 完成，不在任務內部。

---

## 主要 UI

| 檔案 | 角色 |
|------|------|
| `ClearPageUI.cpp` | 分類標籤、任務卡片、進度 |
| `CleanItemWidget` | 單張卡片（勾選 + 容量） |
| `RenderTaskDetailModal` | 掃描後檔案列表 |

---

## 延伸閱讀

- 模組地圖：[../README.zh-TW.md](../README.zh-TW.md)
- English: [README.md](README.md)
