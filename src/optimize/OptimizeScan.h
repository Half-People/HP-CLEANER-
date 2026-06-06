#pragma once

#include <cstdint>
#include <vector>

// 系統優化：診斷掃描、啟動項／服務調整、預設方案（不自動套用清理預設）。
namespace OptimizeScan {

	enum class StartupSource : uint8_t {
		RunKey = 0,
		RunOnce = 1,
		StartupFolder = 2,
	};

	// 啟動速度影響等級（排序／概覽估算用）
	enum class StartupImpactTier : int {
		Unknown = -1,
		None = 0,
		Low = 1,
		Medium = 2,
		High = 3,
	};

	struct StartupEntry {
		char id[64] = {};
		char name_utf8[128] = {};
		char command_utf8[512] = {};
		char exe_path_utf8[512] = {};
		char product_utf8[128] = {};
		char publisher_utf8[128] = {};
		char file_description_utf8[256] = {};
		char how_to_utf8[160] = {};
		char impact_utf8[256] = {};
		char boot_impact_utf8[128] = {};
		int impact_tier = static_cast<int>(StartupImpactTier::Unknown);
		char icon_path_utf8[512] = {};
		char source_label[48] = {};
		wchar_t restore_key[512] = {};
		wchar_t restore_value_name[256] = {};
		wchar_t restore_command[512] = {};
		bool enabled = true;
		bool can_toggle = false;
		StartupSource source = StartupSource::RunKey;
	};

	struct ServiceEntry {
		char service_name[64] = {};
		char display_name[128] = {};
		char binary_path_utf8[512] = {};
		char description_utf8[256] = {};
		char publisher_utf8[128] = {};
		char boot_impact_utf8[128] = {};
		char how_to_utf8[160] = {};
		char role_utf8[320] = {};
		char disable_effect_utf8[320] = {};
		char risk_note_utf8[160] = {};
		bool exists = false;
		bool running = false;
		uint32_t start_type = 0;
		bool recommended_disable = false;
	};

	void FillServiceKnowledge(ServiceEntry& entry);

	struct Snapshot {
		bool valid = false;
		bool scanning = false;
		float progress = 0.f;
		char status_text[128] = {};
		int pending_suggestions = 0;

		char power_plan_name[96] = {};
		char power_plan_guid[48] = {};
		bool game_mode_on = false;
		int visual_fx_setting = -1;
		bool transparency_on = false;
		bool fast_startup_on = false;
		bool game_dvr_on = false;
		bool animations_on = true;
		bool processor_foreground = false;
		bool gpu_scheduling_on = false;
		bool hibernate_on = false;
		bool mouse_accel_on = true;
		bool fullscreen_opt_on = true;
		bool ultimate_plan_available = false;
		bool tips_suggestions_on = true;
		bool background_apps_on = true;
		bool power_throttling_on = true;
		bool game_bar_on = true;
		bool delivery_p2p_on = true;
		bool search_highlights_on = true;
		bool widgets_on = true;
		bool network_throttling_on = true;
		bool game_responsiveness_on = false;
		bool fast_menu_delay = false;

		char suggested_clean_preset[32] = {};
		int64_t suggested_clean_bytes = 0;
		char suggested_clean_label[48] = {};

		float system_drive_used_percent = -1.f;
		int64_t system_drive_free_bytes = -1;
		int64_t system_drive_total_bytes = -1;

		std::vector<StartupEntry> startups;
		std::vector<ServiceEntry> services;
	};

	struct PresetInfo {
		const char* id = "";
		const char* label = "";
		const char* description = "";
	};

	struct DiskOptimizationSnapshot {
		bool valid = false;
		bool running = false;
		float progress = 0.f;
		char drive_letter = 'C';
		bool is_ssd = false;
		int fragmentation_percent = -1;
		bool needs_optimization = false;
		bool last_run_was_optimize = false;
		unsigned last_optimize_elapsed_sec = 0;
		char media_label[16] = {};
		char status_text[128] = {};
		char detail_text[160] = {};
	};

	struct StorageLocalSettings {
		bool valid = false;
		bool storage_sense_on = false;
		bool auto_temp_cleanup = false;
		bool low_disk_auto_run = false;
		int recycle_bin_days = 0;
	};

	enum class StorageQuickCleanFlags : uint32_t {
		TempFiles = 1u,
		RecycleBin = 2u,
		DeliveryCache = 4u,
	};

	struct StorageDriveInfo {
		char letter = 'C';
		char label[48] = {};
		uint32_t drive_type = 0;
		int64_t free_bytes = -1;
		int64_t total_bytes = -1;
	};

	struct StorageWorkSnapshot {
		bool running = false;
		float progress = 0.f;
		char job_name[48] = {};
		char status_text[128] = {};
		int log_count = 0;
		static constexpr int kMaxLogLines = 48;
		static constexpr int kLogLineChars = 96;
		char log_lines[kMaxLogLines][kLogLineChars] = {};
		int64_t last_result_bytes = 0;
	};

	void Init();
	void Shutdown();
	void RequestScan();
	Snapshot GetSnapshot();
	bool IsScanning();

	size_t GetPresetCount();
	const PresetInfo* GetPreset(size_t index);
	const char* GetPresetDescription(const char* preset_id);

	bool ApplyPreset(const char* preset_id);
	bool ApplyPresetWithOptions(const char* preset_id, bool create_restore_point);
	bool ApplyRecommendedServices();
	bool RevertLastApply();
	bool HasLastApplyRevert();
	void GetLastApplySummary(char* buf, size_t buf_size);

	const char* StartupImpactLabel(int impact_tier);
	int EstimateBootSavingsSeconds(const Snapshot& snap);
	int CountRecommendedServicesPending(const Snapshot& snap);
	int CountHighImpactEnabledStartups(const Snapshot& snap);

	bool SetStartupEnabled(const char* entry_id, bool enabled);
	bool SetServiceDisabled(const char* service_name, bool disabled);

	bool FlushDnsCache();
	bool RegisterDnsCache();
	bool RenewIpAddresses();
	bool ResetWinsockCatalog();
	bool CreateSystemRestorePoint();
	bool SetPowerPlanByKind(const char* kind);
	bool SetGameModeEnabled(bool on);
	bool SetVisualEffects(int setting);
	bool SetTransparencyEffects(bool enabled);
	bool SetProcessorSchedulingPrograms(bool foreground);
	bool SetFastStartup(bool enabled);
	bool SetGameDvrEnabled(bool enabled);
	bool SetAnimationsEnabled(bool enabled);
	bool SetHardwareGpuScheduling(bool enabled);
	bool SetHibernateEnabled(bool enabled);
	bool SetMouseAccelerationEnabled(bool enabled);
	bool SetFullscreenOptimizations(bool enabled);
	bool ApplyQuickGamingTune();
	bool ApplyQuickOfficeTune();
	bool ApplyQuickBatteryTune();
	bool ApplyQuickResponsiveTune();
	bool SetTipsAndSuggestionsEnabled(bool enabled);
	bool SetBackgroundAppsEnabled(bool enabled);
	bool SetPowerThrottlingEnabled(bool enabled);
	bool SetGameBarEnabled(bool enabled);
	bool SetDeliveryOptimizationP2P(bool enabled);
	bool SetSearchHighlightsEnabled(bool enabled);
	bool SetWidgetsEnabled(bool enabled);
	bool SetNetworkThrottlingEnabled(bool enabled);
	bool SetGameSystemResponsiveness(bool game_priority);
	bool SetFastMenuDelay(bool fast);
	bool OpenWindowsPowerSettings();
	bool OpenWindowsGameSettings();
	bool EnsureUltimatePowerPlan();
	void RefreshSystemSettings();
	bool OpenTaskManagerStartup();
	bool OpenServicesConsole();
	int GetStorageDriveCount();
	bool GetStorageDrive(int index, StorageDriveInfo& out);
	void SetStorageMaintenanceDrive(char letter);
	char GetStorageMaintenanceDrive();
	void RequestDiskOptimizationAnalyze();
	void RequestDiskOptimizationRun();
	DiskOptimizationSnapshot GetDiskOptimization();
	bool IsDiskOptimizationRunning();
	StorageLocalSettings GetStorageLocalSettings();
	bool SetStorageSenseEnabled(bool enabled);
	bool SetStorageAutoTempCleanup(bool enabled);
	bool SetStorageLowDiskAutoRun(bool enabled);
	bool SetStorageRecycleBinDays(int days);
	void RequestStorageQuickClean(uint32_t flags);
	void RequestStorageCleanScan();
	void RequestStorageRestorePoint();
	void TickStorageWork();
	StorageWorkSnapshot GetStorageWorkSnapshot();
	void ClearStorageWorkLog();

	const char* GetLastActionMessage();
	const char* GetActivePowerPlanKind();
}
