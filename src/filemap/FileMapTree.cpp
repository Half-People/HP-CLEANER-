#include "FileMapTree.h"
#include "HPage.h"
#include <windows.h>
#include <algorithm>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace FileMapTree {
	namespace {
		constexpr int kMaxChildrenPerDir = 512;
		constexpr uint32_t kInvalidId = 0;

		std::mutex g_mutex;
		std::vector<Node> g_nodes;
		uint32_t g_selected_id = kInvalidId;
		char g_filter_text[128] = {};
		FilterMode g_filter_mode = FilterMode::All;

		std::mutex g_queue_mutex;
		std::condition_variable g_queue_cv;
		std::queue<uint32_t> g_load_queue;
		std::thread g_loader_thread;
		std::atomic<bool> g_shutdown{ false };
		bool g_init_done = false;

		wchar_t g_pending_expand_path[1024] = {};
		uint32_t g_pending_walk_id = kInvalidId;
		size_t g_pending_pos = 0;

		static bool Utf8FromWide(const wchar_t* wide, char* out, size_t out_size)
		{
			if (wide == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr) > 0;
		}

		static void NormalizeDirPath(std::wstring& path)
		{
			if (path.empty()) {
				return;
			}
			if (path.back() != L'\\') {
				path += L'\\';
			}
		}

		static bool ShouldSkipName(const wchar_t* name)
		{
			return name == nullptr || name[0] == L'\0'
				|| wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
		}

		static void ExtractExtension(const wchar_t* name, char* out_ext, size_t out_ext_size)
		{
			if (out_ext == nullptr || out_ext_size == 0) {
				return;
			}
			out_ext[0] = '\0';
			const wchar_t* dot = wcsrchr(name, L'.');
			if (dot == nullptr || dot[1] == L'\0') {
				strncpy_s(out_ext, out_ext_size, "(無)", _TRUNCATE);
				return;
			}
			WideCharToMultiByte(CP_UTF8, 0, dot, -1, out_ext, static_cast<int>(out_ext_size), nullptr, nullptr);
		}

		static std::wstring NormalizePathKey(const wchar_t* path)
		{
			if (path == nullptr || path[0] == L'\0') {
				return {};
			}
			std::wstring s(path);
			for (wchar_t& ch : s) {
				if (ch == L'/') {
					ch = L'\\';
				}
			}
			while (s.size() > 3 && s.back() == L'\\') {
				s.pop_back();
			}
			return s;
		}

		static bool PathEquals(const wchar_t* a, const wchar_t* b)
		{
			if (a == nullptr || b == nullptr) {
				return false;
			}
			return _wcsicmp(NormalizePathKey(a).c_str(), NormalizePathKey(b).c_str()) == 0;
		}

		static bool IsDriveRootNode(const Node& node)
		{
			return node.parent_id == kInvalidId && node.is_directory
				&& strcmp(node.extension_utf8, "[磁碟]") == 0;
		}

		static Node* NodePtr(uint32_t id)
		{
			if (id == kInvalidId || id >= g_nodes.size()) {
				return nullptr;
			}
			Node* node = &g_nodes[id];
			if (!node->in_use) {
				return nullptr;
			}
			return node;
		}

		static uint32_t AllocNode(uint32_t parent_id)
		{
			for (size_t i = 1; i < g_nodes.size(); ++i) {
				if (!g_nodes[i].in_use) {
					Node& slot = g_nodes[i];
					slot = {};
					slot.id = static_cast<uint32_t>(i);
					slot.parent_id = parent_id;
					slot.in_use = true;
					return slot.id;
				}
			}
			Node node = {};
			node.id = static_cast<uint32_t>(g_nodes.size());
			node.parent_id = parent_id;
			node.in_use = true;
			g_nodes.push_back(node);
			return node.id;
		}

		static void FreeNodeSubtreeLocked(uint32_t node_id)
		{
			Node* node = NodePtr(node_id);
			if (node == nullptr) {
				return;
			}
			const std::vector<uint32_t> children = node->child_ids;
			for (uint32_t child_id : children) {
				FreeNodeSubtreeLocked(child_id);
			}
			node->child_ids.clear();
			node->expanded = false;
			node->load_state = LoadState::Unloaded;
			node->in_use = false;
		}

		static void UnloadChildrenLocked(uint32_t parent_id)
		{
			Node* parent = NodePtr(parent_id);
			if (parent == nullptr || !parent->is_directory) {
				return;
			}
			const std::vector<uint32_t> children = parent->child_ids;
			for (uint32_t child_id : children) {
				FreeNodeSubtreeLocked(child_id);
			}
			parent->child_ids.clear();
			parent->load_state = LoadState::Unloaded;
		}

		struct EnumResult {
			std::vector<FileMapScan::ChildItem> items;
			bool list_ok = true;
		};

		static EnumResult EnumerateDirectory(const std::wstring& scope)
		{
			EnumResult result;
			std::wstring pattern = scope;
			pattern += L'*';

			WIN32_FIND_DATAW fd = {};
			const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
			if (find == INVALID_HANDLE_VALUE) {
				const DWORD err = GetLastError();
				if (err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD) {
					result.list_ok = false;
				}
				return result;
			}

			do {
				if (result.items.size() >= kMaxChildrenPerDir) {
					break;
				}
				if (ShouldSkipName(fd.cFileName)) {
					continue;
				}

				FileMapScan::ChildItem item = {};
				std::wstring full = scope;
				full += fd.cFileName;
				const bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				if (is_dir) {
					full += L'\\';
				}
				wcsncpy_s(item.full_path, full.c_str(), _TRUNCATE);
				Utf8FromWide(fd.cFileName, item.name_utf8, sizeof(item.name_utf8));
				item.is_directory = is_dir;

				if (item.is_directory) {
					strncpy_s(item.extension_utf8, "[資料夾]", _TRUNCATE);
					FileMapScan::TagWindowsReservedEntry(fd.cFileName, fd.dwFileAttributes, true,
						item.extension_utf8, sizeof(item.extension_utf8));
					item.measure_status = FileMapScan::MeasureStatus::Pending;
				}
				else {
					ExtractExtension(fd.cFileName, item.extension_utf8, sizeof(item.extension_utf8));
					ULARGE_INTEGER sz;
					sz.LowPart = fd.nFileSizeLow;
					sz.HighPart = fd.nFileSizeHigh;
					item.size_bytes = sz.QuadPart;
					item.measure_status = FileMapScan::MeasureStatus::Ok;
				}
				result.items.push_back(item);
			} while (FindNextFileW(find, &fd));

			FindClose(find);
			std::sort(result.items.begin(), result.items.end(), [](const FileMapScan::ChildItem& a, const FileMapScan::ChildItem& b) {
				if (a.is_directory != b.is_directory) {
					return a.is_directory > b.is_directory;
				}
				return _stricmp(a.name_utf8, b.name_utf8) < 0;
			});
			return result;
		}

		static void ApplyChildItemToNode(Node& node, const FileMapScan::ChildItem& item)
		{
			wcsncpy_s(node.path_wide, item.full_path, _TRUNCATE);
			strncpy_s(node.name_utf8, item.name_utf8, _TRUNCATE);
			strncpy_s(node.extension_utf8, item.extension_utf8, _TRUNCATE);
			node.is_directory = item.is_directory;
			node.size_bytes = item.size_bytes;
			node.measure_status = item.measure_status;
			node.load_state = item.is_directory ? LoadState::Unloaded : LoadState::Loaded;
			node.expanded = false;
			node.child_ids.clear();
		}

		static void SetNodeLoadStateLocked(uint32_t node_id, LoadState state)
		{
			if (Node* node = NodePtr(node_id)) {
				node->load_state = state;
			}
		}

		static void LoadNodeChildren(uint32_t parent_id)
		{
			if (g_shutdown.load(std::memory_order_acquire)) {
				return;
			}

			std::wstring scope;
			bool still_wanted = false;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				const Node* parent = NodePtr(parent_id);
				if (parent == nullptr || !parent->is_directory) {
					SetNodeLoadStateLocked(parent_id, LoadState::Failed);
					return;
				}
				if (!parent->expanded) {
					SetNodeLoadStateLocked(parent_id, LoadState::Unloaded);
					return;
				}
				scope = parent->path_wide;
				still_wanted = true;
			}

			if (!still_wanted) {
				return;
			}

			NormalizeDirPath(scope);
			const EnumResult listing = EnumerateDirectory(scope);

			if (g_shutdown.load(std::memory_order_acquire)) {
				return;
			}

			std::lock_guard<std::mutex> lock(g_mutex);
			Node* parent = NodePtr(parent_id);
			if (parent == nullptr || !parent->expanded) {
				SetNodeLoadStateLocked(parent_id, LoadState::Unloaded);
				return;
			}

			if (!listing.list_ok) {
				char path_utf8[1024] = {};
				Utf8FromWide(scope.c_str(), path_utf8, sizeof(path_utf8));
				HLOG_WARN("FileMapTree: 載入子節點失敗（無權限）path='{}'", path_utf8);
				UnloadChildrenLocked(parent_id);
				if (Node* parent_fail = NodePtr(parent_id)) {
					parent_fail->measure_status = FileMapScan::MeasureStatus::AccessDenied;
				}
				SetNodeLoadStateLocked(parent_id, LoadState::Failed);
				return;
			}

			UnloadChildrenLocked(parent_id);

			std::vector<uint32_t> new_child_ids;
			new_child_ids.reserve(listing.items.size());
			for (const auto& item : listing.items) {
				const uint32_t child_id = AllocNode(parent_id);
				Node* child = NodePtr(child_id);
				if (child == nullptr) {
					continue;
				}
				ApplyChildItemToNode(*child, item);
				new_child_ids.push_back(child_id);
			}

			Node* parent_after = NodePtr(parent_id);
			if (parent_after == nullptr || !parent_after->expanded) {
				for (uint32_t child_id : new_child_ids) {
					FreeNodeSubtreeLocked(child_id);
				}
				SetNodeLoadStateLocked(parent_id, LoadState::Unloaded);
				return;
			}
			parent_after->child_ids = std::move(new_child_ids);
			parent_after->load_state = LoadState::Loaded;
			char path_utf8[1024] = {};
			Utf8FromWide(parent_after->path_wide, path_utf8, sizeof(path_utf8));
			HLOG_DEBUG("FileMapTree: 載入完成 path='{}' children={}", path_utf8, parent_after->child_ids.size());
		}

		static void LoaderThreadMain()
		{
			while (!g_shutdown.load(std::memory_order_acquire)) {
				uint32_t node_id = kInvalidId;
				{
					std::unique_lock<std::mutex> lock(g_queue_mutex);
					g_queue_cv.wait(lock, [] {
						return g_shutdown.load(std::memory_order_acquire) || !g_load_queue.empty();
					});
					if (g_shutdown.load(std::memory_order_acquire)) {
						break;
					}
					node_id = g_load_queue.front();
					g_load_queue.pop();
				}

				LoadNodeChildren(node_id);
			}
		}

		static void EnqueueLoadLocked(uint32_t node_id)
		{
			Node* node = NodePtr(node_id);
			if (node == nullptr || !node->is_directory || !node->expanded) {
				return;
			}
			if (node->load_state == LoadState::Loaded || node->load_state == LoadState::Loading) {
				return;
			}
			node->load_state = LoadState::Loading;

			{
				std::lock_guard<std::mutex> lock(g_queue_mutex);
				g_load_queue.push(node_id);
			}
			g_queue_cv.notify_one();
		}

		static void EnqueueLoad(uint32_t node_id)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			EnqueueLoadLocked(node_id);
		}

		static uint32_t FindChildByPath(uint32_t parent_id, const wchar_t* child_path)
		{
			Node* parent = NodePtr(parent_id);
			if (parent == nullptr) {
				return kInvalidId;
			}
			for (uint32_t cid : parent->child_ids) {
				if (Node* child = NodePtr(cid)) {
					if (PathEquals(child->path_wide, child_path)) {
						return cid;
					}
				}
			}
			return kInvalidId;
		}

		static uint32_t FindDriveRootLocked(const wchar_t* scope)
		{
			if (scope == nullptr || scope[0] == L'\0') {
				return kInvalidId;
			}
			wchar_t root[8] = {};
			_snwprintf_s(root, _TRUNCATE, L"%c:\\", scope[0]);
			for (size_t i = 1; i < g_nodes.size(); ++i) {
				if (g_nodes[i].in_use && g_nodes[i].parent_id == kInvalidId
					&& PathEquals(g_nodes[i].path_wide, root)) {
					return static_cast<uint32_t>(i);
				}
			}
			return kInvalidId;
		}

		static void BuildDriveRoots()
		{
			for (Node& node : g_nodes) {
				if (node.in_use && node.parent_id == kInvalidId) {
					for (uint32_t cid : node.child_ids) {
						FreeNodeSubtreeLocked(cid);
					}
					node.in_use = false;
				}
			}
			g_nodes.clear();
			g_nodes.reserve(32);
			Node sentinel = {};
			sentinel.in_use = false;
			g_nodes.push_back(sentinel);

			const std::vector<std::wstring> drives = FileMapScan::ListFixedDrives();
			for (const std::wstring& drive : drives) {
				const uint32_t id = AllocNode(kInvalidId);
				Node* node = NodePtr(id);
				if (node == nullptr) {
					continue;
				}
				std::wstring path = drive;
				NormalizeDirPath(path);
				wcsncpy_s(node->path_wide, path.c_str(), _TRUNCATE);
				Utf8FromWide(path.c_str(), node->name_utf8, sizeof(node->name_utf8));
				strncpy_s(node->extension_utf8, "[磁碟]", _TRUNCATE);
				node->is_directory = true;
				node->load_state = LoadState::Unloaded;
				node->measure_status = FileMapScan::MeasureStatus::Ok;
			}
		}

		static void CollapseAndUnloadLocked(uint32_t node_id)
		{
			Node* node = NodePtr(node_id);
			if (node == nullptr) {
				return;
			}
			node->expanded = false;
			UnloadChildrenLocked(node_id);
		}

		static void AdvancePendingExpand()
		{
			if (g_pending_expand_path[0] == L'\0' || g_pending_walk_id == kInvalidId) {
				return;
			}

			const size_t path_len = wcslen(g_pending_expand_path);
			if (g_pending_pos >= path_len) {
				g_pending_expand_path[0] = L'\0';
				return;
			}

			uint32_t walk_id = kInvalidId;
			size_t next_pos = 0;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				Node* parent = NodePtr(g_pending_walk_id);
				if (parent == nullptr) {
					g_pending_expand_path[0] = L'\0';
					return;
				}
				if (parent->load_state == LoadState::Loading) {
					return;
				}
				if (parent->load_state != LoadState::Loaded) {
					return;
				}

				size_t end = g_pending_pos;
				while (end < path_len && g_pending_expand_path[end] != L'\\') {
					++end;
				}
				std::wstring seg_path(g_pending_expand_path, end);
				if (!seg_path.empty() && seg_path.back() != L'\\') {
					seg_path += L'\\';
				}

				const uint32_t child_id = FindChildByPath(g_pending_walk_id, seg_path.c_str());
				if (child_id == kInvalidId) {
					g_pending_expand_path[0] = L'\0';
					return;
				}

				if (Node* child = NodePtr(child_id)) {
					child->expanded = true;
					g_selected_id = child_id;
				}
				walk_id = child_id;
				next_pos = (end < path_len) ? end + 1 : path_len;
				g_pending_walk_id = child_id;
				g_pending_pos = next_pos;
				if (next_pos >= path_len) {
					g_pending_expand_path[0] = L'\0';
				}
			}

			if (walk_id != kInvalidId && next_pos < path_len) {
				EnqueueLoad(walk_id);
			}
		}
	}

	void Init()
	{
		if (g_init_done) {
			return;
		}
		g_init_done = true;
		g_shutdown.store(false, std::memory_order_release);
		size_t drive_count = 0;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			BuildDriveRoots();
			for (size_t i = 1; i < g_nodes.size(); ++i) {
				if (g_nodes[i].in_use && g_nodes[i].parent_id == kInvalidId) {
					++drive_count;
				}
			}
		}
		HLOG_INFO("FileMapTree: Init 固定磁碟根節點數={}", drive_count);
		g_loader_thread = std::thread(LoaderThreadMain);
	}

	void Shutdown()
	{
		HLOG_INFO("FileMapTree: Shutdown");
		g_shutdown.store(true, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(g_queue_mutex);
			while (!g_load_queue.empty()) {
				g_load_queue.pop();
			}
		}
		g_queue_cv.notify_all();
		if (g_loader_thread.joinable()) {
			g_loader_thread.join();
		}
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_nodes.clear();
			g_nodes.push_back(Node{});
		}
		g_init_done = false;
	}

	void Tick()
	{
		AdvancePendingExpand();
	}

	uint32_t GetSelectedNodeId()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_selected_id;
	}

	void SetSelectedNodeId(uint32_t node_id)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_selected_id = node_id;
	}

	bool CopyNode(uint32_t id, Node& out)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		const Node* node = NodePtr(id);
		if (node == nullptr) {
			return false;
		}
		out = *node;
		return true;
	}

	uint32_t FindNodeIdByPath(const wchar_t* path_wide)
	{
		if (path_wide == nullptr || path_wide[0] == L'\0') {
			return kInvalidId;
		}
		std::wstring path = path_wide;
		NormalizeDirPath(path);
		std::lock_guard<std::mutex> lock(g_mutex);
		for (size_t i = 1; i < g_nodes.size(); ++i) {
			if (g_nodes[i].in_use && PathEquals(g_nodes[i].path_wide, path.c_str())) {
				return static_cast<uint32_t>(i);
			}
		}
		return kInvalidId;
	}

	void SetFilterText(const char* utf8)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (utf8 == nullptr) {
			g_filter_text[0] = '\0';
			return;
		}
		strncpy_s(g_filter_text, utf8, _TRUNCATE);
	}

	void SetFilterMode(FilterMode mode)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_filter_mode = mode;
	}

	const char* GetFilterText()
	{
		return g_filter_text;
	}

	void SetExpanded(uint32_t node_id, bool expanded)
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			Node* node = NodePtr(node_id);
			if (node == nullptr) {
				return;
			}
			if (expanded) {
				node->expanded = true;
			}
			else {
				CollapseAndUnloadLocked(node_id);
			}
		}
		if (expanded) {
			RequestLoadChildren(node_id);
		}
	}

	void ToggleExpanded(uint32_t node_id)
	{
		bool expand = false;
		wchar_t path_wide[1024] = {};
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			Node* node = NodePtr(node_id);
			if (node == nullptr) {
				return;
			}
			if (node->expanded) {
				CollapseAndUnloadLocked(node_id);
				expand = false;
			}
			else {
				if (node->is_directory) {
					wcsncpy_s(path_wide, node->path_wide, _TRUNCATE);
				}
				node->expanded = true;
				expand = true;
			}
		}
		if (expand && path_wide[0] != L'\0') {
			const FileMapScan::ScopeListAccess access = FileMapScan::ProbeScopeListAccess(path_wide);
			if (!access.can_list) {
				HLOG_WARN("FileMapTree: 展開受阻 path='{}' — {}",
					access.path_utf8,
					access.headline_utf8[0] != '\0' ? access.headline_utf8 : "無法列出");
				std::lock_guard<std::mutex> lock(g_mutex);
				Node* node = NodePtr(node_id);
				if (node != nullptr) {
					node->measure_status = access.access_denied
						? FileMapScan::MeasureStatus::AccessDenied
						: FileMapScan::MeasureStatus::Failed;
					node->load_state = LoadState::Failed;
					node->child_ids.clear();
				}
				FileMapScan::NotifyScopeAccessBlocked(access);
				return;
			}
			RequestLoadChildren(node_id);
		}
		else if (expand) {
			RequestLoadChildren(node_id);
		}
	}

	void RequestLoadChildren(uint32_t node_id)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		Node* node = NodePtr(node_id);
		if (node == nullptr || !node->is_directory || !node->expanded) {
			return;
		}
		if (node->load_state == LoadState::Unloaded || node->load_state == LoadState::Failed) {
			EnqueueLoadLocked(node_id);
		}
	}

		static void ExpandAncestorsLocked(uint32_t node_id)
		{
			uint32_t cur = node_id;
			while (cur != kInvalidId) {
				Node* node = NodePtr(cur);
				if (node == nullptr) {
					break;
				}
				if (node->is_directory) {
					node->expanded = true;
				}
				cur = node->parent_id;
			}
		}

		static uint32_t FindNodeIdByPathLocked(const wchar_t* path_normalized)
		{
			if (path_normalized == nullptr || path_normalized[0] == L'\0') {
				return kInvalidId;
			}
			for (size_t i = 1; i < g_nodes.size(); ++i) {
				if (g_nodes[i].in_use && PathEquals(g_nodes[i].path_wide, path_normalized)) {
					return static_cast<uint32_t>(i);
				}
			}
			return kInvalidId;
		}

	void ExpandAndSelectPath(const wchar_t* scope_path_wide)
	{
		if (scope_path_wide == nullptr || scope_path_wide[0] == L'\0') {
			return;
		}

		std::wstring path = scope_path_wide;
		NormalizeDirPath(path);

		uint32_t drive_id = kInvalidId;
		uint32_t load_drive_id = kInvalidId;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			drive_id = FindDriveRootLocked(path.c_str());
			if (drive_id == kInvalidId) {
				return;
			}

			const uint32_t existing_id = FindNodeIdByPathLocked(path.c_str());
			if (existing_id != kInvalidId) {
				char path_utf8[1024] = {};
				Utf8FromWide(path.c_str(), path_utf8, sizeof(path_utf8));
				HLOG_DEBUG("FileMapTree: ExpandAndSelect 命中既有節點 path='{}' id={}",
					path_utf8, existing_id);
				ExpandAncestorsLocked(existing_id);
				g_selected_id = existing_id;
				g_pending_expand_path[0] = L'\0';
				return;
			}

			if (Node* drive = NodePtr(drive_id)) {
				drive->expanded = true;
			}

			g_selected_id = drive_id;
			wcsncpy_s(g_pending_expand_path, path.c_str(), _TRUNCATE);
			g_pending_walk_id = drive_id;
			g_pending_pos = (path.size() > 3) ? 3 : path.size();
			load_drive_id = drive_id;
		}

		{
			char path_utf8[1024] = {};
			Utf8FromWide(path.c_str(), path_utf8, sizeof(path_utf8));
			HLOG_INFO("FileMapTree: ExpandAndSelect 開始逐步展開 path='{}'", path_utf8);
		}

		if (load_drive_id != kInvalidId) {
			RequestLoadChildren(load_drive_id);
		}
	}

	static bool NodePassesFilterUnlocked(const Node& node)
	{
		if (g_filter_mode == FilterMode::FoldersOnly && !node.is_directory) {
			return false;
		}
		if (g_filter_mode == FilterMode::FilesOnly && node.is_directory) {
			return false;
		}
		if (g_filter_text[0] == '\0') {
			return true;
		}
		if (strstr(node.name_utf8, g_filter_text) != nullptr) {
			return true;
		}
		if (strstr(node.extension_utf8, g_filter_text) != nullptr) {
			return true;
		}
		char path_utf8[1024] = {};
		Utf8FromWide(node.path_wide, path_utf8, sizeof(path_utf8));
		return strstr(path_utf8, g_filter_text) != nullptr;
	}

	void BuildVisibleRows(std::vector<VisibleRow>& out_rows)
	{
		out_rows.clear();
		std::lock_guard<std::mutex> lock(g_mutex);

		std::function<void(uint32_t, int)> visit = [&](uint32_t node_id, int depth) {
			Node* node = NodePtr(node_id);
			if (node == nullptr) {
				return;
			}
			if (node->parent_id != kInvalidId && !NodePassesFilterUnlocked(*node)) {
				return;
			}

			VisibleRow row = {};
			row.node_id = node_id;
			row.depth = depth;
			row.has_children = node->is_directory;
			row.expanded = node->expanded;
			out_rows.push_back(row);

			if (!node->is_directory || !node->expanded) {
				return;
			}
			if (node->load_state == LoadState::Unloaded) {
				EnqueueLoadLocked(node_id);
			}
			if (node->load_state == LoadState::Loading) {
				VisibleRow loading = {};
				loading.node_id = kRowPlaceholderLoading;
				loading.placeholder_parent_id = node_id;
				loading.depth = depth + 1;
				out_rows.push_back(loading);
				return;
			}
			if (node->load_state == LoadState::Failed) {
				VisibleRow failed = {};
				failed.node_id = (node->measure_status == FileMapScan::MeasureStatus::AccessDenied)
					? kRowPlaceholderAccessDenied
					: kRowPlaceholderFailed;
				failed.placeholder_parent_id = node_id;
				failed.depth = depth + 1;
				out_rows.push_back(failed);
				return;
			}
			if (node->load_state != LoadState::Loaded) {
				return;
			}
			if (node->child_ids.empty()) {
				VisibleRow empty = {};
				empty.node_id = kRowPlaceholderEmpty;
				empty.placeholder_parent_id = node_id;
				empty.depth = depth + 1;
				out_rows.push_back(empty);
				return;
			}
			for (uint32_t child_id : node->child_ids) {
				visit(child_id, depth + 1);
			}
		};

		for (size_t i = 1; i < g_nodes.size(); ++i) {
			if (g_nodes[i].in_use && g_nodes[i].parent_id == kInvalidId) {
				visit(static_cast<uint32_t>(i), 0);
			}
		}
	}
}
