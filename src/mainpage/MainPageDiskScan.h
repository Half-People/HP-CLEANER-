#pragma once

#include <cstdint>

// 背景執行緒分析系統碟（Windows 所在磁碟）儲存分類，供 MainPage 甜甜圈使用。
// 不在工作執行緒呼叫 ImGui；結果以 mutex 保護。
namespace MainPageDiskScan {

	enum class Category : int {
		System = 0,
		Programs,
		Users,
		Temp,
		Other,
		Free,
		Count
	};

	struct Segment {
		const char* label;
		uint64_t bytes;
		float fraction;
	};

	struct Snapshot {
		wchar_t drive_root[8] = {};
		char volume_label[128] = {};
		char status_text[64] = {};
		uint64_t total_bytes = 0;
		uint64_t free_bytes = 0;
		Segment segments[static_cast<int>(Category::Count)] = {};
		int segment_count = 0;
		bool valid = false;
		bool scanning = false;
		bool failed = false;
		float progress = 0.f;
	};

	void Init();
	void Shutdown();
	void SetDeferUntilMainWindowVisible(bool defer);
	void NotifyMainWindowVisible();
	void RequestRescan();
	Snapshot GetSnapshot();
}
