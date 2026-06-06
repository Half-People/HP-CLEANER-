#pragma once
#ifndef HELEVATION_BROKER_H
#define HELEVATION_BROKER_H

#include <windows.h>

// 背景 UAC 提權代理：主視窗保持運行，特權命令由隱藏的管理員子進程執行。
namespace HElevationBroker {

	bool RequestElevation();
	bool IsConnected();
	bool RunHiddenCommand(const wchar_t* command_line, DWORD timeout_ms = 120000,
		DWORD* exit_code_out = nullptr);
	void Shutdown();

	int RunBrokerMain(DWORD parent_pid, const wchar_t* pipe_base_name,
		const wchar_t* ready_event_name = nullptr);
}

bool HCleanHasElevatedAccess();

#endif
