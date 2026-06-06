<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# i18n

Internationalization layer: built-in string tables plus optional JSON overrides.

## Key files

| File | Purpose |
|------|---------|
| `Hi18n.h` / `Hi18n.cpp` | `I18N()`, language switching, JSON pack loader |
| `Hi18nBuiltin.*` | Small fixed enum keys (nav labels, hero text) |
| `Hi18nBuiltinPages.*` | Large page string table (~3000 entries) |
| `Hi18nLangPicker.*` | In-app language combo UI |

## Runtime packs

Optional JSON files live in [`config/i18n`](../../config/i18n/README.md) and are copied next to the executable as `i18n/`.

Regenerate built-in tables with scripts in [`tools/`](../../tools/README.md).
