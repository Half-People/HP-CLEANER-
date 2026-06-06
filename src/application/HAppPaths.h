#pragma once
#ifndef HAPP_PATHS_H
#define HAPP_PATHS_H

#include <string>

namespace HAppPaths {
	// Root under %APPDATA%: HalfPeople/HP CLEANER++
	std::string GetAppDataRoot();
	std::string GetLogsDir();
	std::string GetConfigDir();
	std::string GetCrashesDir();

	// 建立根目錄與 logs\、config\、crashes\ 子目錄
	bool EnsureAppDataDirs();

	// spdlog daily 基底路徑（傳入 sink 的檔名，實際為 logger_YYYY-MM-DD.log）
	std::string GetDailyLogFilePath();
	std::string GetCurrentDailyLogFilePath();
	// logs\ 下最新 .log（崩潰報告讀日誌用）
	std::string GetLatestLogFilePath();

	// crashes\pending_report.txt — 崩潰後自啟失敗時，下次啟動可讀取
	std::string GetPendingCrashReportPath();
	std::string ReadPendingCrashReport();
	void ClearPendingCrashReport();
}

#endif
