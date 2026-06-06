<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# HP CLEANER++

**HP CLEANER++** is a desktop utility for Windows that helps you **reclaim storage**, **tune system performance**, and **monitor drive health**—without unnecessary complexity. It is built for users who depend on their PC every day: **game players** with large libraries and launchers, and **software developers** with heavy toolchains and local builds.

---

## Who this is for

| Audience | Typical goals | How HP CLEANER++ helps |
|----------|---------------|-------------------------|
| **Gamers** | Free space for games, faster boot, stable drives | Clean launcher & shader caches, trim startup bloat, SMART health before installs |
| **Developers** | Fast SSD, clean dev caches, predictable network | Remove IDE/build/temp artifacts, review startup & services, DNS & adapter tools |
| **Power users** | Full visibility and control | File treemap, detailed logs, multi-language UI, tray quick access |

---

## Core capabilities

| Area | What you get |
|------|----------------|
| **Storage overview** | See what uses disk space; category breakdown on the home page |
| **Smart cleanup** | Grouped tasks (system, browsers, games, dev tools) with scan-before-delete |
| **Performance tuning** | Startup items, background services, power plan, one-click presets |
| **Disk health** | SMART / NVMe attributes, health score, optional speed & surface tests |
| **File map** | Treemap and tree view for large folders |
| **History & logs** | Cleanup records and in-app diagnostic log |
| **Languages** | Built-in **zh-TW**, **zh-CN**, **en-US**, **ja-JP**; optional JSON language packs |

---

## System requirements

| Item | Requirement |
|------|-------------|
| Operating system | Windows 10 or later (64-bit recommended) |
| RAM | 4 GB minimum; 8 GB+ recommended for large scans |
| Display | 1280×720 or higher |
| Privileges | Standard user; **Run as administrator** recommended for full SMART data |

---

## Getting started

### Option A — Build from source (this repository)

**Prerequisites:** Visual Studio 2022 (Desktop development with C++), CMake 3.20+, Windows SDK.

```powershell
cd path\to\HP_CLEANER++\Github
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\HP_CLEANER++.exe
```

After build, language files are copied to `build/Release/i18n/` automatically.

### Option B — Releases

When available, download the latest **`HP_CLEANER++.exe`** from the [Releases](https://github.com/YOUR_ORG/HP_CLEANER/releases) page (replace with your GitHub URL after publish).

---

## Quick tour

```
┌──────────────────────────────────────────────────────────────────┐
│                        HP CLEANER++                              │
├─────────┬─────────┬─────────┬─────────┬──────────────────────────┤
│  Home   │  Clean  │ Optimize│  Disk   │  File map                │
│  Storage│  Safe   │ Startup │  Health │  Visual size analysis    │
│  Memory │  tasks  │ Network │  SMART  │                          │
├─────────┴─────────┴─────────┴─────────┴──────────────────────────┤
│  History · About · Uninstall assistant · System tray               │
└──────────────────────────────────────────────────────────────────┘
```

---

## Repository guide (contributors)

| Folder | Contents |
|--------|----------|
| [`src/`](src/README.md) | Application source by feature |
| [`assets/`](assets/README.md) | Logos, icon, fonts (embedded in the executable) |
| [`config/i18n/`](config/i18n/README.md) | Optional runtime translation JSON |
| [`cmake/`](cmake/README.md) | Build configuration |
| [`tools/`](tools/README.md) | Translation and maintenance scripts |
| [`third_party/`](third_party/README.md) | Bundled libraries (ImGui, json, etc.) |

Full documentation index and build troubleshooting: see sections in [README.zh-TW.md](README.zh-TW.md) (中文).

---

## License

Copyright © 2026 **HalfPeople Studio**. All rights reserved.

Third-party licenses are listed in [`third_party/README.md`](third_party/README.md).

---

<p align="center">
  <sub>HalfPeople Studio · HP CLEANER++ · Built for gamers and developers on Windows</sub>
</p>
