#include "CleanHistory.h"
#include "HAppPaths.h"
#include "HPage.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <chrono>
#include <algorithm>

namespace {
	const char* kHistoryFileName = "clean_history.json";
	constexpr int kMaxEntries = 200;

	std::mutex g_mutex;
	std::vector<CleanHistoryEntry> g_entries;
	bool g_loaded = false;

	std::string GetHistoryFilePath()
	{
		const std::string dir = HAppPaths::GetConfigDir();
		if (dir.empty()) {
			return kHistoryFileName;
		}
		return dir + kHistoryFileName;
	}

	int64_t NowUnixMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	void ParseEntryArray(const nlohmann::json& arr)
	{
		g_entries.clear();
		if (!arr.is_array()) {
			return;
		}
		for (const auto& item : arr) {
			if (!item.is_object()) {
				continue;
			}
			CleanHistoryEntry e;
			if (item.contains("id") && item["id"].is_number_integer()) {
				e.id = item["id"].get<int64_t>();
			}
			if (item.contains("unix_ms") && item["unix_ms"].is_number_integer()) {
				e.unix_ms = item["unix_ms"].get<int64_t>();
			}
			if (item.contains("freed_bytes") && item["freed_bytes"].is_number_integer()) {
				e.freed_bytes = item["freed_bytes"].get<int64_t>();
			}
			if (item.contains("selected_bytes_at_start") && item["selected_bytes_at_start"].is_number_integer()) {
				e.selected_bytes_at_start = item["selected_bytes_at_start"].get<int64_t>();
			}
			if (item.contains("disk_free_before") && item["disk_free_before"].is_number_integer()) {
				e.disk_free_before = item["disk_free_before"].get<int64_t>();
			}
			if (item.contains("disk_free_after") && item["disk_free_after"].is_number_integer()) {
				e.disk_free_after = item["disk_free_after"].get<int64_t>();
			}
			auto read_int = [&](const char* key, int& out) {
				if (item.contains(key) && item[key].is_number_integer()) {
					out = item[key].get<int>();
				}
			};
			read_int("tasks_succeeded", e.tasks_succeeded);
			read_int("tasks_failed", e.tasks_failed);
			read_int("tasks_total", e.tasks_total);
			read_int("skip_locked", e.skip_locked);
			read_int("skip_access_denied", e.skip_access_denied);
			read_int("skip_timeout", e.skip_timeout);
			read_int("skip_reparse", e.skip_reparse);

			if (item.contains("task_ids") && item["task_ids"].is_array()) {
				for (const auto& tid : item["task_ids"]) {
					if (tid.is_string()) {
						e.task_ids.push_back(tid.get<std::string>());
					}
				}
			}

			if (e.unix_ms <= 0) {
				e.unix_ms = e.id > 0 ? e.id : NowUnixMs();
			}
			if (e.id <= 0) {
				e.id = e.unix_ms;
			}
			g_entries.push_back(std::move(e));
		}

		std::sort(g_entries.begin(), g_entries.end(),
			[](const CleanHistoryEntry& a, const CleanHistoryEntry& b) {
				return a.unix_ms > b.unix_ms;
			});
	}

	bool SaveUnlocked()
	{
		HAppPaths::EnsureAppDataDirs();
		nlohmann::json root;
		root["version"] = 1;
		nlohmann::json arr = nlohmann::json::array();
		for (const CleanHistoryEntry& e : g_entries) {
			nlohmann::json item;
			item["id"] = e.id;
			item["unix_ms"] = e.unix_ms;
			item["freed_bytes"] = e.freed_bytes;
			item["selected_bytes_at_start"] = e.selected_bytes_at_start;
			item["disk_free_before"] = e.disk_free_before;
			item["disk_free_after"] = e.disk_free_after;
			item["tasks_succeeded"] = e.tasks_succeeded;
			item["tasks_failed"] = e.tasks_failed;
			item["tasks_total"] = e.tasks_total;
			item["skip_locked"] = e.skip_locked;
			item["skip_access_denied"] = e.skip_access_denied;
			item["skip_timeout"] = e.skip_timeout;
			item["skip_reparse"] = e.skip_reparse;
			nlohmann::json ids = nlohmann::json::array();
			for (const std::string& tid : e.task_ids) {
				ids.push_back(tid);
			}
			item["task_ids"] = std::move(ids);
			arr.push_back(std::move(item));
		}
		root["entries"] = std::move(arr);

		const std::string path = GetHistoryFilePath();
		std::ofstream out(path, std::ios::trunc);
		if (!out.is_open()) {
			HLOG_WARN("CleanHistory: cannot write '{}'", path);
			return false;
		}
		out << root.dump(2);
		HLOG_INFO("CleanHistory: saved {} entries to '{}'", g_entries.size(), path);
		return true;
	}

	void EnsureLoadedUnlocked()
	{
		if (g_loaded) {
			return;
		}
		g_loaded = true;
		g_entries.clear();

		const std::string path = GetHistoryFilePath();
		std::ifstream in(path);
		if (!in.is_open()) {
			HLOG_DEBUG("CleanHistory: no file at '{}' (first run)", path);
			return;
		}

		try {
			nlohmann::json root;
			in >> root;
			if (root.contains("entries")) {
				ParseEntryArray(root["entries"]);
			}
			HLOG_INFO("CleanHistory: loaded {} entries", g_entries.size());
		}
		catch (const std::exception& ex) {
			HLOG_WARN("CleanHistory: parse failed '{}': {}", path, ex.what());
			g_entries.clear();
		}
	}
}

namespace CleanHistory {

void Reload()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_loaded = false;
	EnsureLoadedUnlocked();
}

const std::vector<CleanHistoryEntry>& GetEntries()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	EnsureLoadedUnlocked();
	return g_entries;
}

void GetSummary(CleanHistorySummary* out)
{
	if (out == nullptr) {
		return;
	}
	*out = {};
	std::lock_guard<std::mutex> lock(g_mutex);
	EnsureLoadedUnlocked();
	out->session_count = static_cast<int>(g_entries.size());
	for (const CleanHistoryEntry& e : g_entries) {
		out->total_freed_bytes += e.freed_bytes;
	}
	if (!g_entries.empty()) {
		out->has_last = true;
		out->last = g_entries.front();
	}
}

bool Append(const CleanHistoryEntry& entry)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	EnsureLoadedUnlocked();
	g_entries.insert(g_entries.begin(), entry);
	if (static_cast<int>(g_entries.size()) > kMaxEntries) {
		g_entries.resize(kMaxEntries);
	}
	return SaveUnlocked();
}

bool ClearAll()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	EnsureLoadedUnlocked();
	g_entries.clear();
	return SaveUnlocked();
}

void RecordSession(
	int64_t freed_bytes,
	int64_t selected_bytes_at_start,
	int64_t disk_free_before,
	int64_t disk_free_after,
	int tasks_succeeded,
	int tasks_failed,
	int tasks_total,
	int skip_locked,
	int skip_access_denied,
	int skip_timeout,
	int skip_reparse,
	const std::vector<std::string>& task_ids)
{
	if (tasks_total <= 0 && freed_bytes <= 0) {
		return;
	}

	CleanHistoryEntry e;
	e.unix_ms = NowUnixMs();
	e.id = e.unix_ms;
	e.freed_bytes = freed_bytes;
	e.selected_bytes_at_start = selected_bytes_at_start;
	e.disk_free_before = disk_free_before;
	e.disk_free_after = disk_free_after;
	e.tasks_succeeded = tasks_succeeded;
	e.tasks_failed = tasks_failed;
	e.tasks_total = tasks_total;
	e.skip_locked = skip_locked;
	e.skip_access_denied = skip_access_denied;
	e.skip_timeout = skip_timeout;
	e.skip_reparse = skip_reparse;
	e.task_ids = task_ids;

	if (Append(e)) {
		HLOG_INFO("CleanHistory: recorded session freed={} tasks={}/{}",
			freed_bytes, tasks_succeeded, tasks_total);
	}
}

}
