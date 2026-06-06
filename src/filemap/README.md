<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# filemap

Visual storage analysis: directory tree, treemap, and extension breakdown.

## Key files

| File | Purpose |
|------|---------|
| `FileMapScan.*` | Background folder size scan |
| `FileMapTree.*` | Hierarchical tree data |
| `FileMapUI.*` | Treemap, tree view, detail pane |
| `FileMapPage.cpp` | Page wiring |

Uses **stb_image** (via `third_party/stb`) for optional texture previews where applicable.
