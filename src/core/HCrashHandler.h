#pragma once
#ifndef HCRASH_HANDLER_H
#define HCRASH_HANDLER_H

#include <windows.h>

void HCrashHandlerInstall();
void HCrashHandlerReinstallFilter();
void HCrashMarkGracefulApplicationExit();
bool HCrashShouldShowPendingReportOnStartup();
void HCrashWatchdogSpawn();
bool HCrashIsWatchdogAlive();
int HCrashWatchdogRun(DWORD parent_process_id);
void HCrashClearReportUiLock();
void HCrashWriteReportUiLockCurrent();

#endif
