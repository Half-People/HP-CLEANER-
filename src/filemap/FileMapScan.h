#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 文件地圖：背景掃描目前目錄的直接子項目，供樹狀圖 / Treemap 使用。
namespace FileMapScan {

	enum class MeasureStatus : uint8_t {
		Pending = 0,
		Measuring = 1,
		Ok = 2,
		AccessDenied = 3,
		Failed = 4,
		Partial = 5,
	};

	enum class ScopeBlockReason : uint8_t {
		None = 0,
		AccessDenied = 1,
		NotFound = 2,
		ListError = 3,
	};

	// 檢查能否列舉資料夾內容（與 Treemap 掃描相同條件）。
	struct ScopeListAccess {
		bool can_list = true;
		bool access_denied = false;
		uint32_t win32_error = 0;
		char path_utf8[1024] = {};
		char headline_utf8[160] = {};
		char detail_utf8[512] = {};
	};

	struct ChildItem {
		wchar_t full_path[1024] = {};
		char name_utf8[256] = {};
		char extension_utf8[32] = {};
		uint64_t size_bytes = 0;
		uint64_t item_count = 0;
		bool is_directory = false;
		MeasureStatus measure_status = MeasureStatus::Pending;
		// 列舉當下檔案屬性，用於快取失效判斷（mtime / 檔案大小）
		uint64_t stamp_mtime_utc = 0;
		uint64_t stamp_size_bytes = 0;
		uint64_t measured_at_ms = 0;
	};

	struct Snapshot {
		wchar_t scope_path[1024] = {};
		char scope_utf8[1024] = {};
		std::vector<ChildItem> children;
		bool valid = false;
		bool scanning = false;
		bool failed = false;
		ScopeBlockReason scope_block = ScopeBlockReason::None;
		char scope_block_detail[512] = {};
		float progress = 0.f;
		char status_text[128] = {};
		int selected_index = -1;
	};

	void Init();
	void Shutdown();
	ScopeListAccess ProbeScopeListAccess(const wchar_t* scope_path_wide);
	// force_refresh=true 時略過目錄快照快取（工具列「重新掃描」）。
	bool RequestScanScope(const wchar_t* scope_path_wide, bool force_refresh = false);
	void RequestScanDrive(wchar_t drive_letter);
	void NotifyScopeAccessBlocked(const ScopeListAccess& info);
	bool ConsumeScopeAccessPopup(ScopeListAccess* out);
	std::vector<std::wstring> ListFixedDrives();
	Snapshot GetSnapshot();
	int GetSelectedIndex();
	void SetSelectedIndex(int index);
	const wchar_t* GetScopePathWide();

	// 若該路徑曾被 Treemap 掃描測量過，回傳與中央區塊一致的大小資料（供左欄樹顯示）。
	bool TryLookupMeasured(const wchar_t* path_wide, ChildItem* out);

	// 標記 Windows 保留／連結點等特殊資料夾（extension 設為 [Win保留]）。
	void TagWindowsReservedEntry(const wchar_t* file_name, uint32_t file_attributes, bool is_directory,
		char* extension_utf8, size_t extension_utf8_size);

}
