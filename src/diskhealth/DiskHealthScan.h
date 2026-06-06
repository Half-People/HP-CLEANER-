#pragma once

#include <cstdint>
#include <vector>

// 硬碟健康掃描（參考 CrystalDiskInfo：型號、SMART、溫度、通電時數、健康狀態）
namespace DiskHealthScan {

	enum class HealthLevel : uint8_t {
		Unknown = 0,
		Good = 1,
		Caution = 2,
		Bad = 3,
		Unavailable = 4,
	};

	struct SmartAttribute {
		uint8_t id = 0;
		char name_utf8[64] = {};
		uint8_t current = 0;
		uint8_t worst = 0;
		uint64_t raw = 0;
		uint32_t threshold = 0;
		bool prefailure = false;
	};

	struct DriveInfo {
		int physical_index = -1;
		wchar_t device_path[64] = {};
		char model_utf8[128] = {};
		char serial_utf8[64] = {};
		char firmware_utf8[32] = {};
		char bus_type_utf8[32] = {};
		char volume_letters[32] = {}; // e.g. "C:, D:"
		uint64_t size_bytes = 0;
		HealthLevel health = HealthLevel::Unknown;
		char health_text[32] = {};
		int temperature_c = -1;
		int power_on_hours = -1;
		int reallocated_sectors = -1;
		int pending_sectors = -1;
		int uncorrectable_errors = -1;
		bool smart_supported = false;
		bool smart_available = false;
		// USB 或 winerr 1/50：不再每 3 秒重試 ATA SMART（避免日誌刷屏）
		bool skip_ata_smart = false;
		char status_note[256] = {};
		std::vector<SmartAttribute> smart_attributes;
	};

	struct Snapshot {
		std::vector<DriveInfo> drives;
		bool scanning = false;
		bool live_refreshing = false;
		float progress = 0.f;
		char status_text[128] = {};
		char last_scan_time[64] = {};
		char last_live_update_time[64] = {};
		bool needs_admin_hint = false;
		int live_refresh_interval_sec = 3;
	};

	// 進入硬盤健康頁時啟用；離開頁 release 時關閉
	void SetLiveRefreshEnabled(bool enabled);
	bool IsLiveRefreshEnabled();
	int GetLiveRefreshIntervalSec();

	void Init();
	void Shutdown();
	void RequestRescan();
	Snapshot GetSnapshot();
	const char* HealthLevelLabel(HealthLevel level);

	bool IsRunningAsAdmin();
	// 非管理員時詢問是否提升；若使用者選「是」會重啟程式並回傳 false
	bool PromptAdminElevationIfNeeded();
	void BuildDriveReport(const DriveInfo& drive, char* out, size_t out_size);
	// 將 SMART 屬性同步到溫度、重配置、待處理等摘要欄（供 UI 頂部卡片顯示）
	void EnrichDriveSummary(DriveInfo& drive);
	// 從摘要欄 + smart_attributes 解析扇區 KPI（與 EnrichDriveSummary 邏輯一致）
	void GetSectorCounters(const DriveInfo& drive, int& reallocated, int& pending,
		int& uncorrectable);

}
