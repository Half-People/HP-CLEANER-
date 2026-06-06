<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# cmake — Build modules

CMake logic for HP CLEANER++. The root [`CMakeLists.txt`](../CMakeLists.txt) only sets the project name and includes these files.

---

## Files

| File | Responsibility |
|------|----------------|
| [`HP_CLEANERSources.cmake`](HP_CLEANERSources.cmake) | Collects `.cpp` lists, include directories, source counts |
| [`HP_CLEANERTarget.cmake`](HP_CLEANERTarget.cmake) | Creates `HP_CLEANER` executable, compiler/link flags, Windows libs |

---

## What gets compiled

| Source group | Count | Path pattern |
|--------------|-------|--------------|
| Application | 65 | `src/*/*.cpp` |
| Dear ImGui | 7 | `third_party/imgui/*.cpp` |
| ImPlot | 3 | `third_party/implot/*.cpp` |
| Resources | 1 | `HP_CLEANER++.rc` |
| **Total TUs** | **75** | |

Output binary name: **`HP_CLEANER++`** (target id: `HP_CLEANER`).

---

## Include directories

```
${CMAKE_SOURCE_DIR}                 ← resource.h
${CMAKE_SOURCE_DIR}/src            ← flat headers
${CMAKE_SOURCE_DIR}/src/<module>/ ← per-feature folders
third_party/imgui
third_party/implot
third_party/json/include
third_party/spdlog/include
third_party/stb
```

---

## Key compiler / linker settings (MSVC)

| Setting | Value | Why |
|---------|-------|-----|
| C++ standard | 17 | Project baseline |
| `/utf-8` | on | Correct Chinese source & string literals |
| `/Zm1000` | on | Larger PCH heap for huge translation units |
| `/FS` | on | Parallel compile of PDB (Release) |
| `/bigobj` | `Hi18nBuiltinPages.cpp` only | Exceeds default section limit (~3000 strings) |
| `SPDLOG_HEADER_ONLY` | defined | No prebuilt spdlog.lib required |
| `/ENTRY:mainCRTStartup` | link | App uses `main()`, not `WinMain` |
| Runtime (Release) | `/MT` static | Standalone EXE |

---

## Linked libraries

| Library | Purpose |
|---------|---------|
| `d3d9` | Direct3D 9 rendering |
| `dbghelp` | Crash minidumps |
| `psapi` | Process / memory info |
| `shell32` | Shell execute, folder open |
| `ole32` | COM for icon extraction |
| `version` | File version queries |
| `comctl32` | Common controls |
| `shlwapi` | Path helpers |

---

## Post-build step

When `HP_CLEANER_COPY_RUNTIME_ASSETS=ON` (default):

```
config/i18n/  ──copy──►  build/<Config>/i18n/
```

The app loads optional JSON from `<exe-dir>/i18n/` at runtime.

---

## Configure examples

```powershell
# Standard Release x64
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Debug
cmake --build build --config Debug

# Disable i18n copy
cmake -S . -B build -DHP_CLEANER_COPY_RUNTIME_ASSETS=OFF

# Clean reconfigure
Remove-Item -Recurse -Force build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

---

## See also

- Root build guide: [../README.md](../README.md)
- 中文說明: [README.zh-TW.md](README.zh-TW.md)
