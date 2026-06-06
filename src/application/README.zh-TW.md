<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# application

行程生命週期與 Windows Shell 整合。

## 主要檔案

| 檔案 | 用途 |
|------|------|
| `main.cpp` | WinMain 進入點、ImGui/D3D9 初始化 |
| `HAppShell.*` | 主視窗迴圈與導覽 Shell |
| `HAppTray.*` | 系統匣圖示與選單 |
| `HAppSettings.*` | 使用者設定持久化 |
| `HAppPaths.*` | 已知資料夾與應用程式路徑 |
| `HAppLaunch.*` | 命令列 / 第二實例處理 |
| `HAppSingleInstance.*` | Mutex 單實例 |
| `HElevationBroker.*` | UAC 提升權限 |
| `HAppRegistration.*` | 開機 / 解除安裝登錄項目 |
