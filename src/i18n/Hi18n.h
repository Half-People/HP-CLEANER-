#pragma once
#ifndef HI18N_H
#define HI18N_H

#include <string>
#include <initializer_list>

// 多語言：內建翻譯見 Hi18nBuiltin.cpp；可透過 JSON 覆寫或新增語言
// 語言清單：config/i18n/languages.json + 各語言包 config/i18n/<語言碼>.json
namespace Hi18n {

	struct LanguageInfo {
		const char* code;
		const char* name;
	};

	enum class Key : int {
		NavMainPage,
		NavClearPage,
		NavOptimizePage,
		NavDiskHealthPage,
		NavFileMapPage,
		NavHistoryPage,
		NavAboutPage,

		LangLabel,
		LangZhTW,
		LangZhCN,
		LangEnUS,
		LangJaJP,

		SectionSystem,
		SectionRealtime,
		SectionSystemFiles,
		SectionDisks,
		FreeMemory,
		Processing,
		AdminCacheHint,
		MemoryHint,
		Rescan,
		GotoClearTool,
		Scanning,
		Used,
		Available,
		StorageOverviewFmt,
		StorageCategoryFmt,
		ScanningPctFmt,
		LegendSystem,
		LegendApps,
		LegendUser,
		LegendTemp,
		LegendOther,
		LegendFree,
		HeroSubtitle,
		HeroClear,
		HeroHistory,
		HeroDisk,
		StatReclaimable,
		StatReclaimHint,
		StatLastFreed,
		StatTotalFreed,
		StatNotCleanedYet,
		StatShowAfterScan,
		StatGotoClearAdjust,
		DisksRealtimeFmt,
		NoDrivesDetected,
		ActivityPctFmt,
		UsedSuffix,
		FooterMonitoringFmt,
		FooterNoHistory,

		Count,
	};

	void Init();
	void ReloadLanguageRegistry();

	int GetLanguageCount();
	const LanguageInfo* GetLanguage(int index);
	const char* GetCurrentLanguageCode();
	const char* GetCurrentLanguageName();
	void SetLanguage(const char* code);
	bool SetLanguageByIndex(int index);

	std::string GetLanguagePackPath(const char* code);

	const char* KeyName(Key key);
	bool KeyFromName(const char* name, Key* out_key);

	const char* Tr(Key key);
	// 以繁中原文為 key 查表（各頁面請用 I18N(u8"…")）
	const char* TrZh(const char* zh_tw_text);
	// 取得譯文 std::string（供 printf / ImGui 格式字串使用，避免 % 被誤解析）
	std::string I18NStr(const char* zh_tw_text);
	const char* JoinI18nParts(std::initializer_list<const char*> zh_tw_parts);
	std::wstring TrZhWide(const char* zh_tw_text);
	const wchar_t* TrZhWideCStr(const char* zh_tw_text);
	const char* NavLabel(const char* page_id);
	bool DrawLanguageCombo(const char* id, float width = 0.f);

	// 從 JSON 載入翻譯（覆寫內建預設；缺少的 key 仍用內建或繁中原文）
	// JSON 格式：{ "language": "ko-KR", "name": "한국어", "strings": { … } }
	bool LoadLanguagePackFromFile(const char* file_path);
	bool ReloadLanguagePack();
	bool ExportLanguagePackTemplate(const char* code, const char* file_path);

} // namespace Hi18n

#define HTR(key) Hi18n::Tr(Hi18n::Key::key)
#define I18N(text) Hi18n::TrZh(text)
#define I18NF(text) Hi18n::I18NStr(text).c_str()
#define I18N_JOIN(...) Hi18n::JoinI18nParts({ __VA_ARGS__ })
#define W18N(text) Hi18n::TrZhWideCStr(text)

#endif
