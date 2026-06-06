<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# src — Application source

All HP CLEANER++ logic lives here, split into **11 feature folders** (65 `.cpp` files).  
CMake adds every subfolder to the include path, so code still uses flat includes like `#include "HPage.h"`.

---

## Module map (quick reference)

```
src/
├── application/   Process entry, window shell, tray, settings, UAC
├── core/          HPage framework, logging, crash handler, embedded assets
├── i18n/          ~3000 built-in strings + language picker
├── mainpage/      Home tab — storage chart, memory trim
├── clean/         Cleanup tasks + Clear page UI
├── optimize/      Startup / services / network / presets
├── diskhealth/    SMART, NVMe, diagnostics UI
├── filemap/       Treemap + directory scan
├── about/         About tab, device info, log viewer
├── history/       Cleanup history timeline
└── ui/            Shared widgets, uninstall wizard
```

---

## Modules in detail

| Folder | Files (typical) | User-visible feature | Background work |
|--------|-----------------|----------------------|-----------------|
| [`application/`](application/README.md) | `main.cpp`, `HAppShell.*`, `HAppTray.*` | Window, tray icon, single instance | Elevation broker |
| [`core/`](core/README.md) | `HPage.*`, `HLog*.*`, `HCrash*.*` | All pages inherit from here | spdlog, minidump |
| [`i18n/`](i18n/README.md) | `Hi18n*.cpp` | Language menu | JSON pack loader |
| [`mainpage/`](mainpage/README.md) | `MainPageUI.*`, `MainPageDiskScan.*` | Home dashboard | Disk categorization scan |
| [`clean/`](clean/README.md) | `CleanTasks_*.cpp`, `ClearPageUI.*` | Cleanup cards & detail modal | Per-task size scan |
| [`optimize/`](optimize/README.md) | `OptimizePageUI.*`, `OptimizeScan.*` | Multi-tab optimizer | Startup/service enumeration |
| [`diskhealth/`](diskhealth/README.md) | `DiskHealthUI.*`, `DiskHealthScan.*` | SMART cards, charts | Worker-thread drive scan |
| [`filemap/`](filemap/README.md) | `FileMapUI.*`, `FileMapScan.*` | Treemap view | Folder size walk |
| [`about/`](about/README.md) | `AboutPageUI.*`, `AboutAppLog.*` | Version & logs | WMI / device queries |
| [`history/`](history/README.md) | `HistoryPage.cpp` | Past cleanups list | Reads `CleanHistory` |
| [`ui/`](ui/README.md) | `HUiTheme.h`, `HUninstallUI.*` | Shared styling, uninstall | — |

---

## How pages connect

```
HAppShell (application/)
    │
    ├─► MainPage      (mainpage/)  ──► MainPageUI
    ├─► ClearPage     (clean/)     ──► ClearPageUI + HCleanTask*
    ├─► OptimizePage  (optimize/)  ──► OptimizePageUI + OptimizeScan
    ├─► DiskHealthPage(diskhealth/)──► DiskHealthUI + DiskHealthScan
    ├─► FileMapPage   (filemap/)   ──► FileMapUI + FileMapScan
    ├─► HistoryPage   (history/)
    └─► AboutPage     (about/)     ──► AboutPageUI
```

Each `*Page.cpp` registers with the shell; `*UI.cpp` files contain ImGui drawing code.

---

## Coding conventions

| Topic | Rule |
|-------|------|
| **UI strings** | Store zh-TW text as keys; translate with `I18N()` / `I18NF()` at display |
| **Formatted text** | Use `SnprintfI18n` pattern or `I18NF("…%s…", arg)` — never pass `I18N()` directly as ImGui format string |
| **Snapshots** | Background threads write **keys** or raw data; UI thread translates |
| **Logging** | `HLOG_INFO(...)` via spdlog in `HPage.h` |
| **New clean task** | Add `CleanTasks_YourCategory.cpp`, register in registry, add keys to i18n table |

---

## File count by folder

| Folder | `.cpp` count (approx.) |
|--------|-------------------------|
| clean | 14 |
| optimize | 5 |
| core | 12 |
| application | 9 |
| diskhealth | 5 |
| i18n | 4 |
| filemap | 4 |
| mainpage | 4 |
| about | 4 |
| ui | 3 |
| history | 1 |

---

## See also

- Root overview: [../README.md](../README.md)
- Build system: [../cmake/README.md](../cmake/README.md)
- Translations: [../config/i18n/README.md](../config/i18n/README.md)
- 中文說明: [README.zh-TW.md](README.zh-TW.md)
