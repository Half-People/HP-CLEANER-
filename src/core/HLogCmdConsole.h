#pragma once
#ifndef HLOG_CMD_CONSOLE_H
#define HLOG_CMD_CONSOLE_H

#include <cstdint>
#include <string>
#include <windows.h>

// 以獨立子進程顯示彩色日誌（關閉控制台不影響主程式）
void HLogCmdConsoleLaunch();
void HLogCmdConsoleShutdown();
int RunLogConsoleApplication(const std::string& log_path, DWORD parent_pid, uintptr_t read_handle_val);

#endif
