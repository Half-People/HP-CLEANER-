<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <strong>繁體中文</strong>
</p>
<p align="center">
  <img src="./head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>專為遊戲玩家與開發者設計的 Windows 系統管理工具</sub></p>

---

# assets — 品牌與嵌入資源

**HP CLEANER++** 的視覺識別與二進位資源，經 [`HP_CLEANER++.rc`](../HP_CLEANER++.rc) 編入執行檔。  
本儲存庫所有說明文件頁首亦使用此處的 Logo 圖檔。

---

## 品牌 Logo（文件與應用程式）

| 檔案 | 代表 | 用途 |
|------|------|------|
| **`head.png`** | **HalfPeople Studio · HP CLEANER++**（合併橫幅） | 各 `README*.md` 頁首 |
| **`HP_Logo.png`** | **HalfPeople Studio**（發行商） | 應用內品牌 |
| **`Logo.png`** | **HP CLEANER++**（產品） | 應用主 Logo |
| **`Icon.ico`** | 應用程式圖示 | 工作列、捷徑、標題列 |

於儲存庫根目錄引用範例：

```html
<img src="assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
```

批次更新所有文件頁首：`python tools/inject_readme_branding.py`

---

## 全部嵌入檔案

| 檔案 | 資源 ID | 用途 |
|------|---------|------|
| `Icon.ico` | `IDI_ICON1` | 執行檔與捷徑圖示 |
| `Logo.png` | `IDB_PNG1` | 產品 Logo（HP CLEANER++） |
| `HP_Logo.png` | `IDB_PNG2` | 工作室 Logo（HalfPeople Studio） |
| `kaiu.ttf` | `IDR_FONT1` | 繁體中文介面字型 |

`HP_CLEANER++.rc` 內路徑相對於儲存庫根目錄：`assets\…`

建置前請確認四項檔案齊全，否則資源編譯將失敗。

---

## 延伸閱讀

- 產品總覽：[../README.zh-TW.md](../README.zh-TW.md)
- English: [README.md](README.md)
