#include "Hi18n.h"
#include "Hi18nBuiltin.h"
#include "Hi18nBuiltinPages.h"
#include "HAppSettings.h"
#include "HAppPaths.h"
#include "HPage.h"
#include "HAppShell.h"
#include "HAppTray.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace Hi18n {
	namespace {
		static const char* kKeyNames[] = {
			"NavMainPage",
			"NavClearPage",
			"NavOptimizePage",
			"NavDiskHealthPage",
			"NavFileMapPage",
			"NavHistoryPage",
			"NavAboutPage",
			"LangLabel",
			"LangZhTW",
			"LangZhCN",
			"LangEnUS",
			"LangJaJP",
			"SectionSystem",
			"SectionRealtime",
			"SectionSystemFiles",
			"SectionDisks",
			"FreeMemory",
			"Processing",
			"AdminCacheHint",
			"MemoryHint",
			"Rescan",
			"GotoClearTool",
			"Scanning",
			"Used",
			"Available",
			"StorageOverviewFmt",
			"StorageCategoryFmt",
			"ScanningPctFmt",
			"LegendSystem",
			"LegendApps",
			"LegendUser",
			"LegendTemp",
			"LegendOther",
			"LegendFree",
			"HeroSubtitle",
			"HeroClear",
			"HeroHistory",
			"HeroDisk",
			"StatReclaimable",
			"StatReclaimHint",
			"StatLastFreed",
			"StatTotalFreed",
			"StatNotCleanedYet",
			"StatShowAfterScan",
			"StatGotoClearAdjust",
			"DisksRealtimeFmt",
			"NoDrivesDetected",
			"ActivityPctFmt",
			"UsedSuffix",
			"FooterMonitoringFmt",
			"FooterNoHistory",
		};

		static_assert(static_cast<int>(Key::Count) == static_cast<int>(sizeof(kKeyNames) / sizeof(kKeyNames[0])),
			"Hi18n key/name mismatch");

		struct LangEntry {
			std::string code;
			std::string name;
		};

		struct ZhEntry {
			std::string text[static_cast<size_t>(Hi18nBuiltin::Slot::Count)];
		};

		std::string g_current_code = "zh-TW";
		bool g_i18n_ready = false;
		std::vector<LangEntry> g_languages;
		std::unordered_map<std::string, std::string> g_active_strings;
		std::vector<std::string> g_key_overrides;
		std::unordered_map<std::string, ZhEntry> g_zh_table;

		void ClearKeyOverrides()
		{
			g_key_overrides.assign(static_cast<size_t>(Key::Count), std::string());
		}

		void RegisterZhRow(const char* zh_tw, const char* zh_cn,
			const char* en_us, const char* ja_jp)
		{
			if (zh_tw == nullptr || zh_tw[0] == '\0') {
				return;
			}
			ZhEntry entry;
			entry.text[static_cast<size_t>(Hi18nBuiltin::Slot::ZhTW)] = zh_tw;
			entry.text[static_cast<size_t>(Hi18nBuiltin::Slot::ZhCN)] =
				(zh_cn != nullptr && zh_cn[0] != '\0') ? zh_cn : zh_tw;
			entry.text[static_cast<size_t>(Hi18nBuiltin::Slot::EnUS)] =
				(en_us != nullptr && en_us[0] != '\0') ? en_us : zh_tw;
			entry.text[static_cast<size_t>(Hi18nBuiltin::Slot::JaJP)] =
				(ja_jp != nullptr && ja_jp[0] != '\0') ? ja_jp : zh_tw;
			g_zh_table[zh_tw] = std::move(entry);
		}

		void BuildBuiltinZhTable()
		{
			g_zh_table.clear();
			for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
				const Key key = static_cast<Key>(i);
				RegisterZhRow(
					Hi18nBuiltin::Get(Hi18nBuiltin::Slot::ZhTW, key),
					Hi18nBuiltin::Get(Hi18nBuiltin::Slot::ZhCN, key),
					Hi18nBuiltin::Get(Hi18nBuiltin::Slot::EnUS, key),
					Hi18nBuiltin::Get(Hi18nBuiltin::Slot::JaJP, key));
			}
			const Hi18nBuiltinPages::Row* rows = Hi18nBuiltinPages::Table();
			const int row_count = Hi18nBuiltinPages::Count();
			for (int i = 0; i < row_count; ++i) {
				RegisterZhRow(rows[i].zh_tw, rows[i].zh_cn, rows[i].en_us, rows[i].ja_jp);
			}
		}

		std::string GetI18nDir()
		{
			std::string dir = HAppPaths::GetConfigDir();
			if (!dir.empty()) {
				dir += "\\i18n";
			}
			else {
				dir = "i18n";
			}
			return dir;
		}

		bool EnsureI18nDir()
		{
			HAppPaths::EnsureAppDataDirs();
			const std::string dir = GetI18nDir();
			if (dir.empty()) {
				return false;
			}
			if (CreateDirectoryA(dir.c_str(), nullptr)) {
				return true;
			}
			return GetLastError() == ERROR_ALREADY_EXISTS;
		}

		std::string LanguagesManifestPath()
		{
			return GetI18nDir() + "\\languages.json";
		}

		bool LanguageCodesEqual(const char* a, const char* b)
		{
			if (a == nullptr || b == nullptr) {
				return false;
			}
			if (_stricmp(a, b) == 0) {
				return true;
			}
			std::string na(a);
			std::string nb(b);
			for (char& c : na) {
				if (c == '_') {
					c = '-';
				}
			}
			for (char& c : nb) {
				if (c == '_') {
					c = '-';
				}
			}
			return _stricmp(na.c_str(), nb.c_str()) == 0;
		}

		int FindLanguageIndex(const char* code)
		{
			if (code == nullptr || code[0] == '\0') {
				return -1;
			}
			for (int i = 0; i < static_cast<int>(g_languages.size()); ++i) {
				if (LanguageCodesEqual(g_languages[static_cast<size_t>(i)].code.c_str(), code)) {
					return i;
				}
			}
			return -1;
		}

		void AddLanguageIfMissing(const std::string& code, const std::string& name)
		{
			if (code.empty()) {
				return;
			}
			if (FindLanguageIndex(code.c_str()) >= 0) {
				return;
			}
			LangEntry entry;
			entry.code = code;
			entry.name = name.empty() ? code : name;
			g_languages.push_back(std::move(entry));
		}

		void AddDefaultLanguages()
		{
			g_languages.clear();
			AddLanguageIfMissing("zh-TW", "繁體中文");
			AddLanguageIfMissing("zh-CN", "简体中文");
			AddLanguageIfMissing("en-US", "English");
			AddLanguageIfMissing("ja-JP", "日本語");
		}

		bool TryReadPackMeta(const std::string& file_path, std::string* out_code, std::string* out_name)
		{
			if (out_code == nullptr || out_name == nullptr) {
				return false;
			}
			std::ifstream in(file_path);
			if (!in.is_open()) {
				return false;
			}
			try {
				nlohmann::json root;
				in >> root;
				if (root.contains("language") && root["language"].is_string()) {
					*out_code = root["language"].get<std::string>();
				}
				if (root.contains("name") && root["name"].is_string()) {
					*out_name = root["name"].get<std::string>();
				}
				return !out_code->empty();
			}
			catch (...) {
				return false;
			}
		}

		void ScanLanguagePackFiles()
		{
			const std::string dir = GetI18nDir();
			const std::string pattern = dir + "\\*.json";
			WIN32_FIND_DATAA fd{};
			const HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
			if (h == INVALID_HANDLE_VALUE) {
				return;
			}
			do {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					continue;
				}
				const char* fname = fd.cFileName;
				if (_stricmp(fname, "languages.json") == 0) {
					continue;
				}
				std::string code;
				std::string name;
				const std::string path = dir + "\\" + fname;
				if (!TryReadPackMeta(path, &code, &name)) {
					const size_t dot = std::string(fname).rfind('.');
					if (dot != std::string::npos && dot > 0) {
						code = std::string(fname, 0, dot);
					}
				}
				if (code.empty()) {
					continue;
				}
				AddLanguageIfMissing(code, name);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}

		bool WriteDefaultLanguagesManifest()
		{
			nlohmann::json root;
			nlohmann::json langs = nlohmann::json::array();
			for (const LangEntry& entry : g_languages) {
				nlohmann::json item;
				item["code"] = entry.code;
				item["name"] = entry.name;
				langs.push_back(item);
			}
			root["languages"] = langs;
			const std::string path = LanguagesManifestPath();
			std::ofstream out(path, std::ios::trunc);
			if (!out.is_open()) {
				return false;
			}
			out << root.dump(2);
			return true;
		}

		const char* DetectSystemLanguageCode()
		{
			const LANGID lang = GetUserDefaultUILanguage();
			const WORD primary = PRIMARYLANGID(lang);
			const WORD sub = SUBLANGID(lang);
			if (primary == LANG_CHINESE) {
				if (sub == SUBLANG_CHINESE_SIMPLIFIED
					|| sub == SUBLANG_CHINESE_SINGAPORE) {
					return "zh-CN";
				}
				return "zh-TW";
			}
			if (primary == LANG_JAPANESE) {
				return "ja-JP";
			}
			return "en-US";
		}

		void PopulateBuiltinActiveStrings(int builtin_slot)
		{
			if (builtin_slot < 0
				|| builtin_slot >= static_cast<int>(Hi18nBuiltin::Slot::Count)) {
				return;
			}
			for (const auto& pair : g_zh_table) {
				const ZhEntry& entry = pair.second;
				g_active_strings[pair.first] = entry.text[static_cast<size_t>(builtin_slot)];
			}
		}

		bool ApplyJsonStrings(const nlohmann::json& strings_obj, const char* expect_code)
		{
			if (!strings_obj.is_object()) {
				return false;
			}
			int applied = 0;
			for (auto it = strings_obj.begin(); it != strings_obj.end(); ++it) {
				if (!it.value().is_string()) {
					continue;
				}
				const std::string json_key = it.key();
				const std::string json_val = it.value().get<std::string>();
				Key key = Key::Count;
				if (KeyFromName(json_key.c_str(), &key)) {
					const int idx = static_cast<int>(key);
					g_key_overrides[static_cast<size_t>(idx)] = json_val;
					const char* zh_tw = Hi18nBuiltin::Get(Hi18nBuiltin::Slot::ZhTW, key);
					if (zh_tw != nullptr && zh_tw[0] != '\0') {
						g_active_strings[zh_tw] = json_val;
					}
					++applied;
					continue;
				}
				auto map_it = g_zh_table.find(json_key);
				if (map_it != g_zh_table.end()) {
					g_active_strings[json_key] = json_val;
					++applied;
					continue;
				}
				g_active_strings[json_key] = json_val;
				++applied;
				HLOG_DEBUG("Hi18n: JSON key '{}' applied as custom entry", json_key);
			}
			if (expect_code != nullptr && expect_code[0] != '\0') {
				HLOG_INFO("Hi18n: loaded {} overrides from pack ({})", applied, expect_code);
			}
			else {
				HLOG_INFO("Hi18n: loaded {} JSON overrides", applied);
			}
			return applied > 0;
		}

		void ApplyLanguagePack(const char* code)
		{
			BuildBuiltinZhTable();
			g_active_strings.clear();
			ClearKeyOverrides();

			const int builtin_slot = Hi18nBuiltin::SlotFromCode(code);
			PopulateBuiltinActiveStrings(builtin_slot);

			EnsureI18nDir();
			const std::string path = GetLanguagePackPath(code);
			LoadLanguagePackFromFile(path.c_str());
		}

		const char* ResolveLanguageCode(const char* code)
		{
			if (code == nullptr || code[0] == '\0') {
				return "zh-TW";
			}
			const int known_idx = FindLanguageIndex(code);
			if (known_idx >= 0) {
				return g_languages[static_cast<size_t>(known_idx)].code.c_str();
			}
			std::string discovered_code;
			std::string discovered_name;
			const std::string path = GetLanguagePackPath(code);
			if (TryReadPackMeta(path, &discovered_code, &discovered_name)) {
				AddLanguageIfMissing(discovered_code, discovered_name);
				return g_languages.back().code.c_str();
			}
			return "zh-TW";
		}
	}

	void ReloadLanguageRegistry()
	{
		EnsureI18nDir();
		AddDefaultLanguages();

		const std::string manifest_path = LanguagesManifestPath();
		std::ifstream in(manifest_path);
		if (in.is_open()) {
			try {
				nlohmann::json root;
				in >> root;
				if (root.contains("languages") && root["languages"].is_array()) {
					for (const auto& item : root["languages"]) {
						if (!item.is_object()) {
							continue;
						}
						std::string code;
						std::string name;
						if (item.contains("code") && item["code"].is_string()) {
							code = item["code"].get<std::string>();
						}
						if (item.contains("name") && item["name"].is_string()) {
							name = item["name"].get<std::string>();
						}
						AddLanguageIfMissing(code, name);
					}
				}
			}
			catch (const std::exception& ex) {
				HLOG_WARN("Hi18n: failed to parse languages.json ({})", ex.what());
			}
		}

		ScanLanguagePackFiles();

		if (!in.is_open()) {
			WriteDefaultLanguagesManifest();
		}
	}

	void Init()
	{
		g_i18n_ready = false;
		ReloadLanguageRegistry();
		const char* saved = HAppSettingsGetLanguageCode();
		if (saved != nullptr && saved[0] != '\0') {
			g_current_code = ResolveLanguageCode(saved);
		}
		else {
			g_current_code = ResolveLanguageCode(DetectSystemLanguageCode());
		}
		ApplyLanguagePack(g_current_code.c_str());
		g_i18n_ready = true;
		HLOG_INFO("Hi18n: language={}", g_current_code);
	}

	int GetLanguageCount()
	{
		return static_cast<int>(g_languages.size());
	}

	const LanguageInfo* GetLanguage(int index)
	{
		if (index < 0 || index >= static_cast<int>(g_languages.size())) {
			return nullptr;
		}
		static thread_local LanguageInfo info;
		static thread_local std::string tl_code;
		static thread_local std::string tl_name;
		const LangEntry& entry = g_languages[static_cast<size_t>(index)];
		tl_code = entry.code;
		tl_name = entry.name;
		info.code = tl_code.c_str();
		info.name = tl_name.c_str();
		return &info;
	}

	const char* GetCurrentLanguageCode()
	{
		return g_current_code.c_str();
	}

	const char* GetCurrentLanguageName()
	{
		const int idx = FindLanguageIndex(g_current_code.c_str());
		if (idx >= 0) {
			return g_languages[static_cast<size_t>(idx)].name.c_str();
		}
		return g_current_code.c_str();
	}

	void SetLanguage(const char* code)
	{
		const char* resolved = ResolveLanguageCode(code);
		if (LanguageCodesEqual(g_current_code.c_str(), resolved)) {
			return;
		}
		g_current_code = resolved;
		HAppSettingsSetLanguageCode(g_current_code.c_str());
		ApplyLanguagePack(g_current_code.c_str());
		HAppShellUpdateWindowTitle();
		HAppTrayRebuildMenu();
		HLOG_INFO("Hi18n: language changed to {}", g_current_code);
	}

	bool SetLanguageByIndex(int index)
	{
		const LanguageInfo* info = GetLanguage(index);
		if (info == nullptr || info->code == nullptr || info->code[0] == '\0') {
			return false;
		}
		SetLanguage(info->code);
		return true;
	}

	std::string GetLanguagePackPath(const char* code)
	{
		std::string path = GetI18nDir();
		path += "\\";
		if (code != nullptr && code[0] != '\0') {
			path += code;
		}
		else {
			path += g_current_code;
		}
		path += ".json";
		return path;
	}

	const char* KeyName(Key key)
	{
		const int idx = static_cast<int>(key);
		if (idx < 0 || idx >= static_cast<int>(Key::Count)) {
			return "";
		}
		return kKeyNames[idx];
	}

	bool KeyFromName(const char* name, Key* out_key)
	{
		if (name == nullptr || out_key == nullptr) {
			return false;
		}
		for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
			if (_stricmp(name, kKeyNames[i]) == 0) {
				*out_key = static_cast<Key>(i);
				return true;
			}
		}
		return false;
	}

	const char* Tr(Key key)
	{
		if (!g_i18n_ready) {
			const int idx = static_cast<int>(key);
			if (idx >= 0 && idx < static_cast<int>(Key::Count)) {
				return Hi18nBuiltin::Get(Hi18nBuiltin::Slot::ZhTW, key);
			}
			return "";
		}
		const int idx = static_cast<int>(key);
		if (idx < 0 || idx >= static_cast<int>(Key::Count)) {
			return "";
		}
		if (idx < static_cast<int>(g_key_overrides.size())
			&& !g_key_overrides[static_cast<size_t>(idx)].empty()) {
			return g_key_overrides[static_cast<size_t>(idx)].c_str();
		}
		const char* zh_tw = Hi18nBuiltin::Get(Hi18nBuiltin::Slot::ZhTW, key);
		if (zh_tw != nullptr && zh_tw[0] != '\0') {
			return TrZh(zh_tw);
		}
		const int builtin_slot = Hi18nBuiltin::SlotFromCode(g_current_code.c_str());
		if (builtin_slot >= 0) {
			return Hi18nBuiltin::Get(static_cast<Hi18nBuiltin::Slot>(builtin_slot), key);
		}
		return zh_tw;
	}

	namespace {
		void CollectFormatSpecs(const char* text, std::vector<char>& out)
		{
			out.clear();
			if (text == nullptr) {
				return;
			}
			for (const char* p = text; *p != '\0'; ++p) {
				if (*p != '%') {
					continue;
				}
				if (p[1] == '%') {
					++p;
					continue;
				}
				const char* q = p + 1;
				while (*q != '\0' && strchr("-+#0 ", *q) != nullptr) {
					++q;
				}
				while (*q != '\0' && (strchr("0123456789", *q) != nullptr || *q == '*')) {
					++q;
				}
				if (*q == '.') {
					++q;
					while (*q != '\0' && (strchr("0123456789", *q) != nullptr || *q == '*')) {
						++q;
					}
				}
				while (*q != '\0' && strchr("hlLjzt", *q) != nullptr) {
					++q;
				}
				if (*q != '\0') {
					out.push_back(*q);
					p = q;
				}
			}
		}

		bool SameFormatSpecs(const char* key, const char* translated)
		{
			if (key == nullptr || translated == nullptr) {
				return key == translated;
			}
			if (strchr(key, '%') == nullptr) {
				return true;
			}
			std::vector<char> key_specs;
			std::vector<char> tr_specs;
			CollectFormatSpecs(key, key_specs);
			CollectFormatSpecs(translated, tr_specs);
			return key_specs == tr_specs;
		}
	} // namespace

	const char* TrZh(const char* zh_tw_text)
	{
		if (zh_tw_text == nullptr || zh_tw_text[0] == '\0') {
			return "";
		}
		if (!g_i18n_ready) {
			return zh_tw_text;
		}
		const auto it = g_active_strings.find(zh_tw_text);
		if (it != g_active_strings.end() && !it->second.empty()) {
			return it->second.c_str();
		}
		return zh_tw_text;
	}

	std::string I18NStr(const char* zh_tw_text)
	{
		if (zh_tw_text == nullptr || zh_tw_text[0] == '\0') {
			return {};
		}
		const char* translated = TrZh(zh_tw_text);
		if (translated != nullptr && translated[0] != '\0'
			&& SameFormatSpecs(zh_tw_text, translated)) {
			return std::string(translated);
		}
		return std::string(zh_tw_text);
	}

	const char* JoinI18nParts(std::initializer_list<const char*> zh_tw_parts)
	{
		static thread_local std::string joined;
		joined.clear();
		for (const char* part : zh_tw_parts) {
			if (part != nullptr && part[0] != '\0') {
				joined += TrZh(part);
			}
		}
		return joined.c_str();
	}

	std::wstring TrZhWide(const char* zh_tw_text)
	{
		const char* utf8 = TrZh(zh_tw_text);
		if (utf8 == nullptr || utf8[0] == '\0') {
			return {};
		}
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
		if (wlen <= 0) {
			return {};
		}
		std::vector<wchar_t> buf(static_cast<size_t>(wlen), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf.data(), wlen);
		return std::wstring(buf.data());
	}

	const wchar_t* TrZhWideCStr(const char* zh_tw_text)
	{
		static thread_local std::wstring wide;
		wide = TrZhWide(zh_tw_text);
		return wide.c_str();
	}

	bool LoadLanguagePackFromFile(const char* file_path)
	{
		if (file_path == nullptr || file_path[0] == '\0') {
			return false;
		}
		std::ifstream in(file_path);
		if (!in.is_open()) {
			HLOG_DEBUG("Hi18n: language pack not found {}", file_path);
			return false;
		}
		try {
			nlohmann::json root;
			in >> root;
			const char* pack_lang = nullptr;
			std::string pack_lang_storage;
			std::string pack_name_storage;
			if (root.contains("language") && root["language"].is_string()) {
				pack_lang_storage = root["language"].get<std::string>();
				pack_lang = pack_lang_storage.c_str();
				if (!LanguageCodesEqual(pack_lang, g_current_code.c_str())) {
					HLOG_WARN("Hi18n: pack language {} differs from current {}",
						pack_lang, g_current_code);
				}
				if (root.contains("name") && root["name"].is_string()) {
					pack_name_storage = root["name"].get<std::string>();
					AddLanguageIfMissing(pack_lang_storage, pack_name_storage);
				}
			}
			if (!root.contains("strings") || !root["strings"].is_object()) {
				HLOG_WARN("Hi18n: invalid pack (missing strings object) {}", file_path);
				return false;
			}
			return ApplyJsonStrings(root["strings"], pack_lang);
		}
		catch (const std::exception& ex) {
			HLOG_WARN("Hi18n: failed to parse {} ({})", file_path, ex.what());
			return false;
		}
	}

	bool ReloadLanguagePack()
	{
		ApplyLanguagePack(g_current_code.c_str());
		return true;
	}

	bool ExportLanguagePackTemplate(const char* code, const char* file_path)
	{
		if (file_path == nullptr || file_path[0] == '\0') {
			return false;
		}
		const char* export_code = (code != nullptr && code[0] != '\0') ? code : g_current_code.c_str();
		const std::string prev_code = g_current_code;
		ApplyLanguagePack(export_code);

		nlohmann::json root;
		root["language"] = export_code;
		const int lang_idx = FindLanguageIndex(export_code);
		if (lang_idx >= 0) {
			root["name"] = g_languages[static_cast<size_t>(lang_idx)].name;
		}
		nlohmann::json strings = nlohmann::json::object();
		for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
			const Key key = static_cast<Key>(i);
			strings[kKeyNames[i]] = Tr(key);
		}
		for (const auto& pair : g_zh_table) {
			strings[pair.first] = TrZh(pair.first.c_str());
		}
		root["strings"] = strings;

		std::ofstream out(file_path, std::ios::trunc);
		if (!out.is_open()) {
			HLOG_WARN("Hi18n: export failed {}", file_path);
			ApplyLanguagePack(prev_code.c_str());
			g_current_code = prev_code;
			return false;
		}
		out << root.dump(2);
		ApplyLanguagePack(prev_code.c_str());
		g_current_code = prev_code;
		HLOG_INFO("Hi18n: exported template {} ({})", file_path, export_code);
		return true;
	}

	const char* NavLabel(const char* page_id)
	{
		if (page_id == nullptr) {
			return "";
		}
		if (strcmp(page_id, "MainPage") == 0) {
			return Tr(Key::NavMainPage);
		}
		if (strcmp(page_id, "ClearPage") == 0) {
			return Tr(Key::NavClearPage);
		}
		if (strcmp(page_id, "OptimizePage") == 0) {
			return Tr(Key::NavOptimizePage);
		}
		if (strcmp(page_id, "DiskHealthPage") == 0) {
			return Tr(Key::NavDiskHealthPage);
		}
		if (strcmp(page_id, "FileMapPage") == 0) {
			return Tr(Key::NavFileMapPage);
		}
		if (strcmp(page_id, "HistoryPage") == 0) {
			return Tr(Key::NavHistoryPage);
		}
		if (strcmp(page_id, "AboutPage") == 0) {
			return Tr(Key::NavAboutPage);
		}
		return "";
	}

	bool DrawLanguageCombo(const char* id, float width)
	{
		const char* preview = GetCurrentLanguageName();

		if (width > 0.f) {
			ImGui::SetNextItemWidth(width);
		}
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.09f, 0.10f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.04f, 0.07f, 0.08f, 0.98f));
		bool changed = false;
		if (ImGui::BeginCombo(id, preview)) {
			for (int i = 0; i < GetLanguageCount(); ++i) {
				const LanguageInfo* info = GetLanguage(i);
				if (info == nullptr) {
					continue;
				}
				const bool selected = LanguageCodesEqual(info->code, g_current_code.c_str());
				if (ImGui::Selectable(info->name, selected)) {
					SetLanguage(info->code);
					changed = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopStyleColor(2);
		return changed;
	}

} // namespace Hi18n
