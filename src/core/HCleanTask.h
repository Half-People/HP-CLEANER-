#pragma once
#ifndef HCLEAN_TASK_H
#define HCLEAN_TASK_H

#include <cstddef>
#include <cstdint>

// 清理任務掃描結果（位元組；-1 表示尚未掃描或未知）
struct HCleanSizeInfo {
	int64_t bytes = -1;
	bool valid = false;
};

enum class HCleanScanState {
	Idle,
	Scanning,
	Done,
	Failed,
};

struct HCleanScanProgress {
	HCleanScanState state = HCleanScanState::Idle;
	float percent = 0.f;           // 0-100
	const char* status_text = "";  // 目前步驟說明
};

struct HCleanDetailEntry {
	const char* path = nullptr;  // 資料夾路徑（開啟 + 清理範圍）
	const char* label = nullptr;
	const char* usage = nullptr;   // 該路徑用途
	const char* impact = nullptr;  // 清理後可能影響
	int64_t bytes = 0;
	bool selected = true;
	bool destructive = false;  // 清理可能永久刪除資料或中斷進行中工作
};

struct HCleanCategoryScanInfo {
	int scanning_count = 0;   // 仍在掃描中的任務數
	int completed_count = 0;  // 本輪已完成掃描的任務數
	int task_count = 0;
	float aggregate_percent = 0.f;  // (已完成*100 + 進行中 percent 合計) / 任務總數
	bool any_scanning = false;
};

// 全系統清理掃描進度（底部進度條用）
struct HCleanGlobalScanInfo {
	bool any_scanning = false;
	float aggregate_percent = 0.f;
	int task_count = 0;
	int completed_count = 0;
	int scanning_count = 0;
	char current_task_name[128] = {};
	char status_text[192] = {};
};

enum class HCleanSessionPhase {
	Idle,
	Cleaning,
	Done,
};

struct HCleanDeleteStats {
	int64_t bytes_freed = 0;
	int files_deleted = 0;
	int skip_locked = 0;
	int skip_access_denied = 0;
	int skip_timeout = 0;
	int skip_reparse = 0;
	int dirs_removed = 0;
};

struct HCleanWalkLimits {
	int max_depth = 48;
	int max_files = 50000;
	double timeout_sec = 45.0;
};

struct HCleanSizeSummary {
	int64_t selected_bytes = 0;
	int64_t visible_total_bytes = 0;
	int selected_count = 0;
	int visible_count = 0;
	int visible_scanned_count = 0;
	bool selected_has_unscanned = false;
};

struct HCleanSessionInfo {
	HCleanSessionPhase phase = HCleanSessionPhase::Idle;
	float progress_percent = 0.f;
	int64_t disk_free_bytes = -1;
	int64_t disk_free_before_clean = -1;
	int64_t selected_bytes = 0;
	int64_t selected_bytes_at_clean_start = 0;
	int64_t visible_total_bytes = 0;
	int64_t freed_bytes_session = 0;
	int tasks_completed = 0;
	int tasks_total = 0;
	int details_completed = 0;
	int details_total = 0;
	float detail_progress_percent = 0.f;
	const char* status_text = "";
	const char* current_task_name = "";
	const char* current_detail_path = "";
	const char* last_log_line = "";
	int session_skip_locked = 0;
	int session_skip_access_denied = 0;
	int session_skip_timeout = 0;
	int session_skip_reparse = 0;
};

// 單一清理項：在獨立 .cpp 中繼承並實作，再以 REG_CLEAN_TASK 註冊
class HCleanTask {
public:
	virtual ~HCleanTask() = default;

	virtual const char* GetId() const = 0;
	virtual const char* GetName() const = 0;
	virtual const char* GetPurpose() const = 0;
	virtual const char* GetTooltip() const = 0;

	virtual void RefreshSize() = 0;
	virtual HCleanSizeInfo GetSize() const = 0;
	virtual bool Clean() = 0;
	virtual int64_t GetLastFreedBytes() const { return 0; }

	virtual HCleanScanProgress GetScanProgress() const { return {}; }
	virtual void RenderDetailGui() {}
	virtual bool IsEnabledByDefault() const { return true; }

	virtual size_t GetDetailEntryCount() const { return 0; }
	virtual HCleanDetailEntry* GetDetailEntry(size_t index) { (void)index; return nullptr; }
	virtual void ApplyDetailSelection() {}

	// 卡片 tooltip 用：已選明細摘要（預設依 path/label 組字）
	virtual void GetDetailSelectionSummary(char* buf, size_t buf_size) const;
	virtual void OnDetailConfigLoaded();

	// 掃描完成後若無可清理路徑/大小為 0 則回傳 false；掃描中或未掃描仍顯示
	virtual bool ShouldShowInUI() const { return true; }

	// 任務卡片破壞性標籤：任務已勾選且至少一個已勾選明細為 destructive
	virtual bool IsDestructiveTask() const { return false; }

	// 詳細子項是否預設勾選（未覆寫時跟隨 IsEnabledByDefault）
	virtual bool IsDetailEntryEnabledByDefault(size_t index) const
	{
		(void)index;
		return IsEnabledByDefault();
	}

	bool IsSelected() const { return selected_; }
	void SetSelected(bool value) { selected_ = value; }

	const char* GetCategoryId() const { return category_id_; }
	int GetOrder() const { return order_; }

	friend void RegisterCleanTask_internal(HCleanTask* task, const char* category_id, int order);

protected:
	bool selected_ = false;
	const char* category_id_ = nullptr;
	int order_ = 0;
};

struct HCleanCategoryInfo {
	const char* id = nullptr;
	const char* display_name = nullptr;
	int order = 0;
};

void FormatCleanSize(int64_t bytes, char* out_buf, size_t out_buf_size);

void RegisterCleanCategory_internal(const char* category_id, const char* display_name, int order);
void RegisterCleanTask_internal(HCleanTask* task, const char* category_id, int order);

#define REG_CLEAN_CATEGORY(category_id, display_name, order) \
	static bool REG_CLEAN_CATEGORY_##category_id = []() { \
		RegisterCleanCategory_internal(#category_id, display_name, order); \
		return true; \
	}();

#define REG_CLEAN_TASK(task_class, category_id, order) \
	static task_class task_class##_instance; \
	static bool task_class##_registered = []() { \
		RegisterCleanTask_internal(&task_class##_instance, #category_id, order); \
		return true; \
	}();

size_t GetCleanCategoryCount();
const HCleanCategoryInfo* GetCleanCategory(size_t index);

size_t GetCleanTasksInCategory(const char* category_id, HCleanTask** out_tasks, size_t max_tasks);

bool CategoryHasVisibleCleanTasks(const char* category_id);

HCleanTask* FindCleanTask(const char* task_id);

void RefreshCleanCategorySizes(const char* category_id);
void RefreshAllCleanTaskSizes();

void ScanCategory(const char* category_id);
void ScanAllCleanTasks();
void BeginDeferredScanAllCleanTasks();
void TickDeferredScanAllCleanTasks(size_t batch_size);
bool IsDeferredScanAllCleanTasksActive();
bool IsAnyCleanTaskScanning();
bool GetCleanCategoryScanInfo(const char* category_id, HCleanCategoryScanInfo* out_info);
bool GetGlobalCleanScanInfo(HCleanGlobalScanInfo* out_info);

void SetCleanCategorySelected(const char* category_id, bool selected);
bool IsCleanCategoryAllSelected(const char* category_id);
bool IsCleanCategoryPartiallySelected(const char* category_id);

int64_t GetCleanCategoryTotalBytes(const char* category_id);
int64_t GetSelectedCleanTasksTotalBytes();
void GetCleanTasksSizeSummary(HCleanSizeSummary* out_summary);

size_t CleanSelectedTasks(size_t* out_succeeded = nullptr, size_t* out_failed = nullptr);

void RequestCleanSelectedTasks();
bool HCleanPrepareCleanSelectedTasks();
void TickCleanWorker();
void TickScanWorker();
bool GetCleanSessionInfo(HCleanSessionInfo* out_info);

bool HCleanIsRunningAsAdmin();
bool HCleanHasElevatedAccess();
bool HCleanRequestAdminElevation();
bool HCleanShowAdminElevationPrompt();

void HCleanReportCleanProgress(const char* task_name, const char* detail_path,
	size_t detail_index, size_t detail_total, const HCleanDeleteStats* stats);
void ResetCleanSessionAfterScan();
int64_t QuerySystemDriveFreeBytes();

void SetAllCleanTasksSelected(bool selected);
void ApplyCleanPreset(const char* preset_id);
int64_t EstimateCleanPresetBytes(const char* preset_id);

#endif
