<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# core

各功能分頁共用的基礎設施。

## 主要檔案

| 檔案 | 用途 |
|------|------|
| `HPage.*` | 頁面基底、日誌巨集、ImGui 共用 helper |
| `HLog*.*` | 環形緩衝、Pipe、可選主控台日誌 |
| `HCrashHandler.*` / `HCrashReportUI.*` | Minidump 與崩潰對話框 |
| `HAdminPrompt.*` | 系統管理員權限提示 |
| `HRC_Assets.*` | 資源嵌入的字型 / 圖片 |
| `HUserConfig.*` | 使用者偏好設定 |
| `HCleanRegistry.*` | 登錄清理輔助 |
| `HCleanTask.*` | 清理任務抽象基底 |
| `ConfirmDeleteDirectorys_PopupPage.cpp` | 危險操作確認 UI |
