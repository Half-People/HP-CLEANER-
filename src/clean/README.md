<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# clean — Disk cleanup feature

The **Clean** tab helps **gamers** reclaim space from launchers and caches, and helps **developers** remove IDE and build artifacts—always with scan-before-delete confirmation.

---

## Structure

```
CleanTasks_*.cpp     ← task definitions (one category per file)
HCleanTaskCommon.*   ← shared scan/delete/size helpers
HCleanTask.*         ← abstract task interface (in core/)
ClearPage.cpp        ← page controller
ClearPageUI.*        ← ImGui cards, filters, detail modal
CleanHistory.*       ← persist completed runs
```

---

## Task categories (`CleanTasks_*.cpp`)

| File | Category examples |
|------|-------------------|
| `CleanTasks_System.cpp` | Windows Update cache, temp folders |
| `CleanTasks_Browser.cpp` | Chrome, Edge, Firefox caches |
| `CleanTasks_User.cpp` | User temp, recent docs |
| `CleanTasks_AppData.cpp` | AppData logs & caches |
| `CleanTasks_Developer.cpp` | IDE / SDK caches |
| `CleanTasks_Game.cpp` | Game launcher caches |
| `CleanTasks_Software.cpp` | Third-party app temp |
| `CleanTasks_Advanced.cpp` | Power-user / risky items |
| `CleanTasks_Deep.cpp` | Deep scan patterns |
| `CleanTasks_Discovery.cpp` | Discovered paths |
| `CleanTasks_Communication.cpp` | Chat app caches |

---

## Data flow

```
ClearPageUI
    │  I18N(task->GetName())
    ▼
HCleanTask subclass
    │  Scan() → GetSize() → Execute()
    ▼
HCleanTaskCommon helpers
    └──► CleanHistory (on success)
```

**Important:** `GetName()` returns a **zh-TW key**; translation happens in `ClearPageUI`, not inside the task.

---

## Key UI files

| File | Role |
|------|------|
| `ClearPageUI.cpp` | Category tabs, task cards, progress bars |
| `CleanItemWidget` | Single task card with checkbox + size |
| `RenderTaskDetailModal` | File list after scan |

---

## See also

- Module map: [../README.md](../README.md)
- 中文: [README.zh-TW.md](README.zh-TW.md)
