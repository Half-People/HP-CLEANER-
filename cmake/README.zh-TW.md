<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# cmake — 建置模組

HP CLEANER++ 的 CMake 邏輯。根目錄 [`CMakeLists.txt`](../CMakeLists.txt) 只設定專案名稱並 include 本目錄檔案。

---

## 檔案一覽

| 檔案 | 職責 |
|------|------|
| [`HP_CLEANERSources.cmake`](HP_CLEANERSources.cmake) | 收集 `.cpp` 列表、include 目錄、統計數量 |
| [`HP_CLEANERTarget.cmake`](HP_CLEANERTarget.cmake) | 建立 `HP_CLEANER` 可執行檔、編譯/連結旗標、Windows 函式庫 |

---

## 編譯內容

| 來源群組 | 數量 | 路徑 |
|----------|------|------|
| 應用程式 | 65 | `src/*/*.cpp` |
| Dear ImGui | 7 | `third_party/imgui/*.cpp` |
| ImPlot | 3 | `third_party/implot/*.cpp` |
| 資源 | 1 | `HP_CLEANER++.rc` |
| **編譯單元合計** | **75** | |

輸出檔名：**`HP_CLEANER++`**（CMake target 名稱：`HP_CLEANER`）。

---

## Include 目錄

```
${CMAKE_SOURCE_DIR}                 ← resource.h
${CMAKE_SOURCE_DIR}/src
${CMAKE_SOURCE_DIR}/src/<module>/
third_party/imgui
third_party/implot
third_party/json/include
third_party/spdlog/include
third_party/stb
```

---

## 重要編譯 / 連結設定（MSVC）

| 設定 | 值 | 原因 |
|------|-----|------|
| C++ 標準 | 17 | 專案基線 |
| `/utf-8` | 開 | 正確處理中文原始碼與字串 |
| `/Zm1000` | 開 | 超大編譯單元需要更大堆積 |
| `/FS` | 開 | Release 平行編譯 PDB |
| `/bigobj` | 僅 `Hi18nBuiltinPages.cpp` | 約 3000 字串超過預設 section 上限 |
| `SPDLOG_HEADER_ONLY` | 定義 | 不需預編譯 spdlog.lib |
| `/ENTRY:mainCRTStartup` | 連結 | 使用 `main()` 而非 `WinMain` |
| Release 執行期 | `/MT` 靜態 | 獨立 EXE |

---

## 連結的函式庫

| 函式庫 | 用途 |
|--------|------|
| `d3d9` | Direct3D 9 渲染 |
| `dbghelp` | 崩潰 minidump |
| `psapi` | 行程 / 記憶體資訊 |
| `shell32` | 開啟資料夾、Shell 執行 |
| `ole32` | COM（圖示擷取等） |
| `version` | 檔案版本查詢 |
| `comctl32` | 通用控制項 |
| `shlwapi` | 路徑 helper |

---

## 建置後步驟

`HP_CLEANER_COPY_RUNTIME_ASSETS=ON`（預設）時：

```
config/i18n/  ──複製──►  build/<Config>/i18n/
```

程式執行時從 `<exe 目錄>/i18n/` 載入可選 JSON。

---

## 設定範例

```powershell
# 標準 Release x64
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Debug
cmake --build build --config Debug

# 關閉 i18n 複製
cmake -S . -B build -DHP_CLEANER_COPY_RUNTIME_ASSETS=OFF

# 清除後重新設定
Remove-Item -Recurse -Force build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

---

## 延伸閱讀

- 根目錄建置指南：[../README.zh-TW.md](../README.zh-TW.md)
- English: [README.md](README.md)
