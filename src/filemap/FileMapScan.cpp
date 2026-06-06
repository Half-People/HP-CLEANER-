#include "FileMapScan.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Hi18n.h"

namespace FileMapScan {

	ScopeListAccess ProbeScopeListAccess(const wchar_t* scope_path_wide);
	void NotifyScopeAccessBlocked(const ScopeListAccess& info);
	void TagWindowsReservedEntry(const wchar_t* file_name, uint32_t file_attributes, bool is_directory,
		char* extension_utf8, size_t extension_utf8_size);

	namespace {
		constexpr int kMaxChildren = 256;
		constexpr size_t kMaxMeasureCache = 2048;
		constexpr size_t kMaxScopeSnapshotCache = 48;

		struct ScopeSnapshotCacheEntry {
			std::wstring scope_key;
			std::vector<ChildItem> children;
			uint64_t listing_fingerprint = 0;
			bool valid = false;
			bool failed = false;
			ScopeBlockReason scope_block = ScopeBlockReason::None;
			char scope_block_detail[512] = {};
		};

		constexpr uint64_t kDirMeasureCacheTtlMs = 45 * 1000;

		std::mutex g_mutex;
		std::vector<ChildItem> g_measure_cache;
		std::vector<ScopeSnapshotCacheEntry> g_scope_snapshot_cache;
		ScopeListAccess g_pending_access_popup = {};
		bool g_has_pending_access_popup = false;
		Snapshot g_snapshot;
		std::thread g_worker;
		std::atomic<bool> g_scanning{ false };
		std::atomic<bool> g_cancel{ false };
		std::atomic<bool> g_shutdown{ false };
		std::atomic<float> g_progress{ 0.f };
		int g_selected_index = -1;
		bool g_init_done = false;

		static bool Utf8FromWide(const wchar_t* wide, char* out, size_t out_size)
		{
			if (wide == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
			return n > 0;
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

		static bool PathsEqualWide(const wchar_t* a, const wchar_t* b)
		{
			return _wcsicmp(NormalizePathKey(a).c_str(), NormalizePathKey(b).c_str()) == 0;
		}

		static void SetStatus(const char* text, float progress)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			strncpy_s(g_snapshot.status_text, text, _TRUNCATE);
			g_snapshot.progress = progress;
			g_progress.store(progress, std::memory_order_release);
		}

		static void UpsertMeasureCacheUnlocked(const ChildItem& item)
		{
			for (ChildItem& cached : g_measure_cache) {
				if (PathsEqualWide(cached.full_path, item.full_path)) {
					cached = item;
					return;
				}
			}
			if (g_measure_cache.size() < kMaxMeasureCache) {
				g_measure_cache.push_back(item);
				return;
			}
			char path_utf8[1024] = {};
			Utf8FromWide(item.full_path, path_utf8, sizeof(path_utf8));
			HLOG_WARN("FileMapScan: 路徑測量快取已滿 ({} 筆)，略過新增 '{}'",
				g_measure_cache.size(), path_utf8);
		}

		static void UpsertMeasureCache(const ChildItem& item)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			UpsertMeasureCacheUnlocked(item);
		}

		static bool IsMeasuredStatus(MeasureStatus st)
		{
			return st == MeasureStatus::Ok || st == MeasureStatus::Partial
				|| st == MeasureStatus::AccessDenied || st == MeasureStatus::Failed;
		}

		static bool TryApplyMeasureCacheToItem(ChildItem& item);
		static bool ListScopeChildren(const std::wstring& scope, std::vector<ChildItem>& out, bool* capped);

		static void FillListingStampFromFindData(const WIN32_FIND_DATAW& fd, ChildItem& item)
		{
			ULARGE_INTEGER ft = {};
			ft.LowPart = fd.ftLastWriteTime.dwLowDateTime;
			ft.HighPart = fd.ftLastWriteTime.dwHighDateTime;
			item.stamp_mtime_utc = ft.QuadPart;

			ULARGE_INTEGER sz = {};
			sz.LowPart = fd.nFileSizeLow;
			sz.HighPart = fd.nFileSizeHigh;
			item.stamp_size_bytes = sz.QuadPart;
		}

		static bool RefreshListingStampFromPath(ChildItem& item)
		{
			WIN32_FILE_ATTRIBUTE_DATA fad = {};
			if (!GetFileAttributesExW(item.full_path, GetFileExInfoStandard, &fad)) {
				return false;
			}
			ULARGE_INTEGER ft = {};
			ft.LowPart = fad.ftLastWriteTime.dwLowDateTime;
			ft.HighPart = fad.ftLastWriteTime.dwHighDateTime;
			item.stamp_mtime_utc = ft.QuadPart;

			ULARGE_INTEGER sz = {};
			sz.LowPart = fad.nFileSizeLow;
			sz.HighPart = fad.nFileSizeHigh;
			item.stamp_size_bytes = sz.QuadPart;
			return true;
		}

		static bool IsMeasureCacheValid(const ChildItem& current, const ChildItem& cached)
		{
			if (current.stamp_mtime_utc == 0 || cached.stamp_mtime_utc == 0) {
				return false;
			}
			if (current.stamp_mtime_utc != cached.stamp_mtime_utc) {
				return false;
			}
			if (!current.is_directory) {
				return current.stamp_size_bytes == cached.stamp_size_bytes;
			}
			if (cached.measured_at_ms == 0) {
				return false;
			}
			const uint64_t now = GetTickCount64();
			return (now - cached.measured_at_ms) <= kDirMeasureCacheTtlMs;
		}

		static uint64_t ComputeListingFingerprint(const std::vector<ChildItem>& children)
		{
			uint64_t hash = 14695981039346656037ULL;
			auto mix = [&](uint64_t value) {
				hash ^= value;
				hash *= 1099511628211ULL;
			};

			std::vector<size_t> order;
			order.reserve(children.size());
			for (size_t i = 0; i < children.size(); ++i) {
				order.push_back(i);
			}
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
				return _wcsicmp(children[a].full_path, children[b].full_path) < 0;
			});

			mix(static_cast<uint64_t>(children.size()));
			for (size_t idx : order) {
				const ChildItem& item = children[idx];
				mix(item.is_directory ? 1ULL : 0ULL);
				mix(item.stamp_mtime_utc);
				mix(item.stamp_size_bytes);
				for (size_t i = 0; item.name_utf8[i] != '\0'; ++i) {
					mix(static_cast<unsigned char>(item.name_utf8[i]));
				}
			}
			return hash;
		}

		static bool TryApplyMeasureCacheToItem(ChildItem& item)
		{
			for (const ChildItem& cached : g_measure_cache) {
				if (!PathsEqualWide(cached.full_path, item.full_path)) {
					continue;
				}
				if (!IsMeasuredStatus(cached.measure_status)) {
					return false;
				}
				if (!IsMeasureCacheValid(item, cached)) {
					return false;
				}
				item.size_bytes = cached.size_bytes;
				item.item_count = cached.item_count;
				item.measure_status = cached.measure_status;
				item.measured_at_ms = cached.measured_at_ms;
				return true;
			}
			return false;
		}

		static int MergeMeasureCacheIntoChildren(std::vector<ChildItem>& children)
		{
			int merged = 0;
			for (ChildItem& item : children) {
				if (TryApplyMeasureCacheToItem(item)) {
					++merged;
				}
			}
			return merged;
		}

		static int FindScopeSnapshotCacheIndexUnlocked(const std::wstring& scope_key)
		{
			for (size_t i = 0; i < g_scope_snapshot_cache.size(); ++i) {
				if (_wcsicmp(g_scope_snapshot_cache[i].scope_key.c_str(), scope_key.c_str()) == 0) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		static void TouchScopeSnapshotCacheUnlocked(int index)
		{
			if (index < 0 || index >= static_cast<int>(g_scope_snapshot_cache.size())) {
				return;
			}
			ScopeSnapshotCacheEntry entry = std::move(g_scope_snapshot_cache[static_cast<size_t>(index)]);
			g_scope_snapshot_cache.erase(g_scope_snapshot_cache.begin() + index);
			g_scope_snapshot_cache.push_back(std::move(entry));
		}

		static void SaveScopeSnapshotCacheUnlocked(const Snapshot& snap)
		{
			if (snap.scope_path[0] == L'\0' || snap.scanning) {
				return;
			}
			const std::wstring key = NormalizePathKey(snap.scope_path);
			if (key.empty()) {
				return;
			}

			ScopeSnapshotCacheEntry entry = {};
			entry.scope_key = key;
			entry.children = snap.children;
			entry.listing_fingerprint = ComputeListingFingerprint(snap.children);
			entry.valid = snap.valid;
			entry.failed = snap.failed;
			entry.scope_block = snap.scope_block;
			strncpy_s(entry.scope_block_detail, snap.scope_block_detail, _TRUNCATE);

			char scope_utf8[1024] = {};
			Utf8FromWide(snap.scope_path, scope_utf8, sizeof(scope_utf8));

			const int existing = FindScopeSnapshotCacheIndexUnlocked(key);
			if (existing >= 0) {
				g_scope_snapshot_cache[static_cast<size_t>(existing)] = std::move(entry);
				TouchScopeSnapshotCacheUnlocked(existing);
				HLOG_INFO("FileMapScan: 更新目錄快照快取 scope='{}' items={} slots={}/{}",
					scope_utf8, snap.children.size(), g_scope_snapshot_cache.size(), kMaxScopeSnapshotCache);
				return;
			}

			g_scope_snapshot_cache.push_back(std::move(entry));
			while (g_scope_snapshot_cache.size() > kMaxScopeSnapshotCache) {
				char evicted_utf8[1024] = {};
				const std::wstring& evicted_key = g_scope_snapshot_cache.front().scope_key;
				WideCharToMultiByte(CP_UTF8, 0, evicted_key.c_str(), -1, evicted_utf8,
					static_cast<int>(sizeof(evicted_utf8)), nullptr, nullptr);
				HLOG_INFO("FileMapScan: 目錄快照快取 LRU 淘汰 scope='{}'", evicted_utf8);
				g_scope_snapshot_cache.erase(g_scope_snapshot_cache.begin());
			}
			HLOG_INFO("FileMapScan: 新增目錄快照快取 scope='{}' items={} slots={}/{}",
				scope_utf8, snap.children.size(), g_scope_snapshot_cache.size(), kMaxScopeSnapshotCache);
		}

		static bool RestoreScopeSnapshotCacheUnlocked(const std::wstring& scope)
		{
			const std::wstring key = NormalizePathKey(scope.c_str());
			const int index = FindScopeSnapshotCacheIndexUnlocked(key);
			if (index < 0) {
				return false;
			}

			const ScopeSnapshotCacheEntry& entry = g_scope_snapshot_cache[static_cast<size_t>(index)];

			std::vector<ChildItem> live_listing;
			bool listing_capped = false;
			if (!ListScopeChildren(scope, live_listing, &listing_capped)) {
				return false;
			}
			const uint64_t live_fp = ComputeListingFingerprint(live_listing);
			char scope_utf8[1024] = {};
			Utf8FromWide(scope.c_str(), scope_utf8, sizeof(scope_utf8));

			if (live_fp != entry.listing_fingerprint || listing_capped) {
				HLOG_INFO("FileMapScan: 目錄快照已過期 scope='{}' live_fp={} cached_fp={} capped={}",
					scope_utf8, live_fp, entry.listing_fingerprint, listing_capped);
				return false;
			}

			std::vector<ChildItem> children = live_listing;
			for (ChildItem& item : children) {
				if (!TryApplyMeasureCacheToItem(item)) {
					HLOG_INFO("FileMapScan: 測量快取已過期 scope='{}' path='{}'",
						scope_utf8, item.name_utf8);
					return false;
				}
			}

			g_snapshot = {};
			wcsncpy_s(g_snapshot.scope_path, scope.c_str(), _TRUNCATE);
			Utf8FromWide(g_snapshot.scope_path, g_snapshot.scope_utf8, sizeof(g_snapshot.scope_utf8));
			g_snapshot.children = std::move(children);
			HLOG_INFO("FileMapScan: 還原目錄快照 scope='{}' items={}",
				g_snapshot.scope_utf8, g_snapshot.children.size());
			g_snapshot.valid = entry.valid || !g_snapshot.children.empty();
			g_snapshot.failed = entry.failed;
			g_snapshot.scope_block = entry.scope_block;
			strncpy_s(g_snapshot.scope_block_detail, entry.scope_block_detail, _TRUNCATE);
			g_snapshot.scanning = false;
			g_snapshot.progress = 1.f;
			g_snapshot.selected_index = g_selected_index;
			if (g_selected_index >= static_cast<int>(g_snapshot.children.size())) {
				g_selected_index = -1;
				g_snapshot.selected_index = -1;
			}
			snprintf(g_snapshot.status_text, sizeof(g_snapshot.status_text),
				I18N(u8"已從快取載入：%zu 個項目"), g_snapshot.children.size());
			TouchScopeSnapshotCacheUnlocked(index);
			return true;
		}

		static void PublishChildren(const std::vector<ChildItem>& children, bool scanning, float progress)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot.children = children;
			g_snapshot.scanning = scanning;
			g_snapshot.progress = progress;
			g_progress.store(progress, std::memory_order_release);
			g_snapshot.selected_index = g_selected_index;
			for (const ChildItem& item : children) {
				if (item.measure_status != MeasureStatus::Pending) {
					UpsertMeasureCacheUnlocked(item);
				}
			}
		}

		static bool ShouldSkipName(const wchar_t* name)
		{
			if (name == nullptr || name[0] == L'\0') {
				return true;
			}
			return wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
		}

		static bool DirectoryHasAnyEntry(const wchar_t* path)
		{
			if (path == nullptr || path[0] == L'\0') {
				return false;
			}
			std::wstring pattern = path;
			if (!pattern.empty() && pattern.back() != L'\\') {
				pattern += L'\\';
			}
			pattern += L'*';

			WIN32_FIND_DATAW fd = {};
			const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
			if (find == INVALID_HANDLE_VALUE) {
				return false;
			}
			bool any = false;
			do {
				if (!ShouldSkipName(fd.cFileName)) {
					any = true;
					break;
				}
			} while (FindNextFileW(find, &fd));
			FindClose(find);
			return any;
		}

		static bool NameEqualsInsensitive(const wchar_t* name, const wchar_t* known)
		{
			return name != nullptr && known != nullptr && _wcsicmp(name, known) == 0;
		}

		static bool IsKnownWindowsReservedFolderName(const wchar_t* name)
		{
			if (name == nullptr || name[0] == L'\0') {
				return false;
			}
			static const wchar_t* kReserved[] = {
				L"Documents and Settings",
				L"System Volume Information",
				L"$Recycle.Bin",
				L"$WinREAgent",
				L"Recovery",
				L"Config.Msi",
				L"MSOCache",
				L"All Users",
				L"Default User",
				L"Default",
				nullptr,
			};
			for (size_t i = 0; kReserved[i] != nullptr; ++i) {
				if (NameEqualsInsensitive(name, kReserved[i])) {
					return true;
				}
			}
			return false;
		}

		static void ExtractExtension(const wchar_t* name, char* out_ext, size_t out_ext_size)
		{
			if (out_ext == nullptr || out_ext_size == 0) {
				return;
			}
			out_ext[0] = '\0';
			if (name == nullptr) {
				return;
			}
			const wchar_t* dot = wcsrchr(name, L'.');
			if (dot == nullptr || dot[1] == L'\0') {
				strncpy_s(out_ext, out_ext_size, u8"(無)", _TRUNCATE);
				return;
			}
			WideCharToMultiByte(CP_UTF8, 0, dot, -1, out_ext, static_cast<int>(out_ext_size), nullptr, nullptr);
		}

		static bool ListScopeChildren(const std::wstring& scope, std::vector<ChildItem>& out, bool* capped)
		{
			out.clear();
			if (capped != nullptr) {
				*capped = false;
			}

			std::wstring pattern = scope;
			pattern += L'*';

			WIN32_FIND_DATAW fd = {};
			const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
			if (find == INVALID_HANDLE_VALUE) {
				return false;
			}

			do {
				if (out.size() >= static_cast<size_t>(kMaxChildren)) {
					if (capped != nullptr) {
						*capped = true;
					}
					break;
				}
				if (ShouldSkipName(fd.cFileName)) {
					continue;
				}

				ChildItem item = {};
				std::wstring full = scope;
				full += fd.cFileName;
				const bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				if (is_dir) {
					full += L'\\';
				}
				wcsncpy_s(item.full_path, full.c_str(), _TRUNCATE);
				Utf8FromWide(fd.cFileName, item.name_utf8, sizeof(item.name_utf8));
				item.is_directory = is_dir;
				FillListingStampFromFindData(fd, item);

				if (item.is_directory) {
					strncpy_s(item.extension_utf8, u8"[資料夾]", _TRUNCATE);
					TagWindowsReservedEntry(fd.cFileName, fd.dwFileAttributes, true,
						item.extension_utf8, sizeof(item.extension_utf8));
					item.measure_status = MeasureStatus::Pending;
					item.item_count = 1;
				}
				else {
					ExtractExtension(fd.cFileName, item.extension_utf8, sizeof(item.extension_utf8));
					item.size_bytes = item.stamp_size_bytes;
					item.item_count = 1;
					item.measure_status = MeasureStatus::Pending;
				}

				out.push_back(item);
			} while (FindNextFileW(find, &fd));

			FindClose(find);
			return true;
		}

		static void NormalizeScope(std::wstring& path)
		{
			if (path.empty()) {
				path = L"C:\\";
				return;
			}
			if (path.back() != L'\\') {
				path += L'\\';
			}
		}

		static bool IsAccessDeniedError(DWORD err)
		{
			return err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD;
		}

		static void FormatWin32ErrorUtf8(DWORD err, char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}
			out[0] = '\0';
			if (err == 0) {
				return;
			}
			wchar_t* msg = nullptr;
			const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
				| FORMAT_MESSAGE_IGNORE_INSERTS;
			const DWORD len = FormatMessageW(flags, nullptr, err, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
			if (len == 0 || msg == nullptr) {
				snprintf(out, out_size, I18N(u8"Win32 錯誤碼 %lu"), static_cast<unsigned long>(err));
				return;
			}
			WideCharToMultiByte(CP_UTF8, 0, msg, -1, out, static_cast<int>(out_size), nullptr, nullptr);
			LocalFree(msg);
			for (size_t i = 0; i < out_size && out[i] != '\0'; ++i) {
				if (out[i] == '\r' || out[i] == '\n') {
					out[i] = ' ';
				}
			}
		}

		static void FillScopeListAccessFailure(ScopeListAccess& out, const std::wstring& scope,
			DWORD err, bool access_denied, const char* headline)
		{
			out.can_list = false;
			out.access_denied = access_denied;
			out.win32_error = static_cast<uint32_t>(err);
			Utf8FromWide(scope.c_str(), out.path_utf8, sizeof(out.path_utf8));
			strncpy_s(out.headline_utf8, headline, _TRUNCATE);
			if (access_denied) {
				snprintf(out.detail_utf8, sizeof(out.detail_utf8),
					I18N(u8"Windows 拒絕列出此資料夾的內容。您沒有足夠權限，或該資料夾受系統保護。\n"
						u8"Treemap 無法顯示此路徑的占比圖。"));
			}
			else {
				snprintf(out.detail_utf8, sizeof(out.detail_utf8),
					I18N(u8"無法開啟此資料夾以列出檔案與子資料夾。"));
			}
			char sys_msg[256] = {};
			FormatWin32ErrorUtf8(err, sys_msg, sizeof(sys_msg));
			if (sys_msg[0] != '\0') {
				strncat_s(out.detail_utf8, I18N(u8"\n\n系統訊息："), _TRUNCATE);
				strncat_s(out.detail_utf8, sys_msg, _TRUNCATE);
			}
		}

		static void MeasureDirectoryItem(ChildItem& item)
		{
			const int64_t top_level = HCleanMeasureDirTopLevelFiles(item.full_path);
			const int64_t deep = HCleanMeasurePathBytes(item.full_path, &kHCleanLargeWalkLimits, "filemap");

			if (deep < 0) {
				item.size_bytes = 0;
				item.measure_status = MeasureStatus::AccessDenied;
				return;
			}

			if (deep > 0) {
				item.size_bytes = static_cast<uint64_t>(deep);
				item.measure_status = MeasureStatus::Ok;
				return;
			}

			if (top_level > 0) {
				item.size_bytes = static_cast<uint64_t>(top_level);
				item.measure_status = MeasureStatus::Partial;
				return;
			}

			if (DirectoryHasAnyEntry(item.full_path)) {
				item.size_bytes = 0;
				item.measure_status = MeasureStatus::Partial;
				return;
			}

			item.size_bytes = 0;
			item.measure_status = MeasureStatus::Ok;
		}

		static void MeasureChildItem(ChildItem& item)
		{
			if (g_cancel.load(std::memory_order_relaxed)) {
				return;
			}

			item.measure_status = MeasureStatus::Measuring;

			if (item.is_directory) {
				MeasureDirectoryItem(item);
				if (item.item_count == 0) {
					item.item_count = 1;
				}
				(void)RefreshListingStampFromPath(item);
				item.measured_at_ms = GetTickCount64();
				return;
			}

			if (item.size_bytes > 0) {
				item.measure_status = MeasureStatus::Ok;
				(void)RefreshListingStampFromPath(item);
				item.measured_at_ms = GetTickCount64();
				return;
			}

			const int64_t measured = HCleanMeasurePathBytes(item.full_path, nullptr, "filemap");
			if (measured < 0) {
				item.size_bytes = 0;
				item.measure_status = MeasureStatus::AccessDenied;
				return;
			}
			item.size_bytes = static_cast<uint64_t>(measured);
			item.measure_status = MeasureStatus::Ok;
			item.item_count = 1;
			(void)RefreshListingStampFromPath(item);
			item.measured_at_ms = GetTickCount64();
		}

		static void RunScan(const std::wstring scope, bool force_refresh)
		{
			char scope_utf8[1024] = {};
			Utf8FromWide(scope.c_str(), scope_utf8, sizeof(scope_utf8));
			HLOG_INFO("FileMapScan: RunScan 開始 scope='{}' force_refresh={}", scope_utf8, force_refresh);

			g_scanning.store(true, std::memory_order_release);
			g_cancel.store(false, std::memory_order_release);
			SetStatus(I18N(u8"列出項目…"), 0.02f);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot = {};
				g_snapshot.scanning = true;
				wcsncpy_s(g_snapshot.scope_path, scope.c_str(), _TRUNCATE);
				Utf8FromWide(g_snapshot.scope_path, g_snapshot.scope_utf8, sizeof(g_snapshot.scope_utf8));
			}

			std::vector<ChildItem> collected;
			collected.reserve(64);

			const ScopeListAccess list_access = ProbeScopeListAccess(scope.c_str());
			if (!list_access.can_list) {
				HLOG_INFO("FileMapScan: 列出失敗 scope='{}' — {}",
					list_access.path_utf8,
					list_access.headline_utf8[0] != '\0' ? list_access.headline_utf8 : I18N(u8"無法列出"));
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.failed = true;
				g_snapshot.scanning = false;
				g_snapshot.children.clear();
				g_snapshot.scope_block = list_access.access_denied
					? ScopeBlockReason::AccessDenied : ScopeBlockReason::ListError;
				strncpy_s(g_snapshot.scope_block_detail, list_access.detail_utf8, _TRUNCATE);
				strncpy_s(g_snapshot.status_text, list_access.headline_utf8, _TRUNCATE);
				g_scanning.store(false, std::memory_order_release);
				NotifyScopeAccessBlocked(list_access);
				return;
			}

			bool list_capped = false;
			if (!ListScopeChildren(scope, collected, &list_capped)) {
				const DWORD err = GetLastError();
				const bool denied = IsAccessDeniedError(err);
				ScopeListAccess fail = {};
				FillScopeListAccessFailure(fail, scope, err, denied,
					denied ? I18N(u8"無權限存取此資料夾") : I18N(u8"無法讀取此資料夾"));
				HLOG_WARN("FileMapScan: ListScopeChildren 失敗 scope='{}' err={} denied={}",
					fail.path_utf8, static_cast<unsigned long>(err), denied);
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.failed = true;
				g_snapshot.scanning = false;
				g_snapshot.children.clear();
				g_snapshot.scope_block = denied ? ScopeBlockReason::AccessDenied : ScopeBlockReason::ListError;
				strncpy_s(g_snapshot.scope_block_detail, fail.detail_utf8, _TRUNCATE);
				strncpy_s(g_snapshot.status_text, fail.headline_utf8, _TRUNCATE);
				g_scanning.store(false, std::memory_order_release);
				NotifyScopeAccessBlocked(fail);
				return;
			}

			list_capped = list_capped || collected.size() >= static_cast<size_t>(kMaxChildren);
			HLOG_INFO("FileMapScan: 列出完成 scope='{}' children={}{}",
				scope_utf8, collected.size(), list_capped ? I18N(u8" (已達上限)") : "");

			PublishChildren(collected, true, 0.08f);

			const size_t total = collected.size();
			size_t measure_cache_hits = 0;
			size_t measure_fresh = 0;
			for (size_t i = 0; i < total; ++i) {
				if (g_cancel.load(std::memory_order_relaxed)) {
					HLOG_INFO("FileMapScan: RunScan 已取消 scope='{}' at {}/{}", scope_utf8, i, total);
					break;
				}

				ChildItem& item = collected[i];
				char status[160] = {};
				snprintf(status, sizeof(status), I18N(u8"測量：%s (%zu/%zu)"), item.name_utf8, i + 1, total);
				const float progress = 0.08f + 0.9f * static_cast<float>(i + 1) / static_cast<float>(total > 0 ? total : 1);
				SetStatus(status, progress);

				if (!force_refresh && TryApplyMeasureCacheToItem(item)) {
					++measure_cache_hits;
					UpsertMeasureCache(item);
				}
				else {
					++measure_fresh;
					MeasureChildItem(item);
					UpsertMeasureCache(item);
				}
				PublishChildren(collected, true, progress);
			}

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.children = std::move(collected);
				g_snapshot.valid = !g_snapshot.children.empty();
				g_snapshot.scanning = false;
				g_snapshot.scope_block = ScopeBlockReason::None;
				g_snapshot.scope_block_detail[0] = '\0';
				g_snapshot.failed = g_snapshot.children.empty();
				g_snapshot.progress = 1.f;
				if (g_snapshot.children.empty()) {
					strncpy_s(g_snapshot.status_text, u8"此資料夾為空（無子項目）", _TRUNCATE);
				}
				else {
					snprintf(g_snapshot.status_text, sizeof(g_snapshot.status_text),
						I18N(u8"完成：%zu 個項目"), g_snapshot.children.size());
				}
				if (g_selected_index >= static_cast<int>(g_snapshot.children.size())) {
					g_selected_index = -1;
				}
				g_snapshot.selected_index = g_selected_index;
				SaveScopeSnapshotCacheUnlocked(g_snapshot);
			}

			g_scanning.store(false, std::memory_order_release);
			HLOG_INFO("FileMapScan: RunScan 完成 scope='{}' items={} measure_hit={} measure_new={} measure_cache={}",
				g_snapshot.scope_utf8, g_snapshot.children.size(), measure_cache_hits, measure_fresh,
				g_measure_cache.size());
		}

		static void StartWorker(std::wstring scope, bool force_refresh)
		{
			if (g_shutdown.load(std::memory_order_acquire)) {
				HLOG_WARN("FileMapScan: StartWorker 略過（已 Shutdown）");
				return;
			}
			if (g_worker.joinable()) {
				HLOG_INFO("FileMapScan: 取消進行中的掃描以啟動新 scope");
				g_cancel.store(true, std::memory_order_release);
				g_worker.join();
				g_cancel.store(false, std::memory_order_release);
			}
			g_worker = std::thread([scope, force_refresh]() { RunScan(scope, force_refresh); });
		}
	}

	void Init()
	{
		if (g_init_done) {
			return;
		}
		g_init_done = true;
		g_shutdown.store(false, std::memory_order_release);
		std::wstring scope = L"C:\\";
		NormalizeScope(scope);
		HLOG_INFO("FileMapScan: Init 預設掃描 C:\\");
		StartWorker(scope, false);
	}

	void Shutdown()
	{
		size_t measure_entries = 0;
		size_t scope_entries = 0;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			measure_entries = g_measure_cache.size();
			scope_entries = g_scope_snapshot_cache.size();
		}
		HLOG_INFO("FileMapScan: Shutdown（measure_cache={} scope_cache={}）",
			measure_entries, scope_entries);
		g_shutdown.store(true, std::memory_order_release);
		g_cancel.store(true, std::memory_order_release);
		if (g_worker.joinable()) {
			g_worker.join();
		}
		g_scanning.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_measure_cache.clear();
			g_scope_snapshot_cache.clear();
			g_has_pending_access_popup = false;
		}
	}

	void TagWindowsReservedEntry(const wchar_t* file_name, uint32_t file_attributes, bool is_directory,
		char* extension_utf8, size_t extension_utf8_size)
	{
		if (!is_directory || extension_utf8 == nullptr || extension_utf8_size == 0) {
			return;
		}
		if (IsKnownWindowsReservedFolderName(file_name)) {
			strncpy_s(extension_utf8, extension_utf8_size, u8"[Win保留]", _TRUNCATE);
			return;
		}
		if (file_name != nullptr
			&& (_wcsicmp(file_name, L"Application Data") == 0
				|| _wcsicmp(file_name, L"All Users") == 0)) {
			strncpy_s(extension_utf8, extension_utf8_size, "[→ProgramData]", _TRUNCATE);
			return;
		}
		if ((file_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
			&& file_name != nullptr && file_name[0] == L'$') {
			strncpy_s(extension_utf8, extension_utf8_size, u8"[Win保留]", _TRUNCATE);
		}
	}

	bool TryLookupMeasured(const wchar_t* path_wide, ChildItem* out)
	{
		if (path_wide == nullptr || path_wide[0] == L'\0' || out == nullptr) {
			return false;
		}
		std::lock_guard<std::mutex> lock(g_mutex);
		for (const ChildItem& item : g_snapshot.children) {
			if (PathsEqualWide(item.full_path, path_wide)) {
				*out = item;
				return true;
			}
		}
		for (const ChildItem& item : g_measure_cache) {
			if (PathsEqualWide(item.full_path, path_wide)) {
				*out = item;
				return true;
			}
		}
		return false;
	}

	ScopeListAccess ProbeScopeListAccess(const wchar_t* scope_path_wide)
	{
		ScopeListAccess out = {};
		if (scope_path_wide == nullptr || scope_path_wide[0] == L'\0') {
			FillScopeListAccessFailure(out, L"", ERROR_PATH_NOT_FOUND, false, I18N(u8"路徑無效"));
			return out;
		}

		std::wstring scope = scope_path_wide;
		NormalizeScope(scope);
		Utf8FromWide(scope.c_str(), out.path_utf8, sizeof(out.path_utf8));

		const DWORD attrs = GetFileAttributesW(scope.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			const DWORD err = GetLastError();
			const bool denied = IsAccessDeniedError(err);
			FillScopeListAccessFailure(out, scope, err, denied,
				denied ? I18N(u8"無權限存取此資料夾") : I18N(u8"找不到此資料夾"));
			HLOG_DEBUG("FileMapScan: ProbeScope GetFileAttributes 失敗 path='{}' err={}",
				out.path_utf8, static_cast<unsigned long>(err));
			return out;
		}
		if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			FillScopeListAccessFailure(out, scope, ERROR_DIRECTORY, false, I18N(u8"路徑不是資料夾"));
			HLOG_DEBUG("FileMapScan: ProbeScope 非資料夾 path='{}'", out.path_utf8);
			return out;
		}

		std::wstring pattern = scope;
		pattern += L'*';
		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find != INVALID_HANDLE_VALUE) {
			FindClose(find);
			out.can_list = true;
			return out;
		}

		const DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			out.can_list = true;
			return out;
		}

		const bool denied = IsAccessDeniedError(err);
		FillScopeListAccessFailure(out, scope, err, denied,
			denied ? I18N(u8"無權限存取此資料夾") : I18N(u8"無法讀取此資料夾"));
		HLOG_DEBUG("FileMapScan: ProbeScope 無法試列 path='{}' err={}", out.path_utf8,
			static_cast<unsigned long>(err));
		return out;
	}

	bool ConsumeScopeAccessPopup(ScopeListAccess* out)
	{
		if (out == nullptr) {
			return false;
		}
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_has_pending_access_popup) {
			return false;
		}
		*out = g_pending_access_popup;
		g_has_pending_access_popup = false;
		return true;
	}

	bool RequestScanScope(const wchar_t* scope_path_wide, bool force_refresh)
	{
		if (scope_path_wide == nullptr || scope_path_wide[0] == L'\0') {
			HLOG_INFO("FileMapScan: RequestScanScope 拒絕（空路徑）");
			return false;
		}
		const ScopeListAccess access = ProbeScopeListAccess(scope_path_wide);
		if (!access.can_list) {
			HLOG_INFO("FileMapScan: 無法進入 scope='{}' — {}",
				access.path_utf8,
				access.headline_utf8[0] != '\0' ? access.headline_utf8 : I18N(u8"無法列出目錄"));
			NotifyScopeAccessBlocked(access);
			return false;
		}
		std::wstring scope = scope_path_wide;
		NormalizeScope(scope);
		char scope_utf8[1024] = {};
		WideCharToMultiByte(CP_UTF8, 0, scope.c_str(), -1, scope_utf8, static_cast<int>(sizeof(scope_utf8)),
			nullptr, nullptr);

		if (!force_refresh) {
			if (g_scanning.load(std::memory_order_acquire)) {
				std::lock_guard<std::mutex> lock(g_mutex);
				if (PathsEqualWide(g_snapshot.scope_path, scope.c_str())) {
					HLOG_INFO("FileMapScan: scope='{}' 已在掃描中，略過重複請求", scope_utf8);
					return true;
				}
			}
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (RestoreScopeSnapshotCacheUnlocked(scope)) {
					return true;
				}
				HLOG_DEBUG("FileMapScan: 目錄快照快取未命中 scope='{}' slots={}",
					scope_utf8, g_scope_snapshot_cache.size());
			}
		}
		else {
			HLOG_INFO("FileMapScan: 強制重新掃描 scope='{}'", scope_utf8);
		}

		HLOG_INFO("FileMapScan: 啟動背景掃描 scope='{}' force_refresh={}", scope_utf8, force_refresh);
		g_selected_index = -1;
		StartWorker(scope, force_refresh);
		return true;
	}

	void RequestScanDrive(wchar_t drive_letter)
	{
		wchar_t scope[8] = {};
		_snwprintf_s(scope, _TRUNCATE, L"%c:\\", drive_letter);
		HLOG_INFO("FileMapScan: RequestScanDrive '{}'", static_cast<char>(drive_letter));
		(void)RequestScanScope(scope);
	}

	void NotifyScopeAccessBlocked(const ScopeListAccess& info)
	{
		HLOG_WARN("FileMapScan: 存取受阻 path='{}' denied={} err={} — {}",
			info.path_utf8, info.access_denied, info.win32_error,
			info.headline_utf8[0] != '\0' ? info.headline_utf8 : "(no headline)");
		std::lock_guard<std::mutex> lock(g_mutex);
		g_pending_access_popup = info;
		g_has_pending_access_popup = true;
	}

	std::vector<std::wstring> ListFixedDrives()
	{
		std::vector<std::wstring> drives;
		const DWORD mask = GetLogicalDrives();
		for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
			const DWORD bit = 1u << (letter - L'A');
			if ((mask & bit) == 0) {
				continue;
			}
			std::wstring root = std::wstring(1, letter) + L":\\";
			if (GetDriveTypeW(root.c_str()) == DRIVE_FIXED) {
				drives.push_back(root);
			}
		}
		if (drives.empty()) {
			drives.push_back(L"C:\\");
		}
		return drives;
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		Snapshot copy = g_snapshot;
		copy.selected_index = g_selected_index;
		if (g_scanning.load(std::memory_order_acquire)) {
			copy.scanning = true;
			copy.progress = g_progress.load(std::memory_order_acquire);
		}
		return copy;
	}

	int GetSelectedIndex()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_selected_index;
	}

	void SetSelectedIndex(int index)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_selected_index = index;
		g_snapshot.selected_index = index;
	}

	const wchar_t* GetScopePathWide()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_snapshot.scope_path;
	}
}