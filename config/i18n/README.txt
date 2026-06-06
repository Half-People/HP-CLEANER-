HP CLEANER++ 語言包（JSON）

## 語言清單

路徑：%APPDATA%\HalfPeople\HP CLEANER++\config\i18n\languages.json

格式：
{
  "languages": [
    { "code": "zh-TW", "name": "繁體中文" },
    { "code": "ko-KR", "name": "한국어" }
  ]
}

- 可在此登記任意語言碼（不限內建四語）
- 若未提供此檔，程式會使用內建四語並自動掃描 i18n 目錄下其他 *.json 語言包

## 各語言翻譯包

路徑：%APPDATA%\HalfPeople\HP CLEANER++\config\i18n\<語言碼>.json

格式：
{
  "language": "ko-KR",
  "name": "한국어",
  "strings": {
    "NavMainPage": "홈",
    "系統清理": "시스템 정리",
    "優化掃描": "최적화 스캔"
  }
}

- key 可以是 Hi18n enum 名（見 Hi18n.cpp kKeyNames）或 UI 繁中原文
- 內建語言（zh-TW/zh-CN/en-US/ja-JP）未列出的字串使用 Hi18nBuiltin / Hi18nBuiltinPages
- 自訂語言（如 ko-KR）未列出的字串回退為繁中原文
- 範例：languages.json、ko-KR.json、ja-JP.json
- 重新生成內建表：python tools/gen_i18n_pages.py
