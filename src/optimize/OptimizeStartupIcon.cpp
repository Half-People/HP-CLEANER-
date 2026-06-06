#include "OptimizeStartupIcon.h"
#include "HRC_Assets.h"
#include "HPage.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")

namespace OptimizeStartupIcon {
	namespace {
		std::mutex g_mutex;
		std::unordered_map<std::string, HRC::HTexture> g_icon_cache;
		unsigned long long g_generic_exe_texture = 0;
		unsigned long long g_generic_service_texture = 0;
		bool g_com_initialized = false;

		static constexpr int kIconBgR = 18;
		static constexpr int kIconBgG = 20;
		static constexpr int kIconBgB = 24;

		static bool Utf8FromWide(const wchar_t* wide, char* out, size_t out_size)
		{
			if (wide == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size),
				nullptr, nullptr) > 0;
		}

		static bool WideFromUtf8(const char* utf8, wchar_t* out, size_t out_chars)
		{
			if (utf8 == nullptr || out == nullptr || out_chars == 0) {
				return false;
			}
			return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, static_cast<int>(out_chars)) > 0;
		}

		static void TrimInPlace(std::wstring& s)
		{
			while (!s.empty() && (s.front() == L' ' || s.front() == L'"')) {
				s.erase(s.begin());
			}
			while (!s.empty() && (s.back() == L' ' || s.back() == L'"')) {
				s.pop_back();
			}
		}

		static bool ResolveShortcutTarget(const wchar_t* lnk_path, wchar_t* out, size_t out_chars)
		{
			if (lnk_path == nullptr || out == nullptr || out_chars == 0) {
				return false;
			}
			out[0] = L'\0';
			if (!g_com_initialized) {
				return false;
			}
			IShellLinkW* link = nullptr;
			IPersistFile* file = nullptr;
			bool ok = false;
			if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
				IID_IShellLinkW, reinterpret_cast<void**>(&link)))
				&& SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))
				&& SUCCEEDED(file->Load(lnk_path, STGM_READ))) {
				wchar_t target[MAX_PATH] = {};
				WIN32_FIND_DATAW fd = {};
				if (SUCCEEDED(link->GetPath(target, static_cast<int>(std::size(target)), &fd, SLGP_RAWPATH))
					&& target[0] != L'\0') {
					wcsncpy_s(out, out_chars, target, _TRUNCATE);
					ok = true;
				}
			}
			if (file != nullptr) {
				file->Release();
			}
			if (link != nullptr) {
				link->Release();
			}
			return ok;
		}

		static bool ExtractExecutablePath(const wchar_t* input, wchar_t* out, size_t out_chars,
			bool resolve_shortcuts)
		{
			if (input == nullptr || out == nullptr || out_chars == 0) {
				return false;
			}
			out[0] = L'\0';
			wchar_t expanded[1024] = {};
			if (ExpandEnvironmentStringsW(input, expanded, static_cast<DWORD>(std::size(expanded))) == 0) {
				wcsncpy_s(expanded, input, _TRUNCATE);
			}

			std::wstring s = expanded;
			TrimInPlace(s);
			if (s.empty()) {
				return false;
			}

			const size_t len = s.size();
			if (len >= 4 && _wcsicmp(s.c_str() + len - 4, L".lnk") == 0) {
				if (!resolve_shortcuts) {
					return false;
				}
				return ResolveShortcutTarget(s.c_str(), out, out_chars);
			}

			const wchar_t* exe = nullptr;
			for (size_t i = 0; i + 3 < len; ++i) {
				if (_wcsnicmp(s.c_str() + i, L".exe", 4) == 0) {
					exe = s.c_str() + i;
					break;
				}
			}
			if (exe != nullptr) {
				const size_t end = static_cast<size_t>(exe - s.c_str()) + 4;
				wcsncpy_s(out, out_chars, s.substr(0, end).c_str(), _TRUNCATE);
				return true;
			}

			if (GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES) {
				wcsncpy_s(out, out_chars, s.c_str(), _TRUNCATE);
				return true;
			}
			return false;
		}

		static bool ExtractExecutablePath(const wchar_t* input, wchar_t* out, size_t out_chars)
		{
			return ExtractExecutablePath(input, out, out_chars, true);
		}

		static bool SanitizeExistingExeUtf8(const char* path_utf8, char* out, size_t out_size)
		{
			if (path_utf8 == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			out[0] = '\0';
			wchar_t wide[MAX_PATH * 2] = {};
			if (!WideFromUtf8(path_utf8, wide, std::size(wide))) {
				return false;
			}
			wchar_t exe[MAX_PATH] = {};
			if (!ExtractExecutablePath(wide, exe, std::size(exe), true)) {
				return false;
			}
			const DWORD attr = GetFileAttributesW(exe);
			if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
				return false;
			}
			return Utf8FromWide(exe, out, out_size);
		}

		static bool QueryVersionString(const wchar_t* path, const wchar_t* key, char* out, size_t out_size)
		{
			if (path == nullptr || key == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			out[0] = '\0';
			DWORD dummy = 0;
			const DWORD ver_size = GetFileVersionInfoSizeW(path, &dummy);
			if (ver_size == 0) {
				return false;
			}
			std::vector<BYTE> buf(ver_size);
			if (!GetFileVersionInfoW(path, 0, ver_size, buf.data())) {
				return false;
			}
			struct LANGANDCODEPAGE {
				WORD wLanguage;
				WORD wCodePage;
			} * translate = nullptr;
			UINT translate_size = 0;
			if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
				reinterpret_cast<LPVOID*>(&translate), &translate_size)
				|| translate_size < sizeof(LANGANDCODEPAGE)) {
				return false;
			}
			wchar_t subkey[128] = {};
			_snwprintf_s(subkey, _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\%s",
				translate[0].wLanguage, translate[0].wCodePage, key);
			LPVOID value = nullptr;
			UINT value_len = 0;
			if (!VerQueryValueW(buf.data(), subkey, &value, &value_len) || value == nullptr) {
				return false;
			}
			WideCharToMultiByte(CP_UTF8, 0, static_cast<const wchar_t*>(value), -1,
				out, static_cast<int>(out_size), nullptr, nullptr);
			return out[0] != '\0';
		}

		static void FillImpactHint(OptimizeScan::StartupEntry& entry)
		{
			const char* n = entry.name_utf8;
			if (n == nullptr) {
				return;
			}
			if (_stricmp(n, "SecurityHealth") == 0 || strstr(n, "Windows Defender") != nullptr) {
				strncpy_s(entry.impact_utf8,
					"系統安全相關，不建議停用除非您清楚風險。", _TRUNCATE);
				return;
			}
			if (strstr(n, "Steam") != nullptr || strstr(n, "Discord") != nullptr
				|| strstr(n, "Epic") != nullptr) {
				strncpy_s(entry.impact_utf8,
					"遊戲／通訊平台常駐，停用可加快開機但需手動啟動。", _TRUNCATE);
				return;
			}
			if (strstr(n, "Update") != nullptr || strstr(n, "Adobe") != nullptr) {
				strncpy_s(entry.impact_utf8,
					"可能在背景檢查更新；停用可減少開機負擔。", _TRUNCATE);
				return;
			}
			strncpy_s(entry.impact_utf8,
				"開機時自動啟動；若不需要可停用以縮短啟動時間。", _TRUNCATE);
		}

		static void FillHowToHint(OptimizeScan::StartupEntry& entry)
		{
			if (!entry.can_toggle) {
				strncpy_s(entry.how_to_utf8,
					"無法在此直接變更，請使用右側「工作管理員」按鈕。", _TRUNCATE);
				return;
			}
			if (entry.enabled) {
				strncpy_s(entry.how_to_utf8,
					"雙擊本列可停用開機啟動；或取消右側「開機」勾選。", _TRUNCATE);
			}
			else {
				strncpy_s(entry.how_to_utf8,
					"雙擊本列可啟用開機啟動；或勾選右側「開機」。", _TRUNCATE);
			}
		}

		static void FillBootImpactHint(OptimizeScan::StartupEntry& entry)
		{
			entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::Unknown);
			if (!entry.enabled) {
				entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::None);
				strncpy_s(entry.boot_impact_utf8, "無（目前已停用，不影響開機速度）", _TRUNCATE);
				return;
			}
			const char* n = entry.name_utf8;
			if (strstr(n, "Security") != nullptr || strstr(n, "Defender") != nullptr
				|| _stricmp(n, "SecurityHealth") == 0) {
				entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::Low);
				strncpy_s(entry.boot_impact_utf8, "低～中（安全元件，開機略增 1～3 秒）", _TRUNCATE);
				return;
			}
			if (strstr(n, "Steam") != nullptr || strstr(n, "Discord") != nullptr
				|| strstr(n, "Epic") != nullptr || strstr(n, "Riot") != nullptr) {
				entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::High);
				strncpy_s(entry.boot_impact_utf8, "高（遊戲／通訊平台，常見 +3～8 秒）", _TRUNCATE);
				return;
			}
			if (strstr(n, "Update") != nullptr || strstr(n, "Adobe") != nullptr
				|| strstr(n, "Google") != nullptr) {
				entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::Medium);
				strncpy_s(entry.boot_impact_utf8, "中（背景更新檢查，約 +2～6 秒）", _TRUNCATE);
				return;
			}
			if (entry.exe_path_utf8[0] == '\0') {
				entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::Unknown);
				strncpy_s(entry.boot_impact_utf8, "未知（無法解析程式路徑，可能為 UWP／設定）", _TRUNCATE);
				return;
			}
			entry.impact_tier = static_cast<int>(OptimizeScan::StartupImpactTier::Medium);
			strncpy_s(entry.boot_impact_utf8, "中（一般桌面程式，約 +1～5 秒）", _TRUNCATE);
		}

		static void FillServiceBootImpact(OptimizeScan::ServiceEntry& entry)
		{
			if (!entry.exists) {
				strncpy_s(entry.boot_impact_utf8, "—（服務未安裝）", _TRUNCATE);
				return;
			}
			if (entry.start_type == SERVICE_DISABLED) {
				strncpy_s(entry.boot_impact_utf8, "無（服務已停用）", _TRUNCATE);
				return;
			}
			if (entry.start_type == SERVICE_AUTO_START
				|| entry.start_type == 5) {
				if (std::strcmp(entry.service_name, "SysMain") == 0) {
					strncpy_s(entry.boot_impact_utf8, "高（Superfetch／SysMain，可能明顯拖慢開機與磁碟）", _TRUNCATE);
				}
				else if (std::strcmp(entry.service_name, "WSearch") == 0) {
					strncpy_s(entry.boot_impact_utf8, "中～高（Windows 搜尋索引，背景佔用 CPU／磁碟）", _TRUNCATE);
				}
				else if (std::strcmp(entry.service_name, "DoSvc") == 0) {
					strncpy_s(entry.boot_impact_utf8, "中（傳遞最佳化，背景下載與網路佔用）", _TRUNCATE);
				}
				else {
					strncpy_s(entry.boot_impact_utf8, "中（自動啟動服務，開機後背景執行）", _TRUNCATE);
				}
				return;
			}
			if (entry.running) {
				strncpy_s(entry.boot_impact_utf8, "低～中（手動啟動但目前正在執行）", _TRUNCATE);
			}
			else {
				strncpy_s(entry.boot_impact_utf8, "低（未執行中）", _TRUNCATE);
			}
		}

		static bool HiconToRgba(HICON icon, int size, std::vector<unsigned char>& rgba_out, int& out_w, int& out_h)
		{
			rgba_out.clear();
			out_w = out_h = size;
			HDC screen = GetDC(nullptr);
			HDC mem_dc = CreateCompatibleDC(screen);
			BITMAPINFO bmi = {};
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = size;
			bmi.bmiHeader.biHeight = -size;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			void* bits = nullptr;
			HBITMAP dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
			if (dib == nullptr || bits == nullptr) {
				DeleteDC(mem_dc);
				ReleaseDC(nullptr, screen);
				return false;
			}
			const HGDIOBJ old_bmp = SelectObject(mem_dc, dib);
			const COLORREF bg = RGB(kIconBgR, kIconBgG, kIconBgB);
			HBRUSH brush = CreateSolidBrush(bg);
			RECT rc = { 0, 0, size, size };
			FillRect(mem_dc, &rc, brush);
			DeleteObject(brush);
			DrawIconEx(mem_dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);

			rgba_out.resize(static_cast<size_t>(size * size * 4));
			memcpy(rgba_out.data(), bits, rgba_out.size());
			for (size_t i = 0; i < rgba_out.size(); i += 4) {
				const unsigned char b = rgba_out[i + 0];
				const unsigned char g = rgba_out[i + 1];
				const unsigned char r = rgba_out[i + 2];
				rgba_out[i + 0] = r;
				rgba_out[i + 1] = g;
				rgba_out[i + 2] = b;
				const int dr = abs(static_cast<int>(r) - kIconBgR);
				const int dg = abs(static_cast<int>(g) - kIconBgG);
				const int db = abs(static_cast<int>(b) - kIconBgB);
				const int dist = dr + dg + db;
				if (dist < 28) {
					rgba_out[i + 3] = 0;
				}
				else if (dist < 80) {
					rgba_out[i + 3] = static_cast<unsigned char>((dist - 28) * 5);
				}
				else {
					rgba_out[i + 3] = 255;
				}
			}

			SelectObject(mem_dc, old_bmp);
			DeleteObject(dib);
			DeleteDC(mem_dc);
			ReleaseDC(nullptr, screen);
			return true;
		}

		static HICON LoadShellIconInner(const wchar_t* path_wide)
		{
			if (path_wide == nullptr || path_wide[0] == L'\0') {
				return nullptr;
			}
			const size_t len = wcslen(path_wide);
			const bool is_lnk = len >= 4 && _wcsicmp(path_wide + len - 4, L".lnk") == 0;
			const DWORD file_attr = GetFileAttributesW(path_wide);
			const bool exists = file_attr != INVALID_FILE_ATTRIBUTES;
			if (!is_lnk && !exists) {
				return nullptr;
			}

			SHFILEINFOW sfi = {};
			UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
			DWORD attrs = file_attr;
			if (is_lnk || !exists) {
				flags |= SHGFI_USEFILEATTRIBUTES;
				attrs = FILE_ATTRIBUTE_NORMAL;
			}
			if (SHGetFileInfoW(path_wide, attrs, &sfi, sizeof(sfi), flags) != 0
				&& sfi.hIcon != nullptr) {
				return sfi.hIcon;
			}
			return nullptr;
		}

		static HICON LoadShellIconSeh(const wchar_t* path_wide)
		{
			__try {
				return LoadShellIconInner(path_wide);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		static HICON LoadShellIcon(const wchar_t* path_wide)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			return LoadShellIconSeh(path_wide);
		}

		static unsigned long long LoadIconFromHicon(HICON icon, const char* cache_key)
		{
			if (icon == nullptr || cache_key == nullptr) {
				return 0;
			}
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				auto it = g_icon_cache.find(cache_key);
				if (it != g_icon_cache.end() && it->second.texture != 0) {
					DestroyIcon(icon);
					return static_cast<unsigned long long>(it->second.texture);
				}
			}
			std::vector<unsigned char> rgba;
			int iw = 0;
			int ih = 0;
			const bool ok = HiconToRgba(icon, 32, rgba, iw, ih);
			DestroyIcon(icon);
			if (!ok) {
				return 0;
			}
			IDirect3DDevice9* device = HRC::GetRenderDevice();
			if (device == nullptr) {
				return 0;
			}
			HRC::HTexture tex = HRC::LoadTextureFromRgba(device, rgba.data(), iw, ih);
			if (tex.texture == 0) {
				return 0;
			}
			std::lock_guard<std::mutex> lock(g_mutex);
			g_icon_cache[cache_key] = tex;
			return static_cast<unsigned long long>(tex.texture);
		}

		static unsigned long long LoadIconTextureFromPath(const wchar_t* path_wide, const char* cache_key)
		{
			if (path_wide == nullptr || path_wide[0] == L'\0' || cache_key == nullptr) {
				return 0;
			}
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				auto it = g_icon_cache.find(cache_key);
				if (it != g_icon_cache.end() && it->second.texture != 0) {
					return static_cast<unsigned long long>(it->second.texture);
				}
			}

			HICON icon = LoadShellIcon(path_wide);
			if (icon == nullptr) {
				return 0;
			}
			std::vector<unsigned char> rgba;
			int iw = 0;
			int ih = 0;
			const bool ok = HiconToRgba(icon, 32, rgba, iw, ih);
			DestroyIcon(icon);
			if (!ok) {
				return 0;
			}
			IDirect3DDevice9* device = HRC::GetRenderDevice();
			if (device == nullptr) {
				return 0;
			}
			HRC::HTexture tex = HRC::LoadTextureFromRgba(device, rgba.data(), iw, ih);
			if (tex.texture == 0) {
				return 0;
			}
			std::lock_guard<std::mutex> lock(g_mutex);
			g_icon_cache[cache_key] = tex;
			return static_cast<unsigned long long>(tex.texture);
		}

		static bool PathExistsWide(const wchar_t* path)
		{
			return path != nullptr && path[0] != L'\0'
				&& GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
		}
	}

	void Init()
	{
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		g_com_initialized = (hr == S_OK || hr == S_FALSE);
		HLOG_INFO("OptimizeStartupIcon: Init com_ok={}", g_com_initialized);
	}

	void Shutdown()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (auto& pair : g_icon_cache) {
			HRC::FreeTexture(pair.second);
		}
		g_icon_cache.clear();
		g_generic_exe_texture = 0;
		g_generic_service_texture = 0;
		HLOG_INFO("OptimizeStartupIcon: Shutdown");
	}

	void EnrichStartupEntry(OptimizeScan::StartupEntry& entry)
	{
		wchar_t exe_wide[MAX_PATH] = {};
		if (entry.restore_command[0] != L'\0') {
			ExtractExecutablePath(entry.restore_command, exe_wide, std::size(exe_wide), false);
		}
		if (exe_wide[0] == L'\0' && entry.restore_key[0] != L'\0') {
			ExtractExecutablePath(entry.restore_key, exe_wide, std::size(exe_wide), false);
		}
		if (exe_wide[0] == L'\0' && entry.command_utf8[0] != '\0') {
			wchar_t cmd_w[1024] = {};
			if (WideFromUtf8(entry.command_utf8, cmd_w, std::size(cmd_w))) {
				ExtractExecutablePath(cmd_w, exe_wide, std::size(exe_wide), false);
			}
		}

		if (entry.restore_key[0] != L'\0') {
			const size_t klen = wcslen(entry.restore_key);
			if (klen >= 4 && _wcsicmp(entry.restore_key + klen - 4, L".lnk") == 0) {
				Utf8FromWide(entry.restore_key, entry.icon_path_utf8, sizeof(entry.icon_path_utf8));
			}
		}

		if (exe_wide[0] != L'\0') {
			Utf8FromWide(exe_wide, entry.exe_path_utf8, sizeof(entry.exe_path_utf8));
			if (entry.icon_path_utf8[0] == '\0') {
				strncpy_s(entry.icon_path_utf8, entry.exe_path_utf8, _TRUNCATE);
			}
			QueryVersionString(exe_wide, L"CompanyName", entry.publisher_utf8,
				sizeof(entry.publisher_utf8));
			QueryVersionString(exe_wide, L"ProductName", entry.product_utf8,
				sizeof(entry.product_utf8));
			if (!QueryVersionString(exe_wide, L"FileDescription", entry.file_description_utf8,
				sizeof(entry.file_description_utf8))) {
				strncpy_s(entry.file_description_utf8, entry.name_utf8, _TRUNCATE);
			}
		}
		else {
			strncpy_s(entry.file_description_utf8, entry.name_utf8, _TRUNCATE);
			if (entry.icon_path_utf8[0] == '\0' && entry.command_utf8[0] != '\0') {
				wchar_t cmd_w[1024] = {};
				if (WideFromUtf8(entry.command_utf8, cmd_w, std::size(cmd_w))
					&& PathExistsWide(cmd_w)) {
					Utf8FromWide(cmd_w, entry.icon_path_utf8, sizeof(entry.icon_path_utf8));
				}
			}
		}

		FillHowToHint(entry);
		FillImpactHint(entry);
		FillBootImpactHint(entry);
	}

	void EnrichServiceEntry(OptimizeScan::ServiceEntry& entry)
	{
		wchar_t bin_wide[MAX_PATH * 2] = {};
		if (entry.binary_path_utf8[0] != '\0') {
			WideFromUtf8(entry.binary_path_utf8, bin_wide, std::size(bin_wide));
			wchar_t* sp = wcsrchr(bin_wide, L' ');
			if (sp != nullptr && sp > bin_wide && sp[-1] == L'"') {
				*sp = L'\0';
			}
			if (bin_wide[0] == L'"') {
				wmemmove(bin_wide, bin_wide + 1, wcslen(bin_wide));
			}
			const size_t blen = wcslen(bin_wide);
			if (blen >= 4 && _wcsicmp(bin_wide + blen - 4, L".exe") != 0) {
				wchar_t exe_path[MAX_PATH] = {};
				if (ExtractExecutablePath(bin_wide, exe_path, std::size(exe_path))) {
					wcsncpy_s(bin_wide, exe_path, _TRUNCATE);
				}
			}
			Utf8FromWide(bin_wide, entry.binary_path_utf8, sizeof(entry.binary_path_utf8));
			if (PathExistsWide(bin_wide)) {
				QueryVersionString(bin_wide, L"CompanyName", entry.publisher_utf8,
					sizeof(entry.publisher_utf8));
				QueryVersionString(bin_wide, L"FileDescription", entry.description_utf8,
					sizeof(entry.description_utf8));
			}
		}
		if (entry.description_utf8[0] == '\0') {
			strncpy_s(entry.description_utf8, entry.display_name, _TRUNCATE);
		}
		if (entry.exists) {
			snprintf(entry.how_to_utf8, sizeof(entry.how_to_utf8),
				"右側按鈕可%s此服務（建議以管理員執行）。",
				entry.start_type == SERVICE_DISABLED ? "啟用" : "停用");
		}
		FillServiceBootImpact(entry);
	}

	unsigned long long GetAppFallbackIconTextureId()
	{
		if (Logo::HP_Cleaner_Logo.texture != 0) {
			return static_cast<unsigned long long>(Logo::HP_Cleaner_Logo.texture);
		}
		return 0;
	}

	unsigned long long GetGenericExeIconTextureId()
	{
		if (g_generic_exe_texture != 0) {
			return g_generic_exe_texture;
		}
		wchar_t sys_dir[MAX_PATH] = {};
		GetSystemDirectoryW(sys_dir, static_cast<UINT>(std::size(sys_dir)));
		wchar_t shell32[MAX_PATH] = {};
		_snwprintf_s(shell32, _TRUNCATE, L"%s\\shell32.dll", sys_dir);
		g_generic_exe_texture = LoadIconTextureFromPath(shell32, "__generic_shell32__");
		if (g_generic_exe_texture == 0) {
			wchar_t imageres[MAX_PATH] = {};
			_snwprintf_s(imageres, _TRUNCATE, L"%s\\imageres.dll", sys_dir);
			g_generic_exe_texture = LoadIconTextureFromPath(imageres, "__generic_imageres__");
		}
		return g_generic_exe_texture;
	}

	unsigned long long GetIconTextureId(const char* path_utf8)
	{
		if (path_utf8 == nullptr || path_utf8[0] == '\0') {
			return 0;
		}
		char safe[512] = {};
		if (!SanitizeExistingExeUtf8(path_utf8, safe, sizeof(safe))) {
			wchar_t path_wide[MAX_PATH * 2] = {};
			if (!WideFromUtf8(path_utf8, path_wide, std::size(path_wide))) {
				return 0;
			}
			const size_t len = wcslen(path_wide);
			if (len < 4 || _wcsicmp(path_wide + len - 4, L".lnk") != 0) {
				return 0;
			}
			return LoadIconTextureFromPath(path_wide, path_utf8);
		}
		wchar_t path_wide[MAX_PATH] = {};
		if (!WideFromUtf8(safe, path_wide, std::size(path_wide))) {
			return 0;
		}
		return LoadIconTextureFromPath(path_wide, safe);
	}

	static unsigned long long TryIconForUtf8Path(const char* path_utf8)
	{
		if (path_utf8 == nullptr || path_utf8[0] == '\0') {
			return 0;
		}
		return GetIconTextureId(path_utf8);
	}

	unsigned long long GetStartupIconTextureId(const OptimizeScan::StartupEntry& entry)
	{
		if (entry.icon_path_utf8[0] != '\0') {
			const unsigned long long t = TryIconForUtf8Path(entry.icon_path_utf8);
			if (t != 0) {
				return t;
			}
		}
		if (entry.exe_path_utf8[0] != '\0') {
			const unsigned long long t = TryIconForUtf8Path(entry.exe_path_utf8);
			if (t != 0) {
				return t;
			}
		}
		if (entry.command_utf8[0] != '\0') {
			wchar_t cmd_w[1024] = {};
			if (WideFromUtf8(entry.command_utf8, cmd_w, std::size(cmd_w))) {
				wchar_t exe_wide[MAX_PATH] = {};
				if (ExtractExecutablePath(cmd_w, exe_wide, std::size(exe_wide))) {
					char exe_utf8[512] = {};
					if (Utf8FromWide(exe_wide, exe_utf8, sizeof(exe_utf8))) {
						const unsigned long long t = TryIconForUtf8Path(exe_utf8);
						if (t != 0) {
							return t;
						}
					}
				}
			}
		}
		return GetAppFallbackIconTextureId();
	}

	static unsigned long long GetGenericServiceIconTextureId()
	{
		if (g_generic_service_texture != 0) {
			return g_generic_service_texture;
		}
		wchar_t sys[MAX_PATH] = {};
		GetSystemDirectoryW(sys, static_cast<UINT>(std::size(sys)));
		wchar_t svchost[MAX_PATH] = {};
		_snwprintf_s(svchost, _TRUNCATE, L"%s\\svchost.exe", sys);
		if (GetFileAttributesW(svchost) != INVALID_FILE_ATTRIBUTES) {
			g_generic_service_texture = LoadIconTextureFromPath(svchost, "__svchost__");
		}
		if (g_generic_service_texture == 0) {
			g_generic_service_texture = GetGenericExeIconTextureId();
		}
		return g_generic_service_texture;
	}

	unsigned long long GetServiceIconTextureId(const OptimizeScan::ServiceEntry& entry)
	{
		if (entry.binary_path_utf8[0] != '\0') {
			char safe[512] = {};
			if (SanitizeExistingExeUtf8(entry.binary_path_utf8, safe, sizeof(safe))) {
				const unsigned long long t = TryIconForUtf8Path(safe);
				if (t != 0) {
					return t;
				}
			}
		}
		const unsigned long long app = GetAppFallbackIconTextureId();
		if (app != 0) {
			return app;
		}
		return GetGenericServiceIconTextureId();
	}

	void FormatStartupTooltip(const OptimizeScan::StartupEntry& entry, char* buf, size_t buf_size)
	{
		if (buf == nullptr || buf_size == 0) {
			return;
		}
		buf[0] = '\0';
		const char* title = entry.file_description_utf8[0] != '\0'
			? entry.file_description_utf8 : entry.name_utf8;
		snprintf(buf, buf_size,
			"%s\n\n"
			"【啟動速度影響】%s\n%s\n\n"
			"【一般影響】\n%s\n\n"
			"【操作】\n%s\n\n"
			"登錄名稱：%s\n"
			"來源：%s\n"
			"產品：%s\n"
			"發行者：%s\n"
			"狀態：%s\n"
			"路徑：%s\n"
			"指令：%s",
			title,
			OptimizeScan::StartupImpactLabel(entry.impact_tier),
			entry.boot_impact_utf8[0] ? entry.boot_impact_utf8 : "—",
			entry.impact_utf8[0] ? entry.impact_utf8 : "—",
			entry.how_to_utf8[0] ? entry.how_to_utf8 : "—",
			entry.name_utf8,
			entry.source_label,
			entry.product_utf8[0] ? entry.product_utf8 : "—",
			entry.publisher_utf8[0] ? entry.publisher_utf8 : "—",
			entry.enabled ? "開機啟動" : "已停用",
			entry.exe_path_utf8[0] ? entry.exe_path_utf8 : "—",
			entry.command_utf8[0] ? entry.command_utf8 : "—");
	}

	void FormatServiceTooltip(const OptimizeScan::ServiceEntry& entry, char* buf, size_t buf_size)
	{
		if (buf == nullptr || buf_size == 0) {
			return;
		}
		const char* start = "—";
		switch (entry.start_type) {
		case SERVICE_AUTO_START: start = "自動"; break;
		case SERVICE_DEMAND_START: start = "手動"; break;
		case SERVICE_DISABLED: start = "已停用"; break;
		case 5: start = "延遲自動"; break;
		default: break;
		}
		snprintf(buf, buf_size,
			"%s\n（%s）\n\n"
			"【用途】\n%s\n\n"
			"【停用後】\n%s\n\n"
			"【注意】\n%s\n\n"
			"【開機／效能】\n%s\n\n"
			"【操作】\n%s\n\n"
			"狀態：%s · %s\n"
			"發行者：%s\n"
			"執行檔：%s",
			entry.display_name,
			entry.service_name,
			entry.role_utf8[0] ? entry.role_utf8
				: (entry.description_utf8[0] ? entry.description_utf8 : "—"),
			entry.disable_effect_utf8[0] ? entry.disable_effect_utf8 : "—",
			entry.risk_note_utf8[0] ? entry.risk_note_utf8 : "—",
			entry.boot_impact_utf8[0] ? entry.boot_impact_utf8 : "—",
			entry.how_to_utf8[0] ? entry.how_to_utf8 : "—",
			entry.exists ? (entry.running ? "執行中" : "已停止") : "未安裝",
			start,
			entry.publisher_utf8[0] ? entry.publisher_utf8 : "—",
			entry.binary_path_utf8[0] ? entry.binary_path_utf8 : "—");
	}

	void OpenExecutableFolder(const char* exe_path_utf8)
	{
		if (exe_path_utf8 == nullptr || exe_path_utf8[0] == '\0') {
			return;
		}
		wchar_t path[MAX_PATH] = {};
		WideFromUtf8(exe_path_utf8, path, std::size(path));
		wchar_t* last = wcsrchr(path, L'\\');
		if (last != nullptr) {
			*last = L'\0';
		}
		ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
	}

	void OpenExecutableProperties(const char* exe_path_utf8)
	{
		if (exe_path_utf8 == nullptr || exe_path_utf8[0] == '\0') {
			return;
		}
		wchar_t path[MAX_PATH] = {};
		WideFromUtf8(exe_path_utf8, path, std::size(path));
		ShellExecuteW(nullptr, L"properties", path, nullptr, nullptr, SW_SHOWNORMAL);
	}
}
