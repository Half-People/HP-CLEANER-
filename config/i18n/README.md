<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# i18n

Optional runtime language packs loaded from `<exe-dir>/i18n/`.

## Files

| File | Purpose |
|------|---------|
| `languages.json` | Lists available language codes and display names |
| `en-US.json`, `zh-CN.json`, `ja-JP.json`, … | Override / extend built-in strings |

Format (per file):

```json
{
  "language": "ja-JP",
  "name": "日本語",
  "strings": {
    "繁中原文 key": "Translated text"
  }
}
```

Missing keys fall back to built-in tables in `Hi18nBuiltinPages.cpp`.

Generate or audit packs with scripts in [`tools/`](../../tools/README.md).
