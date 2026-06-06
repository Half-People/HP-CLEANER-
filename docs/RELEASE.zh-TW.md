# 發佈指南（維護者）

[English](RELEASE.md) | **繁體中文**

在 GitHub Releases 發佈 **HP CLEANER++** 的檢查清單。

---

## 打 tag 前

- [ ] 更新 [`CMakeLists.txt`](../CMakeLists.txt) 的 `project(VERSION …)`
- [ ] 更新 [`CHANGELOG.md`](../CHANGELOG.md)
- [ ] 必要時更新 [`docs/release-notes/`](../docs/release-notes/)
- [ ] Release 建置：`cmake --build build --config Release`
- [ ] 冒煙測試：啟動、語言切換、清理掃描、硬碟健康

## 打包

在 `Github/` 目錄：

```powershell
powershell -ExecutionPolicy Bypass -File tools\package-release.ps1
```

產物：`dist/HP_CLEANER++-{version}-win64.zip` 與 `.sha256`。

zip 內含：`HP_CLEANER++.exe`、`i18n/`、`INSTALL.md`、`INSTALL.zh-TW.md`。

## 建立 GitHub Release

1. Tag：`v{version}`（例如 `v1.0.0`）
2. 標題：`HP CLEANER++ v{version} — {簡短摘要}`
3. 正文：複製 [`docs/release-notes/v{version}.md`](release-notes/v1.0.0.md)（中英雙語）
4. 上傳 zip 與 sha256
5. 穩定版請勾選 **Latest**

## 提交時避免 cursoragent 署名

在 Cursor Agent 環境請用 `git commit-tree`（詳見 [`RELEASE.md`](RELEASE.md) 英文版），並關閉 **Cursor Settings → Agents → Attribution → Commit Attribution**。
