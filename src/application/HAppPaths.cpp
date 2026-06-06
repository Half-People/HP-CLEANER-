#include "HAppPaths.h"
#include "HPage.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <vector>

namespace {
	std::string WideToUtf8(const wchar_t* wide)
	{
		if (wide == nullptr || wide[0] == L'\0') {
			return {};
		}
		const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (needed <= 0) {
			return {};
		}
		std::vector<char> buf(static_cast<size_t>(needed));
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf.data(), needed, nullptr, nullptr);
		return std::string(buf.data());
	}

	static bool EnsureDirectoryTreeW(const std::wstring& path)
	{
		if (path.empty()) {
			return false;
		}

		std::wstring norm = path;
		while (!norm.empty() && norm.back() == L'\\') {
			norm.pop_back();
		}
		if (norm.empty()) {
			return false;
		}

		const DWORD attr = GetFileAttributesW(norm.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
			return true;
		}

		const int hr = SHCreateDirectoryExW(nullptr, norm.c_str(), nullptr);
		if (hr == ERROR_SUCCESS || hr == ERROR_ALREADY_EXISTS) {
			return true;
		}

		// 逐段建立（含含空白的資料夾名稱，例如 HP CLEANER++）
		size_t start = 0;
		if (norm.size() >= 2 && norm[1] == L':') {
			start = 3;
		}
		else if (norm.size() >= 2 && norm[0] == L'\\' && norm[1] == L'\\') {
			const size_t slash = norm.find(L'\\', 2);
			start = (slash == std::wstring::npos) ? norm.size() : slash + 1;
		}

		std::wstring built;
		for (size_t i = 0; i < start && i < norm.size(); ++i) {
			built += norm[i];
		}

		for (size_t i = start; i < norm.size(); ++i) {
			const wchar_t ch = norm[i];
			built += ch;
			if (ch != L'\\') {
				continue;
			}
			if (built.size() <= 3) {
				continue;
			}
			const DWORD seg_attr = GetFileAttributesW(built.c_str());
			if (seg_attr != INVALID_FILE_ATTRIBUTES && (seg_attr & FILE_ATTRIBUTE_DIRECTORY)) {
				continue;
			}
			if (CreateDirectoryW(built.c_str(), nullptr) == 0) {
				const DWORD err = GetLastError();
				if (err != ERROR_ALREADY_EXISTS) {
					return false;
				}
			}
		}

		if (CreateDirectoryW(norm.c_str(), nullptr) == 0) {
			const DWORD err = GetLastError();
			if (err != ERROR_ALREADY_EXISTS) {
				return false;
			}
		}
		return true;
	}

	std::wstring GetAppDataRootWide()
	{
		wchar_t appdata[MAX_PATH] = {};
		if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata))) {
			HLOG_ERROR("SHGetFolderPathW(CSIDL_APPDATA) failed");
			return {};
		}
		std::wstring root = appdata;
		if (!root.empty() && root.back() != L'\\') {
			root += L'\\';
		}
		root += L"HalfPeople\\HP CLEANER++\\";
		return root;
	}
}

namespace HAppPaths {
	std::string GetAppDataRoot()
	{
		return WideToUtf8(GetAppDataRootWide().c_str());
	}

	std::string GetLogsDir()
	{
		const std::wstring root = GetAppDataRootWide();
		if (root.empty()) {
			return {};
		}
		return WideToUtf8((root + L"logs\\").c_str());
	}

	std::string GetConfigDir()
	{
		const std::wstring root = GetAppDataRootWide();
		if (root.empty()) {
			return {};
		}
		return WideToUtf8((root + L"config\\").c_str());
	}

	std::string GetCrashesDir()
	{
		const std::wstring root = GetAppDataRootWide();
		if (root.empty()) {
			return {};
		}
		return WideToUtf8((root + L"crashes\\").c_str());
	}

	bool EnsureAppDataDirs()
	{
		const std::wstring root = GetAppDataRootWide();
		if (root.empty()) {
			return false;
		}
		const bool ok_root = EnsureDirectoryTreeW(root);
		const bool ok_logs = EnsureDirectoryTreeW(root + L"logs");
		const bool ok_config = EnsureDirectoryTreeW(root + L"config");
		const bool ok_crashes = EnsureDirectoryTreeW(root + L"crashes");
		if (ok_root && ok_logs && ok_config && ok_crashes) {
			HLOG_INFO("App data directories ready: {}", WideToUtf8(root.c_str()));
			return true;
		}
		HLOG_ERROR("Failed to create app data directories under {}", WideToUtf8(root.c_str()));
		return false;
	}

	std::string GetDailyLogFilePath()
	{
		const std::string logs = GetLogsDir();
		if (logs.empty()) {
			return "logs\\logger.log";
		}
		// spdlog daily_file_sink 基底檔名（會自動變成 logger_YYYY-MM-DD.log）
		return logs + "logger.log";
	}

	std::string GetCurrentDailyLogFilePath()
	{
		const std::string logs = GetLogsDir();
		if (logs.empty()) {
			return {};
		}
		SYSTEMTIME st = {};
		GetLocalTime(&st);
		char dated[64] = {};
		snprintf(dated, sizeof(dated), "logger_%04u-%02u-%02u.log",
			st.wYear, st.wMonth, st.wDay);
		return logs + dated;
	}

	std::string GetLatestLogFilePath()
	{
		const std::wstring root = GetAppDataRootWide();
		if (root.empty()) {
			return GetDailyLogFilePath();
		}

		const std::wstring pattern = root + L"logs\\*.log";
		WIN32_FIND_DATAW fd = {};
		const std::string expected = GetCurrentDailyLogFilePath();
		if (!expected.empty()) {
			const int wlen = MultiByteToWideChar(CP_UTF8, 0, expected.c_str(), -1, nullptr, 0);
			if (wlen > 0) {
				std::vector<wchar_t> wpath(static_cast<size_t>(wlen));
				MultiByteToWideChar(CP_UTF8, 0, expected.c_str(), -1, wpath.data(), wlen);
				const DWORD attr = GetFileAttributesW(wpath.data());
				if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
					return expected;
				}
			}
		}

		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return expected.empty() ? GetDailyLogFilePath() : expected;
		}

		ULONGLONG best_time = 0;
		std::wstring best_name;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				continue;
			}
			const ULONGLONG write_time = (static_cast<ULONGLONG>(fd.ftLastWriteTime.dwHighDateTime) << 32)
				| fd.ftLastWriteTime.dwLowDateTime;
			if (!best_name.empty() && write_time <= best_time) {
				continue;
			}
			best_time = write_time;
			best_name = fd.cFileName;
		} while (FindNextFileW(find, &fd));

		FindClose(find);
		if (best_name.empty()) {
			return expected.empty() ? GetDailyLogFilePath() : expected;
		}
		return WideToUtf8((root + L"logs\\" + best_name).c_str());
	}

	std::string GetPendingCrashReportPath()
	{
		const std::string crashes = GetCrashesDir();
		if (crashes.empty()) {
			return {};
		}
		return crashes + "pending_report.txt";
	}

	std::string ReadPendingCrashReport()
	{
		const std::string pending_path = GetPendingCrashReportPath();
		if (pending_path.empty()) {
			return {};
		}

		const std::wstring pending_wide = [&]() {
			const int needed = MultiByteToWideChar(CP_UTF8, 0, pending_path.c_str(), -1, nullptr, 0);
			if (needed <= 0) {
				return std::wstring{};
			}
			std::vector<wchar_t> buf(static_cast<size_t>(needed));
			MultiByteToWideChar(CP_UTF8, 0, pending_path.c_str(), -1, buf.data(), needed);
			return std::wstring(buf.data());
		}();

		HANDLE file = CreateFileW(pending_wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return {};
		}

		char buffer[MAX_PATH * 4] = {};
		DWORD read = 0;
		const BOOL ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
		CloseHandle(file);
		if (ok != TRUE || read == 0) {
			return {};
		}
		buffer[read] = '\0';

		std::string path = buffer;
		while (!path.empty() && (path.back() == '\r' || path.back() == '\n' || path.back() == ' ')) {
			path.pop_back();
		}
		if (!path.empty() && path.front() == '"') {
			path.erase(path.begin());
		}
		if (!path.empty() && path.back() == '"') {
			path.pop_back();
		}
		return path;
	}

	void ClearPendingCrashReport()
	{
		const std::string pending_path = GetPendingCrashReportPath();
		if (pending_path.empty()) {
			return;
		}
		const std::wstring pending_wide = [&]() {
			const int needed = MultiByteToWideChar(CP_UTF8, 0, pending_path.c_str(), -1, nullptr, 0);
			if (needed <= 0) {
				return std::wstring{};
			}
			std::vector<wchar_t> buf(static_cast<size_t>(needed));
			MultiByteToWideChar(CP_UTF8, 0, pending_path.c_str(), -1, buf.data(), needed);
			return std::wstring(buf.data());
		}();
		DeleteFileW(pending_wide.c_str());
	}
}
