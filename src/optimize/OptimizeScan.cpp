#include "OptimizeScan.h"
#include "OptimizeStartupIcon.h"
#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HElevationBroker.h"
#include "HAppPaths.h"
#include "HPage.h"
#include <windows.h>
#include <winsvc.h>
#include <shlobj.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Hi18n.h"

namespace OptimizeScan {
	namespace {
		constexpr size_t kMaxStartups = 160;
		constexpr wchar_t kBalancedGuid[] = L"381b9ba2-fdda-464f-9a87-0a0ce3457b95";
		constexpr wchar_t kHighPerfGuid[] = L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
		constexpr wchar_t kPowerSaverGuid[] = L"a1841308-3548-4fab-bc81-f71556f20b8a";
		constexpr wchar_t kUltimateGuid[] = L"e9a42b02-d5df-448d-aa00-03f14749eb61";

		struct PresetDef {
			const char* id;
			const char* label;
			const char* description;
			const char* clean_preset;
			const char* power_kind;
			int visual_fx;
			bool game_mode;
			bool disable_sysmain;
			bool disable_wsearch;
			bool disable_dosvc;
		};

		static const PresetDef kPresets[] = {
			{ "safe", u8"安全", u8"平衡電源、保留搜尋與更新傳遞，建議一般清理預設。",
				"general", "balanced", -1, false, false, false, false },
			{ "gamer", u8"玩家", u8"高效能電源、建議關閉 SysMain，建議玩家清理預設。",
				"gamer", "high", 2, true, true, false, true },
			{ "office", u8"辦公", u8"平衡電源，保留背景索引，建議一般清理預設。",
				"general", "balanced", -1, false, false, false, false },
			{ "minimal", u8"極簡", u8"高效能、視覺效能優先，可關閉非必要服務，建議一般清理。",
				"general", "high", 2, false, true, false, true },
			{ "developer", u8"開發者", u8"平衡電源，保留開發相關背景，建議開發者清理預設。",
				"developer", "balanced", -1, false, false, false, false },
		};

		std::mutex g_mutex;
		Snapshot g_snapshot;
		std::thread g_worker;
		std::atomic<bool> g_scanning{ false };
		std::atomic<bool> g_shutdown{ false };
		char g_last_action[256] = {};
		bool g_has_revert_snapshot = false;
		char g_last_apply_summary[256] = {};

		struct RevertServiceState {
			char name[64] = {};
			uint32_t start_type = 0;
		};

		struct RevertSnapshot {
			char preset_id[32] = {};
			char preset_label[48] = {};
			char power_kind[16] = {};
			int visual_fx = -1;
			bool game_mode = false;
			std::vector<RevertServiceState> services;
		};

		static std::string RevertSnapshotPath()
		{
			return HAppPaths::GetConfigDir() + "\\optimize_revert.json";
		}

		static const char* PowerKindFromGuid(const char* guid)
		{
			if (guid == nullptr || guid[0] == '\0') {
				return "balanced";
			}
			if (strstr(guid, "e9a42b02") != nullptr) {
				return "ultimate";
			}
			if (strstr(guid, "8c5e7fda") != nullptr) {
				return "high";
			}
			if (strstr(guid, "a1841308") != nullptr) {
				return "saver";
			}
			if (strstr(guid, "381b9ba2") != nullptr) {
				return "balanced";
			}
			return "balanced";
		}

		static const char* PowerPlanDisplayNameFromKind(const char* kind)
		{
			if (kind == nullptr) {
				return u8"平衡";
			}
			if (strcmp(kind, "high") == 0) {
				return u8"高效能";
			}
			if (strcmp(kind, "ultimate") == 0) {
				return u8"終極效能";
			}
			if (strcmp(kind, "saver") == 0) {
				return u8"省電";
			}
			return u8"平衡";
		}

		static bool IsUuidCharAt(const char* s, int offset, bool expect_dash)
		{
			const char c = s[offset];
			if (expect_dash) {
				return c == '-';
			}
			return (c >= '0' && c <= '9')
				|| (c >= 'a' && c <= 'f')
				|| (c >= 'A' && c <= 'F');
		}

		static bool TryParseUuidAt(const char* s, char* out, size_t out_size)
		{
			if (s == nullptr || out == nullptr || out_size < 37) {
				return false;
			}
			static const int k_dash_at[] = { 8, 13, 18, 23 };
			for (int i = 0; i < 36; ++i) {
				bool expect_dash = false;
				for (int d : k_dash_at) {
					if (i == d) {
						expect_dash = true;
						break;
					}
				}
				if (!IsUuidCharAt(s, i, expect_dash)) {
					return false;
				}
			}
			memcpy(out, s, 36);
			out[36] = '\0';
			return true;
		}

		static bool FindUuidInBuffer(const char* buf, char* out, size_t out_size)
		{
			if (buf == nullptr || out == nullptr || out_size < 37) {
				return false;
			}
			for (size_t i = 0; buf[i] != '\0'; ++i) {
				if (TryParseUuidAt(buf + i, out, out_size)) {
					return true;
				}
			}
			return false;
		}

		static void FinalizePowerPlanName(Snapshot& snap)
		{
			if (snap.power_plan_guid[0] != '\0') {
				const char* kind = PowerKindFromGuid(snap.power_plan_guid);
				strncpy_s(snap.power_plan_name, PowerPlanDisplayNameFromKind(kind), _TRUNCATE);
				return;
			}
			if (snap.power_plan_name[0] == '\0'
				|| strcmp(snap.power_plan_name, u8"使用中") == 0
				|| strcmp(snap.power_plan_name, u8"未知") == 0) {
				strncpy_s(snap.power_plan_name, u8"平衡", _TRUNCATE);
			}
		}

		static void SaveRevertSnapshot(const RevertSnapshot& snap)
		{
			nlohmann::json j;
			j["preset_id"] = snap.preset_id;
			j["preset_label"] = snap.preset_label;
			j["power_kind"] = snap.power_kind;
			j["visual_fx"] = snap.visual_fx;
			j["game_mode"] = snap.game_mode;
			j["services"] = nlohmann::json::array();
			for (const RevertServiceState& s : snap.services) {
				j["services"].push_back({
					{ "name", s.name },
					{ "start_type", s.start_type },
				});
			}
			const std::string path = RevertSnapshotPath();
			std::ofstream out(path, std::ios::binary);
			if (!out) {
				g_has_revert_snapshot = false;
				return;
			}
			out << j.dump(2);
			g_has_revert_snapshot = true;
			snprintf(g_last_apply_summary, sizeof(g_last_apply_summary),
				I18N(u8"%s：已記錄 %zu 項服務變更，可一鍵還原"),
				snap.preset_label[0] ? snap.preset_label : snap.preset_id,
				snap.services.size());
		}

		static bool LoadRevertSnapshot(RevertSnapshot& out)
		{
			const std::string path = RevertSnapshotPath();
			std::ifstream in(path, std::ios::binary);
			if (!in) {
				return false;
			}
			nlohmann::json j;
			try {
				in >> j;
			}
			catch (...) {
				return false;
			}
			strncpy_s(out.preset_id, j.value("preset_id", "").c_str(), _TRUNCATE);
			strncpy_s(out.preset_label, j.value("preset_label", "").c_str(), _TRUNCATE);
			strncpy_s(out.power_kind, j.value("power_kind", "balanced").c_str(), _TRUNCATE);
			out.visual_fx = j.value("visual_fx", -1);
			out.game_mode = j.value("game_mode", false);
			out.services.clear();
			if (j.contains("services") && j["services"].is_array()) {
				for (const auto& item : j["services"]) {
					RevertServiceState s = {};
					strncpy_s(s.name, item.value("name", "").c_str(), _TRUNCATE);
					s.start_type = item.value("start_type", 0u);
					if (s.name[0] != '\0') {
						out.services.push_back(s);
					}
				}
			}
			return out.preset_id[0] != '\0' || !out.services.empty();
		}

		static void RefreshRevertFlagFromDisk()
		{
			RevertSnapshot snap = {};
			g_has_revert_snapshot = LoadRevertSnapshot(snap);
			if (g_has_revert_snapshot) {
				snprintf(g_last_apply_summary, sizeof(g_last_apply_summary),
					I18N(u8"%s：可還原上次優化"),
					snap.preset_label[0] ? snap.preset_label : snap.preset_id);
			}
		}

		static void SetLastAction(const char* msg)
		{
			if (msg == nullptr) {
				g_last_action[0] = '\0';
				return;
			}
			strncpy_s(g_last_action, msg, _TRUNCATE);
		}

		static bool Utf8FromWide(const wchar_t* wide, char* out, size_t out_size)
		{
			if (wide == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size),
				nullptr, nullptr);
			return n > 0;
		}

		static bool RunHiddenCommand(const wchar_t* cmdline)
		{
			if (cmdline == nullptr) {
				return false;
			}
			if (!HCleanIsRunningAsAdmin() && HElevationBroker::IsConnected()) {
				return HElevationBroker::RunHiddenCommand(cmdline, 120000, nullptr);
			}
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			PROCESS_INFORMATION pi = {};
			std::vector<wchar_t> cmd_buf(cmdline, cmdline + wcslen(cmdline) + 1);
			if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
				CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
				return false;
			}
			WaitForSingleObject(pi.hProcess, 120000);
			DWORD exit_code = 1;
			GetExitCodeProcess(pi.hProcess, &exit_code);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return exit_code == 0;
		}

		static void NormalizeCapturedConsoleOutput(char* buf, size_t buf_size);

		static bool RunProcessCaptureStdout(const wchar_t* cmdline, char* out_buf, size_t out_size,
			DWORD* exit_code_out = nullptr, DWORD timeout_ms = 120000)
		{
			if (cmdline == nullptr || out_buf == nullptr || out_size == 0) {
				return false;
			}
			out_buf[0] = '\0';
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
			si.wShowWindow = SW_HIDE;
			SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
			HANDLE read_pipe = nullptr;
			HANDLE write_pipe = nullptr;
			if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
				return false;
			}
			SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
			si.hStdOutput = write_pipe;
			si.hStdError = write_pipe;
			PROCESS_INFORMATION pi = {};
			std::vector<wchar_t> cmd_buf(cmdline, cmdline + wcslen(cmdline) + 1);
			if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
				nullptr, nullptr, &si, &pi)) {
				CloseHandle(read_pipe);
				CloseHandle(write_pipe);
				return false;
			}
			CloseHandle(write_pipe);
			size_t total = 0;
			for (;;) {
				char chunk[512] = {};
				DWORD read = 0;
				if (!ReadFile(read_pipe, chunk, sizeof(chunk) - 1, &read, nullptr) || read == 0) {
					break;
				}
				const size_t limit = out_size - 1 - total;
				const size_t copy = static_cast<size_t>(read) < limit
					? static_cast<size_t>(read) : limit;
				memcpy(out_buf + total, chunk, copy);
				total += copy;
				out_buf[total] = '\0';
				if (total >= out_size - 1) {
					break;
				}
			}
			WaitForSingleObject(pi.hProcess, timeout_ms);
			DWORD exit_code = 1;
			GetExitCodeProcess(pi.hProcess, &exit_code);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			CloseHandle(read_pipe);
			if (exit_code_out != nullptr) {
				*exit_code_out = exit_code;
			}
			NormalizeCapturedConsoleOutput(out_buf, out_size);
			return exit_code == 0;
		}

		static void NormalizeCapturedConsoleOutput(char* buf, size_t buf_size)
		{
			if (buf == nullptr || buf_size < 2 || buf[0] == '\0') {
				return;
			}
			UINT code_page = CP_OEMCP;
			int wide_len = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS,
				buf, -1, nullptr, 0);
			if (wide_len <= 0) {
				code_page = CP_ACP;
				wide_len = MultiByteToWideChar(code_page, 0, buf, -1, nullptr, 0);
			}
			if (wide_len <= 0) {
				return;
			}
			std::vector<wchar_t> wide(static_cast<size_t>(wide_len));
			if (MultiByteToWideChar(code_page, 0, buf, -1, wide.data(), wide_len) <= 0) {
				return;
			}
			const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1,
				nullptr, 0, nullptr, nullptr);
			if (utf8_len <= 0) {
				return;
			}
			std::vector<char> utf8(static_cast<size_t>(utf8_len));
			if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1,
				utf8.data(), utf8_len, nullptr, nullptr) <= 0) {
				return;
			}
			strncpy_s(buf, buf_size, utf8.data(), _TRUNCATE);
		}

		static bool RunCommandCaptureStdout(const wchar_t* cmdline, char* out_buf, size_t out_size,
			DWORD* exit_code_out = nullptr, DWORD timeout_ms = 120000)
		{
			if (cmdline == nullptr || out_buf == nullptr || out_size == 0) {
				return false;
			}
			if (!HCleanIsRunningAsAdmin() && HElevationBroker::IsConnected()) {
				wchar_t temp_dir[MAX_PATH] = {};
				if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
					return false;
				}
				wchar_t out_file[MAX_PATH] = {};
				_snwprintf_s(out_file, _TRUNCATE, L"%shp_cmd_%llu.txt",
					temp_dir, static_cast<unsigned long long>(GetTickCount64()));
				wchar_t wrapper[1024] = {};
				_snwprintf_s(wrapper, _TRUNCATE,
					L"cmd.exe /C \"(%s) > \"%s\" 2>&1\"", cmdline, out_file);
				DWORD exit_code = 1;
				const bool run_ok = HElevationBroker::RunHiddenCommand(wrapper, timeout_ms, &exit_code);
				out_buf[0] = '\0';
				const HANDLE hf = CreateFileW(out_file, GENERIC_READ, FILE_SHARE_READ, nullptr,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hf != INVALID_HANDLE_VALUE) {
					size_t total = 0;
					for (;;) {
						char chunk[512] = {};
						DWORD read = 0;
						if (!ReadFile(hf, chunk, sizeof(chunk) - 1, &read, nullptr) || read == 0) {
							break;
						}
						const size_t limit = out_size - 1 - total;
						const size_t copy = static_cast<size_t>(read) < limit
							? static_cast<size_t>(read) : limit;
						memcpy(out_buf + total, chunk, copy);
						total += copy;
						out_buf[total] = '\0';
						if (total >= out_size - 1) {
							break;
						}
					}
					CloseHandle(hf);
				}
				DeleteFileW(out_file);
				NormalizeCapturedConsoleOutput(out_buf, out_size);
				if (exit_code_out != nullptr) {
					*exit_code_out = exit_code;
				}
				return run_ok && exit_code == 0;
			}
			return RunProcessCaptureStdout(cmdline, out_buf, out_size, exit_code_out, timeout_ms);
		}

		static bool RunPowerCfgCapture(const wchar_t* args, char* out_buf, size_t out_size,
			DWORD* exit_code_out = nullptr)
		{
			wchar_t cmd[512] = {};
			_snwprintf_s(cmd, _TRUNCATE, L"powercfg.exe %s", args);
			return RunProcessCaptureStdout(cmd, out_buf, out_size, exit_code_out);
		}

		static bool RunPowerCfgSetActive(const wchar_t* guid)
		{
			wchar_t cmd[256] = {};
			_snwprintf_s(cmd, _TRUNCATE, L"powercfg.exe /setactive %s", guid);
			return RunHiddenCommand(cmd);
		}

		static bool IsHexChar(char c)
		{
			return (c >= '0' && c <= '9')
				|| (c >= 'a' && c <= 'f')
				|| (c >= 'A' && c <= 'F');
		}

		static bool ParseGuidFromPowerCfgText(const char* text, wchar_t* guid_out, size_t guid_out_count)
		{
			if (text == nullptr || guid_out == nullptr || guid_out_count < 37) {
				return false;
			}
			for (const char* p = text; *p != '\0'; ++p) {
				if (strlen(p) < 36) {
					continue;
				}
				bool valid = true;
				for (int i = 0; i < 36 && valid; ++i) {
					const char c = p[i];
					if (i == 8 || i == 13 || i == 18 || i == 23) {
						valid = (c == '-');
					}
					else {
						valid = IsHexChar(c);
					}
				}
				if (!valid) {
					continue;
				}
				wchar_t wide[48] = {};
				for (int i = 0; i < 36; ++i) {
					wide[i] = static_cast<wchar_t>(p[i]);
				}
				wide[36] = L'\0';
				wcsncpy_s(guid_out, guid_out_count, wide, _TRUNCATE);
				return true;
			}
			return false;
		}

		static bool PowerPlanLineLooksUltimate(const char* line)
		{
			if (line == nullptr) {
				return false;
			}
			return strstr(line, "e9a42b02") != nullptr
				|| strstr(line, "Ultimate") != nullptr
				|| strstr(line, "ultimate") != nullptr
				|| strstr(line, I18N(u8"終極")) != nullptr
				|| strstr(line, I18N(u8"终极")) != nullptr;
		}

		static bool FindUltimatePowerPlanGuid(wchar_t* guid_out, size_t guid_out_count)
		{
			char buf[8192] = {};
			if (!RunPowerCfgCapture(L"/list", buf, sizeof(buf))) {
				return false;
			}
			char* ctx = nullptr;
			char* line = strtok_s(buf, "\r\n", &ctx);
			while (line != nullptr) {
				if (PowerPlanLineLooksUltimate(line)
					&& ParseGuidFromPowerCfgText(line, guid_out, guid_out_count)) {
					return true;
				}
				line = strtok_s(nullptr, "\r\n", &ctx);
			}
			return false;
		}

		static bool QueryUltimatePlanAvailable()
		{
			wchar_t guid[48] = {};
			return FindUltimatePowerPlanGuid(guid, std::size(guid));
		}

		static bool ActivateUltimatePowerPlan(bool* duplicated_out = nullptr)
		{
			if (duplicated_out != nullptr) {
				*duplicated_out = false;
			}
			if (RunPowerCfgSetActive(kUltimateGuid)) {
				return true;
			}

			wchar_t existing[48] = {};
			if (FindUltimatePowerPlanGuid(existing, std::size(existing))) {
				return RunPowerCfgSetActive(existing);
			}

			char dup_out[512] = {};
			DWORD dup_exit = 1;
			if (!RunPowerCfgCapture(
				L"-duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61",
				dup_out, sizeof(dup_out), &dup_exit)) {
				HLOG_WARN("OptimizeScan: duplicatescheme ultimate exit={} out='{}'",
					dup_exit, dup_out);
				return false;
			}

			wchar_t new_guid[48] = {};
			if (!ParseGuidFromPowerCfgText(dup_out, new_guid, std::size(new_guid))) {
				HLOG_WARN("OptimizeScan: duplicatescheme ultimate parse failed out='{}'", dup_out);
				return false;
			}
			if (duplicated_out != nullptr) {
				*duplicated_out = true;
			}
			return RunPowerCfgSetActive(new_guid);
		}

		static void SetVisualFxSetting(int setting)
		{
			HKEY key = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects", 0, nullptr, 0,
				KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
				return;
			}
			const DWORD val = static_cast<DWORD>(setting);
			RegSetValueExW(key, L"VisualFXSetting", 0, REG_DWORD,
				reinterpret_cast<const BYTE*>(&val), sizeof(val));
			RegCloseKey(key);
		}

		static void SetGameMode(bool on)
		{
			HKEY key = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", 0, nullptr, 0,
				KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
				return;
			}
			const DWORD val = on ? 1u : 0u;
			RegSetValueExW(key, L"AutoGameModeEnabled", 0, REG_DWORD,
				reinterpret_cast<const BYTE*>(&val), sizeof(val));
			RegCloseKey(key);
		}

		static void QueryPowerPlan(Snapshot& snap)
		{
			wchar_t cmd[] = L"powercfg.exe /getactivescheme";
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
			si.wShowWindow = SW_HIDE;
			SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
			HANDLE read_pipe = nullptr;
			HANDLE write_pipe = nullptr;
			if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
				strncpy_s(snap.power_plan_name, u8"未知", _TRUNCATE);
				return;
			}
			SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
			si.hStdOutput = write_pipe;
			si.hStdError = write_pipe;
			PROCESS_INFORMATION pi = {};
			if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
				nullptr, nullptr, &si, &pi)) {
				CloseHandle(read_pipe);
				CloseHandle(write_pipe);
				strncpy_s(snap.power_plan_name, u8"未知", _TRUNCATE);
				return;
			}
			CloseHandle(write_pipe);
			char buf[512] = {};
			DWORD read = 0;
			ReadFile(read_pipe, buf, sizeof(buf) - 1, &read, nullptr);
			buf[read] = '\0';
			WaitForSingleObject(pi.hProcess, 10000);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			CloseHandle(read_pipe);

			char guid_buf[48] = {};
			if (FindUuidInBuffer(buf, guid_buf, sizeof(guid_buf))) {
				strncpy_s(snap.power_plan_guid, guid_buf, _TRUNCATE);
			}
			FinalizePowerPlanName(snap);
		}

		static void QueryGameMode(Snapshot& snap)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER,
				L"Software\\Microsoft\\GameBar", 0, KEY_READ, &key) != ERROR_SUCCESS) {
				return;
			}
			DWORD val = 0;
			DWORD size = sizeof(val);
			if (RegQueryValueExW(key, L"AutoGameModeEnabled", nullptr, nullptr,
				reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
				snap.game_mode_on = (val != 0);
			}
			RegCloseKey(key);
		}

		static void QueryVisualFx(Snapshot& snap)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects", 0,
				KEY_READ, &key) != ERROR_SUCCESS) {
				return;
			}
			DWORD val = 0;
			DWORD size = sizeof(val);
			if (RegQueryValueExW(key, L"VisualFXSetting", nullptr, nullptr,
				reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
				snap.visual_fx_setting = static_cast<int>(val);
			}
			RegCloseKey(key);
		}

		static bool QueryRegDwordHKCU(const wchar_t* subkey, const wchar_t* value_name, DWORD& out)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
				return false;
			}
			DWORD size = sizeof(out);
			const bool ok = RegQueryValueExW(key, value_name, nullptr, nullptr,
				reinterpret_cast<LPBYTE>(&out), &size) == ERROR_SUCCESS;
			RegCloseKey(key);
			return ok;
		}

		static bool QueryRegDwordHKLM(const wchar_t* subkey, const wchar_t* value_name, DWORD& out)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
				return false;
			}
			DWORD size = sizeof(out);
			const bool ok = RegQueryValueExW(key, value_name, nullptr, nullptr,
				reinterpret_cast<LPBYTE>(&out), &size) == ERROR_SUCCESS;
			RegCloseKey(key);
			return ok;
		}

		static void QuerySystemPerfExtras(Snapshot& snap)
		{
			DWORD val = 0;
			if (QueryRegDwordHKCU(
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
				L"EnableTransparency", val)) {
				snap.transparency_on = (val != 0);
			}

			if (QueryRegDwordHKLM(
				L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power",
				L"HiberbootEnabled", val)) {
				snap.fast_startup_on = (val != 0);
			}

			if (QueryRegDwordHKCU(L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
				L"AppCaptureEnabled", val)) {
				snap.game_dvr_on = (val != 0);
			}
			else if (QueryRegDwordHKCU(L"Software\\Microsoft\\GameBar",
				L"AppCaptureEnabled", val)) {
				snap.game_dvr_on = (val != 0);
			}

			if (QueryRegDwordHKLM(L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
				L"Win32PrioritySeparation", val)) {
				snap.processor_foreground = (val == 26u || val == 38u);
			}

			ANIMATIONINFO anim = { sizeof(ANIMATIONINFO) };
			if (SystemParametersInfoW(SPI_GETANIMATION, sizeof(anim), &anim, 0)) {
				snap.animations_on = (anim.iMinAnimate != 0);
			}

			if (QueryRegDwordHKLM(L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
				L"HwSchMode", val)) {
				snap.gpu_scheduling_on = (val == 2u);
			}

			if (QueryRegDwordHKLM(L"SYSTEM\\CurrentControlSet\\Control\\Power",
				L"HibernateEnabled", val)) {
				snap.hibernate_on = (val != 0);
			}

			HKEY mouse_key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, KEY_READ,
				&mouse_key) == ERROR_SUCCESS) {
				wchar_t speed[8] = {};
				DWORD speed_size = sizeof(speed);
				if (RegQueryValueExW(mouse_key, L"MouseSpeed", nullptr, nullptr,
					reinterpret_cast<LPBYTE>(speed), &speed_size) == ERROR_SUCCESS) {
					snap.mouse_accel_on = (wcstol(speed, nullptr, 10) != 0);
				}
				RegCloseKey(mouse_key);
			}

			DWORD fse = 0;
			if (QueryRegDwordHKCU(L"System\\GameConfigStore", L"GameDVR_FSEBehavior", fse)) {
				snap.fullscreen_opt_on = (fse != 2u);
			}
			else {
				snap.fullscreen_opt_on = true;
			}

			DWORD tips = 1;
			if (QueryRegDwordHKCU(
				L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
				L"SubscribedContent-338388Enabled", tips)) {
				snap.tips_suggestions_on = (tips != 0);
			}
			else if (QueryRegDwordHKCU(
				L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
				L"SoftLandingEnabled", tips)) {
				snap.tips_suggestions_on = (tips != 0);
			}

			DWORD bg_disabled = 0;
			if (QueryRegDwordHKCU(
				L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
				L"GlobalUserDisabled", bg_disabled)) {
				snap.background_apps_on = (bg_disabled == 0);
			}

			DWORD throttle_off = 0;
			if (QueryRegDwordHKLM(L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling",
				L"PowerThrottlingOff", throttle_off)) {
				snap.power_throttling_on = (throttle_off == 0);
			}

			DWORD game_bar = 1;
			if (QueryRegDwordHKCU(L"System\\GameConfigStore", L"GameBarEnabled", game_bar)) {
				snap.game_bar_on = (game_bar != 0);
			}

			DWORD download_mode = 0;
			if (QueryRegDwordHKLM(
				L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DeliveryOptimization\\Settings",
				L"DownloadMode", download_mode)) {
				snap.delivery_p2p_on = (download_mode >= 1u && download_mode <= 3u);
			}

			DWORD search_dyn = 1;
			if (QueryRegDwordHKCU(L"Software\\Microsoft\\Windows\\CurrentVersion\\Search",
				L"ShowDynamicContentInWSB", search_dyn)) {
				snap.search_highlights_on = (search_dyn != 0);
			}
			else {
				DWORD bing = 1;
				if (QueryRegDwordHKCU(L"Software\\Microsoft\\Windows\\CurrentVersion\\Search",
					L"BingSearchEnabled", bing)) {
					snap.search_highlights_on = (bing != 0);
				}
			}

			DWORD widgets = 1;
			if (QueryRegDwordHKCU(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
				L"TaskbarDa", widgets)) {
				snap.widgets_on = (widgets != 0);
			}

			DWORD net_throttle = 10;
			if (QueryRegDwordHKLM(
				L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
				L"NetworkThrottlingIndex", net_throttle)) {
				snap.network_throttling_on = (net_throttle != 0xFFFFFFFFu);
			}

			DWORD sys_resp = 20;
			if (QueryRegDwordHKLM(
				L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
				L"SystemResponsiveness", sys_resp)) {
				snap.game_responsiveness_on = (sys_resp == 0u);
			}

			HKEY desk = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ,
				&desk) == ERROR_SUCCESS) {
				wchar_t delay[16] = {};
				DWORD delay_size = sizeof(delay);
				DWORD delay_type = 0;
				if (RegQueryValueExW(desk, L"MenuShowDelay", nullptr, &delay_type,
					reinterpret_cast<LPBYTE>(delay), &delay_size) == ERROR_SUCCESS
					&& delay_type == REG_SZ) {
					snap.fast_menu_delay = (wcstol(delay, nullptr, 10) <= 100);
				}
				RegCloseKey(desk);
			}

			snap.ultimate_plan_available = QueryUltimatePlanAvailable();
		}

		static void QuerySystemDriveUsage(Snapshot& snap)
		{
			ULARGE_INTEGER free_bytes = {};
			ULARGE_INTEGER total = {};
			if (!GetDiskFreeSpaceExW(L"C:\\", &free_bytes, &total, nullptr) || total.QuadPart == 0) {
				return;
			}
			const uint64_t used = total.QuadPart - free_bytes.QuadPart;
			snap.system_drive_free_bytes = static_cast<int64_t>(free_bytes.QuadPart);
			snap.system_drive_total_bytes = static_cast<int64_t>(total.QuadPart);
			snap.system_drive_used_percent = static_cast<float>(used) / static_cast<float>(total.QuadPart) * 100.f;
		}

		static constexpr DWORD kDefragCommandTimeoutMs = 600000;

		static DiskOptimizationSnapshot g_disk_opt = {};
		static std::mutex g_disk_opt_mutex;
		static std::thread g_disk_opt_worker;
		static std::atomic<bool> g_disk_opt_running{ false };

		static StorageWorkSnapshot g_storage_work = {};
		static std::mutex g_storage_work_mutex;
		static std::thread g_storage_worker;
		static std::atomic<bool> g_storage_worker_running{ false };
		static bool g_storage_monitor_clean_scan = false;
		static char g_storage_last_scan_task[128] = {};
		static char g_storage_last_progress_status[160] = {};
		static char g_disk_maint_drive = 'C';
		static std::vector<StorageDriveInfo> g_storage_drives;

		static void RefreshStorageDriveList()
		{
			g_storage_drives.clear();
			const DWORD mask = GetLogicalDrives();
			for (int i = 0; i < 26; ++i) {
				if ((mask & (1u << i)) == 0) {
					continue;
				}
				const wchar_t letter = static_cast<wchar_t>(L'A' + i);
				wchar_t root[4] = { letter, L':', L'\\', L'\0' };
				const UINT drive_type = GetDriveTypeW(root);
				if (drive_type != DRIVE_FIXED && drive_type != DRIVE_REMOVABLE) {
					continue;
				}
				StorageDriveInfo info = {};
				info.letter = static_cast<char>(letter);
				info.drive_type = drive_type;
				wchar_t vol_name[64] = {};
				if (GetVolumeInformationW(root, vol_name, static_cast<DWORD>(std::size(vol_name)),
					nullptr, nullptr, nullptr, nullptr, 0)) {
					WideCharToMultiByte(CP_UTF8, 0, vol_name, -1, info.label,
						static_cast<int>(sizeof(info.label)), nullptr, nullptr);
				}
				ULARGE_INTEGER free_bytes = {};
				ULARGE_INTEGER total = {};
				if (GetDiskFreeSpaceExW(root, &free_bytes, &total, nullptr)) {
					info.free_bytes = static_cast<int64_t>(free_bytes.QuadPart);
					info.total_bytes = static_cast<int64_t>(total.QuadPart);
				}
				g_storage_drives.push_back(info);
			}
			if (g_storage_drives.empty()) {
				StorageDriveInfo fallback = {};
				fallback.letter = 'C';
				strncpy_s(fallback.label, u8"系統碟", _TRUNCATE);
				g_storage_drives.push_back(fallback);
			}
			bool found = false;
			for (const auto& d : g_storage_drives) {
				if (d.letter == g_disk_maint_drive) {
					found = true;
					break;
				}
			}
			if (!found) {
				g_disk_maint_drive = g_storage_drives[0].letter;
			}
		}

		static constexpr wchar_t kStoragePoliciesKey[] =
			L"Software\\Microsoft\\Windows\\CurrentVersion\\StorageSense\\Parameters\\StoragePolicies";

		static float ClampUnitFloat(float value)
		{
			if (value < 0.f) {
				return 0.f;
			}
			if (value > 1.f) {
				return 1.f;
			}
			return value;
		}

		static void StorageWorkSetLocked(float progress, const char* status)
		{
			g_storage_work.progress = ClampUnitFloat(progress);
			if (status != nullptr && status[0] != '\0') {
				strncpy_s(g_storage_work.status_text, status, _TRUNCATE);
			}
		}

		static void StorageWorkLogLocked(const char* line)
		{
			if (line == nullptr || line[0] == '\0') {
				return;
			}
			const int max_lines = StorageWorkSnapshot::kMaxLogLines;
			if (g_storage_work.log_count < max_lines) {
				strncpy_s(g_storage_work.log_lines[g_storage_work.log_count], line, _TRUNCATE);
				++g_storage_work.log_count;
			}
			else {
				for (int i = 1; i < max_lines; ++i) {
					strncpy_s(g_storage_work.log_lines[i - 1],
						g_storage_work.log_lines[i], _TRUNCATE);
				}
				strncpy_s(g_storage_work.log_lines[max_lines - 1], line, _TRUNCATE);
			}
		}

		static void StorageWorkBegin(const char* job_name)
		{
			std::lock_guard<std::mutex> lock(g_storage_work_mutex);
			g_storage_work.running = true;
			g_storage_work.progress = 0.f;
			g_storage_work.last_result_bytes = 0;
			if (job_name != nullptr) {
				strncpy_s(g_storage_work.job_name, job_name, _TRUNCATE);
			}
			g_storage_last_progress_status[0] = '\0';
			StorageWorkSetLocked(0.f, u8"準備中…");
			HLOG_INFO("OptimizeScan: Storage 開始作業 '{}'", job_name != nullptr ? job_name : "");
		}

		static void StorageWorkEnd(const char* status, float progress = 1.f)
		{
			std::lock_guard<std::mutex> lock(g_storage_work_mutex);
			g_storage_work.running = false;
			StorageWorkSetLocked(progress, status != nullptr ? status : I18N(u8"完成"));
			HLOG_INFO("OptimizeScan: Storage 作業結束 '{}' progress={:.2f}",
				status != nullptr ? status : I18N(u8"完成"), static_cast<double>(progress));
		}

		static void StorageWorkLog(const char* line)
		{
			std::lock_guard<std::mutex> lock(g_storage_work_mutex);
			StorageWorkLogLocked(line);
			HLOG_INFO("OptimizeScan: Storage {}", line);
		}

		static void StorageWorkSetProgress(float progress, const char* status)
		{
			std::lock_guard<std::mutex> lock(g_storage_work_mutex);
			StorageWorkSetLocked(progress, status);
			if (status != nullptr && status[0] != '\0'
				&& strcmp(g_storage_last_progress_status, status) != 0) {
				strncpy_s(g_storage_last_progress_status, status, _TRUNCATE);
				HLOG_DEBUG("OptimizeScan: Storage progress={:.2f} {}",
					static_cast<double>(ClampUnitFloat(progress)), status);
			}
		}

		static bool QueryStoragePolicyDword(const wchar_t* value_name, DWORD& out)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kStoragePoliciesKey, 0, KEY_READ, &key)
				!= ERROR_SUCCESS) {
				return false;
			}
			DWORD size = sizeof(out);
			const bool ok = RegQueryValueExW(key, value_name, nullptr, nullptr,
				reinterpret_cast<LPBYTE>(&out), &size) == ERROR_SUCCESS;
			RegCloseKey(key);
			return ok;
		}

		static bool SetStoragePolicyDword(const wchar_t* value_name, DWORD value)
		{
			HKEY key = nullptr;
			DWORD disp = 0;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, kStoragePoliciesKey, 0, nullptr, 0,
				KEY_SET_VALUE, nullptr, &key, &disp) != ERROR_SUCCESS) {
				SetLastAction(u8"無法寫入儲存感知設定");
				HLOG_WARN("OptimizeScan: SetStoragePolicyDword 無法開啟登錄鍵 err={}",
					GetLastError());
				return false;
			}
			const LONG err = RegSetValueExW(key, value_name, 0, REG_DWORD,
				reinterpret_cast<const BYTE*>(&value), sizeof(value));
			RegCloseKey(key);
			if (err != ERROR_SUCCESS) {
				SetLastAction(u8"儲存感知設定寫入失敗");
				HLOG_WARN("OptimizeScan: SetStoragePolicyDword 寫入失敗 value={} err={}",
					value, err);
				return false;
			}
			return true;
		}

		static void QueryStorageLocalSettings(StorageLocalSettings& out)
		{
			out.valid = true;
			DWORD val = 0;
			if (QueryStoragePolicyDword(L"01", val)) {
				out.storage_sense_on = (val != 0);
			}
			else if (QueryStoragePolicyDword(L"2048", val)) {
				out.storage_sense_on = (val != 0);
			}
			if (QueryStoragePolicyDword(L"02", val)) {
				out.auto_temp_cleanup = (val != 0);
			}
			if (QueryStoragePolicyDword(L"256", val)) {
				out.low_disk_auto_run = (val != 0);
			}
			if (QueryStoragePolicyDword(L"04", val)) {
				out.recycle_bin_days = static_cast<int>(val);
			}
		}

		static int64_t StorageCleanTempDir(const wchar_t* path, const char* label)
		{
			if (path == nullptr || path[0] == L'\0') {
				return 0;
			}
			char log_ctx[96] = {};
			snprintf(log_ctx, sizeof(log_ctx), "StorageQuickClean:%s", label != nullptr ? label : "temp");
			HCleanDeleteStats stats = {};
			const int64_t freed = HCleanSafeDeleteFilesShallow(path, log_ctx, &stats);
			char line[128] = {};
			char sz[32] = {};
			FormatCleanSize(freed > 0 ? freed : stats.bytes_freed, sz, sizeof(sz));
			snprintf(line, sizeof(line), I18N(u8"%s：釋放 %s"), label != nullptr ? label : I18N(u8"暫存"), sz);
			StorageWorkLog(line);
			return freed > 0 ? freed : stats.bytes_freed;
		}

		static void RunStorageQuickCleanWorker(uint32_t flags)
		{
			g_storage_worker_running.store(true, std::memory_order_release);
			StorageWorkBegin(I18N(u8"本機快速清理"));
			StorageWorkLog(I18N(u8"開始本機快速清理（不開啟外部精靈）"));
			int steps = 0;
			if ((flags & static_cast<uint32_t>(StorageQuickCleanFlags::TempFiles)) != 0) {
				++steps;
			}
			if ((flags & static_cast<uint32_t>(StorageQuickCleanFlags::RecycleBin)) != 0) {
				++steps;
			}
			if ((flags & static_cast<uint32_t>(StorageQuickCleanFlags::DeliveryCache)) != 0) {
				++steps;
			}
			if (steps < 1) {
				steps = 1;
			}
			int step = 0;
			int64_t total_freed = 0;

			if ((flags & static_cast<uint32_t>(StorageQuickCleanFlags::TempFiles)) != 0) {
				StorageWorkSetProgress(static_cast<float>(step) / static_cast<float>(steps),
					I18N(u8"清理暫存檔…"));
				wchar_t user_temp[MAX_PATH] = {};
				wchar_t win_dir[MAX_PATH] = {};
				if (GetTempPathW(MAX_PATH, user_temp) > 0) {
					total_freed += StorageCleanTempDir(user_temp, I18N(u8"使用者暫存"));
				}
				if (GetWindowsDirectoryW(win_dir, MAX_PATH) > 0) {
					wchar_t win_temp[MAX_PATH] = {};
					_snwprintf_s(win_temp, _TRUNCATE, L"%s\\Temp", win_dir);
					total_freed += StorageCleanTempDir(win_temp, I18N(u8"系統暫存"));
				}
				++step;
			}

			if ((flags & static_cast<uint32_t>(StorageQuickCleanFlags::RecycleBin)) != 0) {
				StorageWorkSetProgress(static_cast<float>(step) / static_cast<float>(steps),
					I18N(u8"清空資源回收筒…"));
				StorageWorkLog(I18N(u8"清空資源回收筒…"));
				const bool ok = HCleanEmptyRecycleBin();
				StorageWorkLog(ok ? I18N(u8"資源回收筒已清空") : I18N(u8"資源回收筒清空失敗或為空"));
				++step;
			}

			if ((flags & static_cast<uint32_t>(StorageQuickCleanFlags::DeliveryCache)) != 0) {
				StorageWorkSetProgress(static_cast<float>(step) / static_cast<float>(steps),
					I18N(u8"清理傳遞優化快取…"));
				StorageWorkLog(I18N(u8"清理 Windows 更新傳遞快取…"));
				const bool ok = HCleanClearDeliveryOptimizationCache();
				StorageWorkLog(ok ? I18N(u8"傳遞優化快取已清理") : I18N(u8"傳遞優化快取清理失敗"));
				++step;
			}

			{
				std::lock_guard<std::mutex> lock(g_storage_work_mutex);
				g_storage_work.last_result_bytes = total_freed;
			}
			char done[96] = {};
			char sz[32] = {};
			FormatCleanSize(total_freed, sz, sizeof(sz));
			snprintf(done, sizeof(done), I18N(u8"本機快速清理完成（釋放 %s）"), sz);
			StorageWorkLog(done);
			HLOG_INFO("OptimizeScan: RunStorageQuickClean done flags=0x{:X} freed={}",
				flags, total_freed);
			StorageWorkEnd(done, 1.f);
			SetLastAction(done);
			g_storage_worker_running.store(false, std::memory_order_release);
		}

		static void RunStorageRestorePointWorker()
		{
			g_storage_worker_running.store(true, std::memory_order_release);
			StorageWorkBegin(I18N(u8"建立還原點"));
			StorageWorkLog(I18N(u8"正在建立系統還原點…"));
			StorageWorkSetProgress(0.35f, I18N(u8"建立還原點中…"));
			const bool ok = RunHiddenCommand(
				L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
				L"\"Checkpoint-Computer -Description 'HP CLEANER++' -RestorePointType MODIFY_SETTINGS\"");
			SetLastAction(ok ? I18N(u8"已要求建立還原點") : I18N(u8"還原點建立失敗"));
			StorageWorkSetProgress(1.f, ok ? I18N(u8"還原點建立完成") : I18N(u8"還原點建立失敗"));
			StorageWorkLog(ok ? I18N(u8"系統還原點建立成功") : I18N(u8"系統還原點建立失敗"));
			HLOG_INFO("OptimizeScan: RunStorageRestorePoint ok={}", ok);
			StorageWorkEnd(ok ? I18N(u8"還原點建立完成") : I18N(u8"還原點建立失敗"), 1.f);
			g_storage_worker_running.store(false, std::memory_order_release);
		}

		static bool StrIContains(const char* hay, const char* needle)
		{
			if (hay == nullptr || needle == nullptr || needle[0] == '\0') {
				return false;
			}
			const size_t nlen = strlen(needle);
			for (const char* p = hay; *p != '\0'; ++p) {
				bool match = true;
				for (size_t i = 0; i < nlen; ++i) {
					const char a = p[i];
					const char b = needle[i];
					if (tolower(static_cast<unsigned char>(a))
						!= tolower(static_cast<unsigned char>(b))) {
						match = false;
						break;
					}
				}
				if (match) {
					return true;
				}
			}
			return false;
		}

		static bool DefragOutputIndicatesUnavailable(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return false;
			}
			static const char* patterns[] = {
				"optimization is not available",
				"not available for this volume",
				I18N(u8"無法進行最佳化"),
				I18N(u8"不支援此磁碟區"),
				nullptr,
			};
			for (int i = 0; patterns[i] != nullptr; ++i) {
				if (StrIContains(text, patterns[i])) {
					return true;
				}
			}
			return false;
		}

		static bool DefragOutputIndicatesError(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return false;
			}
			if (DefragOutputIndicatesUnavailable(text)) {
				return true;
			}
			static const char* patterns[] = {
				"insufficient privileges",
				"insufficient privilege",
				"cannot start",
				"access is denied",
				"access denied",
				"elevated permissions",
				"not have permission",
				I18N(u8"權限不足"),
				I18N(u8"沒有足夠的權限"),
				I18N(u8"沒有足夠權限"),
				I18N(u8"存取被拒"),
				I18N(u8"無法啟動"),
				nullptr,
			};
			for (int i = 0; patterns[i] != nullptr; ++i) {
				if (StrIContains(text, patterns[i])) {
					return true;
				}
			}
			return false;
		}

		static int ParsePercentNear(const char* text, const char* label)
		{
			if (text == nullptr || label == nullptr) {
				return -1;
			}
			const char* p = strstr(text, label);
			if (p == nullptr) {
				return -1;
			}
			p += strlen(label);
			for (int skip = 0; skip < 32 && *p != '\0'; ++skip, ++p) {
				if ((*p >= '0' && *p <= '9') || *p == '-') {
					break;
				}
			}
			if (*p == '\0') {
				return -1;
			}
			return atoi(p);
		}

		static int ParseChineseFragmentPercent(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return -1;
			}
			for (const char* p = text; *p != '\0'; ++p) {
				if (*p != '%') {
					continue;
				}
				const char* q = p + 1;
				while (*q == ' ' || *q == '\t') {
					++q;
				}
				if (strncmp(q, I18N(u8"分散"), strlen(I18N(u8"分散"))) != 0) {
					continue;
				}
				int end = static_cast<int>(p - text) - 1;
				while (end >= 0 && (text[end] == ' ' || text[end] == '\t' || text[end] == '(')) {
					--end;
				}
				int start = end;
				while (start >= 0 && text[start] >= '0' && text[start] <= '9') {
					--start;
				}
				if (start < end) {
					char num[16] = {};
					const int len = end - start;
					if (len > 0 && len < static_cast<int>(sizeof(num))) {
						memcpy(num, text + start + 1, static_cast<size_t>(len));
						const int val = atoi(num);
						if (val >= 0 && val <= 100) {
							return val;
						}
					}
				}
			}
			return -1;
		}

		static int ParseFragmentationPercent(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return -1;
			}
			static const char* labels[] = {
				"Percent file fragmentation",
				"Total fragmented space",
				"Total fragmentation",
				"Percent MFT fragmentation",
				"MFT fragmentation",
				I18N(u8"檔案分散百分比"),
				I18N(u8"總分散空間"),
				I18N(u8"總分散"),
				nullptr,
			};
			for (int i = 0; labels[i] != nullptr; ++i) {
				const int val = ParsePercentNear(text, labels[i]);
				if (val >= 0) {
					return val;
				}
			}
			const int zh_val = ParseChineseFragmentPercent(text);
			if (zh_val >= 0) {
				return zh_val;
			}
			for (const char* p = text; *p != '\0'; ++p) {
				if (*p != '%') {
					continue;
				}
				char window[64] = {};
				const ptrdiff_t back = p - text;
				const size_t win_start = back > 48 ? static_cast<size_t>(back - 48) : 0;
				const size_t win_len = static_cast<size_t>(p - text) - win_start;
				if (win_len >= sizeof(window)) {
					strncpy_s(window, text + win_start, sizeof(window) - 1);
				}
				else {
					memcpy(window, text + win_start, win_len);
					window[win_len] = '\0';
				}
				if (!StrIContains(window, "fragment") && !strstr(window, I18N(u8"分散"))) {
					continue;
				}
				int end = static_cast<int>(p - text) - 1;
				while (end >= 0 && (text[end] == ' ' || text[end] == '\t')) {
					--end;
				}
				int start = end;
				while (start >= 0 && text[start] >= '0' && text[start] <= '9') {
					--start;
				}
				if (start < end) {
					char num[16] = {};
					const int len = end - start;
					if (len > 0 && len < static_cast<int>(sizeof(num))) {
						memcpy(num, text + start + 1, static_cast<size_t>(len));
						const int val = atoi(num);
						if (val >= 0 && val <= 100) {
							return val;
						}
					}
				}
			}
			return -1;
		}

		static bool DefragOutputIndicatesSkipped(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return false;
			}
			static const char* patterns[] = {
				"do not need to defragment",
				"don't need to defragment",
				"does not need to be defragmented",
				"not need to be defragmented",
				I18N(u8"不需要對這個磁碟區進行重組"),
				I18N(u8"不需要重組"),
				I18N(u8"無需重組"),
				nullptr,
			};
			for (int i = 0; patterns[i] != nullptr; ++i) {
				if (StrIContains(text, patterns[i])) {
					return true;
				}
			}
			return false;
		}

		static bool DefragOutputIndicatesWorkDone(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return false;
			}
			static const char* patterns[] = {
				"invoking defragmentation",
				"invoking retrim",
				"post defragmentation",
				"post optimization",
				"successfully optimized",
				I18N(u8"最佳化後"),
				I18N(u8"執行重組"),
				"retrim",
				nullptr,
			};
			for (int i = 0; patterns[i] != nullptr; ++i) {
				if (StrIContains(text, patterns[i])) {
					return true;
				}
			}
			return false;
		}

		static bool DefragOutputIndicatesHealthy(const char* text)
		{
			if (text == nullptr || text[0] == '\0') {
				return false;
			}
			static const char* patterns[] = {
				"do not need to defragment",
				"don't need to defragment",
				I18N(u8"不需要對這個磁碟區進行重組"),
				I18N(u8"不需要重組"),
				I18N(u8"狀況良好"),
				"operation completed success",
				I18N(u8"操作順利完成"),
				nullptr,
			};
			for (int i = 0; patterns[i] != nullptr; ++i) {
				if (StrIContains(text, patterns[i])) {
					return true;
				}
			}
			return false;
		}

		static bool ParseNeedsOptimizationFlag(const char* text)
		{
			static const char* labels[] = {
				"Needs optimization",
				I18N(u8"需要最佳化"),
				I18N(u8"需要优化"),
				nullptr,
			};
			for (int i = 0; labels[i] != nullptr; ++i) {
				const char* p = strstr(text, labels[i]);
				if (p == nullptr) {
					continue;
				}
				p += strlen(labels[i]);
				for (int skip = 0; skip < 24 && *p != '\0'; ++skip, ++p) {
					if (StrIContains(p, "yes") || StrIContains(p, I18N(u8"是"))
						|| StrIContains(p, "true")) {
						return true;
					}
					if (StrIContains(p, "no") || StrIContains(p, I18N(u8"否"))
						|| StrIContains(p, "false")) {
						return false;
					}
				}
			}
			return false;
		}

		static void ParseDefragAnalyzeOutput(const char* text, DiskOptimizationSnapshot& snap)
		{
			if (text == nullptr || text[0] == '\0') {
				snap.valid = false;
				strncpy_s(snap.status_text, u8"磁碟分析失敗", _TRUNCATE);
				strncpy_s(snap.detail_text, u8"defrag 無輸出", _TRUNCATE);
				return;
			}
			if (DefragOutputIndicatesError(text)) {
				snap.valid = false;
				strncpy_s(snap.status_text, u8"磁碟分析失敗", _TRUNCATE);
				if (DefragOutputIndicatesUnavailable(text)) {
					strncpy_s(snap.detail_text, u8"此磁碟區不支援 defrag 最佳化", _TRUNCATE);
				}
				else if (StrIContains(text, "privilege") || StrIContains(text, I18N(u8"權限"))) {
					strncpy_s(snap.detail_text, u8"需要以管理員執行 defrag", _TRUNCATE);
				}
				else {
					strncpy_s(snap.detail_text, u8"defrag 回報錯誤，請查看工作日誌", _TRUNCATE);
				}
				return;
			}
			snap.is_ssd = StrIContains(text, "retrim") || StrIContains(text, "ssd")
				|| StrIContains(text, "solid state") || StrIContains(text, I18N(u8"固態"));
			strncpy_s(snap.media_label, snap.is_ssd ? "SSD" : "HDD", _TRUNCATE);

			int frag = ParseFragmentationPercent(text);
			if (frag < 0 && DefragOutputIndicatesHealthy(text)) {
				frag = 0;
			}
			snap.fragmentation_percent = frag;
			snap.needs_optimization = ParseNeedsOptimizationFlag(text);
			if (!snap.needs_optimization && frag >= 10 && !snap.is_ssd) {
				snap.needs_optimization = true;
			}
			if (frag == 0 && !snap.needs_optimization && DefragOutputIndicatesHealthy(text)) {
				snap.needs_optimization = false;
			}

			if (snap.is_ssd) {
				snprintf(snap.detail_text, sizeof(snap.detail_text), "%s · %s",
					snap.needs_optimization ? I18N(u8"建議執行 TRIM 最佳化") : I18N(u8"TRIM 狀態良好"),
					snap.needs_optimization ? I18N(u8"需要最佳化") : I18N(u8"無需最佳化"));
			}
			else if (frag >= 0) {
				snprintf(snap.detail_text, sizeof(snap.detail_text),
					I18N(u8"檔案分散率 %d%% · %s · 與 Windows 一致"), frag,
					snap.needs_optimization ? I18N(u8"建議重組") : I18N(u8"狀態良好"));
			}
			else {
				strncpy_s(snap.detail_text,
					snap.needs_optimization ? I18N(u8"建議執行硬碟最佳化") : I18N(u8"未能解析分散率，請查看日誌"),
					_TRUNCATE);
			}
			strncpy_s(snap.status_text, u8"磁碟分析完成", _TRUNCATE);
			snap.last_run_was_optimize = false;
			snap.last_optimize_elapsed_sec = 0;
			snap.valid = true;
		}

		static void RunDiskOptimizationAnalyzeWorker(char drive_letter)
		{
			g_disk_opt_running.store(true, std::memory_order_release);
			char log_intro[96] = {};
			snprintf(log_intro, sizeof(log_intro), I18N(u8"執行 defrag %c: /A /V（詳細分析）"), drive_letter);
			StorageWorkLog(log_intro);
			StorageWorkSetProgress(0.12f, I18N(u8"啟動磁碟分析…"));
			{
				std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
				g_disk_opt.running = true;
				g_disk_opt.progress = 0.12f;
				g_disk_opt.drive_letter = drive_letter;
				snprintf(g_disk_opt.status_text, sizeof(g_disk_opt.status_text),
					I18N(u8"正在分析 %c: 磁碟…"), drive_letter);
			}
			char output[8192] = {};
			wchar_t cmd[96] = {};
			_snwprintf_s(cmd, _TRUNCATE, L"defrag.exe %c: /A /V", drive_letter);
			StorageWorkSetProgress(0.35f, I18N(u8"分析磁碟中…"));
			DWORD exit_code = 1;
			const bool ok = RunCommandCaptureStdout(cmd, output, sizeof(output), &exit_code,
				kDefragCommandTimeoutMs);
			StorageWorkSetProgress(0.82f, I18N(u8"解析分析結果…"));
			DiskOptimizationSnapshot result = {};
			result.drive_letter = drive_letter;
			result.running = false;
			result.progress = 1.f;
			if (ok || output[0] != '\0') {
				ParseDefragAnalyzeOutput(output, result);
				if (!ok && result.valid) {
					result.valid = false;
					strncpy_s(result.status_text, u8"磁碟分析失敗", _TRUNCATE);
					snprintf(result.detail_text, sizeof(result.detail_text),
						I18N(u8"defrag 結束碼 %lu"), static_cast<unsigned long>(exit_code));
				}
				if (result.fragmentation_percent >= 0) {
					char summary[128] = {};
					snprintf(summary, sizeof(summary), I18N(u8"分析結果：分散率 %d%% · %s"),
						result.fragmentation_percent,
						result.needs_optimization ? I18N(u8"建議最佳化") : I18N(u8"狀況良好"));
					StorageWorkLog(summary);
				}
				else if (output[0] != '\0') {
					char preview[StorageWorkSnapshot::kLogLineChars] = {};
					strncpy_s(preview, output, _TRUNCATE);
					for (char* p = preview; *p != '\0'; ++p) {
						if (*p == '\r' || *p == '\n') {
							*p = ' ';
						}
					}
					StorageWorkLog(preview);
				}
			}
			else {
				strncpy_s(result.status_text, u8"磁碟分析失敗", _TRUNCATE);
				snprintf(result.detail_text, sizeof(result.detail_text),
					I18N(u8"defrag 結束碼 %lu"), static_cast<unsigned long>(exit_code));
				char fail[96] = {};
				snprintf(fail, sizeof(fail), I18N(u8"磁碟分析失敗（結束碼 %lu）"),
					static_cast<unsigned long>(exit_code));
				StorageWorkLog(fail);
				HLOG_WARN("OptimizeScan: 磁碟分析失敗 drive={} exit={} ok={}",
					drive_letter, static_cast<unsigned long>(exit_code), ok);
			}
			{
				std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
				g_disk_opt = result;
			}
			StorageWorkLog(result.status_text);
			if (result.valid) {
				HLOG_INFO("OptimizeScan: 磁碟分析完成 drive={} ssd={} frag={} needs_opt={}",
					drive_letter, result.is_ssd, result.fragmentation_percent,
					result.needs_optimization);
			}
			else if (result.status_text[0] != '\0') {
				HLOG_WARN("OptimizeScan: 磁碟分析失敗 drive={} detail='{}'",
					drive_letter, result.detail_text);
			}
			StorageWorkEnd(result.status_text, 1.f);
			g_disk_opt_running.store(false, std::memory_order_release);
			SetLastAction(result.status_text);
		}

		static void RunDiskOptimizationWorker(char drive_letter)
		{
			const ULONGLONG start_tick = GetTickCount64();
			g_disk_opt_running.store(true, std::memory_order_release);
			char log_intro[96] = {};
			snprintf(log_intro, sizeof(log_intro), I18N(u8"執行 defrag %c: /O /V（TRIM 或重組）"), drive_letter);
			StorageWorkLog(log_intro);
			StorageWorkSetProgress(0.1f, I18N(u8"啟動硬碟最佳化…"));
			{
				std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
				g_disk_opt.running = true;
				g_disk_opt.progress = 0.1f;
				g_disk_opt.drive_letter = drive_letter;
				snprintf(g_disk_opt.status_text, sizeof(g_disk_opt.status_text),
					I18N(u8"正在最佳化 %c: …"), drive_letter);
			}
			wchar_t cmd[96] = {};
			_snwprintf_s(cmd, _TRUNCATE, L"defrag.exe %c: /O /V", drive_letter);
			StorageWorkSetProgress(0.45f, I18N(u8"硬碟最佳化進行中…"));
			{
				std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
				g_disk_opt.progress = 0.45f;
			}
			char output[8192] = {};
			DWORD exit_code = 1;
			const bool ok = RunCommandCaptureStdout(cmd, output, sizeof(output), &exit_code,
				kDefragCommandTimeoutMs);
			const bool output_error = DefragOutputIndicatesError(output);
			StorageWorkSetProgress(0.9f, I18N(u8"完成硬碟最佳化…"));
			DiskOptimizationSnapshot result = {};
			{
				std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
				result = g_disk_opt;
			}
			result.running = false;
			result.progress = 1.f;
			result.drive_letter = drive_letter;
			if (ok && !output_error) {
				result.valid = true;
				result.needs_optimization = false;
				int frag = ParseFragmentationPercent(output);
				if (frag < 0 && DefragOutputIndicatesHealthy(output)) {
					frag = 0;
				}
				if (frag >= 0) {
					result.fragmentation_percent = frag;
				}
				const unsigned elapsed_sec = static_cast<unsigned>(
					(GetTickCount64() - start_tick) / 1000ULL);
				const bool skipped = DefragOutputIndicatesSkipped(output);
				const bool work_done = DefragOutputIndicatesWorkDone(output)
					|| elapsed_sec >= 5;
				result.last_run_was_optimize = true;
				result.last_optimize_elapsed_sec = elapsed_sec;
				snprintf(result.status_text, sizeof(result.status_text),
					I18N(u8"%c: 硬碟最佳化完成"), drive_letter);
				const char* media = result.media_label[0] ? result.media_label : I18N(u8"磁碟");
				const char* mode = result.is_ssd ? "TRIM" : I18N(u8"重組");
				char summary[160] = {};
				if (skipped && !work_done) {
					snprintf(result.detail_text, sizeof(result.detail_text),
						I18N(u8"%s · 已是 %d%% 分散 · Windows 判定無需%s"),
						media, result.fragmentation_percent >= 0 ? result.fragmentation_percent : 0,
						mode);
					snprintf(summary, sizeof(summary),
						I18N(u8"檢查完成：%c: 狀況良好（%d%% 分散），無需執行%s"),
						drive_letter,
						result.fragmentation_percent >= 0 ? result.fragmentation_percent : 0,
						mode);
				}
				else {
					snprintf(result.detail_text, sizeof(result.detail_text),
						I18N(u8"%s · 已執行%s（%u 秒）· 分散率 %d%% · 與 Windows 一致"),
						media, mode, elapsed_sec,
						result.fragmentation_percent >= 0 ? result.fragmentation_percent : 0);
					snprintf(summary, sizeof(summary),
						I18N(u8"最佳化已執行（%u 秒）：分散率 %d%% · 成功（0%% 表示已良好）"),
						elapsed_sec,
						result.fragmentation_percent >= 0 ? result.fragmentation_percent : 0);
				}
				StorageWorkLog(summary);
				HLOG_INFO("OptimizeScan: 硬碟最佳化完成 drive={} frag={} elapsed={}s skipped={} work={}",
					drive_letter, result.fragmentation_percent, elapsed_sec, skipped, work_done);
			}
			else {
				result.valid = false;
				strncpy_s(result.status_text, u8"硬碟最佳化失敗", _TRUNCATE);
				if (output_error) {
					strncpy_s(result.detail_text, u8"需要以管理員執行 defrag", _TRUNCATE);
				}
				else {
					snprintf(result.detail_text, sizeof(result.detail_text),
						I18N(u8"defrag 結束碼 %lu"), static_cast<unsigned long>(exit_code));
				}
				StorageWorkLog(I18N(u8"硬碟最佳化失敗，請以管理員重試"));
				HLOG_WARN("OptimizeScan: 硬碟最佳化失敗 drive={} exit={} output_error={}",
					drive_letter, static_cast<unsigned long>(exit_code), output_error);
			}
			{
				std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
				g_disk_opt = result;
			}
			StorageWorkEnd(result.status_text, 1.f);
			g_disk_opt_running.store(false, std::memory_order_release);
			SetLastAction(result.status_text);
		}

		static void EnumRegRunKey(HKEY root, const wchar_t* subkey, const char* source_label,
			std::vector<StartupEntry>& out)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
				return;
			}
			for (DWORD index = 0; out.size() < kMaxStartups; ++index) {
				wchar_t value_name[256] = {};
				DWORD name_len = static_cast<DWORD>(std::size(value_name));
				wchar_t data[1024] = {};
				DWORD data_size = sizeof(data);
				DWORD type = 0;
				const LONG err = RegEnumValueW(key, index, value_name, &name_len, nullptr, &type,
					reinterpret_cast<LPBYTE>(data), &data_size);
				if (err == ERROR_NO_MORE_ITEMS) {
					break;
				}
				if (err != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
					continue;
				}

				StartupEntry entry = {};
				snprintf(entry.id, sizeof(entry.id), "reg_%zu", out.size());
				Utf8FromWide(value_name, entry.name_utf8, sizeof(entry.name_utf8));
				Utf8FromWide(data, entry.command_utf8, sizeof(entry.command_utf8));
				strncpy_s(entry.source_label, source_label, _TRUNCATE);
				entry.enabled = true;
				entry.can_toggle = true;
				entry.source = StartupSource::RunKey;
				if (root == HKEY_CURRENT_USER) {
					wcsncpy_s(entry.restore_key,
						L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", _TRUNCATE);
				}
				else {
					wcsncpy_s(entry.restore_key,
						L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", _TRUNCATE);
				}
				wcsncpy_s(entry.restore_value_name, value_name, _TRUNCATE);
				wcsncpy_s(entry.restore_command, data, _TRUNCATE);
				out.push_back(entry);
			}
			RegCloseKey(key);
		}

		static void EnumStartupFolder(int csidl, const char* source_label, std::vector<StartupEntry>& out)
		{
			wchar_t dir[MAX_PATH] = {};
			if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, dir))) {
				return;
			}
			std::wstring pattern = dir;
			if (!pattern.empty() && pattern.back() != L'\\') {
				pattern += L'\\';
			}
			pattern += L'*';

			WIN32_FIND_DATAW fd = {};
			const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
			if (find == INVALID_HANDLE_VALUE) {
				return;
			}
			do {
				if (out.size() >= kMaxStartups) {
					break;
				}
				if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
					continue;
				}
				const size_t name_len = wcslen(fd.cFileName);
				if (name_len < 4) {
					continue;
				}
				const wchar_t* ext = fd.cFileName + name_len - 4;
				if (_wcsicmp(ext, L".lnk") != 0 && _wcsicmp(ext, L".exe") != 0) {
					continue;
				}

				std::wstring full = dir;
				if (full.back() != L'\\') {
					full += L'\\';
				}
				full += fd.cFileName;

				StartupEntry entry = {};
				snprintf(entry.id, sizeof(entry.id), "lnk_%zu", out.size());
				Utf8FromWide(fd.cFileName, entry.name_utf8, sizeof(entry.name_utf8));
				Utf8FromWide(full.c_str(), entry.command_utf8, sizeof(entry.command_utf8));
				strncpy_s(entry.source_label, source_label, _TRUNCATE);
				entry.enabled = true;
				entry.can_toggle = (_wcsicmp(ext, L".lnk") == 0);
				entry.source = StartupSource::StartupFolder;
				wcsncpy_s(entry.restore_key, full.c_str(), _TRUNCATE);
				out.push_back(entry);
			} while (FindNextFileW(find, &fd));
			FindClose(find);
		}

		static bool IsStartupEntryEnabled(const StartupEntry& entry)
		{
			if (entry.source == StartupSource::StartupFolder) {
				return GetFileAttributesW(entry.restore_key) != INVALID_FILE_ATTRIBUTES;
			}
			HKEY root = nullptr;
			const wchar_t* sub = nullptr;
			if (wcsstr(entry.restore_key, L"HKCU\\") == entry.restore_key) {
				root = HKEY_CURRENT_USER;
				sub = entry.restore_key + 5;
			}
			else if (wcsstr(entry.restore_key, L"HKLM\\") == entry.restore_key) {
				root = HKEY_LOCAL_MACHINE;
				sub = entry.restore_key + 5;
			}
			else {
				return entry.enabled;
			}
			HKEY key = nullptr;
			if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) {
				return false;
			}
			wchar_t buf[1024] = {};
			DWORD size = sizeof(buf);
			const LONG q = RegQueryValueExW(key, entry.restore_value_name, nullptr, nullptr,
				reinterpret_cast<LPBYTE>(buf), &size);
			RegCloseKey(key);
			return q == ERROR_SUCCESS;
		}

		static bool QueryStartupApprovedEnabled(HKEY root, const wchar_t* value_name, bool& enabled_out)
		{
			HKEY approved = nullptr;
			if (RegOpenKeyExW(root,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
				0, KEY_READ, &approved) != ERROR_SUCCESS) {
				return false;
			}
			BYTE data[12] = {};
			DWORD cb = sizeof(data);
			const LONG q = RegQueryValueExW(approved, value_name, nullptr, nullptr, data, &cb);
			RegCloseKey(approved);
			if (q != ERROR_SUCCESS) {
				return false;
			}
			if (data[0] == 0x03) {
				enabled_out = false;
				return true;
			}
			if (data[0] == 0x02 || data[0] == 0x06 || data[0] == 0x07) {
				enabled_out = true;
				return true;
			}
			enabled_out = (data[0] & 0x0F) != 0x03;
			return true;
		}

		static void RefreshStartupEnabledFlags(std::vector<StartupEntry>& startups)
		{
			for (StartupEntry& e : startups) {
				if (e.source == StartupSource::RunKey) {
					HKEY root = nullptr;
					if (wcsstr(e.restore_key, L"HKCU\\") == e.restore_key) {
						root = HKEY_CURRENT_USER;
					}
					else if (wcsstr(e.restore_key, L"HKLM\\") == e.restore_key) {
						root = HKEY_LOCAL_MACHINE;
					}
					bool approved_enabled = e.enabled;
					if (root != nullptr
						&& QueryStartupApprovedEnabled(root, e.restore_value_name, approved_enabled)) {
						e.enabled = approved_enabled;
						continue;
					}
				}
				e.enabled = IsStartupEntryEnabled(e);
			}
		}

		struct ServiceGuideRow {
			const char* name;
			const char* role;
			const char* disable_effect;
			const char* risk;
			bool recommend_disable;
		};

		static const ServiceGuideRow kServiceGuides[] = {
			{ "SysMain", I18N(u8"Superfetch／記憶體預載，加速常用程式啟動。"),
				I18N(u8"停用後開機與磁碟活動常明顯下降；少數舊機械硬碟可能略慢載入已裝程式。"),
				I18N(u8"建議 SSD 或遊戲／極簡優化時停用。"), true },
			{ "WSearch", I18N(u8"Windows 搜尋索引，支援檔案總管與開始功能表快速搜尋。"),
				I18N(u8"停用後搜尋變慢、首次搜尋需重建索引；背景 CPU／磁碟佔用降低。"),
				I18N(u8"與 Explorer／開始功能表緊密相關，不建議停用。"), false },
			{ "DoSvc", I18N(u8"傳遞最佳化（P2P 更新與下載），與 Windows Update 相關。"),
				I18N(u8"停用後更新仍可用，但背景下載與區網分享減少，部分網路佔用降低。"),
				I18N(u8"網路頻寬吃緊或不想被動分享時可停用。"), true },
			{ "DiagTrack", I18N(u8"相容性與遙測資料收集（Connected User Experiences）。"),
				I18N(u8"停用後隱私與背景上傳減少，不影響一般桌面使用。"),
				I18N(u8"注重隱私時建議停用。"), true },
			{ "dmwappushservice", I18N(u8"WAP 推播訊息路由（多與遙測相關）。"),
				I18N(u8"停用後幾乎無日常使用影響。"), I18N(u8"可安全停用。"), true },
			{ "WerSvc", I18N(u8"Windows 錯誤報告，傳送當機資訊給微軟。"),
				I18N(u8"停用後仍會本機記錄部分錯誤，但不會自動上傳。"), I18N(u8"除錯需要外可停用。"), true },
			{ "MapsBroker", I18N(u8"離線地圖資料下載代理。"),
				I18N(u8"停用後「地圖」應用離線功能受限。"), I18N(u8"不使用地圖可停用。"), true },
			{ "Fax", I18N(u8"傳真服務。"),
				I18N(u8"停用後無傳真功能；多數家用電腦無影響。"), I18N(u8"無傳真需求可停用。"), true },
			{ "XblAuthManager", I18N(u8"Xbox Live 驗證。"),
				I18N(u8"停用後 Xbox 遊戲與部分 Microsoft Store 遊戲登入受影響。"), I18N(u8"不玩 Xbox 遊戲可停用。"), true },
			{ "XblGameSave", I18N(u8"Xbox 雲端存檔同步。"),
				I18N(u8"停用後 Xbox 雲端存檔不可用。"), I18N(u8"不玩 Xbox 遊戲可停用。"), true },
			{ "XboxNetApiSvc", I18N(u8"Xbox 網路 API。"),
				I18N(u8"停用後 Xbox 多人與部分遊戲網路功能受限。"), I18N(u8"不玩 Xbox 遊戲可停用。"), true },
			{ "XboxGipSvc", I18N(u8"Xbox 周邊輸入裝置驅動。"),
				I18N(u8"停用後 Xbox 手把可能需手動安裝驅動。"), I18N(u8"不用 Xbox 手把可停用。"), true },
			{ "TabletInputService", I18N(u8"觸控筆與手寫輸入服務。"),
				I18N(u8"停用後觸控筆／手寫螢幕功能失效。"), I18N(u8"非觸控筆設備可停用。"), true },
			{ "PhoneSvc", I18N(u8"手機連結與您的手機體驗。"),
				I18N(u8"停用後無法與 Android/iPhone 深度連動。"), I18N(u8"不用「手機連結」可停用。"), true },
			{ "RetailDemo", I18N(u8"零售展示模式。"),
				I18N(u8"停用後無影響（家用電腦）。"), I18N(u8"可安全停用。"), true },
			{ "RemoteRegistry", I18N(u8"遠端修改登錄檔。"),
				I18N(u8"停用後降低遠端攻擊面；本機登錄不受影響。"), I18N(u8"建議一般使用者停用。"), true },
			{ "lfsvc", I18N(u8"地理定位服務。"),
				I18N(u8"停用後天氣、地圖定位等需位置的功能受限。"), I18N(u8"注重隱私可停用。"), true },
			{ "WbioSrvc", I18N(u8"Windows 指紋／生物辨識。"),
				I18N(u8"停用後指紋／臉部登入不可用。"), I18N(u8"無生物辨識硬體可停用。"), true },
			{ "WalletService", I18N(u8"Wallet／支付相關背景服務。"),
				I18N(u8"停用後 Microsoft Pay 等受限。"), I18N(u8"一般桌面可停用。"), true },
			{ "wisvc", I18N(u8"Windows 測試人員／預覽體驗。"),
				I18N(u8"停用後不影響正式版更新通道。"), I18N(u8"非 Insider 可停用。"), true },
			{ "SharedAccess", I18N(u8"網際網路連線共用（ICS 熱點）。"),
				I18N(u8"停用後無法透過本機分享 Wi‑Fi 熱點。"), I18N(u8"不分享熱點可停用。"), true },
			{ "Spooler", I18N(u8"列印多工緩衝處理器。"),
				I18N(u8"停用後無法列印（含 PDF 虛擬印表機部分情境）。"), I18N(u8"有列印需求請勿停用。"), false },
			{ "BITS", I18N(u8"背景智慧傳輸（更新與下載）。"),
				I18N(u8"停用後 Windows Update 與部分下載可能異常。"), I18N(u8"不建議停用。"), false },
		};

		static const ServiceGuideRow* FindServiceGuide(const char* service_name)
		{
			if (service_name == nullptr) {
				return nullptr;
			}
			for (const ServiceGuideRow& row : kServiceGuides) {
				if (std::strcmp(row.name, service_name) == 0) {
					return &row;
				}
			}
			return nullptr;
		}

		static void ApplyRecommendedDisableFlag(ServiceEntry& entry)
		{
			entry.recommended_disable = false;
			const ServiceGuideRow* guide = FindServiceGuide(entry.service_name);
			if (guide != nullptr && guide->recommend_disable) {
				entry.recommended_disable = true;
			}
		}

		static void QueryService(ServiceEntry& entry, const wchar_t* display_name_wide = nullptr)
		{
			wchar_t wname[64] = {};
			MultiByteToWideChar(CP_UTF8, 0, entry.service_name, -1, wname,
				static_cast<int>(std::size(wname)));

			if (display_name_wide != nullptr && display_name_wide[0] != L'\0') {
				Utf8FromWide(display_name_wide, entry.display_name, sizeof(entry.display_name));
			}

			SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
			if (scm == nullptr) {
				return;
			}
			SC_HANDLE svc = OpenServiceW(scm, wname, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
			if (svc == nullptr) {
				CloseServiceHandle(scm);
				return;
			}
			entry.exists = true;

			SERVICE_STATUS_PROCESS status = {};
			DWORD bytes = 0;
			if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
				reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) {
				entry.running = (status.dwCurrentState == SERVICE_RUNNING
					|| status.dwCurrentState == SERVICE_START_PENDING);
			}

			DWORD needed = 0;
			QueryServiceConfigW(svc, nullptr, 0, &needed);
			std::vector<BYTE> buffer(needed);
			auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
			if (QueryServiceConfigW(svc, cfg, needed, &needed)) {
				entry.start_type = cfg->dwStartType;
				if (cfg->lpBinaryPathName != nullptr) {
					Utf8FromWide(cfg->lpBinaryPathName, entry.binary_path_utf8,
						sizeof(entry.binary_path_utf8));
				}
			}

			CloseServiceHandle(svc);
			CloseServiceHandle(scm);
			ApplyRecommendedDisableFlag(entry);
		}

		static bool ShouldListService(const ServiceEntry& e)
		{
			if (!e.exists) {
				return false;
			}
			const uint32_t st = e.start_type;
			const bool auto_start = (st == SERVICE_AUTO_START || st == 5);
			if (auto_start) {
				return true;
			}
			if (st == SERVICE_DEMAND_START && e.running) {
				return true;
			}
			if (e.recommended_disable) {
				return true;
			}
			return false;
		}

		static void EnumBackgroundServices(std::vector<ServiceEntry>& out)
		{
			SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
			if (scm == nullptr) {
				return;
			}

			DWORD bytes = 0;
			DWORD count = 0;
			DWORD resume = 0;
			EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
				SERVICE_STATE_ALL, nullptr, 0, &bytes, &count, &resume, nullptr);
			if (bytes == 0) {
				CloseServiceHandle(scm);
				return;
			}

			std::vector<BYTE> buf(bytes);
			auto* list = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
			if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
				SERVICE_STATE_ALL, reinterpret_cast<LPBYTE>(list), bytes, &bytes, &count,
				&resume, nullptr)) {
				CloseServiceHandle(scm);
				return;
			}

			std::vector<ServiceEntry> collected;
			collected.reserve(count);
			for (DWORD i = 0; i < count; ++i) {
				const ENUM_SERVICE_STATUS_PROCESSW& st = list[i];
				const DWORD type = st.ServiceStatusProcess.dwServiceType;
				if ((type & SERVICE_WIN32) == 0) {
					continue;
				}
				ServiceEntry entry = {};
				Utf8FromWide(st.lpServiceName, entry.service_name, sizeof(entry.service_name));
				QueryService(entry, st.lpDisplayName);
				if (!ShouldListService(entry)) {
					continue;
				}
				collected.push_back(entry);
			}
			CloseServiceHandle(scm);

			for (const ServiceGuideRow& row : kServiceGuides) {
				if (!row.recommend_disable) {
					continue;
				}
				bool found = false;
				for (const ServiceEntry& e : collected) {
					if (std::strcmp(e.service_name, row.name) == 0) {
						found = true;
						break;
					}
				}
				if (found) {
					continue;
				}
				ServiceEntry extra = {};
				strncpy_s(extra.service_name, row.name, _TRUNCATE);
				QueryService(extra, nullptr);
				if (extra.exists) {
					collected.push_back(extra);
				}
			}

			std::sort(collected.begin(), collected.end(),
				[](const ServiceEntry& a, const ServiceEntry& b) {
					return std::strcmp(a.display_name, b.display_name) < 0;
				});

			constexpr size_t kMaxServices = 80;
			if (collected.size() > kMaxServices) {
				collected.resize(kMaxServices);
			}
			for (ServiceEntry& e : collected) {
				FillServiceKnowledge(e);
				OptimizeStartupIcon::EnrichServiceEntry(e);
			}
			out = std::move(collected);
		}

		static int CountPending(const Snapshot& snap)
		{
			int n = 0;
			if (snap.startups.size() > 14) {
				++n;
			}
			if (snap.suggested_clean_bytes > 64LL * 1024 * 1024) {
				++n;
			}
			if (snap.system_drive_used_percent >= 85.f) {
				++n;
			}
			for (const ServiceEntry& s : snap.services) {
				if (s.recommended_disable && s.running) {
					++n;
				}
			}
			return n;
		}

		static void FillCleanSuggestion(Snapshot& snap)
		{
			strncpy_s(snap.suggested_clean_preset, "general", _TRUNCATE);
			strncpy_s(snap.suggested_clean_label, u8"一般", _TRUNCATE);
			snap.suggested_clean_bytes = EstimateCleanPresetBytes("general");
		}

		static void RunScanWorker()
		{
			g_scanning.store(true, std::memory_order_release);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.scanning = true;
				g_snapshot.valid = false;
				g_snapshot.progress = 0.05f;
				strncpy_s(g_snapshot.status_text, u8"讀取電源與系統設定…", _TRUNCATE);
			}

			Snapshot snap = {};
			QueryPowerPlan(snap);
			QueryGameMode(snap);
			QueryVisualFx(snap);
			QuerySystemPerfExtras(snap);
			QuerySystemDriveUsage(snap);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.progress = 0.25f;
				strncpy_s(g_snapshot.status_text, u8"列出啟動項…", _TRUNCATE);
			}

			EnumRegRunKey(HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", u8"目前使用者 Run", snap.startups);
			EnumRegRunKey(HKEY_LOCAL_MACHINE,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", u8"本機 Run", snap.startups);
			EnumStartupFolder(CSIDL_STARTUP, u8"使用者啟動資料夾", snap.startups);
			EnumStartupFolder(CSIDL_COMMON_STARTUP, u8"共用啟動資料夾", snap.startups);
			RefreshStartupEnabledFlags(snap.startups);
			for (StartupEntry& item : snap.startups) {
				OptimizeStartupIcon::EnrichStartupEntry(item);
			}

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.progress = 0.55f;
				strncpy_s(g_snapshot.status_text, u8"查詢背景服務…", _TRUNCATE);
			}

			EnumBackgroundServices(snap.services);

			FillCleanSuggestion(snap);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.progress = 0.9f;
				strncpy_s(g_snapshot.status_text, u8"整理建議…", _TRUNCATE);
			}

			snap.valid = true;
			snap.scanning = false;
			snap.progress = 1.f;
			snap.pending_suggestions = CountPending(snap);
			snprintf(snap.status_text, sizeof(snap.status_text),
				I18N(u8"完成：%zu 個啟動項，%d 項建議"),
				snap.startups.size(), snap.pending_suggestions);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot = std::move(snap);
			}
			g_scanning.store(false, std::memory_order_release);
			HLOG_INFO("OptimizeScan: 掃描完成 startups={} pending={}",
				g_snapshot.startups.size(), g_snapshot.pending_suggestions);
		}

		static const PresetDef* FindPresetDef(const char* preset_id)
		{
			if (preset_id == nullptr) {
				return nullptr;
			}
			for (const PresetDef& p : kPresets) {
				if (std::strcmp(p.id, preset_id) == 0) {
					return &p;
				}
			}
			return nullptr;
		}

		static bool SetRegistryStartupEnabled(StartupEntry& entry, bool enabled)
		{
			HKEY root = nullptr;
			const wchar_t* sub = nullptr;
			if (wcsstr(entry.restore_key, L"HKCU\\") == entry.restore_key) {
				root = HKEY_CURRENT_USER;
				sub = entry.restore_key + 5;
			}
			else if (wcsstr(entry.restore_key, L"HKLM\\") == entry.restore_key) {
				root = HKEY_LOCAL_MACHINE;
				sub = entry.restore_key + 5;
			}
			else {
				return false;
			}

			HKEY key = nullptr;
			if (RegOpenKeyExW(root, sub, 0, KEY_SET_VALUE | KEY_READ, &key) != ERROR_SUCCESS) {
				return false;
			}
			bool ok = false;
			if (enabled) {
				ok = (RegSetValueExW(key, entry.restore_value_name, 0, REG_SZ,
					reinterpret_cast<const BYTE*>(entry.restore_command),
					static_cast<DWORD>((wcslen(entry.restore_command) + 1) * sizeof(wchar_t)))
					== ERROR_SUCCESS);
			}
			else {
				ok = (RegDeleteValueW(key, entry.restore_value_name) == ERROR_SUCCESS);
			}
			RegCloseKey(key);
			entry.enabled = enabled;
			return ok;
		}

		static bool SetStartupFolderEnabled(StartupEntry& entry, bool enabled)
		{
			const std::wstring path = entry.restore_key;
			if (path.empty()) {
				return false;
			}
			if (enabled) {
				const std::wstring disabled = path + L".disabled";
				if (GetFileAttributesW(disabled.c_str()) != INVALID_FILE_ATTRIBUTES) {
					return MoveFileW(disabled.c_str(), path.c_str()) != FALSE;
				}
				return true;
			}
			if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
				return false;
			}
			const std::wstring disabled = path + L".disabled";
			if (GetFileAttributesW(disabled.c_str()) != INVALID_FILE_ATTRIBUTES) {
				DeleteFileW(disabled.c_str());
			}
			const bool ok = MoveFileW(path.c_str(), disabled.c_str()) != FALSE;
			if (ok) {
				entry.enabled = false;
			}
			return ok;
		}

		static bool ApplyServiceStartType(const wchar_t* service_name, uint32_t start_type)
		{
			SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
			if (scm == nullptr) {
				scm = OpenSCManagerW(nullptr, nullptr,
					SC_MANAGER_CONNECT | SC_MANAGER_MODIFY_BOOT_CONFIG);
			}
			if (scm == nullptr) {
				HLOG_WARN("OptimizeScan: OpenSCManager 失敗 err={}", GetLastError());
				return false;
			}
			SC_HANDLE svc = OpenServiceW(scm, service_name,
				SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
			if (svc == nullptr) {
				HLOG_WARN("OptimizeScan: OpenService 失敗 err={}", GetLastError());
				CloseServiceHandle(scm);
				return false;
			}
			if (start_type == SERVICE_DISABLED) {
				SERVICE_STATUS status = {};
				ControlService(svc, SERVICE_CONTROL_STOP, &status);
			}
			const BOOL changed = ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, start_type,
				SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
			const bool ok = (changed != FALSE);
			if (!ok) {
				HLOG_WARN("OptimizeScan: ChangeServiceConfig err={}", GetLastError());
			}
			CloseServiceHandle(svc);
			CloseServiceHandle(scm);
			return ok;
		}

		static bool IsProtectedSystemService(const char* service_name)
		{
			if (service_name == nullptr) {
				return false;
			}
			// WSearch 與 Explorer／開始功能表搜尋緊密相關，停用可能導致 Shell 異常。
			return _stricmp(service_name, "WSearch") == 0;
		}

		static bool ApplyServiceDisable(const wchar_t* service_name, bool disable)
		{
			if (!HCleanIsRunningAsAdmin() && HElevationBroker::IsConnected()) {
				wchar_t stop_cmd[280] = {};
				_snwprintf_s(stop_cmd, _TRUNCATE, L"sc.exe stop %s", service_name);
				HElevationBroker::RunHiddenCommand(stop_cmd, 60000, nullptr);
				wchar_t cfg_cmd[320] = {};
				_snwprintf_s(cfg_cmd, _TRUNCATE, L"sc.exe config %s start= %s",
					service_name, disable ? L"disabled" : L"demand");
				return HElevationBroker::RunHiddenCommand(cfg_cmd, 60000, nullptr);
			}
			return ApplyServiceStartType(service_name,
				disable ? SERVICE_DISABLED : SERVICE_DEMAND_START);
		}

		static void MarkServiceRecommendations(Snapshot& snap, const PresetDef& preset)
		{
			for (ServiceEntry& s : snap.services) {
				ApplyRecommendedDisableFlag(s);
				if (std::strcmp(s.service_name, "SysMain") == 0) {
					s.recommended_disable = preset.disable_sysmain;
				}
				else if (std::strcmp(s.service_name, "WSearch") == 0) {
					s.recommended_disable = preset.disable_wsearch;
				}
				else if (std::strcmp(s.service_name, "DoSvc") == 0) {
					s.recommended_disable = preset.disable_dosvc;
				}
			}
		}
	}

	void FillServiceKnowledge(ServiceEntry& entry)
	{
		const ServiceGuideRow* guide = FindServiceGuide(entry.service_name);
		if (guide != nullptr) {
			strncpy_s(entry.role_utf8, guide->role, _TRUNCATE);
			strncpy_s(entry.disable_effect_utf8, guide->disable_effect, _TRUNCATE);
			strncpy_s(entry.risk_note_utf8, guide->risk, _TRUNCATE);
		}
		else if (entry.exists) {
			snprintf(entry.role_utf8, sizeof(entry.role_utf8),
				I18N(u8"Windows 背景服務「%s」。"), entry.display_name);
			strncpy_s(entry.disable_effect_utf8, u8"停用後可能影響相依功能；不確定時請先建立還原點。", _TRUNCATE);
			strncpy_s(entry.risk_note_utf8, u8"未在內建建議清單中，請謹慎變更。", _TRUNCATE);
		}
	}

	void Init()
	{
		OptimizeStartupIcon::Init();
		RefreshRevertFlagFromDisk();
		HLOG_INFO("OptimizeScan: Init");
	}

	void Shutdown()
	{
		g_shutdown.store(true, std::memory_order_release);
		if (g_worker.joinable()) {
			g_worker.join();
		}
		if (g_disk_opt_worker.joinable()) {
			g_disk_opt_worker.join();
		}
		if (g_storage_worker.joinable()) {
			g_storage_worker.join();
		}
		g_shutdown.store(false, std::memory_order_release);
		OptimizeStartupIcon::Shutdown();
	}

	void RequestScan()
	{
		if (g_scanning.load(std::memory_order_acquire)) {
			HLOG_INFO("OptimizeScan: 掃描進行中，略過");
			return;
		}
		if (g_worker.joinable()) {
			g_worker.join();
		}
		g_worker = std::thread([] { RunScanWorker(); });
		HLOG_INFO("OptimizeScan: 已請求掃描");
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		Snapshot copy = g_snapshot;
		if (g_scanning.load(std::memory_order_acquire)) {
			copy.scanning = true;
		}
		return copy;
	}

	bool IsScanning()
	{
		return g_scanning.load(std::memory_order_acquire);
	}

	size_t GetPresetCount()
	{
		return sizeof(kPresets) / sizeof(kPresets[0]);
	}

	const PresetInfo* GetPreset(size_t index)
	{
		if (index >= GetPresetCount()) {
			return nullptr;
		}
		static PresetInfo info = {};
		info.id = kPresets[index].id;
		info.label = kPresets[index].label;
		info.description = kPresets[index].description;
		return &info;
	}

	const char* GetPresetDescription(const char* preset_id)
	{
		const PresetDef* p = FindPresetDef(preset_id);
		return p != nullptr ? p->description : "";
	}

	bool SetStartupEnabled(const char* entry_id, bool enabled)
	{
		if (entry_id == nullptr) {
			return false;
		}
		std::lock_guard<std::mutex> lock(g_mutex);
		for (StartupEntry& e : g_snapshot.startups) {
			if (std::strcmp(e.id, entry_id) != 0 || !e.can_toggle) {
				continue;
			}
			bool ok = false;
			if (e.source == StartupSource::StartupFolder) {
				ok = SetStartupFolderEnabled(e, enabled);
			}
			else {
				ok = SetRegistryStartupEnabled(e, enabled);
			}
			HLOG_INFO("OptimizeScan: 啟動項 '{}' -> {}", e.name_utf8, enabled ? "啟用" : "停用");
			SetLastAction(ok ? (enabled ? I18N(u8"已啟用啟動項") : I18N(u8"已停用啟動項")) : I18N(u8"啟動項變更失敗"));
			return ok;
		}
		return false;
	}

	bool SetServiceDisabled(const char* service_name, bool disabled)
	{
		if (service_name == nullptr) {
			return false;
		}
		if (disabled && IsProtectedSystemService(service_name)) {
			SetLastAction(u8"Windows Search (WSearch) 為系統關鍵服務，本程式已阻止停用");
			HLOG_WARN("OptimizeScan: blocked disable of protected service '{}'", service_name);
			return false;
		}
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"變更服務需要以管理員執行本程式");
			HLOG_WARN("OptimizeScan: SetServiceDisabled 需要管理員");
			return false;
		}
		wchar_t wname[64] = {};
		MultiByteToWideChar(CP_UTF8, 0, service_name, -1, wname, static_cast<int>(std::size(wname)));
		const bool ok = ApplyServiceDisable(wname, disabled);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			for (ServiceEntry& s : g_snapshot.services) {
				if (std::strcmp(s.service_name, service_name) == 0) {
					QueryService(s, nullptr);
					ApplyRecommendedDisableFlag(s);
					FillServiceKnowledge(s);
					OptimizeStartupIcon::EnrichServiceEntry(s);
					break;
				}
			}
		}
		HLOG_INFO("OptimizeScan: 服務 '{}' disabled={} ok={}", service_name, disabled, ok);
		SetLastAction(ok
			? (disabled ? I18N(u8"已停用服務") : I18N(u8"已改為手動啟動（可於服務主控台啟動）"))
			: I18N(u8"服務設定失敗（權限不足或系統保護）"));
		return ok;
	}

	bool SetPowerPlanByKind(const char* kind)
	{
		bool ok = false;
		bool duplicated = false;
		if (kind != nullptr && std::strcmp(kind, "ultimate") == 0) {
			ok = ActivateUltimatePowerPlan(&duplicated);
		}
		else {
			const wchar_t* guid = kBalancedGuid;
			if (kind != nullptr && std::strcmp(kind, "high") == 0) {
				guid = kHighPerfGuid;
			}
			else if (kind != nullptr && std::strcmp(kind, "saver") == 0) {
				guid = kPowerSaverGuid;
			}
			ok = RunPowerCfgSetActive(guid);
		}
		HLOG_INFO("OptimizeScan: SetPowerPlan kind='{}' ok={} duplicated={}",
			kind != nullptr ? kind : "", ok, duplicated);
		if (ok) {
			SetLastAction(duplicated
				? I18N(u8"已切換為終極效能（已自動建立電源計畫）")
				: I18N(u8"已切換電源計畫"));
			std::lock_guard<std::mutex> lock(g_mutex);
			QueryPowerPlan(g_snapshot);
			g_snapshot.ultimate_plan_available = QueryUltimatePlanAvailable();
		}
		else if (kind != nullptr && std::strcmp(kind, "ultimate") == 0) {
			SetLastAction(u8"終極效能不可用（請以管理員執行，或系統不支援）");
		}
		else {
			SetLastAction(u8"電源計畫切換失敗");
		}
		return ok;
	}

	bool EnsureUltimatePowerPlan()
	{
		bool duplicated = false;
		const bool ok = ActivateUltimatePowerPlan(&duplicated);
		HLOG_INFO("OptimizeScan: EnsureUltimatePowerPlan ok={} duplicated={}", ok, duplicated);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			QueryPowerPlan(g_snapshot);
			g_snapshot.ultimate_plan_available = true;
			SetLastAction(duplicated
				? I18N(u8"已建立並啟用終極效能電源計畫")
				: I18N(u8"終極效能電源計畫已就緒"));
		}
		else {
			SetLastAction(u8"無法建立終極效能計畫（需管理員或系統版本不支援）");
		}
		return ok;
	}

	bool ApplyPreset(const char* preset_id)
	{
		return ApplyPresetWithOptions(preset_id, false);
	}

	bool ApplyPresetWithOptions(const char* preset_id, bool create_restore_point)
	{
		const PresetDef* preset = FindPresetDef(preset_id);
		if (preset == nullptr) {
			HLOG_WARN("OptimizeScan: 未知預設 '{}'", preset_id != nullptr ? preset_id : "");
			return false;
		}
		HLOG_INFO("OptimizeScan: ApplyPreset '{}' restore_point={}", preset_id, create_restore_point);

		if (create_restore_point) {
			CreateSystemRestorePoint();
		}

		RevertSnapshot revert = {};
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			strncpy_s(revert.preset_id, preset_id, _TRUNCATE);
			strncpy_s(revert.preset_label, preset->label, _TRUNCATE);
			strncpy_s(revert.power_kind,
				PowerKindFromGuid(g_snapshot.power_plan_guid), _TRUNCATE);
			revert.visual_fx = g_snapshot.visual_fx_setting;
			revert.game_mode = g_snapshot.game_mode_on;
			MarkServiceRecommendations(g_snapshot, *preset);
			for (const ServiceEntry& s : g_snapshot.services) {
				if (s.recommended_disable && s.exists
					&& s.start_type != SERVICE_DISABLED) {
					RevertServiceState rs = {};
					strncpy_s(rs.name, s.service_name, _TRUNCATE);
					rs.start_type = s.start_type;
					revert.services.push_back(rs);
				}
			}
		}

		SetPowerPlanByKind(preset->power_kind);
		if (preset->visual_fx >= 0) {
			SetVisualFxSetting(preset->visual_fx);
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.visual_fx_setting = preset->visual_fx;
		}
		if (preset->game_mode) {
			SetGameMode(true);
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.game_mode_on = true;
		}

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (preset->clean_preset != nullptr) {
				strncpy_s(g_snapshot.suggested_clean_preset, preset->clean_preset, _TRUNCATE);
				strncpy_s(g_snapshot.suggested_clean_label, preset->label, _TRUNCATE);
				g_snapshot.suggested_clean_bytes = EstimateCleanPresetBytes(preset->clean_preset);
			}
		}

		int svc_changed = 0;
		if (HCleanIsRunningAsAdmin()) {
			for (const RevertServiceState& rs : revert.services) {
				if (SetServiceDisabled(rs.name, true)) {
					++svc_changed;
				}
			}
		}
		else if (preset->disable_sysmain || preset->disable_wsearch || preset->disable_dosvc) {
			SetLastAction(u8"部分服務變更需管理員權限");
		}

		if (!revert.services.empty()) {
			SaveRevertSnapshot(revert);
		}

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.pending_suggestions = CountPending(g_snapshot);
		}

		char msg[256] = {};
		snprintf(msg, sizeof(msg), I18N(u8"已套用「%s」"), preset->label);
		if (svc_changed > 0) {
			char extra[64] = {};
			snprintf(extra, sizeof(extra), I18N(u8"，已停用 %d 項服務"), svc_changed);
			strncat_s(msg, extra, _TRUNCATE);
		}
		if (g_has_revert_snapshot) {
			strncat_s(msg, I18N(u8"（可還原）"), _TRUNCATE);
		}
		SetLastAction(msg);
		return true;
	}

	bool ApplyRecommendedServices()
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"停用建議服務需要管理員權限");
			return false;
		}
		std::vector<std::string> names;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			for (const ServiceEntry& s : g_snapshot.services) {
				if (s.recommended_disable && s.exists
					&& s.start_type != SERVICE_DISABLED) {
					names.push_back(s.service_name);
				}
			}
		}
		if (names.empty()) {
			SetLastAction(u8"目前無建議停用的服務");
			return true;
		}
		int ok_count = 0;
		for (const std::string& name : names) {
			if (SetServiceDisabled(name.c_str(), true)) {
				++ok_count;
			}
		}
		char msg[128] = {};
		snprintf(msg, sizeof(msg), I18N(u8"已停用 %d / %zu 項建議服務"), ok_count, names.size());
		SetLastAction(msg);
		RequestScan();
		return ok_count > 0;
	}

	bool HasLastApplyRevert()
	{
		return g_has_revert_snapshot;
	}

	void GetLastApplySummary(char* buf, size_t buf_size)
	{
		if (buf == nullptr || buf_size == 0) {
			return;
		}
		strncpy_s(buf, buf_size, g_last_apply_summary, _TRUNCATE);
	}

	bool RevertLastApply()
	{
		RevertSnapshot snap = {};
		if (!LoadRevertSnapshot(snap)) {
			SetLastAction(u8"找不到可還原的記錄");
			g_has_revert_snapshot = false;
			return false;
		}
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"還原服務設定需要管理員權限");
			return false;
		}
		SetPowerPlanByKind(snap.power_kind);
		if (snap.visual_fx >= 0) {
			SetVisualFxSetting(snap.visual_fx);
		}
		SetGameMode(snap.game_mode);

		int restored = 0;
		for (const RevertServiceState& rs : snap.services) {
			wchar_t wname[64] = {};
			MultiByteToWideChar(CP_UTF8, 0, rs.name, -1, wname, static_cast<int>(std::size(wname)));
			if (ApplyServiceStartType(wname, rs.start_type)) {
				++restored;
			}
		}

		const std::string path = RevertSnapshotPath();
		DeleteFileA(path.c_str());
		g_has_revert_snapshot = false;
		g_last_apply_summary[0] = '\0';

		char msg[128] = {};
		snprintf(msg, sizeof(msg), I18N(u8"已還原「%s」（%d 項服務）"),
			snap.preset_label[0] ? snap.preset_label : snap.preset_id, restored);
		SetLastAction(msg);
		RequestScan();
		HLOG_INFO("OptimizeScan: RevertLastApply ok services={}", restored);
		return true;
	}

	const char* StartupImpactLabel(int impact_tier)
	{
		switch (static_cast<StartupImpactTier>(impact_tier)) {
		case StartupImpactTier::None: return u8"無";
		case StartupImpactTier::Low: return u8"低";
		case StartupImpactTier::Medium: return u8"中";
		case StartupImpactTier::High: return u8"高";
		default: return u8"未知";
		}
	}

	int CountHighImpactEnabledStartups(const Snapshot& snap)
	{
		int n = 0;
		for (const StartupEntry& e : snap.startups) {
			if (e.enabled && e.impact_tier >= static_cast<int>(StartupImpactTier::High)) {
				++n;
			}
		}
		return n;
	}

	int CountRecommendedServicesPending(const Snapshot& snap)
	{
		int n = 0;
		for (const ServiceEntry& s : snap.services) {
			if (s.recommended_disable && s.exists && s.start_type != SERVICE_DISABLED) {
				++n;
			}
		}
		return n;
	}

	int EstimateBootSavingsSeconds(const Snapshot& snap)
	{
		int sec = 0;
		for (const StartupEntry& e : snap.startups) {
			if (!e.enabled) {
				continue;
			}
			switch (static_cast<StartupImpactTier>(e.impact_tier)) {
			case StartupImpactTier::High: sec += 6; break;
			case StartupImpactTier::Medium: sec += 3; break;
			case StartupImpactTier::Low: sec += 1; break;
			default: break;
			}
		}
		sec += CountRecommendedServicesPending(snap) * 4;
		if (snap.visual_fx_setting != 2) {
			sec += 2;
		}
		if (snap.startups.size() > 14) {
			sec += 5;
		}
		return sec > 60 ? 60 : sec;
	}

	bool FlushDnsCache()
	{
		const bool ok = RunHiddenCommand(L"ipconfig.exe /flushdns");
		HLOG_INFO("OptimizeScan: FlushDns ok={}", ok);
		SetLastAction(ok ? "已重新整理 DNS 快取" : I18N(u8"DNS 刷新失敗"));
		return ok;
	}

	bool RegisterDnsCache()
	{
		const bool ok = RunHiddenCommand(L"ipconfig.exe /registerdns");
		HLOG_INFO("OptimizeScan: RegisterDns ok={}", ok);
		SetLastAction(ok ? "已重新註冊 DNS" : I18N(u8"DNS 註冊失敗"));
		return ok;
	}

	bool RenewIpAddresses()
	{
		const bool ok = RunHiddenCommand(L"ipconfig.exe /renew");
		HLOG_INFO("OptimizeScan: RenewIp ok={}", ok);
		SetLastAction(ok ? "已更新 IP 位址" : I18N(u8"IP 更新失敗"));
		return ok;
	}

	bool ResetWinsockCatalog()
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"重設 Winsock 需要管理員權限");
			return false;
		}
		const bool ok = RunHiddenCommand(L"netsh.exe winsock reset");
		HLOG_INFO("OptimizeScan: WinsockReset ok={}", ok);
		SetLastAction(ok ? "已要求重設 Winsock（建議重新開機）" : I18N(u8"Winsock 重設失敗"));
		return ok;
	}

	bool SetGameModeEnabled(bool on)
	{
		SetGameMode(on);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.game_mode_on = on;
		}
		SetLastAction(on ? I18N(u8"已開啟遊戲模式") : I18N(u8"已關閉遊戲模式"));
		return true;
	}

	bool SetVisualEffects(int setting)
	{
		if (setting < 0 || setting > 3) {
			SetLastAction(u8"無效的視覺效果設定");
			return false;
		}
		SetVisualFxSetting(setting);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.visual_fx_setting = setting;
		}
		SetLastAction(u8"已更新視覺效果設定");
		return true;
	}

	bool SetTransparencyEffects(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入透明度設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		RegSetValueExW(key, L"EnableTransparency", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.transparency_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用視窗透明效果") : I18N(u8"已關閉視窗透明效果"));
		return true;
	}

	bool SetProcessorSchedulingPrograms(bool foreground)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", 0,
			KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
			SetLastAction(u8"無法調整處理器排程（需管理員）");
			return false;
		}
		const DWORD val = foreground ? 26u : 18u;
		const LONG err = RegSetValueExW(key, L"Win32PrioritySeparation", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.processor_foreground = foreground;
		}
		SetLastAction(ok
			? (foreground ? I18N(u8"已優先前景程式") : I18N(u8"已還原預設排程"))
			: I18N(u8"處理器排程設定失敗"));
		return ok;
	}

	bool SetFastStartup(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"快速啟動需管理員權限");
			return false;
		}
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", 0,
			KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入快速啟動設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		const LONG err = RegSetValueExW(key, L"HiberbootEnabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.fast_startup_on = enabled;
		}
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用快速啟動") : I18N(u8"已關閉快速啟動"))
			: I18N(u8"快速啟動設定失敗"));
		return ok;
	}

	bool SetGameDvrEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入 Game DVR 設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		RegSetValueExW(key, L"AppCaptureEnabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.game_dvr_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用背景錄影") : I18N(u8"已關閉 Xbox 背景錄影"));
		return true;
	}

	bool SetAnimationsEnabled(bool enabled)
	{
		ANIMATIONINFO anim = { sizeof(ANIMATIONINFO) };
		anim.iMinAnimate = enabled ? 1 : 0;
		const BOOL ok_anim = SystemParametersInfoW(SPI_SETANIMATION, sizeof(anim), &anim,
			SPIF_SENDCHANGE);
		const BOOL ok_menu = SystemParametersInfoW(SPI_SETMENUANIMATION, 0,
			reinterpret_cast<PVOID>(static_cast<INT_PTR>(enabled ? TRUE : FALSE)),
			SPIF_SENDCHANGE);
		const BOOL ok_combo = SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0,
			reinterpret_cast<PVOID>(static_cast<INT_PTR>(enabled ? TRUE : FALSE)),
			SPIF_SENDCHANGE);
		const bool ok = (ok_anim != FALSE) || (ok_menu != FALSE) || (ok_combo != FALSE);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.animations_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用視窗動畫") : I18N(u8"已關閉視窗動畫（更流暢）"));
		return ok;
	}

	bool SetHardwareGpuScheduling(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"硬體 GPU 排程需管理員權限");
			return false;
		}
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", 0,
			KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入 GPU 排程設定");
			return false;
		}
		const DWORD val = enabled ? 2u : 1u;
		const LONG err = RegSetValueExW(key, L"HwSchMode", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.gpu_scheduling_on = enabled;
		}
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用硬體 GPU 排程（需重開機）") : I18N(u8"已關閉硬體 GPU 排程（需重開機）"))
			: I18N(u8"GPU 排程設定失敗"));
		return ok;
	}

	bool SetHibernateEnabled(bool enabled)
	{
		if (enabled && !HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"啟用休眠需管理員權限");
			return false;
		}
		wchar_t cmd[64] = {};
		_snwprintf_s(cmd, _TRUNCATE, L"powercfg.exe -h %s", enabled ? L"on" : L"off");
		const bool ok = RunHiddenCommand(cmd);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.hibernate_on = enabled;
		}
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用休眠") : I18N(u8"已關閉休眠（可釋放磁碟空間）"))
			: I18N(u8"休眠設定失敗（需管理員）"));
		HLOG_INFO("OptimizeScan: SetHibernate enabled={} ok={}", enabled, ok);
		return ok;
	}

	bool SetMouseAccelerationEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0,
			KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入滑鼠設定");
			return false;
		}
		const wchar_t* speed = enabled ? L"1" : L"0";
		const wchar_t* th1 = enabled ? L"6" : L"0";
		const wchar_t* th2 = enabled ? L"10" : L"0";
		RegSetValueExW(key, L"MouseSpeed", 0, REG_SZ,
			reinterpret_cast<const BYTE*>(speed),
			static_cast<DWORD>((wcslen(speed) + 1) * sizeof(wchar_t)));
		RegSetValueExW(key, L"MouseThreshold1", 0, REG_SZ,
			reinterpret_cast<const BYTE*>(th1),
			static_cast<DWORD>((wcslen(th1) + 1) * sizeof(wchar_t)));
		RegSetValueExW(key, L"MouseThreshold2", 0, REG_SZ,
			reinterpret_cast<const BYTE*>(th2),
			static_cast<DWORD>((wcslen(th2) + 1) * sizeof(wchar_t)));
		RegCloseKey(key);

		int mouse_params[3] = { enabled ? 1 : 0, enabled ? 6 : 0, enabled ? 10 : 0 };
		SystemParametersInfoW(SPI_SETMOUSE, 0, mouse_params, SPIF_SENDCHANGE);

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.mouse_accel_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用滑鼠加速") : I18N(u8"已關閉滑鼠加速（遊戲更精準）"));
		return true;
	}

	bool SetFullscreenOptimizations(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"System\\GameConfigStore", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入全螢幕最佳化設定");
			return false;
		}
		const DWORD fse = enabled ? 0u : 2u;
		const DWORD honor = 1u;
		RegSetValueExW(key, L"GameDVR_FSEBehavior", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&fse), sizeof(fse));
		RegSetValueExW(key, L"GameDVR_FSEBehaviorMode", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&fse), sizeof(fse));
		RegSetValueExW(key, L"GameDVR_HonorUserFSEBehaviorMode", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&honor), sizeof(honor));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.fullscreen_opt_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用全螢幕最佳化") : I18N(u8"已關閉全螢幕最佳化（部分遊戲更順）"));
		return true;
	}

	bool ApplyQuickGamingTune()
	{
		HLOG_INFO("OptimizeScan: ApplyQuickGamingTune");
		bool ok = true;
		ok = SetPowerPlanByKind("high") && ok;
		ok = SetGameModeEnabled(true) && ok;
		ok = SetVisualEffects(2) && ok;
		ok = SetGameDvrEnabled(false) && ok;
		ok = SetAnimationsEnabled(false) && ok;
		ok = SetTransparencyEffects(false) && ok;
		ok = SetMouseAccelerationEnabled(false) && ok;
		ok = SetFullscreenOptimizations(false) && ok;
		ok = SetGameBarEnabled(false) && ok;
		ok = SetFastMenuDelay(true) && ok;
		if (HCleanIsRunningAsAdmin()) {
			ok = SetProcessorSchedulingPrograms(true) && ok;
			ok = SetNetworkThrottlingEnabled(false) && ok;
			ok = SetGameSystemResponsiveness(true) && ok;
		}
		SetLastAction(ok ? I18N(u8"已套用遊戲極致調校（電源／視覺／輸入）") : I18N(u8"遊戲調校部分失敗，請查看最近操作"));
		return ok;
	}

	bool ApplyQuickOfficeTune()
	{
		HLOG_INFO("OptimizeScan: ApplyQuickOfficeTune");
		bool ok = true;
		ok = SetPowerPlanByKind("balanced") && ok;
		ok = SetGameModeEnabled(false) && ok;
		ok = SetVisualEffects(0) && ok;
		ok = SetAnimationsEnabled(true) && ok;
		ok = SetTransparencyEffects(true) && ok;
		ok = SetMouseAccelerationEnabled(true) && ok;
		ok = SetFullscreenOptimizations(true) && ok;
		ok = SetGameDvrEnabled(false) && ok;
		SetLastAction(ok ? I18N(u8"已套用辦公平衡調校") : I18N(u8"辦公調校部分失敗"));
		return ok;
	}

	bool ApplyQuickBatteryTune()
	{
		HLOG_INFO("OptimizeScan: ApplyQuickBatteryTune");
		bool ok = true;
		ok = SetPowerPlanByKind("saver") && ok;
		ok = SetGameModeEnabled(false) && ok;
		ok = SetVisualEffects(2) && ok;
		ok = SetAnimationsEnabled(false) && ok;
		ok = SetTransparencyEffects(false) && ok;
		ok = SetGameDvrEnabled(false) && ok;
		SetLastAction(ok ? I18N(u8"已套用省電續航調校") : I18N(u8"省電調校部分失敗"));
		return ok;
	}

	bool SetTipsAndSuggestionsEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入提示與建議設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		RegSetValueExW(key, L"SubscribedContent-338388Enabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegSetValueExW(key, L"SubscribedContent-310093Enabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegSetValueExW(key, L"SoftLandingEnabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.tips_suggestions_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用 Windows 提示與建議") : I18N(u8"已關閉 Windows 提示與建議"));
		return true;
	}

	bool SetBackgroundAppsEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入背景應用程式設定");
			return false;
		}
		const DWORD val = enabled ? 0u : 1u;
		RegSetValueExW(key, L"GlobalUserDisabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.background_apps_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已允許背景應用程式") : I18N(u8"已限制背景應用程式（更省資源）"));
		return true;
	}

	bool SetPowerThrottlingEnabled(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"電源節流設定需管理員權限");
			return false;
		}
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入電源節流設定");
			return false;
		}
		const DWORD val = enabled ? 0u : 1u;
		const LONG err = RegSetValueExW(key, L"PowerThrottlingOff", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.power_throttling_on = enabled;
		}
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用電源節流（較省電）") : I18N(u8"已關閉電源節流（較高效能）"))
			: I18N(u8"電源節流設定失敗"));
		return ok;
	}

	bool SetGameBarEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"System\\GameConfigStore", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入 Xbox Game Bar 設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		RegSetValueExW(key, L"GameBarEnabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);

		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
			const DWORD panel = enabled ? 1u : 0u;
			RegSetValueExW(key, L"ShowStartupPanel", 0, REG_DWORD,
				reinterpret_cast<const BYTE*>(&panel), sizeof(panel));
			RegCloseKey(key);
		}

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.game_bar_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用 Xbox Game Bar") : I18N(u8"已關閉 Xbox Game Bar 疊加層"));
		return true;
	}

	bool SetDeliveryOptimizationP2P(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"傳遞優化 P2P 設定需管理員權限");
			HLOG_WARN("OptimizeScan: SetDeliveryOptimizationP2P 需要管理員權限");
			return false;
		}
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DeliveryOptimization\\Settings", 0,
			nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入傳遞優化設定");
			HLOG_WARN("OptimizeScan: SetDeliveryOptimizationP2P 無法開啟登錄鍵 err={}",
				GetLastError());
			return false;
		}
		const DWORD val = enabled ? 3u : 0u;
		const LONG err = RegSetValueExW(key, L"DownloadMode", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.delivery_p2p_on = enabled;
		}
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用更新 P2P 分享") : I18N(u8"已關閉更新 P2P（減少背景網路）"))
			: I18N(u8"傳遞優化設定失敗"));
		HLOG_INFO("OptimizeScan: SetDeliveryOptimizationP2P enabled={} ok={}", enabled, ok);
		return ok;
	}

	bool SetSearchHighlightsEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Search", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入搜尋亮點設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		RegSetValueExW(key, L"ShowDynamicContentInWSB", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegSetValueExW(key, L"BingSearchEnabled", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.search_highlights_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用搜尋亮點") : I18N(u8"已關閉搜尋亮點與動態建議"));
		return true;
	}

	bool SetWidgetsEnabled(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入小工具列設定");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		RegSetValueExW(key, L"TaskbarDa", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.widgets_on = enabled;
		}
		SetLastAction(enabled ? I18N(u8"已啟用工作列小工具") : I18N(u8"已關閉工作列小工具"));
		return true;
	}

	bool SetNetworkThrottlingEnabled(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"網路節流設定需管理員權限");
			return false;
		}
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", 0,
			nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入網路節流設定");
			return false;
		}
		const DWORD val = enabled ? 10u : 0xFFFFFFFFu;
		const LONG err = RegSetValueExW(key, L"NetworkThrottlingIndex", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.network_throttling_on = enabled;
		}
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用網路節流（多媒體優先）") : I18N(u8"已關閉網路節流（遊戲／即時通訊較順）"))
			: I18N(u8"網路節流設定失敗"));
		return ok;
	}

	bool SetGameSystemResponsiveness(bool game_priority)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"系統響應度設定需管理員權限");
			return false;
		}
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", 0,
			nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入系統響應度設定");
			return false;
		}
		const DWORD val = game_priority ? 0u : 20u;
		const LONG err = RegSetValueExW(key, L"SystemResponsiveness", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.game_responsiveness_on = game_priority;
		}
		SetLastAction(ok
			? (game_priority ? I18N(u8"已設為遊戲高響應度") : I18N(u8"已還原預設系統響應度"))
			: I18N(u8"系統響應度設定失敗"));
		return ok;
	}

	bool SetFastMenuDelay(bool fast)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0,
			KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
			SetLastAction(u8"無法寫入選單延遲設定");
			return false;
		}
		const wchar_t* delay = fast ? L"0" : L"400";
		const LONG err = RegSetValueExW(key, L"MenuShowDelay", 0, REG_SZ,
			reinterpret_cast<const BYTE*>(delay),
			static_cast<DWORD>((wcslen(delay) + 1) * sizeof(wchar_t)));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		if (ok) {
			DWORD delay_ms = fast ? 0u : 400u;
			SystemParametersInfoW(SPI_SETMENUSHOWDELAY, 0,
				reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(delay_ms)), SPIF_SENDCHANGE);
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.fast_menu_delay = fast;
		}
		SetLastAction(ok
			? (fast ? I18N(u8"已設為即時選單回應") : I18N(u8"已還原預設選單延遲"))
			: I18N(u8"選單延遲設定失敗"));
		return ok;
	}

	bool ApplyQuickResponsiveTune()
	{
		HLOG_INFO("OptimizeScan: ApplyQuickResponsiveTune");
		bool ok = true;
		ok = SetPowerPlanByKind("high") && ok;
		ok = SetVisualEffects(2) && ok;
		ok = SetAnimationsEnabled(false) && ok;
		ok = SetTransparencyEffects(false) && ok;
		ok = SetGameDvrEnabled(false) && ok;
		ok = SetGameBarEnabled(false) && ok;
		ok = SetTipsAndSuggestionsEnabled(false) && ok;
		ok = SetBackgroundAppsEnabled(false) && ok;
		ok = SetSearchHighlightsEnabled(false) && ok;
		ok = SetWidgetsEnabled(false) && ok;
		ok = SetFastMenuDelay(true) && ok;
		ok = SetMouseAccelerationEnabled(false) && ok;
		if (HCleanIsRunningAsAdmin()) {
			ok = SetProcessorSchedulingPrograms(true) && ok;
			ok = SetPowerThrottlingEnabled(false) && ok;
			ok = SetDeliveryOptimizationP2P(false) && ok;
			ok = SetNetworkThrottlingEnabled(false) && ok;
			ok = SetGameSystemResponsiveness(true) && ok;
		}
		SetLastAction(ok ? I18N(u8"已套用極致響應調校") : I18N(u8"極致響應調校部分失敗"));
		return ok;
	}

	bool OpenWindowsPowerSettings()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"ms-settings:powersleep",
			nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟 Windows 電源設定") : I18N(u8"無法開啟電源設定"));
		return ok;
	}

	bool OpenWindowsGameSettings()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"ms-settings:gaming-gamemode",
			nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟 Windows 遊戲設定") : I18N(u8"無法開啟遊戲設定"));
		return ok;
	}

	void RefreshSystemSettings()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		QueryPowerPlan(g_snapshot);
		QueryGameMode(g_snapshot);
		QueryVisualFx(g_snapshot);
		QuerySystemPerfExtras(g_snapshot);
		QuerySystemDriveUsage(g_snapshot);
		SetLastAction(u8"已重新讀取系統設定");
		HLOG_INFO("OptimizeScan: RefreshSystemSettings");
	}

	bool CreateSystemRestorePoint()
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"建立還原點需要管理員權限");
			return false;
		}
		const bool ok = RunHiddenCommand(
			L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"Checkpoint-Computer -Description 'HP CLEANER++' -RestorePointType MODIFY_SETTINGS\"");
		HLOG_INFO("OptimizeScan: CreateRestorePoint ok={}", ok);
		SetLastAction(ok ? I18N(u8"已要求建立還原點") : I18N(u8"還原點建立失敗"));
		return ok;
	}

	bool OpenTaskManagerStartup()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟工作管理員") : I18N(u8"無法開啟工作管理員"));
		return ok;
	}

	bool OpenServicesConsole()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"services.msc", nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟服務主控台") : I18N(u8"無法開啟服務"));
		return ok;
	}

	const char* GetActivePowerPlanKind()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return PowerKindFromGuid(g_snapshot.power_plan_guid);
	}

	int GetStorageDriveCount()
	{
		RefreshStorageDriveList();
		return static_cast<int>(g_storage_drives.size());
	}

	bool GetStorageDrive(int index, StorageDriveInfo& out)
	{
		RefreshStorageDriveList();
		if (index < 0 || index >= static_cast<int>(g_storage_drives.size())) {
			return false;
		}
		out = g_storage_drives[static_cast<size_t>(index)];
		return true;
	}

	void SetStorageMaintenanceDrive(char letter)
	{
		const char upper = static_cast<char>(toupper(static_cast<unsigned char>(letter)));
		if (upper < 'A' || upper > 'Z') {
			HLOG_WARN("OptimizeScan: SetStorageMaintenanceDrive 無效磁碟代號 '{}'", letter);
			return;
		}
		wchar_t root[4] = { static_cast<wchar_t>(upper), L':', L'\\', L'\0' };
		if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) {
			HLOG_WARN("OptimizeScan: SetStorageMaintenanceDrive {}: 不存在", upper);
			return;
		}
		g_disk_maint_drive = upper;
		HLOG_INFO("OptimizeScan: SetStorageMaintenanceDrive {}", upper);
	}

	char GetStorageMaintenanceDrive()
	{
		return g_disk_maint_drive;
	}

	void RequestDiskOptimizationAnalyze()
	{
		if (g_disk_opt_running.load(std::memory_order_acquire)) {
			SetLastAction(u8"磁碟作業進行中，請稍候");
			HLOG_WARN("OptimizeScan: RequestDiskOptimizationAnalyze 略過（作業進行中）");
			return;
		}
		if (!HCleanHasElevatedAccess()) {
			SetLastAction(u8"磁碟分析需要管理員權限");
			HLOG_WARN("OptimizeScan: RequestDiskOptimizationAnalyze 需要管理員權限");
			return;
		}
		if (g_storage_worker_running.load(std::memory_order_acquire)
			|| g_storage_monitor_clean_scan) {
			SetLastAction(u8"儲存作業進行中，請稍候");
			HLOG_WARN("OptimizeScan: RequestDiskOptimizationAnalyze 略過（儲存作業進行中）");
			return;
		}
		if (g_disk_opt_worker.joinable()) {
			g_disk_opt_worker.join();
		}
		const char drive = g_disk_maint_drive;
		StorageWorkBegin(I18N(u8"磁碟分析"));
		char intro[96] = {};
		snprintf(intro, sizeof(intro), I18N(u8"準備分析 %c: 磁碟…"), drive);
		StorageWorkLog(intro);
		StorageWorkSetProgress(0.05f, intro);
		{
			std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
			g_disk_opt.running = true;
			g_disk_opt.progress = 0.05f;
			g_disk_opt.drive_letter = drive;
			snprintf(g_disk_opt.status_text, sizeof(g_disk_opt.status_text),
				I18N(u8"正在分析 %c: 磁碟…"), drive);
		}
		g_disk_opt_running.store(true, std::memory_order_release);
		char action[48] = {};
		snprintf(action, sizeof(action), I18N(u8"已開始分析 %c: 磁碟"), drive);
		SetLastAction(action);
		HLOG_INFO("OptimizeScan: RequestDiskOptimizationAnalyze drive={}", drive);
		g_disk_opt_worker = std::thread([drive] { RunDiskOptimizationAnalyzeWorker(drive); });
	}

	void RequestDiskOptimizationRun()
	{
		if (g_disk_opt_running.load(std::memory_order_acquire)) {
			SetLastAction(u8"磁碟作業進行中，請稍候");
			HLOG_WARN("OptimizeScan: RequestDiskOptimizationRun 略過（作業進行中）");
			return;
		}
		if (!HCleanHasElevatedAccess()) {
			SetLastAction(u8"硬碟最佳化需要管理員權限");
			HLOG_WARN("OptimizeScan: RequestDiskOptimizationRun 需要管理員權限");
			return;
		}
		if (g_storage_worker_running.load(std::memory_order_acquire)
			|| g_storage_monitor_clean_scan) {
			SetLastAction(u8"儲存作業進行中，請稍候");
			HLOG_WARN("OptimizeScan: RequestDiskOptimizationRun 略過（儲存作業進行中）");
			return;
		}
		if (g_disk_opt_worker.joinable()) {
			g_disk_opt_worker.join();
		}
		const char drive = g_disk_maint_drive;
		StorageWorkBegin(I18N(u8"硬碟最佳化"));
		char intro[96] = {};
		snprintf(intro, sizeof(intro), I18N(u8"準備最佳化 %c: 磁碟…"), drive);
		StorageWorkLog(intro);
		StorageWorkSetProgress(0.05f, intro);
		{
			std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
			g_disk_opt.running = true;
			g_disk_opt.progress = 0.05f;
			g_disk_opt.drive_letter = drive;
			snprintf(g_disk_opt.status_text, sizeof(g_disk_opt.status_text),
				I18N(u8"正在最佳化 %c: …"), drive);
		}
		g_disk_opt_running.store(true, std::memory_order_release);
		char action[48] = {};
		snprintf(action, sizeof(action), I18N(u8"已開始最佳化 %c: 磁碟"), drive);
		SetLastAction(action);
		HLOG_INFO("OptimizeScan: RequestDiskOptimizationRun drive={}", drive);
		g_disk_opt_worker = std::thread([drive] { RunDiskOptimizationWorker(drive); });
	}

	DiskOptimizationSnapshot GetDiskOptimization()
	{
		std::lock_guard<std::mutex> lock(g_disk_opt_mutex);
		return g_disk_opt;
	}

	bool IsDiskOptimizationRunning()
	{
		return g_disk_opt_running.load(std::memory_order_acquire);
	}

	StorageLocalSettings GetStorageLocalSettings()
	{
		StorageLocalSettings settings = {};
		QueryStorageLocalSettings(settings);
		return settings;
	}

	bool SetStorageSenseEnabled(bool enabled)
	{
		const DWORD val = enabled ? 1u : 0u;
		const bool ok01 = SetStoragePolicyDword(L"01", val);
		const bool ok2048 = SetStoragePolicyDword(L"2048", val);
		const bool ok = ok01 || ok2048;
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用儲存感知") : I18N(u8"已關閉儲存感知"))
			: I18N(u8"儲存感知設定失敗"));
		HLOG_INFO("OptimizeScan: SetStorageSenseEnabled enabled={} ok={}", enabled, ok);
		return ok;
	}

	bool SetStorageAutoTempCleanup(bool enabled)
	{
		const bool ok = SetStoragePolicyDword(L"02", enabled ? 1u : 0u);
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用自動清理暫存") : I18N(u8"已關閉自動清理暫存"))
			: I18N(u8"暫存清理設定失敗"));
		HLOG_INFO("OptimizeScan: SetStorageAutoTempCleanup enabled={} ok={}", enabled, ok);
		return ok;
	}

	bool SetStorageLowDiskAutoRun(bool enabled)
	{
		const bool ok = SetStoragePolicyDword(L"256", enabled ? 1u : 0u);
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用低空間自動清理") : I18N(u8"已關閉低空間自動清理"))
			: I18N(u8"低空間清理設定失敗"));
		HLOG_INFO("OptimizeScan: SetStorageLowDiskAutoRun enabled={} ok={}", enabled, ok);
		return ok;
	}

	bool SetStorageRecycleBinDays(int days)
	{
		if (days < 0) {
			days = 0;
		}
		const bool ok = SetStoragePolicyDword(L"04", static_cast<DWORD>(days));
		char msg[64] = {};
		if (days == 0) {
			snprintf(msg, sizeof(msg), I18N(u8"回收筒：永不自動清理"));
		}
		else {
			snprintf(msg, sizeof(msg), I18N(u8"回收筒：%d 天後自動清理"), days);
		}
		SetLastAction(ok ? msg : I18N(u8"回收筒保留設定失敗"));
		HLOG_INFO("OptimizeScan: SetStorageRecycleBinDays days={} ok={}", days, ok);
		return ok;
	}

	void RequestStorageQuickClean(uint32_t flags)
	{
		if (flags == 0) {
			flags = static_cast<uint32_t>(StorageQuickCleanFlags::TempFiles)
				| static_cast<uint32_t>(StorageQuickCleanFlags::RecycleBin)
				| static_cast<uint32_t>(StorageQuickCleanFlags::DeliveryCache);
		}
		if (g_storage_worker_running.load(std::memory_order_acquire)
			|| g_disk_opt_running.load(std::memory_order_acquire)) {
			SetLastAction(u8"儲存作業進行中，請稍候");
			HLOG_WARN("OptimizeScan: RequestStorageQuickClean 略過（作業進行中）");
			return;
		}
		if (g_storage_worker.joinable()) {
			g_storage_worker.join();
		}
		HLOG_INFO("OptimizeScan: RequestStorageQuickClean flags=0x{:X}", flags);
		g_storage_worker = std::thread([flags] { RunStorageQuickCleanWorker(flags); });
	}

	void RequestStorageCleanScan()
	{
		if (g_storage_monitor_clean_scan || IsDeferredScanAllCleanTasksActive()
			|| IsAnyCleanTaskScanning()) {
			SetLastAction(u8"清理掃描進行中");
			HLOG_WARN("OptimizeScan: RequestStorageCleanScan 略過（掃描進行中）");
			return;
		}
		StorageWorkBegin(I18N(u8"掃描可清理項目"));
		StorageWorkLog(I18N(u8"開始掃描本程式清理任務大小…"));
		g_storage_monitor_clean_scan = true;
		g_storage_last_scan_task[0] = '\0';
		BeginDeferredScanAllCleanTasks();
		SetLastAction(u8"已開始掃描可清理項目");
		HLOG_INFO("OptimizeScan: RequestStorageCleanScan");
	}

	void RequestStorageRestorePoint()
	{
		if (g_storage_worker_running.load(std::memory_order_acquire)) {
			SetLastAction(u8"儲存作業進行中，請稍候");
			HLOG_WARN("OptimizeScan: RequestStorageRestorePoint 略過（作業進行中）");
			return;
		}
		if (!HCleanHasElevatedAccess()) {
			SetLastAction(u8"建立還原點需要管理員權限");
			HLOG_WARN("OptimizeScan: RequestStorageRestorePoint 需要管理員權限");
			return;
		}
		if (g_storage_worker.joinable()) {
			g_storage_worker.join();
		}
		HLOG_INFO("OptimizeScan: RequestStorageRestorePoint");
		g_storage_worker = std::thread([] { RunStorageRestorePointWorker(); });
	}

	void TickStorageWork()
	{
		if (g_storage_monitor_clean_scan) {
			TickDeferredScanAllCleanTasks(2);
			HCleanGlobalScanInfo info = {};
			if (GetGlobalCleanScanInfo(&info) && info.any_scanning) {
				const float pct = ClampUnitFloat(info.aggregate_percent / 100.f);
				const float scan_pct = pct > 0.99f ? 0.99f : pct;
				char status[160] = {};
				if (info.current_task_name[0] != '\0') {
					snprintf(status, sizeof(status), I18N(u8"掃描：%s"), info.current_task_name);
				}
				else {
					strncpy_s(status, u8"掃描可清理項目中…", _TRUNCATE);
				}
				StorageWorkSetProgress(scan_pct, status);
				if (info.current_task_name[0] != '\0'
					&& strcmp(g_storage_last_scan_task, info.current_task_name) != 0) {
					strncpy_s(g_storage_last_scan_task, info.current_task_name, _TRUNCATE);
					char line[160] = {};
					snprintf(line, sizeof(line), I18N(u8"正在掃描：%s"), info.current_task_name);
					StorageWorkLog(line);
				}
			}
			else if (!IsDeferredScanAllCleanTasksActive() && !IsAnyCleanTaskScanning()) {
				g_storage_monitor_clean_scan = false;
				HCleanSizeSummary summary = {};
				GetCleanTasksSizeSummary(&summary);
				char done[128] = {};
				char sz[32] = {};
				FormatCleanSize(summary.visible_total_bytes, sz, sizeof(sz));
				snprintf(done, sizeof(done), I18N(u8"掃描完成：估計可清理 %s（%d 項已掃描）"),
					sz, summary.visible_scanned_count);
				StorageWorkLog(done);
				StorageWorkEnd(I18N(u8"掃描完成"), 1.f);
				SetLastAction(done);
			}
		}
	}

	StorageWorkSnapshot GetStorageWorkSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_storage_work_mutex);
		return g_storage_work;
	}

	void ClearStorageWorkLog()
	{
		std::lock_guard<std::mutex> lock(g_storage_work_mutex);
		g_storage_work.log_count = 0;
		g_storage_work.log_lines[0][0] = '\0';
		HLOG_DEBUG("OptimizeScan: ClearStorageWorkLog");
	}

	const char* GetLastActionMessage()
	{
		return g_last_action;
	}
}