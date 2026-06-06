<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# config

Runtime configuration shipped beside the executable (not compiled in).

## Subdirectories

| Path | Description |
|------|-------------|
| [`i18n/`](i18n/README.md) | Optional JSON language packs and `languages.json` registry |

CMake copies `config/i18n` → `<exe-dir>/i18n/` on post-build when `HP_CLEANER_COPY_RUNTIME_ASSETS=ON`.

Built-in translations in [`src/i18n`](../src/i18n/README.md) are always available even without these files.
