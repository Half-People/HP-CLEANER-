#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "CleanHistory.h"
#include "HPage.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <tlhelp32.h>
#include "Hi18n.h"

int64_t QuerySystemDriveFreeBytes();

namespace {
	struct CategoryBucket {
		HCleanCategoryInfo info;
		std::vector<HCleanTask*> tasks;
	};

	std::unordered_map<std::string, CategoryBucket>* g_categories = nullptr;
	std::unordered_map<std::string, HCleanTask*>* g_tasks_by_id = nullptr;

	CategoryBucket& EnsureCategory(const char* category_id)
	{
		auto it = g_categories->find(category_id);
		if (it == g_categories->end()) {
			HLOG_WARN("Clean category '{}' not registered, creating placeholder.", category_id);
			CategoryBucket bucket;
			bucket.info.id = category_id;
			bucket.info.display_name = category_id;
			bucket.info.order = 9999;
			it = g_categories->emplace(category_id, std::move(bucket)).first;
		}
		return it->second;
	}

	void SortCategories(std::vector<CategoryBucket*>& out_sorted)
	{
		out_sorted.clear();
		out_sorted.reserve(g_categories->size());
		for (auto& pair : *g_categories) {
			out_sorted.push_back(&pair.second);
		}
		std::sort(out_sorted.begin(), out_sorted.end(),
			[](const CategoryBucket* a, const CategoryBucket* b) {
				if (a->info.order != b->info.order) {
					return a->info.order < b->info.order;
				}
				return std::strcmp(a->info.id, b->info.id) < 0;
			});
	}

	void SortTasks(std::vector<HCleanTask*>& tasks)
	{
		std::sort(tasks.begin(), tasks.end(),
			[](const HCleanTask* a, const HCleanTask* b) {
				if (a->GetOrder() != b->GetOrder()) {
					return a->GetOrder() < b->GetOrder();
				}
				return std::strcmp(a->GetId(), b->GetId()) < 0;
			});
	}

	struct CleanWorkerState {
		HCleanSessionPhase phase = HCleanSessionPhase::Idle;
		std::vector<HCleanTask*> queue;
		size_t next_index = 0;
		size_t succeeded = 0;
		size_t failed = 0;
		int64_t disk_free_before = -1;
		int64_t disk_free_now = -1;
		int64_t selected_at_start = 0;
		int64_t freed_total = 0;
		float progress = 0.f;
		const char* status_text = "";
		const char* current_task_name = "";
		const char* current_detail_path = "";
		int details_completed = 0;
		int details_total = 0;
		float detail_progress_percent = 0.f;
		char last_log_line[256]{};
		int session_skip_locked = 0;
		int session_skip_access_denied = 0;
		int session_skip_timeout = 0;
		int session_skip_reparse = 0;
		bool history_recorded = false;
	};

	CleanWorkerState g_clean_worker;

	void PersistCleanHistoryFromWorker()
	{
		if (g_clean_worker.history_recorded) {
			return;
		}
		g_clean_worker.history_recorded = true;

		const int64_t freed = g_clean_worker.freed_total;
		const int64_t selected_start = g_clean_worker.selected_at_start;
		const int64_t free_before = g_clean_worker.disk_free_before;
		const int64_t free_after = g_clean_worker.disk_free_now;
		const int succeeded = static_cast<int>(g_clean_worker.succeeded);
		const int failed = static_cast<int>(g_clean_worker.failed);
		const int total = static_cast<int>(g_clean_worker.queue.size());
		const int skip_locked = g_clean_worker.session_skip_locked;
		const int skip_denied = g_clean_worker.session_skip_access_denied;
		const int skip_timeout = g_clean_worker.session_skip_timeout;
		const int skip_reparse = g_clean_worker.session_skip_reparse;

		std::vector<std::string> task_ids;
		task_ids.reserve(g_clean_worker.queue.size());
		for (HCleanTask* task : g_clean_worker.queue) {
			if (task != nullptr && task->GetId() != nullptr) {
				task_ids.emplace_back(task->GetId());
			}
		}

		CleanHistory::RecordSession(
			freed, selected_start, free_before, free_after,
			succeeded, failed, total,
			skip_locked, skip_denied, skip_timeout, skip_reparse,
			task_ids);
	}
	std::mutex g_clean_worker_mutex;
	std::condition_variable g_clean_worker_cv;
	std::thread g_clean_thread;
	std::atomic<bool> g_clean_thread_started{ false };
	std::atomic<bool> g_clean_stop{ false };
	std::atomic<bool> g_clean_run_requested{ false };

	void EnsureCleanWorkerThread()
	{
		if (g_clean_thread_started.load(std::memory_order_acquire)) {
			return;
		}
		bool expected = false;
		if (!g_clean_thread_started.compare_exchange_strong(expected, true)) {
			return;
		}
		g_clean_thread = std::thread([] {
			while (!g_clean_stop.load(std::memory_order_acquire)) {
				{
					std::unique_lock<std::mutex> lock(g_clean_worker_mutex);
					g_clean_worker_cv.wait(lock, [] {
						return g_clean_stop.load(std::memory_order_acquire)
							|| g_clean_run_requested.load(std::memory_order_acquire);
					});
					if (g_clean_stop.load(std::memory_order_acquire)) {
						break;
					}
					g_clean_run_requested.store(false, std::memory_order_release);
				}

				while (true) {
					HCleanTask* task = nullptr;
					{
						std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
						if (g_clean_worker.phase != HCleanSessionPhase::Cleaning) {
							break;
						}
						if (g_clean_worker.next_index >= g_clean_worker.queue.size()) {
							g_clean_worker.disk_free_now = QuerySystemDriveFreeBytes();
							g_clean_worker.phase = HCleanSessionPhase::Done;
							g_clean_worker.progress = 100.f;
							g_clean_worker.status_text = I18N(u8"清理完成");
							PersistCleanHistoryFromWorker();
							HLOG_INFO("Clean worker finished: {} succeeded, {} failed, freed {} bytes",
								g_clean_worker.succeeded, g_clean_worker.failed, g_clean_worker.freed_total);
							break;
						}

						task = g_clean_worker.queue[g_clean_worker.next_index];
						g_clean_worker.current_task_name = task->GetName();
						g_clean_worker.status_text = task->GetName();
					}

					const bool ok = task->Clean();
					{
						std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
						if (ok) {
							++g_clean_worker.succeeded;
						}
						else {
							++g_clean_worker.failed;
							HLOG_WARN("Clean task '{}' failed", task->GetId());
						}
						g_clean_worker.freed_total += task->GetLastFreedBytes();

						++g_clean_worker.next_index;
						const size_t total = g_clean_worker.queue.size();
						g_clean_worker.progress = total > 0
							? static_cast<float>(g_clean_worker.next_index) / static_cast<float>(total) * 100.f
							: 100.f;
						g_clean_worker.disk_free_now = QuerySystemDriveFreeBytes();

						if (g_clean_worker.next_index >= total) {
							g_clean_worker.phase = HCleanSessionPhase::Done;
							g_clean_worker.progress = 100.f;
							g_clean_worker.status_text = I18N(u8"清理完成");
							PersistCleanHistoryFromWorker();
							HLOG_INFO("Clean worker finished: {} succeeded, {} failed, freed {} bytes",
								g_clean_worker.succeeded, g_clean_worker.failed, g_clean_worker.freed_total);
						}
					}
				}

				g_clean_worker_cv.notify_all();
			}
		});
	}

	struct ProcessCheckEntry {
		const char* task_id;
		const char* app_name;
		const char* const* exe_names;
		size_t exe_count;
	};

	bool TaskNeedsProcessCheck(const char* task_id, const ProcessCheckEntry** out_entry)
	{
		static const char* k_chrome[] = { "chrome.exe" };
		static const char* k_edge[] = { "msedge.exe" };
		static const char* k_brave[] = { "brave.exe" };
		static const char* k_firefox[] = { "firefox.exe" };
		static const char* k_discord[] = { "Discord.exe", "discord.exe" };
		static const char* k_steam[] = { "steam.exe" };
		static const char* k_riot[] = { "RiotClientServices.exe", "LeagueClient.exe" };

		static const ProcessCheckEntry k_entries[] = {
			{ "browser_chrome_cache", "Google Chrome", k_chrome, 1 },
			{ "browser_edge_cache", "Microsoft Edge", k_edge, 1 },
			{ "browser_brave_cache", "Brave", k_brave, 1 },
			{ "browser_firefox_cache", "Firefox", k_firefox, 1 },
			{ "browser_discord_cache", "Discord", k_discord, 2 },
			{ "game_steam_shader", "Steam", k_steam, 1 },
			{ "game_riot_cache", "Riot Client", k_riot, 2 },
		};

		for (const ProcessCheckEntry& entry : k_entries) {
			if (std::strcmp(entry.task_id, task_id) == 0) {
				if (out_entry != nullptr) {
					*out_entry = &entry;
				}
				return true;
			}
		}
		return false;
	}

	void CollectSelectedCleanTasks(std::vector<HCleanTask*>& out_tasks)
	{
		out_tasks.clear();
		if (g_tasks_by_id == nullptr) {
			return;
		}
		for (const auto& pair : *g_tasks_by_id) {
			HCleanTask* task = pair.second;
			if (task != nullptr && task->IsSelected() && task->ShouldShowInUI()) {
				out_tasks.push_back(task);
			}
		}
		std::sort(out_tasks.begin(), out_tasks.end(),
			[](const HCleanTask* a, const HCleanTask* b) {
				if (a->GetOrder() != b->GetOrder()) {
					return a->GetOrder() < b->GetOrder();
				}
				return std::strcmp(a->GetId(), b->GetId()) < 0;
			});
	}
}

void FormatCleanSize(int64_t bytes, char* out_buf, size_t out_buf_size)
{
	if (out_buf == nullptr || out_buf_size == 0) {
		return;
	}
	if (bytes < 0) {
		snprintf(out_buf, out_buf_size, "—");
		return;
	}
	if (bytes == 0) {
		snprintf(out_buf, out_buf_size, "0 B");
		return;
	}
	const double kb = 1024.0;
	const double mb = kb * 1024.0;
	const double gb = mb * 1024.0;
	const double b = static_cast<double>(bytes);
	if (b >= gb) {
		snprintf(out_buf, out_buf_size, "%.1f GB", b / gb);
	}
	else if (b >= mb) {
		snprintf(out_buf, out_buf_size, "%.1f MB", b / mb);
	}
	else if (b >= kb) {
		snprintf(out_buf, out_buf_size, "%.1f KB", b / kb);
	}
	else {
		snprintf(out_buf, out_buf_size, "%lld B", static_cast<long long>(bytes));
	}
}

void RegisterCleanCategory_internal(const char* category_id, const char* display_name, int order)
{
	if (g_categories == nullptr) {
		g_categories = new std::unordered_map<std::string, CategoryBucket>();
	}
	CategoryBucket& bucket = (*g_categories)[category_id];
	bucket.info.id = category_id;
	bucket.info.display_name = display_name;
	bucket.info.order = order;
	HLOG_INFO("Registered clean category '{}' ({})", category_id, display_name);
}

void RegisterCleanTask_internal(HCleanTask* task, const char* category_id, int order)
{
	if (task == nullptr) {
		HLOG_ERROR("RegisterCleanTask_internal: null task");
		return;
	}
	if (g_categories == nullptr) {
		g_categories = new std::unordered_map<std::string, CategoryBucket>();
	}
	if (g_tasks_by_id == nullptr) {
		g_tasks_by_id = new std::unordered_map<std::string, HCleanTask*>();
	}

	const char* task_id = task->GetId();
	if (g_tasks_by_id->find(task_id) != g_tasks_by_id->end()) {
		HLOG_WARN("Clean task '{}' already registered. Overwriting.", task_id);
	}

	task->category_id_ = category_id;
	task->order_ = order;

	CategoryBucket& bucket = EnsureCategory(category_id);
	bucket.tasks.push_back(task);
	SortTasks(bucket.tasks);
	(*g_tasks_by_id)[task_id] = task;

	if (task->IsEnabledByDefault()) {
		task->SetSelected(true);
	}

	HLOG_INFO("Registered clean task '{}' in category '{}'", task_id, category_id);
}

size_t GetCleanCategoryCount()
{
	return g_categories != nullptr ? g_categories->size() : 0;
}

const HCleanCategoryInfo* GetCleanCategory(size_t index)
{
	if (g_categories == nullptr || g_categories->empty()) {
		return nullptr;
	}
	std::vector<CategoryBucket*> sorted;
	SortCategories(sorted);
	if (index >= sorted.size()) {
		return nullptr;
	}
	return &sorted[index]->info;
}

size_t GetCleanTasksInCategory(const char* category_id, HCleanTask** out_tasks, size_t max_tasks)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return 0;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end()) {
		return 0;
	}
	const std::vector<HCleanTask*>& tasks = it->second.tasks;
	const size_t count = tasks.size();
	if (out_tasks != nullptr && max_tasks > 0) {
		const size_t n = (std::min)(count, max_tasks);
		for (size_t i = 0; i < n; ++i) {
			out_tasks[i] = tasks[i];
		}
	}
	return count;
}

bool CategoryHasVisibleCleanTasks(const char* category_id)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return false;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end()) {
		return false;
	}
	for (HCleanTask* task : it->second.tasks) {
		if (task != nullptr && task->ShouldShowInUI()) {
			return true;
		}
	}
	return false;
}

HCleanTask* FindCleanTask(const char* task_id)
{
	if (g_tasks_by_id == nullptr || task_id == nullptr) {
		return nullptr;
	}
	auto it = g_tasks_by_id->find(task_id);
	if (it == g_tasks_by_id->end()) {
		HLOG_WARN("Clean task '{}' not found", task_id);
		return nullptr;
	}
	return it->second;
}

void RefreshCleanCategorySizes(const char* category_id)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end()) {
		return;
	}
	for (HCleanTask* task : it->second.tasks) {
		if (task != nullptr) {
			task->RefreshSize();
		}
	}
}

void RefreshAllCleanTaskSizes()
{
	if (g_categories == nullptr) {
		return;
	}
	for (auto& pair : *g_categories) {
		for (HCleanTask* task : pair.second.tasks) {
			if (task != nullptr) {
				task->RefreshSize();
			}
		}
	}
}

void ScanCategory(const char* category_id)
{
	if (category_id == nullptr) {
		HLOG_WARN("ScanCategory: null category_id");
		return;
	}
	if (IsAnyCleanTaskScanning()) {
		HLOG_INFO("ScanCategory skipped: scan already in progress");
		return;
	}
	if (g_categories == nullptr || g_categories->find(category_id) == g_categories->end()) {
		HLOG_WARN("ScanCategory: category '{}' not found", category_id);
		return;
	}
	HLOG_INFO("Scan started for category '{}'", category_id);
	ResetCleanSessionAfterScan();
	RefreshCleanCategorySizes(category_id);
}

void ScanAllCleanTasks()
{
	if (IsAnyCleanTaskScanning()) {
		HLOG_INFO("Scan all skipped: scan already in progress");
		return;
	}
	HLOG_INFO("Scan all clean tasks started");
	ResetCleanSessionAfterScan();
	RefreshAllCleanTaskSizes();
}

namespace {
	std::vector<HCleanTask*> g_deferred_scan_tasks;
	size_t g_deferred_scan_index = 0;
	bool g_deferred_scan_active = false;
}

void BeginDeferredScanAllCleanTasks()
{
	if (g_deferred_scan_active || IsAnyCleanTaskScanning()) {
		return;
	}
	if (g_tasks_by_id == nullptr) {
		return;
	}
	HLOG_INFO("Deferred scan all: scheduling tasks");
	ResetCleanSessionAfterScan();
	g_deferred_scan_tasks.clear();
	g_deferred_scan_tasks.reserve(g_tasks_by_id->size());
	for (const auto& pair : *g_tasks_by_id) {
		if (pair.second != nullptr) {
			g_deferred_scan_tasks.push_back(pair.second);
		}
	}
	g_deferred_scan_index = 0;
	g_deferred_scan_active = !g_deferred_scan_tasks.empty();
}

void TickDeferredScanAllCleanTasks(size_t batch_size)
{
	if (!g_deferred_scan_active || batch_size == 0) {
		return;
	}
	size_t done = 0;
	while (g_deferred_scan_index < g_deferred_scan_tasks.size() && done < batch_size) {
		HCleanTask* task = g_deferred_scan_tasks[g_deferred_scan_index++];
		if (task != nullptr) {
			task->RefreshSize();
		}
		++done;
	}
	if (g_deferred_scan_index >= g_deferred_scan_tasks.size()) {
		g_deferred_scan_active = false;
		g_deferred_scan_tasks.clear();
		HLOG_INFO("Deferred scan all: finished scheduling");
	}
}

bool IsDeferredScanAllCleanTasksActive()
{
	return g_deferred_scan_active;
}

bool IsAnyCleanTaskScanning()
{
	if (HCleanIsAsyncScanWorkerBusy()) {
		return true;
	}
	if (g_tasks_by_id == nullptr) {
		return false;
	}
	for (const auto& pair : *g_tasks_by_id) {
		const HCleanTask* task = pair.second;
		if (task == nullptr) {
			continue;
		}
		if (task->GetScanProgress().state == HCleanScanState::Scanning) {
			return true;
		}
	}
	return false;
}

bool GetCleanCategoryScanInfo(const char* category_id, HCleanCategoryScanInfo* out_info)
{
	if (out_info == nullptr) {
		return false;
	}
	*out_info = {};

	if (g_categories == nullptr || category_id == nullptr) {
		return false;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end()) {
		return false;
	}

	const std::vector<HCleanTask*>& tasks = it->second.tasks;
	out_info->task_count = static_cast<int>(tasks.size());

	int done_count = 0;
	int in_progress_count = 0;
	float percent_sum = 0.f;
	for (HCleanTask* task : tasks) {
		if (task == nullptr) {
			continue;
		}
		const HCleanScanProgress scan = task->GetScanProgress();
		if (scan.state == HCleanScanState::Scanning) {
			++in_progress_count;
			percent_sum += scan.percent;
		}
		else if (scan.state == HCleanScanState::Done) {
			const HCleanSizeInfo size = task->GetSize();
			if (size.valid) {
				++done_count;
			}
		}
	}

	out_info->scanning_count = in_progress_count;
	out_info->completed_count = done_count;
	out_info->any_scanning = in_progress_count > 0;
	if (out_info->task_count > 0) {
		out_info->aggregate_percent = (static_cast<float>(done_count) * 100.f + percent_sum)
			/ static_cast<float>(out_info->task_count);
	}
	return true;
}

bool GetGlobalCleanScanInfo(HCleanGlobalScanInfo* out_info)
{
	if (out_info == nullptr) {
		return false;
	}
	*out_info = {};

	if (g_tasks_by_id == nullptr) {
		return false;
	}

	int total = 0;
	int done_count = 0;
	int scanning_count = 0;
	float percent_sum = 0.f;
	const HCleanTask* active_task = nullptr;
	HCleanScanProgress active_progress = {};

	for (const auto& pair : *g_tasks_by_id) {
		HCleanTask* task = pair.second;
		if (task == nullptr) {
			continue;
		}
		++total;
		const HCleanScanProgress scan = task->GetScanProgress();
		if (scan.state == HCleanScanState::Scanning) {
			++scanning_count;
			percent_sum += scan.percent;
			if (active_task == nullptr || scan.percent > active_progress.percent) {
				active_task = task;
				active_progress = scan;
			}
		}
		else if (scan.state == HCleanScanState::Done) {
			const HCleanSizeInfo size = task->GetSize();
			if (size.valid) {
				++done_count;
				percent_sum += 100.f;
			}
		}
	}

	out_info->task_count = total;
	out_info->completed_count = done_count;
	out_info->scanning_count = scanning_count;
	out_info->any_scanning = scanning_count > 0 || HCleanIsAsyncScanWorkerBusy();
	if (total > 0) {
		out_info->aggregate_percent = percent_sum / static_cast<float>(total);
	}
	if (active_task != nullptr) {
		strncpy_s(out_info->current_task_name, active_task->GetName(), _TRUNCATE);
		if (active_progress.status_text != nullptr && active_progress.status_text[0] != '\0') {
			strncpy_s(out_info->status_text, active_progress.status_text, _TRUNCATE);
		}
		else {
			snprintf(out_info->status_text, sizeof(out_info->status_text),
				I18N(u8"掃描中 %.0f%%"), static_cast<double>(active_progress.percent));
		}
	}
	else if (out_info->any_scanning) {
		strncpy_s(out_info->status_text, I18N(u8"背景掃描進行中…"), _TRUNCATE);
	}
	return true;
}

void SetCleanCategorySelected(const char* category_id, bool selected)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end()) {
		return;
	}
	for (HCleanTask* task : it->second.tasks) {
		if (task != nullptr && task->ShouldShowInUI()) {
			task->SetSelected(selected);
		}
	}
}

bool IsCleanCategoryAllSelected(const char* category_id)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return false;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end() || it->second.tasks.empty()) {
		return false;
	}
	bool any_visible = false;
	for (HCleanTask* task : it->second.tasks) {
		if (task == nullptr || !task->ShouldShowInUI()) {
			continue;
		}
		any_visible = true;
		if (!task->IsSelected()) {
			return false;
		}
	}
	return any_visible;
}

bool IsCleanCategoryPartiallySelected(const char* category_id)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return false;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end() || it->second.tasks.empty()) {
		return false;
	}
	bool any_visible = false;
	bool any = false;
	bool all = true;
	for (HCleanTask* task : it->second.tasks) {
		if (task == nullptr || !task->ShouldShowInUI()) {
			continue;
		}
		any_visible = true;
		if (task->IsSelected()) {
			any = true;
		}
		else {
			all = false;
		}
	}
	return any_visible && any && !all;
}

int64_t GetCleanCategoryTotalBytes(const char* category_id)
{
	if (g_categories == nullptr || category_id == nullptr) {
		return 0;
	}
	auto it = g_categories->find(category_id);
	if (it == g_categories->end()) {
		return 0;
	}
	int64_t total = 0;
	for (HCleanTask* task : it->second.tasks) {
		if (task == nullptr) {
			continue;
		}
		const HCleanSizeInfo size = task->GetSize();
		if (size.valid && size.bytes > 0) {
			total += size.bytes;
		}
	}
	return total;
}

int64_t GetSelectedCleanTasksTotalBytes()
{
	if (g_tasks_by_id == nullptr) {
		return 0;
	}
	int64_t total = 0;
	for (const auto& pair : *g_tasks_by_id) {
		HCleanTask* task = pair.second;
		if (task == nullptr || !task->IsSelected() || !task->ShouldShowInUI()) {
			continue;
		}
		const HCleanSizeInfo size = task->GetSize();
		if (size.valid && size.bytes > 0) {
			total += size.bytes;
		}
	}
	return total;
}

void GetCleanTasksSizeSummary(HCleanSizeSummary* out_summary)
{
	if (out_summary == nullptr) {
		return;
	}
	*out_summary = {};

	if (g_tasks_by_id == nullptr) {
		return;
	}

	for (const auto& pair : *g_tasks_by_id) {
		HCleanTask* task = pair.second;
		if (task == nullptr || !task->ShouldShowInUI()) {
			continue;
		}

		++out_summary->visible_count;
		const HCleanSizeInfo size = task->GetSize();
		if (size.valid) {
			++out_summary->visible_scanned_count;
			if (size.bytes > 0) {
				out_summary->visible_total_bytes += size.bytes;
			}
		}

		if (!task->IsSelected()) {
			continue;
		}

		++out_summary->selected_count;
		if (size.valid) {
			if (size.bytes > 0) {
				out_summary->selected_bytes += size.bytes;
			}
		}
		else {
			out_summary->selected_has_unscanned = true;
		}
	}
}

int64_t QuerySystemDriveFreeBytes()
{
	wchar_t sys_dir[MAX_PATH] = {};
	if (GetSystemDirectoryW(sys_dir, MAX_PATH) == 0) {
		return -1;
	}
	const std::wstring root(sys_dir, 3);
	ULARGE_INTEGER avail = {};
	if (!GetDiskFreeSpaceExW(root.c_str(), &avail, nullptr, nullptr)) {
		return -1;
	}
	return static_cast<int64_t>(avail.QuadPart);
}

void RequestCleanSelectedTasks()
{
	{
		std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
		if (g_clean_worker.phase == HCleanSessionPhase::Cleaning) {
			return;
		}
	}

	if (!HCleanPrepareCleanSelectedTasks()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
		if (g_clean_worker.phase == HCleanSessionPhase::Cleaning) {
			return;
		}

		g_clean_worker.phase = HCleanSessionPhase::Cleaning;
		g_clean_worker.next_index = 0;
		g_clean_worker.succeeded = 0;
		g_clean_worker.failed = 0;
		g_clean_worker.disk_free_before = QuerySystemDriveFreeBytes();
		g_clean_worker.disk_free_now = g_clean_worker.disk_free_before;
		g_clean_worker.selected_at_start = GetSelectedCleanTasksTotalBytes();
		g_clean_worker.freed_total = 0;
		g_clean_worker.progress = 0.f;
		g_clean_worker.status_text = I18N(u8"準備清理…");
		g_clean_worker.current_task_name = "";
		g_clean_worker.current_detail_path = "";
		g_clean_worker.details_completed = 0;
		g_clean_worker.details_total = 0;
		g_clean_worker.detail_progress_percent = 0.f;
		g_clean_worker.last_log_line[0] = '\0';
		g_clean_worker.session_skip_locked = 0;
		g_clean_worker.session_skip_access_denied = 0;
		g_clean_worker.session_skip_timeout = 0;
		g_clean_worker.session_skip_reparse = 0;
		g_clean_worker.history_recorded = false;
		HLOG_INFO("Clean worker started: {} task(s), selected {} bytes",
			g_clean_worker.queue.size(), g_clean_worker.selected_at_start);
		g_clean_run_requested.store(true, std::memory_order_release);
	}

	EnsureCleanWorkerThread();
	g_clean_worker_cv.notify_one();
}

bool HCleanPrepareCleanSelectedTasks()
{
	{
		std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
		if (g_clean_worker.phase == HCleanSessionPhase::Cleaning) {
			return false;
		}
	}

	if (!HCleanShowAdminElevationPrompt()) {
		HLOG_INFO("Clean cancelled: user declined admin prompt");
		return false;
	}

	std::vector<HCleanTask*> selected;
	CollectSelectedCleanTasks(selected);
	if (selected.empty()) {
		HLOG_INFO("Clean requested: no selected tasks");
		return false;
	}

	for (HCleanTask* task : selected) {
		if (task == nullptr) {
			continue;
		}
		const ProcessCheckEntry* entry = nullptr;
		if (TaskNeedsProcessCheck(task->GetId(), &entry) && entry != nullptr) {
			if (!HCleanShowProcessRunningPrompt(entry->app_name, entry->exe_names, entry->exe_count)) {
				HLOG_INFO("Clean cancelled: user declined process warning for '{}'", task->GetId());
				return false;
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
		if (g_clean_worker.phase == HCleanSessionPhase::Cleaning) {
			return false;
		}
		g_clean_worker.queue = std::move(selected);
	}

	return true;
}

void HCleanReportCleanProgress(const char* task_name, const char* detail_path,
	size_t detail_index, size_t detail_total, const HCleanDeleteStats* stats)
{
	std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
	if (task_name != nullptr) {
		g_clean_worker.current_task_name = task_name;
	}
	if (detail_path != nullptr) {
		g_clean_worker.current_detail_path = detail_path;
	}
	g_clean_worker.details_completed = static_cast<int>(detail_index);
	g_clean_worker.details_total = static_cast<int>(detail_total);
	g_clean_worker.detail_progress_percent = detail_total > 0
		? static_cast<float>(detail_index) / static_cast<float>(detail_total) * 100.f
		: 0.f;

	if (stats != nullptr && detail_path != nullptr) {
		g_clean_worker.session_skip_locked += stats->skip_locked;
		g_clean_worker.session_skip_access_denied += stats->skip_access_denied;
		g_clean_worker.session_skip_timeout += stats->skip_timeout;
		g_clean_worker.session_skip_reparse += stats->skip_reparse;
		snprintf(g_clean_worker.last_log_line, sizeof(g_clean_worker.last_log_line),
			I18N(u8"%s — 已釋放 %lld B（略過：鎖定 %d / 拒絕 %d / 逾時 %d）"),
			detail_path,
			static_cast<long long>(stats->bytes_freed),
			stats->skip_locked, stats->skip_access_denied, stats->skip_timeout);
	}
}

void TickCleanWorker()
{
	// 清理在背景執行緒執行；主執行緒僅透過 GetCleanSessionInfo 輪詢進度。
}

void TickScanWorker()
{
	HCleanTickAsyncScanWorker();
}

bool GetCleanSessionInfo(HCleanSessionInfo* out_info)
{
	if (out_info == nullptr) {
		return false;
	}

	HCleanSessionPhase phase = HCleanSessionPhase::Idle;
	{
		std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
		*out_info = {};
		out_info->phase = g_clean_worker.phase;
		out_info->progress_percent = g_clean_worker.progress;
		out_info->tasks_completed = static_cast<int>(g_clean_worker.next_index);
		out_info->tasks_total = static_cast<int>(g_clean_worker.queue.size());
		out_info->status_text = g_clean_worker.status_text;
		out_info->current_task_name = g_clean_worker.current_task_name;
		out_info->current_detail_path = g_clean_worker.current_detail_path;
		out_info->details_completed = g_clean_worker.details_completed;
		out_info->details_total = g_clean_worker.details_total;
		out_info->detail_progress_percent = g_clean_worker.detail_progress_percent;
		out_info->last_log_line = g_clean_worker.last_log_line;
		out_info->session_skip_locked = g_clean_worker.session_skip_locked;
		out_info->session_skip_access_denied = g_clean_worker.session_skip_access_denied;
		out_info->session_skip_timeout = g_clean_worker.session_skip_timeout;
		out_info->session_skip_reparse = g_clean_worker.session_skip_reparse;
		phase = g_clean_worker.phase;

		if (g_clean_worker.phase == HCleanSessionPhase::Cleaning
			|| g_clean_worker.phase == HCleanSessionPhase::Done) {
			out_info->disk_free_before_clean = g_clean_worker.disk_free_before;
			out_info->disk_free_bytes = g_clean_worker.disk_free_now >= 0
				? g_clean_worker.disk_free_now
				: QuerySystemDriveFreeBytes();
			out_info->selected_bytes_at_clean_start = g_clean_worker.selected_at_start;
			out_info->freed_bytes_session = g_clean_worker.freed_total;
		}
	}

	if (phase == HCleanSessionPhase::Cleaning
		|| phase == HCleanSessionPhase::Done) {
		out_info->selected_bytes = GetSelectedCleanTasksTotalBytes();
		HCleanSizeSummary summary{};
		GetCleanTasksSizeSummary(&summary);
		out_info->visible_total_bytes = summary.visible_total_bytes;
	}
	else {
		out_info->disk_free_bytes = QuerySystemDriveFreeBytes();
		HCleanSizeSummary summary{};
		GetCleanTasksSizeSummary(&summary);
		out_info->selected_bytes = summary.selected_bytes;
		out_info->visible_total_bytes = summary.visible_total_bytes;
		out_info->selected_bytes_at_clean_start = summary.selected_bytes;
		out_info->status_text = IsAnyCleanTaskScanning() ? I18N(u8"掃描中…") : I18N(u8"就緒");
	}

	return true;
}

void ResetCleanSessionAfterScan()
{
	std::lock_guard<std::mutex> lock(g_clean_worker_mutex);
	if (g_clean_worker.phase == HCleanSessionPhase::Cleaning) {
		return;
	}
	g_clean_worker.phase = HCleanSessionPhase::Idle;
	g_clean_worker.queue.clear();
	g_clean_worker.next_index = 0;
	g_clean_worker.succeeded = 0;
	g_clean_worker.failed = 0;
	g_clean_worker.disk_free_before = -1;
	g_clean_worker.disk_free_now = -1;
	g_clean_worker.selected_at_start = 0;
	g_clean_worker.freed_total = 0;
	g_clean_worker.progress = 0.f;
	g_clean_worker.status_text = "";
	g_clean_worker.history_recorded = false;
}

size_t CleanSelectedTasks(size_t* out_succeeded, size_t* out_failed)
{
	if (out_succeeded != nullptr) {
		*out_succeeded = 0;
	}
	if (out_failed != nullptr) {
		*out_failed = 0;
	}

	RequestCleanSelectedTasks();

	std::unique_lock<std::mutex> lock(g_clean_worker_mutex);
	g_clean_worker_cv.wait(lock, [] {
		return g_clean_worker.phase != HCleanSessionPhase::Cleaning;
	});

	const size_t queue_size = g_clean_worker.queue.size();
	if (out_succeeded != nullptr) {
		*out_succeeded = g_clean_worker.succeeded;
	}
	if (out_failed != nullptr) {
		*out_failed = g_clean_worker.failed;
	}
	return queue_size;
}

namespace {
	void SetTaskSelected(const char* task_id, bool selected)
	{
		HCleanTask* task = FindCleanTask(task_id);
		if (task != nullptr) {
			task->SetSelected(selected);
		}
	}

	void ApplyPresetTasks(const char* const* task_ids, size_t count)
	{
		for (size_t i = 0; i < count; ++i) {
			SetTaskSelected(task_ids[i], true);
		}
	}
}

void SetAllCleanTasksSelected(bool selected)
{
	if (g_tasks_by_id == nullptr) {
		return;
	}
	for (const auto& pair : *g_tasks_by_id) {
		if (pair.second != nullptr) {
			pair.second->SetSelected(selected);
		}
	}
	HLOG_INFO("SetAllCleanTasksSelected: {}", selected ? "true" : "false");
}

void ApplyCleanPreset(const char* preset_id)
{
	if (preset_id == nullptr) {
		HLOG_WARN("ApplyCleanPreset: null preset_id");
		return;
	}

	HLOG_INFO("ApplyCleanPreset: preset '{}'", preset_id);
	SetAllCleanTasksSelected(false);

	if (std::strcmp(preset_id, "gamer") == 0) {
		static const char* kTasks[] = {
			"system_temp",
			"user_temp",
			"system_thumbnail",
			"game_steam_shader",
			"game_epic_cache",
			"game_xbox_ms_store",
			"browser_edge_cache",
			"system_dx_shader",
			"system_delivery_optimization",
			"game_engine_unity",
			"game_genre_fps",
			"game_riot_cache",
			"system_ms_store_cache",
			"browser_discord_cache",
		};
		ApplyPresetTasks(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	else if (std::strcmp(preset_id, "general") == 0) {
		static const char* kTasks[] = {
			"system_temp",
			"user_temp",
			"system_thumbnail",
			"system_win_update_cache",
			"system_delivery_optimization",
			"browser_edge_cache",
			"user_recycle_bin",
			"system_wer",
			"system_dx_shader",
			"system_ms_store_cache",
			"system_windows_logs",
			"user_logs",
			"system_font_cache",
			"system_crash_dumps",
			"system_uwp_cache",
			"system_defender_history",
			"comm_teams",
			"system_onedrive",
		};
		ApplyPresetTasks(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	else if (std::strcmp(preset_id, "developer") == 0) {
		static const char* kTasks[] = {
			"system_temp",
			"user_temp",
			"dev_npm_cache",
			"dev_pip_cache",
			"dev_nuget_cache",
			"dev_vs_component_cache",
			"dev_gradle_cache",
			"dev_unity_cache",
			"dev_web_build_cache",
			"dev_ai_model_cache",
			"dev_backend_build_cache",
			"dev_yarn_cache",
			"dev_pnpm_store",
			"dev_cargo_cache",
			"dev_go_build_cache",
			"dev_docker",
		};
		ApplyPresetTasks(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	else if (std::strcmp(preset_id, "advanced") == 0 || std::strcmp(preset_id, "bleachbit") == 0) {
		static const char* kTasks[] = {
			"system_temp",
			"user_temp",
			"system_thumbnail",
			"system_win_update_cache",
			"system_wer",
			"system_windows_logs",
			"user_logs",
			"system_font_cache",
			"user_recycle_bin",
			"user_recent",
			"browser_edge_cache",
			"browser_chrome_cache",
			"browser_firefox_cache",
			"system_defender_history",
			"system_crash_dumps",
			"advanced_legacy_web",
			"advanced_legacy_plugins",
			"advanced_rdp_remote",
			"advanced_explorer_extras",
			"advanced_third_party_logs",
		};
		ApplyPresetTasks(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	else {
		HLOG_WARN("ApplyCleanPreset: unknown preset '{}'", preset_id);
	}
}

namespace {
	int64_t SumPresetTaskBytes(const char* const* task_ids, size_t count)
	{
		int64_t total = 0;
		for (size_t i = 0; i < count; ++i) {
			HCleanTask* task = FindCleanTask(task_ids[i]);
			if (task == nullptr) {
				continue;
			}
			const HCleanSizeInfo size = task->GetSize();
			if (size.valid && size.bytes > 0) {
				total += size.bytes;
			}
		}
		return total;
	}
}

int64_t EstimateCleanPresetBytes(const char* preset_id)
{
	if (preset_id == nullptr) {
		return 0;
	}
	if (std::strcmp(preset_id, "gamer") == 0) {
		static const char* kTasks[] = {
			"system_temp", "user_temp", "system_thumbnail", "game_steam_shader",
			"game_epic_cache", "game_xbox_ms_store", "browser_edge_cache", "system_dx_shader",
			"system_delivery_optimization", "game_engine_unity", "game_genre_fps",
			"game_riot_cache", "system_ms_store_cache", "browser_discord_cache",
		};
		return SumPresetTaskBytes(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	if (std::strcmp(preset_id, "general") == 0) {
		static const char* kTasks[] = {
			"system_temp", "user_temp", "system_thumbnail", "system_win_update_cache",
			"system_delivery_optimization", "browser_edge_cache", "user_recycle_bin",
			"system_wer", "system_dx_shader", "system_ms_store_cache", "system_windows_logs",
			"user_logs", "system_font_cache", "system_crash_dumps", "system_uwp_cache",
			"system_defender_history", "system_search_temp", "comm_teams", "system_onedrive",
		};
		return SumPresetTaskBytes(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	if (std::strcmp(preset_id, "developer") == 0) {
		static const char* kTasks[] = {
			"system_temp", "user_temp", "dev_npm_cache", "dev_pip_cache", "dev_nuget_cache",
			"dev_vs_component_cache", "dev_gradle_cache", "dev_unity_cache", "dev_web_build_cache",
			"dev_ai_model_cache", "dev_backend_build_cache", "dev_yarn_cache", "dev_pnpm_store",
			"dev_cargo_cache", "dev_go_build_cache", "dev_docker",
		};
		return SumPresetTaskBytes(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	if (std::strcmp(preset_id, "advanced") == 0 || std::strcmp(preset_id, "bleachbit") == 0) {
		static const char* kTasks[] = {
			"system_temp", "user_temp", "system_thumbnail", "system_win_update_cache",
			"system_wer", "system_windows_logs", "user_logs", "system_font_cache",
			"user_recycle_bin", "user_recent", "browser_edge_cache", "browser_chrome_cache",
			"browser_firefox_cache", "system_defender_history", "system_crash_dumps",
			"advanced_legacy_web", "advanced_legacy_plugins", "advanced_rdp_remote",
			"advanced_explorer_extras", "advanced_third_party_logs",
		};
		return SumPresetTaskBytes(kTasks, sizeof(kTasks) / sizeof(kTasks[0]));
	}
	HLOG_DEBUG("EstimateCleanPresetBytes: unknown preset '{}'", preset_id);
	return 0;
}