<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# third_party — 內嵌第三方函式庫

所有外部函式庫都**隨 repo 一起發佈**，CMake **不會**在 configure 時下載。

---

## 套件總覽

| 目錄 | 專案 | 用途 | 授權 | 是否編譯 |
|------|------|------|------|----------|
| [`imgui/`](imgui/README.zh-TW.md) | [Dear ImGui](https://github.com/ocornut/imgui) | 全部 UI | MIT | 是（7 個 `.cpp`） |
| [`implot/`](implot/README.zh-TW.md) | [ImPlot](https://github.com/epezent/implot) | 硬碟健康圖表 | MIT | 是（3 個 `.cpp`） |
| [`json/`](json/README.zh-TW.md) | [nlohmann/json](https://github.com/nlohmann/json) | 設定、i18n、快照 | MIT | 僅標頭檔 |
| [`spdlog/`](spdlog/README.zh-TW.md) | [spdlog](https://github.com/gabime/spdlog) | 日誌 `HLOG_*` | MIT | 僅標頭檔* |
| [`stb/`](stb/README.zh-TW.md) | [stb](https://github.com/nothings/stb) | `stb_image` | 公有領域 | 僅標頭檔 |

\*本建置定義 `SPDLOG_HEADER_ONLY`，不連結 `.lib`。

---

## Include 路徑（CMake 設定）

```
third_party/imgui          →  #include <imgui.h>
third_party/implot         →  #include <implot.h>
third_party/json/include   →  #include <nlohmann/json.hpp>
third_party/spdlog/include →  #include <spdlog/spdlog.h>
third_party/stb            →  #include "stb_image.h"
```

---

## 升級檢查清單

更新 vendored 版本時：

1. 替換目錄內容
2. Release 全量建置 — 修正 ImGui API 變更
3. 測試硬碟健康圖表（ImPlot）與日誌（spdlog）
4. 必要時更新本 README 版本備註

---

## 延伸閱讀

- CMake 連結：[../cmake/README.zh-TW.md](../cmake/README.zh-TW.md)
- English: [README.md](README.md)
