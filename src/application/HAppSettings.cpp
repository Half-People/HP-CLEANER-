#include "HAppSettings.h"
#include "HAppPaths.h"
#include "HCleanTask.h"
#include "HElevationBroker.h"
#include "HPage.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <vector>

namespace {
	const char* kSettingsFileName = "app_settings.json";
	const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	const wchar_t* kRunValueName = L"HP CLEANER++";
	const wchar_t* kStartupTaskName = L"HP CLEANER++";

	struct Settings {
		bool console_logger = false;
		bool run_at_startup = true;
		bool run_at_startup_elevated = false;
		std::string language_code;
	};

	Settings g_settings;
	bool g_loaded = false;

	std::string SettingsFilePath()
	{
		const std::string dir = HAppPaths::GetConfigDir();
		if (dir.empty()) {
			return kSettingsFileName;
		}
		return dir + "\\" + kSettingsFileName;
	}

	std::wstring BuildStartupCommandLine()
	{
		wchar_t exe_path[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
			return {};
		}
		std::wstring cmd = L"\"";
		cmd += exe_path;
		cmd += L"\" --tray";
		return cmd;
	}

	bool GetExePath(wchar_t* out, size_t out_chars)
	{
		if (out == nullptr || out_chars == 0) {
			return false;
		}
		out[0] = L'\0';
		return GetModuleFileNameW(nullptr, out, static_cast<DWORD>(out_chars)) != 0;
	}

	bool RunLocalCmdAndWait(const wchar_t* command_line, DWORD timeout_ms, DWORD* exit_code_out)
	{
		if (command_line == nullptr || command_line[0] == L'\0') {
			return false;
		}
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};

		std::wstring cmd = L"cmd.exe /C ";
		cmd += command_line;
		std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
		cmd_buf.push_back(L'\0');

		if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi)) {
			return false;
		}
		WaitForSingleObject(pi.hProcess, timeout_ms);
		DWORD exit_code = 1;
		GetExitCodeProcess(pi.hProcess, &exit_code);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		if (exit_code_out != nullptr) {
			*exit_code_out = exit_code;
		}
		return exit_code == 0;
	}

	bool RunPrivilegedCmdAndWait(const wchar_t* command_line, DWORD timeout_ms, DWORD* exit_code_out)
	{
		if (command_line == nullptr || command_line[0] == L'\0') {
			return false;
		}
		if (HCleanIsRunningAsAdmin()) {
			return RunLocalCmdAndWait(command_line, timeout_ms, exit_code_out);
		}
		if (HElevationBroker::IsConnected()) {
			return HElevationBroker::RunHiddenCommand(command_line, timeout_ms, exit_code_out);
		}
		return false;
	}

	bool QueryRunAtStartupStandardFromRegistry()
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
			return false;
		}
		wchar_t buf[1024] = {};
		DWORD size = sizeof(buf);
		DWORD type = 0;
		const LONG rc = RegQueryValueExW(key, kRunValueName, nullptr, &type,
			reinterpret_cast<LPBYTE>(buf), &size);
		RegCloseKey(key);
		return rc == ERROR_SUCCESS && type == REG_SZ && buf[0] != L'\0';
	}

	bool QueryRunAtStartupElevatedTask()
	{
		wchar_t cmd[256] = {};
		_snwprintf_s(cmd, _TRUNCATE, L"schtasks /Query /TN \"%s\" >nul 2>&1", kStartupTaskName);
		return RunLocalCmdAndWait(cmd, 15000, nullptr);
	}

	bool RemoveRunAtStartupStandard()
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
			return true;
		}
		const LONG rc = RegDeleteValueW(key, kRunValueName);
		RegCloseKey(key);
		return rc == ERROR_SUCCESS
			|| rc == ERROR_FILE_NOT_FOUND;
	}

	bool ApplyRunAtStartupStandard()
	{
		(void)RemoveRunAtStartupStandard();

		wchar_t delete_cmd[256] = {};
		_snwprintf_s(delete_cmd, _TRUNCATE, L"schtasks /Delete /TN \"%s\" /F", kStartupTaskName);
		(void)RunPrivilegedCmdAndWait(delete_cmd, 30000, nullptr);

		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			return false;
		}

		const std::wstring cmd = BuildStartupCommandLine();
		bool ok = false;
		if (!cmd.empty()) {
			const DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
			ok = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
				reinterpret_cast<const BYTE*>(cmd.c_str()), bytes) == ERROR_SUCCESS;
		}
		RegCloseKey(key);
		return ok;
	}

	bool ApplyRunAtStartupElevated()
	{
		if (!HCleanIsRunningAsAdmin() && !HElevationBroker::IsConnected()) {
			HLOG_WARN("HAppSettings: elevated startup requires admin or broker");
			return false;
		}

		wchar_t exe_path[MAX_PATH] = {};
		if (!GetExePath(exe_path, MAX_PATH)) {
			return false;
		}

		(void)RemoveRunAtStartupStandard();

		wchar_t create_cmd[2048] = {};
		_snwprintf_s(create_cmd, _TRUNCATE,
			L"schtasks /Create /TN \"%s\" /TR \"\\\"%s\\\" --tray\" /SC ONLOGON /RL HIGHEST /F",
			kStartupTaskName, exe_path);

		if (!RunPrivilegedCmdAndWait(create_cmd, 30000, nullptr)) {
			HLOG_WARN("HAppSettings: schtasks create failed for elevated startup");
			return false;
		}

		if (!QueryRunAtStartupElevatedTask()) {
			HLOG_WARN("HAppSettings: elevated startup task not visible after create");
			return false;
		}

		HLOG_INFO("HAppSettings: run-at-startup promoted to elevated scheduled task");
		return true;
	}

	bool RemoveAllRunAtStartup()
	{
		const bool reg_ok = RemoveRunAtStartupStandard();
		wchar_t delete_cmd[256] = {};
		_snwprintf_s(delete_cmd, _TRUNCATE, L"schtasks /Delete /TN \"%s\" /F", kStartupTaskName);
		(void)RunPrivilegedCmdAndWait(delete_cmd, 30000, nullptr);
		return reg_ok;
	}

	bool QueryRunAtStartupActive()
	{
		return QueryRunAtStartupStandardFromRegistry() || QueryRunAtStartupElevatedTask();
	}

	bool ApplyRunAtStartup(bool enabled, bool elevated)
	{
		if (!enabled) {
			RemoveAllRunAtStartup();
			return !QueryRunAtStartupActive();
		}
		if (elevated) {
			return ApplyRunAtStartupElevated();
		}
		return ApplyRunAtStartupStandard();
	}

	void SyncSettingsFromSystem()
	{
		const bool active = QueryRunAtStartupActive();
		g_settings.run_at_startup = active;
		if (QueryRunAtStartupElevatedTask()) {
			g_settings.run_at_startup_elevated = true;
		}
		else if (!QueryRunAtStartupStandardFromRegistry()) {
			g_settings.run_at_startup_elevated = false;
		}
	}
}

void HAppSettingsLoad()
{
	g_loaded = true;
	g_settings = {};
	g_settings.run_at_startup = true;

	HAppPaths::EnsureAppDataDirs();
	const std::string path = SettingsFilePath();
	std::ifstream in(path);
	if (!in.is_open()) {
		if (!QueryRunAtStartupActive()) {
			ApplyRunAtStartup(true, false);
		}
		SyncSettingsFromSystem();
		HAppSettingsSave();
		return;
	}

	try {
		nlohmann::json root;
		in >> root;
		if (root.contains("console_logger") && root["console_logger"].is_boolean()) {
			g_settings.console_logger = root["console_logger"].get<bool>();
		}
		if (root.contains("run_at_startup") && root["run_at_startup"].is_boolean()) {
			g_settings.run_at_startup = root["run_at_startup"].get<bool>();
		}
		if (root.contains("run_at_startup_elevated") && root["run_at_startup_elevated"].is_boolean()) {
			g_settings.run_at_startup_elevated = root["run_at_startup_elevated"].get<bool>();
		}
		if (root.contains("language") && root["language"].is_string()) {
			g_settings.language_code = root["language"].get<std::string>();
		}
	}
	catch (...) {
		HLOG_WARN("HAppSettings: failed to parse {}", path);
	}

	SyncSettingsFromSystem();
	if (g_settings.run_at_startup != QueryRunAtStartupActive()) {
		ApplyRunAtStartup(g_settings.run_at_startup,
			g_settings.run_at_startup_elevated && HCleanHasElevatedAccess());
		SyncSettingsFromSystem();
	}
	else if (g_settings.run_at_startup && g_settings.run_at_startup_elevated
		&& !QueryRunAtStartupElevatedTask() && HCleanHasElevatedAccess()) {
		ApplyRunAtStartup(true, true);
		SyncSettingsFromSystem();
	}
}

void HAppSettingsSave()
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}

	HAppPaths::EnsureAppDataDirs();
	nlohmann::json root;
	root["console_logger"] = g_settings.console_logger;
	root["run_at_startup"] = g_settings.run_at_startup;
	root["run_at_startup_elevated"] = g_settings.run_at_startup_elevated;
	if (!g_settings.language_code.empty()) {
		root["language"] = g_settings.language_code;
	}

	const std::string path = SettingsFilePath();
	std::ofstream out(path, std::ios::trunc);
	if (!out.is_open()) {
		HLOG_WARN("HAppSettings: save failed {}", path);
		return;
	}
	out << root.dump(2);
}

bool HAppSettingsGetConsoleLogger()
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	return g_settings.console_logger;
}

void HAppSettingsSetConsoleLogger(bool enabled)
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	g_settings.console_logger = enabled;
	HAppSettingsSave();
}

bool HAppSettingsGetRunAtStartup()
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	return g_settings.run_at_startup;
}

bool HAppSettingsGetRunAtStartupElevated()
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	return g_settings.run_at_startup_elevated;
}

bool HAppSettingsSetRunAtStartup(bool enabled)
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}

	const bool elevated = enabled && HCleanHasElevatedAccess();
	if (!ApplyRunAtStartup(enabled, elevated)) {
		HLOG_WARN("HAppSettings: run-at-startup registry update failed err={}", GetLastError());
		return false;
	}

	SyncSettingsFromSystem();
	if (!enabled) {
		g_settings.run_at_startup_elevated = false;
	}
	else if (HCleanHasElevatedAccess() && QueryRunAtStartupElevatedTask()) {
		g_settings.run_at_startup_elevated = true;
	}
	else if (!QueryRunAtStartupElevatedTask()) {
		g_settings.run_at_startup_elevated = false;
	}

	HAppSettingsSave();
	return g_settings.run_at_startup == enabled;
}

const char* HAppSettingsGetLanguageCode()
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	return g_settings.language_code.c_str();
}

void HAppSettingsSetLanguageCode(const char* code)
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	if (code == nullptr) {
		g_settings.language_code.clear();
	}
	else {
		g_settings.language_code = code;
	}
	HAppSettingsSave();
}

bool HAppSettingsPromoteStartupToElevatedIfEnabled()
{
	if (!g_loaded) {
		HAppSettingsLoad();
	}
	if (!g_settings.run_at_startup) {
		return false;
	}
	if (g_settings.run_at_startup_elevated && QueryRunAtStartupElevatedTask()) {
		return false;
	}
	if (!HCleanHasElevatedAccess()) {
		return false;
	}
	if (!ApplyRunAtStartup(true, true)) {
		return false;
	}
	g_settings.run_at_startup = true;
	g_settings.run_at_startup_elevated = true;
	HAppSettingsSave();
	HLOG_INFO("HAppSettings: startup switched to elevated (admin) mode for next boot");
	return true;
}
