#pragma once

#include "HCleanTask.h"
#include <windows.h>
#include <cstddef>
#include <cstdint>

struct HCleanDemoScanSimulator {
	mutable HCleanScanState state = HCleanScanState::Idle;
	mutable float percent = 0.f;
	mutable const char* status_text = "";
	double start_time = 0.0;
	int64_t target_bytes = 0;
	static constexpr double kDurationSec = 1.4;

	void Begin(int64_t target, const char* status);
	HCleanScanProgress Poll(int64_t& cached_bytes) const;
};

void HCleanSumSelectedDetailBytes(const HCleanDetailEntry* entries, size_t count, int64_t& out_bytes);

bool HCleanExpandPathUtf8(const char* path_template, char* out, size_t out_size);
bool HCleanExpandPathWide(const char* path_template, wchar_t* out, size_t out_chars);

extern const HCleanWalkLimits kHCleanDefaultWalkLimits;
extern const HCleanWalkLimits kHCleanLargeWalkLimits;

int64_t HCleanMeasureDirShallow(const wchar_t* path, const HCleanWalkLimits* limits = nullptr);
int64_t HCleanMeasureDirTopLevelFiles(const wchar_t* path);

int64_t HCleanSafeDeleteFilesShallow(const wchar_t* dir_path, const char* log_context,
	HCleanDeleteStats* stats = nullptr);
int64_t HCleanSafeDeleteFilesDirShallow(const wchar_t* dir_path, const char* log_context,
	HCleanDeleteStats* stats = nullptr, const HCleanWalkLimits* limits = nullptr);

bool HCleanFindSteamInstallPathUtf8(char* out, size_t out_size);
size_t HCleanParseSteamLibraryPaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

bool HCleanFindFirefoxProfileCacheUtf8(char* out, size_t out_size);
size_t HCleanEnumerateFirefoxProfileCaches(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

size_t HCleanEnumerateChromiumProfileCaches(const char* user_data_template,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

size_t HCleanEnumerateRecycleBinPaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

size_t HCleanEnumerateSubdirsWithCache(const char* root_template, const char* const* subdir_names,
	size_t subdir_count, char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count,
	const char* label_prefix = nullptr);

size_t HCleanEnumerateUnrealEngineCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateAdobeProductCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateMinecraftUwpCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateExplorerThumbcachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateExplorerIconcachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateCommonUwpCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateUwpPackagePaths(const char* package_name_glob,
	const wchar_t* const* rel_subpaths, size_t subpath_count, const char* label_prefix,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

size_t HCleanEnumerateElectronAppCaches(const char* root_template, const char* label_prefix,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

size_t HCleanEnumerateAppDataCacheCandidates(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names = nullptr, size_t skip_app_count = 0);

// 發現驅動：收集候選 → 依實測大小排序 → 取前 N（靜態清單之外的動態補充）
size_t HCleanDiscoverRoamingCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names = nullptr, size_t skip_app_count = 0);
size_t HCleanDiscoverLocalAppDataCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names = nullptr, size_t skip_app_count = 0);
size_t HCleanDiscoverProgramDataCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names = nullptr, size_t skip_app_count = 0);
size_t HCleanDiscoverAllUwpCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);
size_t HCleanDiscoverInstallLocationCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);
size_t HCleanDiscoverStaleUserFilesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, int min_age_days = 30);

bool HCleanIsRegistryDetailPath(const char* path);
bool HCleanRunRegistryDetail(const char* path);
bool HCleanRunCliDetail(const char* path);

bool HCleanAppDataCacheSubdirDefaultSelected(const wchar_t* subdir_name);
bool HCleanAppDataCacheSubdirDestructive(const wchar_t* subdir_name);

size_t HCleanEnumerateJetBrainsIdeCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count);

size_t HCleanEnumerateWildcardChildSubdirs(const char* parent_template, const wchar_t* dir_name_glob,
	const char* const* subdir_names, size_t subdir_count, const char* label_prefix,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count);

bool HCleanPathIsDirectory(const wchar_t* path);
bool HCleanPathIsFile(const wchar_t* path);
int64_t HCleanMeasurePathBytes(const wchar_t* path, const HCleanWalkLimits* limits = nullptr,
	const char* log_context = nullptr);
bool HCleanFindVsComponentModelCacheUtf8(char* out, size_t out_size);
bool HCleanEmptyRecycleBin();

bool HCleanIsProcessRunning(const wchar_t* exe_name);
bool HCleanIsProcessRunningUtf8(const char* exe_name);
bool HCleanShowProcessRunningPrompt(const char* app_display_name, const char* const* exe_names, size_t exe_count);

bool HCleanStopWindowsUpdateServices();
bool HCleanStartWindowsUpdateServices();
bool HCleanClearDeliveryOptimizationCache();
bool HCleanRefreshThumbnailIconCache();

bool HCleanRunHiddenCommand(const wchar_t* command_line, DWORD timeout_ms = 120000);
bool HCleanIsCliDetailPath(const char* path);
bool HCleanRunNpmCacheClean();
bool HCleanRunPnpmStorePrune();
bool HCleanRunPnpmCacheClean();
bool HCleanRunNugetHttpCacheClear();
bool HCleanRunNugetGlobalPackagesClear();
bool HCleanRunDockerBuilderPrune();
bool HCleanRunDockerContainerPrune();
bool HCleanRunDockerImagePrune();
bool HCleanRunDockerVolumePrune();
bool HCleanRunDockerSystemPruneAll();

void HCleanRefreshDetailSizes(HCleanDetailEntry* entries, size_t count, int64_t& cached_bytes);

class HCleanDetailListTask;

struct HCleanAsyncScanPathCopy {
	char path[MAX_PATH * 4]{};
	bool selected = true;
};

void HCleanTickAsyncScanWorker();
void HCleanShutdownAsyncScanWorker();
bool HCleanIsAsyncScanWorkerBusy();
bool HCleanIsTaskAsyncRealScanActive(const HCleanDetailListTask* task);
float HCleanGetTaskAsyncRealScanPercent(const HCleanDetailListTask* task);
const char* HCleanGetTaskAsyncRealScanStatus(const HCleanDetailListTask* task);
void HCleanSubmitAsyncScanWork(HCleanDetailListTask* task, const HCleanAsyncScanPathCopy* paths, size_t path_count);

class HCleanDetailListTask : public HCleanTask {
public:
	static constexpr size_t kMaxDetails = 24;

	void RefreshSize() override;
	HCleanScanProgress GetScanProgress() const override;
	HCleanSizeInfo GetSize() const override;
	bool ShouldShowInUI() const override;
	int64_t GetLastFreedBytes() const override { return last_freed_bytes_; }
	size_t GetDetailEntryCount() const override;
	HCleanDetailEntry* GetDetailEntry(size_t index) override;
	void ApplyDetailSelection() override;
	bool IsDestructiveTask() const override;

	bool IsAsyncRealScanPending() const { return real_scan_queued_ && !details_ready_; }
	bool IsScanResultPending() const { return scan_result_pending_; }
	HCleanWalkLimits GetScanWalkLimits() const { return walk_limits_; }
	void MarkScanResultPending() const { scan_result_pending_ = true; }
	void ApplyMeasuredDetails(const int64_t* detail_bytes, size_t count, int64_t cached_bytes);
	void RunPathBuildAndQueueScanOnWorker();

protected:
	void SetScanTarget(int64_t bytes) { scan_target_ = bytes; }
	void SetWalkLimits(const HCleanWalkLimits& limits) { walk_limits_ = limits; }
	const HCleanWalkLimits& GetWalkLimits() const { return walk_limits_; }

	bool CleanSelectedDetails();
	bool CleanSelectedDetailsWithProgress(const char* task_display_name);
	void AddDetail(const char* path_template, const char* label, int64_t fallback_bytes, bool selected,
		const char* usage = nullptr, const char* impact = nullptr, bool destructive = false) const;
	void AddDetailPath(const char* path_utf8, const char* label, int64_t fallback_bytes, bool selected,
		const char* usage = nullptr, const char* impact = nullptr, bool destructive = false) const;
	void AddChromiumProfileCaches(const char* user_data_template, bool default_selected = true) const;
	void AddFirefoxProfileCaches(bool default_selected = true) const;
	void AddSteamLibraryDetails(bool include_downloading = true) const;
	void AddRecycleBinDetails(bool default_selected = true) const;
	void AddDetailIfExists(const char* path_template, const char* label, int64_t fallback_bytes, bool selected,
		const char* usage = nullptr, const char* impact = nullptr, bool destructive = false) const;
	void AddFileDetailIfExists(const char* path_template, const char* label, int64_t fallback_bytes, bool selected,
		const char* usage = nullptr, const char* impact = nullptr, bool destructive = false) const;
	void AddUnrealEngineCacheDetails(bool default_selected = true) const;
	void AddSortedRoamingDiscoveryDetails(size_t max_slots, const char* const* skip_app_names = nullptr,
		size_t skip_app_count = 0) const;
	void AddSortedLocalDiscoveryDetails(size_t max_slots, const char* const* skip_app_names = nullptr,
		size_t skip_app_count = 0) const;
	void AddSortedProgramDataDiscoveryDetails(size_t max_slots, const char* const* skip_app_names = nullptr,
		size_t skip_app_count = 0) const;
	void AddSortedUwpDiscoveryDetails(size_t max_slots) const;
	void AddSortedInstallLocationDiscoveryDetails(size_t max_slots) const;
	void AddSortedStaleFileDiscoveryDetails(size_t max_slots) const;

	void AddDiscoveredPathEntries(char paths[][MAX_PATH * 4], char labels[][128], size_t count,
		const char* usage, const char* impact_prefix) const;

	virtual void BuildDetails() const = 0;

	mutable int64_t cached_bytes_ = -1;
	mutable int64_t last_freed_bytes_ = 0;
	mutable HCleanDemoScanSimulator scan_;
	mutable HCleanDetailEntry details_[kMaxDetails]{};
	mutable char path_bufs_[kMaxDetails][MAX_PATH * 4]{};
	mutable char label_bufs_[kMaxDetails][128]{};
	mutable size_t detail_count_ = 0;
	mutable bool details_ready_ = false;
	mutable bool real_scan_queued_ = false;
	mutable bool path_build_queued_ = false;
	mutable bool scan_result_pending_ = false;
	int64_t scan_target_ = 0;
	HCleanWalkLimits walk_limits_ = kHCleanDefaultWalkLimits;

	void EnsureDetails() const;
	void EnsurePathsOnly() const;
	void RealScanDetails() const;
	void RequestAsyncRealScan() const;
};
