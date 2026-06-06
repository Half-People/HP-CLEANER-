# HP CLEANER++ 工作筆記

> 最後更新：2026-06-06

## 當前任務

| 項目 | 狀態 |
|------|------|
| 內建翻譯表（3038 key） | ✅ EN/JA 幾乎全覆蓋 |
| **執行期未譯中文** | ✅ 本輪修復（顯示層 + 快照 key） |
| 硬碟健康度 en-US 崩潰 | ✅ format spec + I18NStr 回退 |
| Release 建置 | ✅ `-UseAltBuildRoot` |

## 本輪：未完成翻譯（根因與修復）

### 根因

內建表已譯完，但 UI 仍見繁中，主因是：

1. **快照存繁中 key，顯示時未 `I18N()`**  
   例：`SmartIdName()` → `attr.name_utf8` → `DiskHealthUI` 直接 `AddText`
2. **掃描執行緒誤用 `I18N()` 寫入快照**  
   語言切換後 stale；`AttrNameHas` 用譯文比對導致 en/ja 下 SMART 計數錯誤
3. **`ImGui::TextColored(color, I18N(...))` 未用 `"%s"` / `I18NF`**  
   譯文含 `%` 可能崩潰；部分標題未安全格式化

### 已修改檔案

| 檔案 | 變更 |
|------|------|
| `DiskHealthScan.cpp` | NVMe/SMART 名稱改存 zh key；`SnprintfI18n` 寫動態 status；`AttrNameHas` 用 zh token；`health_text` 存 key |
| `DiskHealthUI.cpp` | `StatusDisplayText` / `BusDisplayText` / `FormatSmartAttrTitle`；status、SMART 標題、匯流排顯示走 I18N |
| `OptimizeScan.cpp` | `source_label` 改存 u8 key（顯示層已有 `UiTxt`） |
| `OptimizePageUI.cpp` | 啟動影響標籤 `I18N()`；`TextColored` 改 `"%s"` / `I18NF` |
| `ClearPageUI.cpp` / `FileMapUI.cpp` / `HUninstallUI.cpp` / `HistoryPage.cpp` / `AboutAppLog.cpp` | 同上 TextColored 安全格式化 |
| `tools/audit_hardcoded_zh.py` | 移除誤 skip 專案根目錄 `HP_CLEANER++` |

### 資料流（硬碟健康 SMART 名稱）

```
SmartIdName(id) → zh key 存入 attr.name_utf8
       ↓
DiskHealthUI::FormatSmartAttrTitle → I18N(name) 或溫度感測器 SnprintfI18n
       ↓
DrawSmartAttributeCard 顯示譯文
```

### 仍可能顯示原文（預期）

- Windows 回傳的程式描述、路徑、產品名（非本 app 字串）
- 掃描進行中動態 status（`SnprintfI18n` 在掃描當下語言寫入；切語言需重掃）
- `MainPageDiskScan.cpp` 等少數 status key（MainPageUI 已 `I18N(snap.status_text)`）

## 翻譯工具鏈

```powershell
python tools\batch_translate_ja.py
python tools\apply_ja_manual.py
python tools\gen_i18n_pages.py
python tools\audit_i18n_coverage.py
python tools\audit_hardcoded_zh.py   # 找未包 I18N 的 u8"中文"
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Configuration Release -Platform x64 -UseAltBuildRoot
```

快取：`tools/ja_phrases_batch.json`

## 建置

```powershell
cd "f:\HalfPeopleStudioC++ Porject\HP_CLEANER++"
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Configuration Release -Platform x64 -UseAltBuildRoot
```

產物：`x64\Release\HP_CLEANER++.exe`

## 注意

1. 含 `%d`/`%s` 的譯文**不可調換佔位符順序**
2. ImGui：`"%s", I18N(...)` 或 `I18NF(...)` / `SnprintfI18n`
3. **快照欄位存 zh-TW key**，只在 UI 執行緒 `I18N()` / `UiTxt()`
4. `audit_hardcoded_zh.py` 命中多為 `GetName()` 等 key 來源（ClearPageUI 已 `I18N(item.title)`）

## AI 筆記

- 表譯完 ≠ UI 譯完：必須追蹤「存 key → 顯示 I18N」整條鏈
- `HealthLevelLabel()` 回傳已譯字串；`health_text` 改存 `HealthLevelKey()` 中文 key
- 溫度感測器 attr id 240–247：顯示時依 id 做 `SnprintfI18n(u8"溫度感測器 %d")`，不依賴快照內文
