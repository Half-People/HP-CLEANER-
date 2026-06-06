#pragma once

#include "OptimizeScan.h"

// 啟動項／服務圖示與檔案資訊（D3D 圖示僅在主執行緒建立）
namespace OptimizeStartupIcon {

	void Init();
	void Shutdown();

	void EnrichStartupEntry(OptimizeScan::StartupEntry& entry);
	void EnrichServiceEntry(OptimizeScan::ServiceEntry& entry);

	unsigned long long GetAppFallbackIconTextureId();
	unsigned long long GetGenericExeIconTextureId();
	unsigned long long GetIconTextureId(const char* path_utf8);
	unsigned long long GetStartupIconTextureId(const OptimizeScan::StartupEntry& entry);
	unsigned long long GetServiceIconTextureId(const OptimizeScan::ServiceEntry& entry);

	void FormatStartupTooltip(const OptimizeScan::StartupEntry& entry, char* buf, size_t buf_size);
	void FormatServiceTooltip(const OptimizeScan::ServiceEntry& entry, char* buf, size_t buf_size);

	void OpenExecutableFolder(const char* exe_path_utf8);
	void OpenExecutableProperties(const char* exe_path_utf8);
}
