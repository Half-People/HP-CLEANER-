<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# diskhealth — 硬碟健康與 SMART

**硬碟健康度**分頁：實體磁碟列表、SMART 屬性、圖表與可選診斷。

---

## 結構

| 檔案 | 角色 |
|------|------|
| `DiskHealthScan.*` | 背景掃描：IOCTL SMART、NVMe 健康日誌、USB 說明 |
| `DiskHealthUI.*` | 儀表、屬性卡片、ImPlot 圖、測試分頁 |
| `DiskHealthPage.cpp` | UI 首幀延後觸發掃描 |
| `DiskHealthTest.*` | 速度測試、壞軌矩陣工作 |

---

## 掃描流程

```
Init() ──不立即全碟掃描──► UI 首幀 ──► RequestRescan()
                                        │
                                        ▼
                              對每個 PhysicalDriveN
                                        │
                   ┌────────────────────┼────────────────────┐
                   ▼                    ▼                    ▼
              ATA SMART            NVMe 健康日誌         USB 狀態說明
                   │                    │                    │
                   └──────────► DriveInfo + smart_attributes[]
                                        │
                                        ▼
                              DiskHealthUI（顯示時 I18N）
```

---

## SMART 屬性名稱

| 儲存 | 格式 | 顯示 |
|------|------|------|
| 已知 ID | `SmartIdName()` 繁中 key | UI 中 `I18N(name_utf8)` |
| NVMe 合成屬性 | `AppendNvmeSmartAttr` 繁中 key | 同上 |
| 感測器 240–247 | 顯示時格式化 | `SnprintfI18n("溫度感測器 %d")` |

---

## UI 區塊

| 區塊 | 元件 |
|------|------|
| 磁碟列表 | 左側卡片 + 健康分數 |
| 詳情標題 | 型號、匯流排、槽位 |
| SMART 網格 | 每屬性一張 `DrawSmartAttributeCard` |
| 圖表 | ImPlot 風險長條 |
| 診斷 | 速度 / 壞軌背景工作 |

---

## 管理員權限

部分 SMART 資料需**系統管理員**；資料不完整時 UI 顯示 `needs_admin_hint`。

---

## 延伸閱讀

- ImPlot：[../../third_party/implot/README.zh-TW.md](../../third_party/implot/README.zh-TW.md)
- English: [README.md](README.md)
