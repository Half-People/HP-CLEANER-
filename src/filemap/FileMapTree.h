#pragma once

#include "FileMapScan.h"
#include <cstdint>
#include <vector>

// 左欄檔案樹：以磁碟為根、展開時動態載入子項。
namespace FileMapTree {

	enum class LoadState : uint8_t {
		Unloaded = 0,
		Loading = 1,
		Loaded = 2,
		Failed = 3,
	};

	enum class FilterMode : uint8_t {
		All = 0,
		FoldersOnly = 1,
		FilesOnly = 2,
	};

	struct Node {
		uint32_t id = 0;
		uint32_t parent_id = 0;
		bool in_use = false;
		wchar_t path_wide[1024] = {};
		char name_utf8[256] = {};
		char extension_utf8[32] = {};
		bool is_directory = false;
		uint64_t size_bytes = 0;
		FileMapScan::MeasureStatus measure_status = FileMapScan::MeasureStatus::Pending;
		LoadState load_state = LoadState::Unloaded;
		bool expanded = false;
		std::vector<uint32_t> child_ids;
	};

	constexpr uint32_t kRowPlaceholderLoading = 0xFFFFFFFEu;
	constexpr uint32_t kRowPlaceholderFailed = 0xFFFFFFFDu;
	constexpr uint32_t kRowPlaceholderAccessDenied = 0xFFFFFFFBu;
	constexpr uint32_t kRowPlaceholderEmpty = 0xFFFFFFFCu;

	struct VisibleRow {
		uint32_t node_id = 0;
		uint32_t placeholder_parent_id = 0; // 佔位列對應的父節點（用於區分磁碟根提示）
		int depth = 0;
		bool has_children = false;
		bool expanded = false;
	};

	void Init();
	void Shutdown();
	void Tick();

	uint32_t GetSelectedNodeId();
	void SetSelectedNodeId(uint32_t node_id);
	bool CopyNode(uint32_t id, Node& out);
	uint32_t FindNodeIdByPath(const wchar_t* path_wide);

	void SetFilterText(const char* utf8);
	void SetFilterMode(FilterMode mode);
	const char* GetFilterText();

	void SetExpanded(uint32_t node_id, bool expanded);
	void ToggleExpanded(uint32_t node_id);
	void RequestLoadChildren(uint32_t node_id);

	void ExpandAndSelectPath(const wchar_t* scope_path_wide);
	void BuildVisibleRows(std::vector<VisibleRow>& out_rows);

}
