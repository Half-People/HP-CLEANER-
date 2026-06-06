# HP CLEANER++ — Installation & first run

**English** | [繁體中文](INSTALL.zh-TW.md)

---

## Quick start

1. Download **`HP_CLEANER++-1.0.0-win64.zip`** from [Releases](https://github.com/Half-People/HP-CLEANER-/releases).
2. Extract to any folder (e.g. `C:\Tools\HP_CLEANER++`).
3. Run **`HP_CLEANER++.exe`**.
4. Keep the **`i18n/`** folder next to the executable (required for language packs).

## Recommended

- Create a **system restore point** before your first cleanup or disk test.
- **Run as administrator** for full SMART / disk health data.
- Change UI language from the app footer (zh-TW, zh-CN, en-US, ja-JP).

## Troubleshooting

| Issue | What to try |
|-------|-------------|
| App will not start | Windows 10+ (64-bit), working display / DirectX 9 |
| Language does not switch | Ensure `i18n/` is in the same folder as the `.exe` |
| Incomplete SMART data | Restart the app as administrator |
| Blocked by SmartScreen | Click **More info** → **Run anyway**, or use a signed build when available |

## Updating

Download the newer zip from [Releases](https://github.com/Half-People/HP-CLEANER-/releases), extract to a new folder or replace the `.exe` and `i18n/` folder. User settings are stored outside the install folder.

## License

Copyright © HalfPeople Studio. Third-party licenses: [`third_party/README.md`](../third_party/README.md).
