#pragma once
#ifndef CLEAN_HISTORY_H
#define CLEAN_HISTORY_H

#include <cstdint>
#include <string>
#include <vector>

struct CleanHistoryEntry {
	int64_t id = 0;
	int64_t unix_ms = 0;
	int64_t freed_bytes = 0;
	int64_t selected_bytes_at_start = 0;
	int64_t disk_free_before = -1;
	int64_t disk_free_after = -1;
	int tasks_succeeded = 0;
	int tasks_failed = 0;
	int tasks_total = 0;
	int skip_locked = 0;
	int skip_access_denied = 0;
	int skip_timeout = 0;
	int skip_reparse = 0;
	std::vector<std::string> task_ids;
};

struct CleanHistorySummary {
	int session_count = 0;
	int64_t total_freed_bytes = 0;
	bool has_last = false;
	CleanHistoryEntry last;
};

namespace CleanHistory {
	void Reload();
	const std::vector<CleanHistoryEntry>& GetEntries();
	void GetSummary(CleanHistorySummary* out);

	bool Append(const CleanHistoryEntry& entry);
	bool ClearAll();

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
		const std::vector<std::string>& task_ids);
}

#endif
