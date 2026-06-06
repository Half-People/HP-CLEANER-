#pragma once
#ifndef HAPP_LAUNCH_H
#define HAPP_LAUNCH_H

#include <cstdint>
#include <string>
#include <windows.h>

enum class HAppRunMode {
	MainApp,
	CrashReport,
	Watchdog,
	ElevBroker,
	Uninstall,
	LogConsole,
	TestCrash,
	Help,
};

struct HAppLaunchOptions {
	HAppRunMode mode = HAppRunMode::MainApp;
	std::string crash_report_path;
	DWORD watchdog_parent_pid = 0;
	DWORD elev_broker_parent_pid = 0;
	std::wstring elev_broker_pipe;
	std::wstring elev_broker_ready_event;
	std::string log_console_path;
	uintptr_t log_read_handle = 0;
	bool start_to_tray = false;
};

HAppLaunchOptions HAppParseCommandLine();

#endif
