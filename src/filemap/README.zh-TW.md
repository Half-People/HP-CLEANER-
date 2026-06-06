<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# filemap

儲存視覺化分析：目錄樹、Treemap、副檔名統計。

## 主要檔案

| 檔案 | 用途 |
|------|------|
| `FileMapScan.*` | 背景資料夾大小掃描 |
| `FileMapTree.*` | 階層樹資料 |
| `FileMapUI.*` | Treemap、樹狀檢視、詳情窗格 |
| `FileMapPage.cpp` | 分頁連接 |

必要時透過 `third_party/stb` 的 stb_image 載入預覽紋理。
