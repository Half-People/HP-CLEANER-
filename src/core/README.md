<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# core

Shared infrastructure used by every feature page.

## Key files

| File | Purpose |
|------|---------|
| `HPage.*` | Base page type, logging macros, global ImGui helpers |
| `HLog*.*` | Ring buffer, pipe, optional console log viewer |
| `HCrashHandler.*` / `HCrashReportUI.*` | Minidump and crash dialog |
| `HAdminPrompt.*` | Administrator privilege prompts |
| `HRC_Assets.*` | Embedded fonts/images from resources |
| `HUserConfig.*` | User preference storage |
| `HCleanRegistry.*` | Registry clean helpers |
| `HCleanTask.*` | Abstract clean task base class |
| `ConfirmDeleteDirectorys_PopupPage.cpp` | Destructive action confirmation UI |
