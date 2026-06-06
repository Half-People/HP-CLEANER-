<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# application

Process lifecycle and Windows shell integration.

## Key files

| File | Purpose |
|------|---------|
| `main.cpp` | WinMain entry, ImGui/D3D9 bootstrap |
| `HAppShell.*` | Main window loop and navigation shell |
| `HAppTray.*` | System tray icon and menu |
| `HAppSettings.*` | Persistent user settings |
| `HAppPaths.*` | Known folders and app data paths |
| `HAppLaunch.*` | CLI / secondary instance handling |
| `HAppSingleInstance.*` | Mutex-based single instance |
| `HElevationBroker.*` | UAC elevation helper |
| `HAppRegistration.*` | Startup / uninstall registry entries |
