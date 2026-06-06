#include "HAppLaunch.h"
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include <string>
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
		std::vector<char> out(static_cast<size_t>(needed - 1));
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
		return std::string(out.begin(), out.end());
	}

	bool ArgEquals(const char* arg, const char* name)
	{
		return arg != nullptr && _stricmp(arg, name) == 0;
	}

	bool ArgStartsWith(const char* arg, const char* prefix)
	{
		if (arg == nullptr || prefix == nullptr) {
			return false;
		}
		const size_t prefix_len = std::strlen(prefix);
		return _strnicmp(arg, prefix, prefix_len) == 0 && arg[prefix_len] == '=';
	}

	std::string ValueAfterEquals(const char* arg)
	{
		const char* eq = std::strchr(arg, '=');
		if (eq == nullptr) {
			return {};
		}
		std::string value = eq + 1;
		while (!value.empty() && (value.front() == ' ' || value.front() == '"')) {
			value.erase(value.begin());
		}
		while (!value.empty() && (value.back() == ' ' || value.back() == '"')) {
			value.pop_back();
		}
		return value;
	}

	void ParseModeToken(const char* arg, HAppLaunchOptions& opts)
	{
		if (ArgEquals(arg, "--crash-report") || ArgEquals(arg, "/crash-report")) {
			opts.mode = HAppRunMode::CrashReport;
			return;
		}
		if (ArgEquals(arg, "--help") || ArgEquals(arg, "/help") || ArgEquals(arg, "-h") || ArgEquals(arg, "/?")) {
			opts.mode = HAppRunMode::Help;
			return;
		}
		if (ArgEquals(arg, "--tray") || ArgEquals(arg, "/tray")) {
			opts.start_to_tray = true;
			return;
		}
		if (ArgEquals(arg, "--test-crash") || ArgEquals(arg, "/test-crash")) {
			opts.mode = HAppRunMode::TestCrash;
			return;
		}
		if (ArgEquals(arg, "--watchdog") || ArgEquals(arg, "/watchdog")) {
			opts.mode = HAppRunMode::Watchdog;
			return;
		}
		if (ArgStartsWith(arg, "--mode")) {
			const std::string value = ValueAfterEquals(arg);
			if (_stricmp(value.c_str(), "crash-report") == 0) {
				opts.mode = HAppRunMode::CrashReport;
			}
			else if (_stricmp(value.c_str(), "test-crash") == 0) {
				opts.mode = HAppRunMode::TestCrash;
			}
			else if (_stricmp(value.c_str(), "watchdog") == 0) {
				opts.mode = HAppRunMode::Watchdog;
			}
			else if (_stricmp(value.c_str(), "elev-broker") == 0) {
				opts.mode = HAppRunMode::ElevBroker;
			}
			else if (_stricmp(value.c_str(), "uninstall") == 0) {
				opts.mode = HAppRunMode::Uninstall;
			}
			else if (_stricmp(value.c_str(), "logconsole") == 0) {
				opts.mode = HAppRunMode::LogConsole;
			}
			else if (_stricmp(value.c_str(), "app") == 0 || _stricmp(value.c_str(), "main") == 0) {
				opts.mode = HAppRunMode::MainApp;
			}
		}
	}
}

HAppLaunchOptions HAppParseCommandLine()
{
	HAppLaunchOptions opts;

	int argc = 0;
	wchar_t** argv_wide = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv_wide == nullptr) {
		return opts;
	}

	for (int i = 1; i < argc; ++i) {
		const wchar_t* warg = argv_wide[i];
		if (warg != nullptr) {
			if (wcsncmp(warg, L"--pipe=", 7) == 0) {
				opts.elev_broker_pipe.assign(warg + 7);
			}
			else if (wcsncmp(warg, L"--ready-event=", 14) == 0) {
				opts.elev_broker_ready_event.assign(warg + 14);
			}
			else if (wcsncmp(warg, L"--parent-pid=", 13) == 0) {
				opts.elev_broker_parent_pid = wcstoul(warg + 13, nullptr, 10);
			}
			else if (wcsncmp(warg, L"--log=", 6) == 0) {
				opts.log_console_path = WideToUtf8(warg + 6);
			}
			else if (wcsncmp(warg, L"--log-read-handle=", 18) == 0) {
				opts.log_read_handle = static_cast<uintptr_t>(_wcstoui64(warg + 18, nullptr, 10));
			}
		}

		const std::string arg = WideToUtf8(argv_wide[i]);
		if (arg.empty()) {
			continue;
		}

		ParseModeToken(arg.c_str(), opts);

		if (ArgStartsWith(arg.c_str(), "--log")) {
			opts.log_console_path = ValueAfterEquals(arg.c_str());
		}
		else if (ArgStartsWith(arg.c_str(), "--report")) {
			opts.crash_report_path = ValueAfterEquals(arg.c_str());
		}
		else if (ArgEquals(arg.c_str(), "--report") && i + 1 < argc) {
			opts.crash_report_path = WideToUtf8(argv_wide[++i]);
		}
		else if (ArgStartsWith(arg.c_str(), "--parent-pid")) {
			opts.watchdog_parent_pid = static_cast<DWORD>(std::strtoul(ValueAfterEquals(arg.c_str()).c_str(), nullptr, 10));
		}
		else if (ArgStartsWith(arg.c_str(), "--log-read-handle")) {
			opts.log_read_handle = static_cast<uintptr_t>(
				std::strtoull(ValueAfterEquals(arg.c_str()).c_str(), nullptr, 10));
		}
		else if (ArgEquals(arg.c_str(), "--parent-pid") && i + 1 < argc) {
			opts.watchdog_parent_pid = static_cast<DWORD>(std::strtoul(WideToUtf8(argv_wide[++i]).c_str(), nullptr, 10));
		}
	}

	LocalFree(argv_wide);
	return opts;
}
