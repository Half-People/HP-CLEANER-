<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="./head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# assets — Branding & embedded resources

Visual identity and binary resources compiled into **HP CLEANER++** via [`HP_CLEANER++.rc`](../HP_CLEANER++.rc).  
These files also appear at the top of every documentation page in this repository.

---

## Brand logos (documentation & in-app)

| File | Identity | Used in |
|------|----------|---------|
| **`head.png`** | **HalfPeople Studio · HP CLEANER++** (combined banner) | All `README*.md` headers |
| **`HP_Logo.png`** | **HalfPeople Studio** (publisher) | In-app alternate branding |
| **`Logo.png`** | **HP CLEANER++** (product) | Primary in-app logo |
| **`Icon.ico`** | Application icon | Windows shell, taskbar, title bar |

Markdown example (repository root):

```html
<img src="assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
```

Regenerate headers across docs: `python tools/inject_readme_branding.py`

---

## All embedded files

| File | Resource ID | Purpose |
|------|-------------|---------|
| `Icon.ico` | `IDI_ICON1` | Executable and shortcut icon |
| `Logo.png` | `IDB_PNG1` | Product logo (HP CLEANER++) |
| `HP_Logo.png` | `IDB_PNG2` | Studio logo (HalfPeople Studio) |
| `kaiu.ttf` | `IDR_FONT1` | Traditional Chinese UI typeface |

Paths in `HP_CLEANER++.rc` are relative to the repository root: `assets\…`

Ensure all four files are present before building; missing assets will fail the resource compiler.

---

## See also

- Product overview: [../README.md](../README.md)
- 中文說明: [README.zh-TW.md](README.zh-TW.md)
