#include "HCleanTaskCommon.h"
#include "Hi18n.h"
#include "HAppShell.h"
#include "HElevationBroker.h"
#include "HAdminPrompt.h"
#include "HPage.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <algorithm>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <tlhelp32.h>

namespace {
	const HCleanWalkLimits kDefaultLimitsInternal{ 48, 50000, 45.0 };

	// 遞迴遍歷整棵目錄樹（僅統計/刪除檔案，不刪子資料夾）；上限避免掃描卡死 UI

	bool ShouldSkipDirName(const wchar_t* name)
	{
		if (name == nullptr || name[0] == L'\0') {
			return true;
		}
		if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
			return true;
		}
		return false;
	}

	bool ResolveJunctionTarget(const std::wstring& path, std::wstring& out_target)
	{
		out_target.clear();
		const HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			return false;
		}

		wchar_t final_path[MAX_PATH * 4] = {};
		const DWORD len = GetFinalPathNameByHandleW(h, final_path,
			static_cast<DWORD>(sizeof(final_path) / sizeof(final_path[0])), FILE_NAME_NORMALIZED);
		CloseHandle(h);
		if (len == 0 || len >= sizeof(final_path) / sizeof(final_path[0])) {
			return false;
		}

		if (len >= 4 && final_path[0] == L'\\' && final_path[1] == L'\\'
			&& final_path[2] == L'?' && final_path[3] == L'\\') {
			out_target.assign(final_path + 4, len - 4);
		}
		else {
			out_target.assign(final_path, len);
		}
		return !out_target.empty() && _wcsicmp(out_target.c_str(), path.c_str()) != 0;
	}

	static void NormalizePathSlashes(std::wstring& path)
	{
		for (wchar_t& ch : path) {
			if (ch == L'/') {
				ch = L'\\';
			}
		}
	}

	static void StripTrailingDirSlash(std::wstring& path)
	{
		while (path.size() > 3 && path.back() == L'\\') {
			path.pop_back();
		}
	}

	static std::wstring PathKeyForVisit(const std::wstring& path)
	{
		std::wstring key = path;
		NormalizePathSlashes(key);
		StripTrailingDirSlash(key);
		return key;
	}

	static bool CanonicalizeLegacyPathForMeasure(std::wstring& path)
	{
		std::wstring norm = path;
		NormalizePathSlashes(norm);
		StripTrailingDirSlash(norm);

		wchar_t programdata[MAX_PATH * 4] = {};
		if (ExpandEnvironmentStringsW(L"%ProgramData%", programdata,
				static_cast<DWORD>(sizeof(programdata) / sizeof(programdata[0]))) == 0
			|| programdata[0] == L'\0') {
			return false;
		}
		std::wstring canonical = programdata;
		NormalizePathSlashes(canonical);
		StripTrailingDirSlash(canonical);

		static const wchar_t* k_legacy_roots[] = {
			L"C:\\Users\\All Users\\Application Data",
			L"C:\\Users\\All Users",
			L"C:\\Documents and Settings\\All Users\\Application Data",
			L"C:\\Documents and Settings\\All Users",
		};
		for (const wchar_t* legacy : k_legacy_roots) {
			if (_wcsicmp(norm.c_str(), legacy) == 0) {
				path = canonical + L'\\';
				return true;
			}
		}
		return false;
	}

	std::wstring ResolveTraversalRoot(const std::wstring& path)
	{
		std::wstring working = path;
		if (CanonicalizeLegacyPathForMeasure(working)) {
			return working;
		}
		const DWORD attrs = GetFileAttributesW(path.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
			return path;
		}
		std::wstring resolved;
		if (ResolveJunctionTarget(path, resolved)) {
			CanonicalizeLegacyPathForMeasure(resolved);
			return resolved;
		}
		return path;
	}

	bool IsDirectoryListingAccessDenied(const std::wstring& path)
	{
		std::wstring pattern = path;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';
		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find != INVALID_HANDLE_VALUE) {
			FindClose(find);
			return false;
		}
		return GetLastError() == ERROR_ACCESS_DENIED;
	}

	uint64_t DirSizeShallowImpl(const std::wstring& path, int depth, int& files_seen,
		const std::chrono::steady_clock::time_point& deadline, const HCleanWalkLimits& limits,
		std::atomic<int>* progress_files = nullptr)
	{
		if (depth > limits.max_depth || files_seen >= limits.max_files) {
			return 0;
		}
		if (std::chrono::steady_clock::now() >= deadline) {
			return 0;
		}

		thread_local std::set<std::wstring> visited_roots;
		if (depth == 0) {
			visited_roots.clear();
		}

		const std::wstring traverse_root = ResolveTraversalRoot(path);
		const std::wstring visit_key = PathKeyForVisit(traverse_root);
		if (!visit_key.empty()) {
			if (visited_roots.count(visit_key) != 0) {
				return 0;
			}
			visited_roots.insert(visit_key);
		}

		std::wstring pattern = traverse_root;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return 0;
		}

		uint64_t total = 0;
		do {
			if (files_seen >= limits.max_files
				|| std::chrono::steady_clock::now() >= deadline) {
				break;
			}

			const wchar_t* name = fd.cFileName;
			if (ShouldSkipDirName(name)) {
				continue;
			}

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				std::wstring sub = traverse_root;
				if (!sub.empty() && sub.back() != L'\\') {
					sub += L'\\';
				}
				sub += name;
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
					std::wstring junction_target;
					if (ResolveJunctionTarget(sub, junction_target)) {
						CanonicalizeLegacyPathForMeasure(junction_target);
						const std::wstring jkey = PathKeyForVisit(
							ResolveTraversalRoot(junction_target));
						if (!jkey.empty() && visited_roots.count(jkey) != 0) {
							continue;
						}
						if (!jkey.empty()) {
							visited_roots.insert(jkey);
						}
						total += DirSizeShallowImpl(junction_target, depth + 1, files_seen, deadline, limits,
							progress_files);
					}
					continue;
				}
				total += DirSizeShallowImpl(sub, depth + 1, files_seen, deadline, limits, progress_files);
			}
			else {
				++files_seen;
				if (progress_files != nullptr && (files_seen & 127) == 0) {
					progress_files->store(files_seen, std::memory_order_relaxed);
				}
				ULARGE_INTEGER file_size;
				file_size.LowPart = fd.nFileSizeLow;
				file_size.HighPart = fd.nFileSizeHigh;
				total += file_size.QuadPart;
			}
		} while (FindNextFileW(find, &fd));

		FindClose(find);
		return total;
	}

	int64_t DeleteFilesDirShallowImpl(const std::wstring& path, int depth, int& files_seen,
		const std::chrono::steady_clock::time_point& deadline, const char* log_context,
		HCleanDeleteStats* stats, const HCleanWalkLimits& limits, bool* timed_out)
	{
		if (depth > limits.max_depth || files_seen >= limits.max_files) {
			if (timed_out != nullptr && std::chrono::steady_clock::now() >= deadline) {
				*timed_out = true;
			}
			return 0;
		}
		if (std::chrono::steady_clock::now() >= deadline) {
			if (timed_out != nullptr) {
				*timed_out = true;
			}
			return 0;
		}

		const DWORD attrs = GetFileAttributesW(path.c_str());
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
			if (stats != nullptr) {
				++stats->skip_reparse;
			}
			return 0;
		}

		std::wstring pattern = path;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return 0;
		}

		int64_t freed = 0;
		std::vector<std::wstring> subdirs;
		do {
			if (files_seen >= limits.max_files
				|| std::chrono::steady_clock::now() >= deadline) {
				if (timed_out != nullptr && std::chrono::steady_clock::now() >= deadline) {
					*timed_out = true;
				}
				break;
			}

			const wchar_t* name = fd.cFileName;
			if (ShouldSkipDirName(name)) {
				continue;
			}

			std::wstring child = path;
			if (!child.empty() && child.back() != L'\\') {
				child += L'\\';
			}
			child += name;

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
					if (stats != nullptr) {
						++stats->skip_reparse;
					}
					continue;
				}
				subdirs.push_back(std::move(child));
			}
			else {
				++files_seen;
				ULARGE_INTEGER sz;
				sz.LowPart = fd.nFileSizeLow;
				sz.HighPart = fd.nFileSizeHigh;
				if (DeleteFileW(child.c_str())) {
					freed += static_cast<int64_t>(sz.QuadPart);
					if (stats != nullptr) {
						++stats->files_deleted;
					}
				}
				else {
					const DWORD err = GetLastError();
					if (stats != nullptr) {
						if (err == ERROR_SHARING_VIOLATION) {
							++stats->skip_locked;
						}
						else if (err == ERROR_ACCESS_DENIED) {
							++stats->skip_access_denied;
						}
					}
					if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) {
						HLOG_DEBUG("Skip delete: {} err={}",
							log_context != nullptr ? log_context : "clean", static_cast<unsigned>(err));
					}
				}
			}
		} while (FindNextFileW(find, &fd));

		FindClose(find);

		for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) {
			freed += DeleteFilesDirShallowImpl(*it, depth + 1, files_seen, deadline, log_context, stats, limits,
				timed_out);
			if (RemoveDirectoryW(it->c_str())) {
				if (stats != nullptr) {
					++stats->dirs_removed;
				}
			}
		}

		return freed;
	}

	struct AsyncScanPathItem {
		char path[MAX_PATH * 4]{};
		bool selected = true;
	};

	struct AsyncScanWorkItem {
		HCleanDetailListTask* task = nullptr;
		std::vector<AsyncScanPathItem> paths;
		HCleanWalkLimits walk_limits = kDefaultLimitsInternal;
	};

	struct AsyncScanResult {
		HCleanDetailListTask* task = nullptr;
		std::vector<int64_t> detail_bytes;
		int64_t cached_bytes = 0;
	};

	std::mutex g_async_scan_mutex;
	std::condition_variable g_async_scan_cv;
	std::deque<HCleanDetailListTask*> g_path_build_queue;
	std::deque<AsyncScanWorkItem> g_async_scan_queue;
	std::deque<AsyncScanResult> g_async_scan_results;
	std::recursive_mutex g_detail_task_state_mutex;
	std::thread g_async_scan_thread;
	std::atomic<bool> g_async_scan_thread_started{ false };
	std::atomic<bool> g_async_scan_stop{ false };
	std::atomic<HCleanDetailListTask*> g_async_scan_active_task{ nullptr };
	std::atomic<size_t> g_async_scan_active_path_idx{ 0 };
	std::atomic<size_t> g_async_scan_active_path_total{ 0 };
	std::atomic<int> g_async_scan_active_files_seen{ 0 };
	std::atomic<int> g_async_scan_active_files_max{ 0 };

	int64_t MeasureDirShallowWithProgress(const wchar_t* path, const HCleanWalkLimits& limits,
		std::atomic<int>* progress_files)
	{
		if (path == nullptr || path[0] == L'\0') {
			return 0;
		}

		const DWORD attrs = GetFileAttributesW(path);
		if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			return 0;
		}

		const auto deadline = std::chrono::steady_clock::now()
			+ std::chrono::milliseconds(static_cast<int>(limits.timeout_sec * 1000.0));

		if (progress_files != nullptr) {
			progress_files->store(0, std::memory_order_relaxed);
		}

		int files_seen = 0;
		const uint64_t n = DirSizeShallowImpl(path, 0, files_seen, deadline, limits, progress_files);
		if (progress_files != nullptr) {
			progress_files->store(files_seen, std::memory_order_relaxed);
		}
		return static_cast<int64_t>(n > INT64_MAX ? INT64_MAX : n);
	}

	void AsyncScanWorkerMain()
	{
		while (!g_async_scan_stop.load(std::memory_order_acquire)) {
			HCleanDetailListTask* path_build_task = nullptr;
			AsyncScanWorkItem work;
			{
				std::unique_lock<std::mutex> lock(g_async_scan_mutex);
				g_async_scan_cv.wait(lock, [] {
					return g_async_scan_stop.load(std::memory_order_acquire)
						|| !g_path_build_queue.empty()
						|| !g_async_scan_queue.empty();
				});
				if (g_async_scan_stop.load(std::memory_order_acquire)) {
					break;
				}
				if (!g_path_build_queue.empty()) {
					path_build_task = g_path_build_queue.front();
					g_path_build_queue.pop_front();
				}
				else if (!g_async_scan_queue.empty()) {
					work = std::move(g_async_scan_queue.front());
					g_async_scan_queue.pop_front();
				}
			}

			if (path_build_task != nullptr) {
				path_build_task->RunPathBuildAndQueueScanOnWorker();
				continue;
			}

			if (work.task == nullptr || work.paths.empty()) {
				continue;
			}

			g_async_scan_active_task.store(work.task, std::memory_order_release);
			g_async_scan_active_path_total.store(work.paths.size(), std::memory_order_release);
			g_async_scan_active_path_idx.store(0, std::memory_order_release);

			AsyncScanResult result;
			result.task = work.task;
			result.detail_bytes.resize(work.paths.size(), 0);

			g_async_scan_active_files_max.store(work.walk_limits.max_files, std::memory_order_release);
			g_async_scan_active_files_seen.store(0, std::memory_order_release);

			wchar_t wide[MAX_PATH * 4] = {};
			for (size_t i = 0; i < work.paths.size(); ++i) {
				g_async_scan_active_path_idx.store(i, std::memory_order_release);
				g_async_scan_active_files_seen.store(0, std::memory_order_release);
				int64_t measured = 0;
				if (HCleanExpandPathWide(work.paths[i].path, wide, sizeof(wide) / sizeof(wide[0]))) {
					measured = HCleanMeasurePathBytes(wide, &work.walk_limits, work.task->GetId());
				}
				result.detail_bytes[i] = measured;
			}

			result.cached_bytes = 0;
			for (size_t i = 0; i < work.paths.size(); ++i) {
				if (work.paths[i].selected && result.detail_bytes[i] > 0) {
					result.cached_bytes += result.detail_bytes[i];
				}
			}

			g_async_scan_active_task.store(nullptr, std::memory_order_release);
			g_async_scan_active_path_total.store(0, std::memory_order_release);
			g_async_scan_active_path_idx.store(0, std::memory_order_release);
			g_async_scan_active_files_seen.store(0, std::memory_order_release);
			g_async_scan_active_files_max.store(0, std::memory_order_release);

			{
				std::lock_guard<std::mutex> lock(g_async_scan_mutex);
				work.task->MarkScanResultPending();
				g_async_scan_results.push_back(std::move(result));
			}
		}
	}

	void EnsureAsyncScanWorkerThread()
	{
		if (g_async_scan_thread_started.load(std::memory_order_acquire)) {
			return;
		}
		bool expected = false;
		if (!g_async_scan_thread_started.compare_exchange_strong(expected, true)) {
			return;
		}
		g_async_scan_thread = std::thread(AsyncScanWorkerMain);
	}

	void QueueTaskPathBuild(HCleanDetailListTask* task)
	{
		if (task == nullptr) {
			return;
		}
		{
			std::lock_guard<std::mutex> lock(g_async_scan_mutex);
			g_path_build_queue.push_back(task);
		}
		EnsureAsyncScanWorkerThread();
		g_async_scan_cv.notify_one();
	}

	void SubmitAsyncScanJob(HCleanDetailListTask* task, const AsyncScanPathItem* paths, size_t path_count)
	{
		if (task == nullptr || paths == nullptr || path_count == 0) {
			return;
		}

		AsyncScanWorkItem work;
		work.task = task;
		work.paths.assign(paths, paths + path_count);
		work.walk_limits = task->GetScanWalkLimits();

		{
			std::lock_guard<std::mutex> lock(g_async_scan_mutex);
			g_async_scan_queue.push_back(std::move(work));
		}
		EnsureAsyncScanWorkerThread();
		g_async_scan_cv.notify_one();
	}

	void ApplyAsyncScanResult(const AsyncScanResult& result)
	{
		if (result.task == nullptr) {
			return;
		}
		result.task->ApplyMeasuredDetails(
			result.detail_bytes.empty() ? nullptr : result.detail_bytes.data(),
			result.detail_bytes.size(),
			result.cached_bytes);
	}

	void TickAsyncScanWorkerImpl()
	{
		std::deque<AsyncScanResult> completed;
		{
			std::lock_guard<std::mutex> lock(g_async_scan_mutex);
			completed.swap(g_async_scan_results);
		}
		for (const AsyncScanResult& result : completed) {
			ApplyAsyncScanResult(result);
		}
	}

	bool IsAsyncScanWorkerBusyImpl()
	{
		std::lock_guard<std::mutex> lock(g_async_scan_mutex);
		return !g_path_build_queue.empty() || !g_async_scan_queue.empty() || !g_async_scan_results.empty()
			|| g_async_scan_active_task.load(std::memory_order_acquire) != nullptr;
	}

	HCleanDetailListTask* GetAsyncScanActiveTaskImpl()
	{
		return g_async_scan_active_task.load(std::memory_order_acquire);
	}

	size_t GetAsyncScanActivePathIdxImpl()
	{
		return g_async_scan_active_path_idx.load(std::memory_order_acquire);
	}

	size_t GetAsyncScanActivePathTotalImpl()
	{
		return g_async_scan_active_path_total.load(std::memory_order_acquire);
	}

	int GetAsyncScanActiveFilesSeenImpl()
	{
		return g_async_scan_active_files_seen.load(std::memory_order_acquire);
	}

	int GetAsyncScanActiveFilesMaxImpl()
	{
		return g_async_scan_active_files_max.load(std::memory_order_acquire);
	}
}

namespace {
	const HCleanWalkLimits& ResolveLimits(const HCleanWalkLimits* limits)
	{
		return limits != nullptr ? *limits : kDefaultLimitsInternal;
	}
}

const HCleanWalkLimits kHCleanDefaultWalkLimits{ 48, 50000, 45.0 };
const HCleanWalkLimits kHCleanLargeWalkLimits{ 48, 200000, 120.0 };

void HCleanDemoScanSimulator::Begin(int64_t target, const char* status)
{
	state = HCleanScanState::Scanning;
	percent = 0.f;
	status_text = status;
	start_time = static_cast<double>(GetTickCount64()) / 1000.0;
	target_bytes = target;
}

HCleanScanProgress HCleanDemoScanSimulator::Poll(int64_t& cached_bytes) const
{
	if (state != HCleanScanState::Scanning) {
		return { state, percent, status_text };
	}

	const double now = static_cast<double>(GetTickCount64()) / 1000.0;
	const double elapsed = now - start_time;
	percent = static_cast<float>((elapsed / kDurationSec) * 100.0);
	if (percent > 100.f) {
		percent = 100.f;
	}

	if (percent < 35.f) {
		status_text = u8"枚舉目錄…";
	}
	else if (percent < 70.f) {
		status_text = u8"計算大小…";
	}
	else {
		status_text = u8"整理結果…";
	}

	if (percent >= 100.f) {
		state = HCleanScanState::Done;
		status_text = u8"掃描完成";
	}

	return { state, percent, status_text };
}

void HCleanSumSelectedDetailBytes(const HCleanDetailEntry* entries, size_t count, int64_t& out_bytes)
{
	out_bytes = 0;
	if (entries == nullptr) {
		return;
	}
	for (size_t i = 0; i < count; ++i) {
		if (entries[i].selected && entries[i].bytes > 0) {
			out_bytes += entries[i].bytes;
		}
	}
}

bool HCleanExpandPathUtf8(const char* path_template, char* out, size_t out_size)
{
	if (path_template == nullptr || out == nullptr || out_size == 0) {
		return false;
	}

	wchar_t wide[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide(path_template, wide, sizeof(wide) / sizeof(wide[0]))) {
		return false;
	}

	const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
	return n > 0;
}

	static DWORD ExpandEnvTokenToWide(const char* var, wchar_t* buf, size_t buf_chars)
	{
		if (var == nullptr || buf == nullptr || buf_chars == 0) {
			return 0;
		}
		buf[0] = L'\0';

		if (_stricmp(var, "TEMP") == 0 || _stricmp(var, "TMP") == 0) {
			return GetTempPathW(static_cast<DWORD>(buf_chars), buf);
		}
		if (_stricmp(var, "WINDIR") == 0 || _stricmp(var, "SystemRoot") == 0) {
			const UINT n = GetWindowsDirectoryW(buf, static_cast<UINT>(buf_chars));
			return n > 0 ? n : 0;
		}
		if (_stricmp(var, "SystemDrive") == 0) {
			const UINT n = GetWindowsDirectoryW(buf, static_cast<UINT>(buf_chars));
			if (n >= 2) {
				buf[2] = L'\0';
				return 2;
			}
			return 0;
		}

		wchar_t env_key[128] = {};
		if (MultiByteToWideChar(CP_UTF8, 0, var, -1, env_key,
			static_cast<int>(sizeof(env_key) / sizeof(env_key[0]))) <= 0) {
			return 0;
		}
		return GetEnvironmentVariableW(env_key, buf, static_cast<DWORD>(buf_chars));
	}

	static bool AppendWide(wchar_t* out, size_t out_chars, size_t& used, const wchar_t* piece)
	{
		if (out == nullptr || piece == nullptr) {
			return false;
		}
		const size_t piece_len = wcslen(piece);
		if (used + piece_len >= out_chars) {
			return false;
		}
		wcscpy_s(out + used, out_chars - used, piece);
		used += piece_len;
		return true;
	}

bool HCleanExpandPathWide(const char* path_template, wchar_t* out, size_t out_chars)
{
	if (path_template == nullptr || out == nullptr || out_chars == 0) {
		return false;
	}

	out[0] = L'\0';
	size_t used = 0;

	for (size_t i = 0; path_template[i] != '\0';) {
		if (path_template[i] == '%') {
			const char* end = strchr(path_template + i + 1, '%');
			if (end != nullptr && end > path_template + i + 1) {
				char var[64] = {};
				const size_t var_len = static_cast<size_t>(end - (path_template + i + 1));
				if (var_len >= sizeof(var)) {
					return false;
				}
				memcpy(var, path_template + i + 1, var_len);
				var[var_len] = '\0';

				wchar_t buf[MAX_PATH * 4] = {};
				const DWORD n = ExpandEnvTokenToWide(var, buf, sizeof(buf) / sizeof(buf[0]));
				if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) {
					return false;
				}
				if (!AppendWide(out, out_chars, used, buf)) {
					return false;
				}
				i = static_cast<size_t>(end - path_template) + 1;
				continue;
			}
		}

		wchar_t ch[2] = {};
		if (MultiByteToWideChar(CP_UTF8, 0, path_template + i, 1, ch, 2) <= 0) {
			return false;
		}
		if (!AppendWide(out, out_chars, used, ch)) {
			return false;
		}
		++i;
	}

	return used > 0;
}

int64_t HCleanMeasureDirShallow(const wchar_t* path, const HCleanWalkLimits* limits)
{
	return HCleanMeasurePathBytes(path, limits, nullptr);
}

bool HCleanPathIsFile(const wchar_t* path)
{
	if (path == nullptr || path[0] == L'\0') {
		return false;
	}
	const DWORD attrs = GetFileAttributesW(path);
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

	int64_t HCleanMeasurePathBytes(const wchar_t* path, const HCleanWalkLimits* limits, const char* log_context)
	{
		if (path == nullptr || path[0] == L'\0') {
			return 0;
		}

		std::wstring measure_path(path);
		const bool legacy_redirect = CanonicalizeLegacyPathForMeasure(measure_path);
		const wchar_t* path_for_api = legacy_redirect ? measure_path.c_str() : path;

		char path_log[MAX_PATH * 4] = {};
	auto log_path = [&]() -> const char* {
		if (WideCharToMultiByte(CP_UTF8, 0, path_for_api, -1, path_log, static_cast<int>(sizeof(path_log)),
				nullptr, nullptr) <= 0) {
			return "(path)";
		}
		return path_log;
	};

	if (legacy_redirect) {
		HLOG_INFO("Measure: 舊版路徑已對應至 ProgramData（{}）", log_path());
	}

	const DWORD attrs = GetFileAttributesW(path_for_api);
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		const DWORD err = GetLastError();
		if (err == ERROR_ACCESS_DENIED) {
			HLOG_DEBUG("Measure access denied (missing attrs): ctx={}, path={}",
				log_context != nullptr ? log_context : "scan", log_path());
			return -1;
		}
		HLOG_DEBUG("Measure path missing: ctx={}, path={}, err={}",
			log_context != nullptr ? log_context : "scan", log_path(), static_cast<unsigned>(err));
		return 0;
	}

	if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
		HANDLE h = CreateFileW(path_for_api, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_ACCESS_DENIED) {
				HLOG_DEBUG("Measure file access denied: ctx={}, path={}",
					log_context != nullptr ? log_context : "scan", log_path());
				return -1;
			}
			return 0;
		}
		LARGE_INTEGER sz = {};
		if (!GetFileSizeEx(h, &sz)) {
			CloseHandle(h);
			return 0;
		}
		CloseHandle(h);
		return sz.QuadPart > 0 ? sz.QuadPart : 0;
	}

	const HCleanWalkLimits& lim = ResolveLimits(limits);
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(static_cast<int>(lim.timeout_sec * 1000.0));

	int files_seen = 0;
	const uint64_t n = DirSizeShallowImpl(measure_path, 0, files_seen, deadline, lim);
	if (n == 0 && IsDirectoryListingAccessDenied(measure_path)) {
		HLOG_DEBUG("Measure dir access denied: ctx={}, path={}",
			log_context != nullptr ? log_context : "scan", log_path());
		return -1;
	}
	if (n == 0) {
		HLOG_DEBUG("Measure dir exists but 0 bytes (or empty): ctx={}, path={}",
			log_context != nullptr ? log_context : "scan", log_path());
	}
	return static_cast<int64_t>(n > INT64_MAX ? INT64_MAX : n);
}

bool HCleanPathIsDirectory(const wchar_t* path)
{
	if (path == nullptr || path[0] == L'\0') {
		return false;
	}
	const DWORD attrs = GetFileAttributesW(path);
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool HCleanFindVsComponentModelCacheUtf8(char* out, size_t out_size)
{
	if (out == nullptr || out_size == 0) {
		return false;
	}

	wchar_t vs_root[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Microsoft\\VisualStudio", vs_root,
			sizeof(vs_root) / sizeof(vs_root[0]))) {
		return false;
	}
	if (!HCleanPathIsDirectory(vs_root)) {
		return false;
	}

	std::wstring pattern = vs_root;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool found = false;
	do {
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		std::wstring candidate = vs_root;
		if (!candidate.empty() && candidate.back() != L'\\') {
			candidate += L'\\';
		}
		candidate += fd.cFileName;
		candidate += L"\\ComponentModelCache";

		if (!HCleanPathIsDirectory(candidate.c_str())) {
			continue;
		}

		const int n = WideCharToMultiByte(CP_UTF8, 0, candidate.c_str(), -1, out,
			static_cast<int>(out_size), nullptr, nullptr);
		if (n > 0) {
			found = true;
			break;
		}
	} while (FindNextFileW(find, &fd));

	FindClose(find);
	return found;
}

int64_t HCleanMeasureDirTopLevelFiles(const wchar_t* path)
{
	if (path == nullptr || path[0] == L'\0') {
		return 0;
	}

	const DWORD attrs = GetFileAttributesW(path);
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
		return 0;
	}

	std::wstring pattern = path;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	int64_t total = 0;
	do {
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}

		ULARGE_INTEGER file_size;
		file_size.LowPart = fd.nFileSizeLow;
		file_size.HighPart = fd.nFileSizeHigh;
		total += static_cast<int64_t>(file_size.QuadPart);
	} while (FindNextFileW(find, &fd));

	FindClose(find);
	return total;
}

int64_t HCleanSafeDeleteFilesShallow(const wchar_t* dir_path, const char* log_context, HCleanDeleteStats* stats)
{
	if (dir_path == nullptr || dir_path[0] == L'\0') {
		return 0;
	}

	const DWORD attrs = GetFileAttributesW(dir_path);
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
		return 0;
	}

	std::wstring pattern = dir_path;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	int64_t freed = 0;
	do {
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}

		std::wstring file_path = dir_path;
		if (!file_path.empty() && file_path.back() != L'\\') {
			file_path += L'\\';
		}
		file_path += fd.cFileName;

		ULARGE_INTEGER sz;
		sz.LowPart = fd.nFileSizeLow;
		sz.HighPart = fd.nFileSizeHigh;

		if (DeleteFileW(file_path.c_str())) {
			freed += static_cast<int64_t>(sz.QuadPart);
			if (stats != nullptr) {
				++stats->files_deleted;
			}
		}
		else {
			const DWORD err = GetLastError();
			if (stats != nullptr) {
				if (err == ERROR_SHARING_VIOLATION) {
					++stats->skip_locked;
				}
				else if (err == ERROR_ACCESS_DENIED) {
					++stats->skip_access_denied;
				}
			}
			if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) {
				HLOG_DEBUG("Skip delete: {} err={}", log_context != nullptr ? log_context : "clean",
					static_cast<unsigned>(err));
			}
		}
	} while (FindNextFileW(find, &fd));

	FindClose(find);
	if (stats != nullptr) {
		stats->bytes_freed += freed;
	}
	return freed;
}

int64_t HCleanSafeDeleteFilesDirShallow(const wchar_t* dir_path, const char* log_context,
	HCleanDeleteStats* stats, const HCleanWalkLimits* limits)
{
	if (dir_path == nullptr || dir_path[0] == L'\0') {
		return 0;
	}

	const HCleanWalkLimits& lim = ResolveLimits(limits);

	const DWORD attrs = GetFileAttributesW(dir_path);
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
		return 0;
	}

	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(static_cast<int>(lim.timeout_sec * 1000.0));

	int files_seen = 0;
	bool timed_out = false;
	const int64_t freed = DeleteFilesDirShallowImpl(dir_path, 0, files_seen, deadline, log_context, stats, lim,
		&timed_out);
	if (stats != nullptr) {
		stats->bytes_freed += freed;
		if (timed_out) {
			++stats->skip_timeout;
		}
		HLOG_INFO("Delete stats [{}]: freed {} bytes, files={}, locked={}, denied={}, timeout={}, reparse={}, dirs={}",
			log_context != nullptr ? log_context : "clean",
			stats->bytes_freed, stats->files_deleted, stats->skip_locked, stats->skip_access_denied,
			stats->skip_timeout, stats->skip_reparse, stats->dirs_removed);
	}
	return freed;
}

bool HCleanFindSteamInstallPathUtf8(char* out, size_t out_size)
{
	if (out == nullptr || out_size == 0) {
		return false;
	}

	wchar_t wide[MAX_PATH * 4] = {};
	DWORD cb = static_cast<DWORD>(sizeof(wide));
	LSTATUS st = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath",
		RRF_RT_REG_SZ, nullptr, wide, &cb);
	if (st != ERROR_SUCCESS) {
		cb = static_cast<DWORD>(sizeof(wide));
		st = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", RRF_RT_REG_SZ, nullptr,
			wide, &cb);
	}
	if (st != ERROR_SUCCESS || wide[0] == L'\0') {
		return false;
	}

	size_t len = wcslen(wide);
	while (len > 0 && wide[len - 1] == L'\\') {
		wide[--len] = L'\0';
	}

	const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
	return n > 0;
}

bool HCleanFindFirefoxProfileCacheUtf8(char* out, size_t out_size)
{
	if (out == nullptr || out_size == 0) {
		return false;
	}

	wchar_t profiles[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles", profiles,
			sizeof(profiles) / sizeof(profiles[0]))
		|| !HCleanPathIsDirectory(profiles)) {
		return false;
	}

	std::wstring pattern = profiles;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool found = false;
	do {
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		const std::wstring name = fd.cFileName;
		if (name.find(L".default") == std::wstring::npos) {
			continue;
		}

		static const wchar_t* k_cache_subdirs[] = { L"cache2", L"cache" };
		for (const wchar_t* sub : k_cache_subdirs) {
			std::wstring candidate = profiles;
			if (!candidate.empty() && candidate.back() != L'\\') {
				candidate += L'\\';
			}
			candidate += name;
			candidate += L'\\';
			candidate += sub;
			if (!HCleanPathIsDirectory(candidate.c_str())) {
				continue;
			}

			const int n = WideCharToMultiByte(CP_UTF8, 0, candidate.c_str(), -1, out,
				static_cast<int>(out_size), nullptr, nullptr);
			if (n > 0) {
				found = true;
				break;
			}
		}
		if (found) {
			break;
		}
	} while (FindNextFileW(find, &fd));

	FindClose(find);
	return found;
}

bool HCleanEmptyRecycleBin()
{
	const HRESULT hr = SHEmptyRecycleBinW(nullptr, nullptr,
		SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
	if (SUCCEEDED(hr)) {
		HLOG_INFO("Recycle bin emptied");
		return true;
	}
	HLOG_WARN("SHEmptyRecycleBin failed: 0x{:08X}", static_cast<unsigned>(hr));
	return false;
}

namespace {
	bool Utf8FromWide(const wchar_t* wide, char* out, size_t out_size)
	{
		if (wide == nullptr || out == nullptr || out_size == 0) {
			return false;
		}
		const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
		return n > 0;
	}

	bool WideFromUtf8(const char* utf8, wchar_t* out, size_t out_chars)
	{
		if (utf8 == nullptr || out == nullptr || out_chars == 0) {
			return false;
		}
		const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, static_cast<int>(out_chars));
		return n > 0;
	}

	bool IsChromiumSystemSubdir(const wchar_t* name)
	{
		static const wchar_t* k_skip[] = {
			L"Crashpad", L"ShaderCache", L"GrShaderCache", L"WidevineCdm", L"Safe Browsing",
			L"BrowserMetrics", L"OriginTrials", L"OptimizationHints", L"RecoveryImproved",
			L"SwReporter", L"SSLErrorAssistant", L"Subresource Filter", L"TrustTokenKeyCommitments",
			L"PKIMetadata", L"CertificateRevocation", L"FirstPartySetsPreloaded", L"OnDeviceHeadSuggestModel",
			L"hyphen-data", L"MediaFoundationWidevineCdm", L"MEIPreload", L"OpenCookieDatabase",
		};
		for (const wchar_t* skip : k_skip) {
			if (_wcsicmp(name, skip) == 0) {
				return true;
			}
		}
		return false;
	}

	bool TryAddPathIfDir(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count, size_t& count,
		const wchar_t* wide_path, const char* label)
	{
		if (count >= max_count || wide_path == nullptr || !HCleanPathIsDirectory(wide_path)) {
			return false;
		}
		if (!Utf8FromWide(wide_path, out_paths[count], MAX_PATH * 4)) {
			return false;
		}
		if (label != nullptr) {
			strncpy_s(out_labels[count], 128, label, _TRUNCATE);
		}
		else {
			out_labels[count][0] = '\0';
		}
		++count;
		return true;
	}

	bool TryAddPathIfFile(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count, size_t& count,
		const wchar_t* wide_path, const char* label)
	{
		if (count >= max_count || wide_path == nullptr || !HCleanPathIsFile(wide_path)) {
			return false;
		}
		if (!Utf8FromWide(wide_path, out_paths[count], MAX_PATH * 4)) {
			return false;
		}
		if (label != nullptr) {
			strncpy_s(out_labels[count], 128, label, _TRUNCATE);
		}
		else {
			out_labels[count][0] = '\0';
		}
		++count;
		return true;
	}

	bool TryAddChromiumCacheSubdir(const std::wstring& profile_dir, const wchar_t* subpath, const char* label_prefix,
		const char* suffix, char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count, size_t& count)
	{
		std::wstring candidate = profile_dir;
		if (!candidate.empty() && candidate.back() != L'\\') {
			candidate += L'\\';
		}
		candidate += subpath;
		char label[128] = {};
		snprintf(label, sizeof(label), "%s %s", label_prefix, suffix);
		return TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
	}

	SC_HANDLE OpenServiceSimple(const wchar_t* name, DWORD access)
	{
		SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (scm == nullptr) {
			return nullptr;
		}
		SC_HANDLE svc = OpenServiceW(scm, name, access);
		CloseServiceHandle(scm);
		return svc;
	}

	bool ControlServiceSimple(const wchar_t* name, DWORD control)
	{
		SC_HANDLE svc = OpenServiceSimple(name, SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_USER_DEFINED_CONTROL);
		if (svc == nullptr) {
			return false;
		}
		SERVICE_STATUS status = {};
		const BOOL ok = ControlService(svc, control, &status);
		CloseServiceHandle(svc);
		return ok != FALSE;
	}

	bool StopServiceSimple(const wchar_t* name)
	{
		SC_HANDLE svc = OpenServiceSimple(name, SERVICE_STOP | SERVICE_QUERY_STATUS);
		if (svc == nullptr) {
			HLOG_WARN("Cannot open service {} for stop", static_cast<const void*>(name));
			return false;
		}
		SERVICE_STATUS status = {};
		const BOOL ok = ControlService(svc, SERVICE_CONTROL_STOP, &status);
		CloseServiceHandle(svc);
		return ok != FALSE;
	}

	bool StartServiceSimple(const wchar_t* name)
	{
		SC_HANDLE svc = OpenServiceSimple(name, SERVICE_START);
		if (svc == nullptr) {
			return false;
		}
		const BOOL ok = StartServiceW(svc, 0, nullptr);
		CloseServiceHandle(svc);
		return ok != FALSE;
	}
}

size_t HCleanParseSteamLibraryPaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	char steam[MAX_PATH * 4] = {};
	if (!HCleanFindSteamInstallPathUtf8(steam, sizeof(steam))) {
		return 0;
	}

	size_t count = 0;
	wchar_t steam_wide[MAX_PATH * 4] = {};
	if (WideFromUtf8(steam, steam_wide, MAX_PATH * 4)) {
		TryAddPathIfDir(out_paths, out_labels, max_count, count, steam_wide, "Steam 主函式庫");
	}

	char vdf_path[MAX_PATH * 4] = {};
	snprintf(vdf_path, sizeof(vdf_path), "%s\\steamapps\\libraryfolders.vdf", steam);

	wchar_t vdf_wide[MAX_PATH * 4] = {};
	if (!WideFromUtf8(vdf_path, vdf_wide, MAX_PATH * 4)) {
		return count;
	}

	HANDLE file = CreateFileW(vdf_wide, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return count;
	}

	std::vector<char> content;
	content.resize(65536);
	DWORD read = 0;
	if (!ReadFile(file, content.data(), static_cast<DWORD>(content.size() - 1), &read, nullptr)) {
		CloseHandle(file);
		return count;
	}
	CloseHandle(file);
	content[read] = '\0';
	std::string text(content.data(), read);

	for (size_t pos = 0; pos < text.size();) {
		const size_t key_pos = text.find("\"path\"", pos);
		if (key_pos == std::string::npos) {
			break;
		}
		const size_t q1 = text.find('"', key_pos + 6);
		if (q1 == std::string::npos) {
			break;
		}
		const size_t q2 = text.find('"', q1 + 1);
		if (q2 == std::string::npos) {
			break;
		}
		const size_t q3 = text.find('"', q2 + 1);
		if (q3 == std::string::npos) {
			break;
		}
		const size_t q4 = text.find('"', q3 + 1);
		if (q4 == std::string::npos) {
			break;
		}

		std::string lib_path = text.substr(q3 + 1, q4 - q3 - 1);
		for (char& ch : lib_path) {
			if (ch == '/') {
				ch = '\\';
			}
		}

		wchar_t lib_wide[MAX_PATH * 4] = {};
		if (WideFromUtf8(lib_path.c_str(), lib_wide, MAX_PATH * 4)) {
			char label[128] = {};
			snprintf(label, sizeof(label), "Steam 函式庫 %zu", count);
			TryAddPathIfDir(out_paths, out_labels, max_count, count, lib_wide, label);
		}
		pos = q4 + 1;
	}

	return count;
}

size_t HCleanEnumerateFirefoxProfileCaches(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t profiles[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles", profiles,
			sizeof(profiles) / sizeof(profiles[0]))
		|| !HCleanPathIsDirectory(profiles)) {
		return 0;
	}

	std::wstring pattern = profiles;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	size_t count = 0;
	do {
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		char profile_label[128] = {};
		Utf8FromWide(fd.cFileName, profile_label, sizeof(profile_label));

		static const wchar_t* k_cache_subdirs[] = { L"cache2", L"cache" };
		for (const wchar_t* sub : k_cache_subdirs) {
			if (count >= max_count) {
				break;
			}
			std::wstring candidate = profiles;
			if (!candidate.empty() && candidate.back() != L'\\') {
				candidate += L'\\';
			}
			candidate += fd.cFileName;
			candidate += L'\\';
			candidate += sub;

			char label[128] = {};
			snprintf(label, sizeof(label), "Firefox %s", profile_label);
			TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
		}
	} while (FindNextFileW(find, &fd) && count < max_count);

	FindClose(find);
	return count;
}

size_t HCleanEnumerateChromiumProfileCaches(const char* user_data_template,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (user_data_template == nullptr || out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t user_data[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide(user_data_template, user_data, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(user_data)) {
		return 0;
	}

	size_t count = 0;

	std::wstring shader = user_data;
	shader += L"\\ShaderCache";
	TryAddPathIfDir(out_paths, out_labels, max_count, count, shader.c_str(), "ShaderCache");

	std::wstring pattern = user_data;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return count;
	}

	static const struct { const wchar_t* sub; const char* suffix; } k_subs[] = {
		{ L"Cache\\Cache_Data", "快取" },
		{ L"Cache", "快取" },
		{ L"Code Cache", "Code Cache" },
		{ L"GPUCache", "GPUCache" },
		{ L"Service Worker\\CacheStorage", "Service Worker" },
		{ L"blob_storage", "blob_storage" },
		{ L"Media Cache", "Media Cache" },
		{ L"DawnCache", "DawnCache" },
	};

	do {
		if (count >= max_count) {
			break;
		}
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}
		if (IsChromiumSystemSubdir(fd.cFileName)) {
			continue;
		}

		std::wstring profile_dir = user_data;
		if (!profile_dir.empty() && profile_dir.back() != L'\\') {
			profile_dir += L'\\';
		}
		profile_dir += fd.cFileName;

		const std::wstring prefs = profile_dir + L"\\Preferences";
		const bool has_prefs = GetFileAttributesW(prefs.c_str()) != INVALID_FILE_ATTRIBUTES;
		const bool is_default = _wcsicmp(fd.cFileName, L"Default") == 0;
		const bool is_profile = wcsstr(fd.cFileName, L"Profile") != nullptr;
		if (!has_prefs && !is_default && !is_profile) {
			continue;
		}

		char profile_label[128] = {};
		Utf8FromWide(fd.cFileName, profile_label, sizeof(profile_label));

		for (const auto& sub : k_subs) {
			if (count >= max_count) {
				break;
			}
			TryAddChromiumCacheSubdir(profile_dir, sub.sub, profile_label, sub.suffix,
				out_paths, out_labels, max_count, count);
		}
	} while (FindNextFileW(find, &fd));

	FindClose(find);
	return count;
}

size_t HCleanEnumerateRecycleBinPaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	size_t count = 0;
	const DWORD drives = GetLogicalDrives();
	for (int i = 0; i < 26 && count < max_count; ++i) {
		if ((drives & (1u << i)) == 0) {
			continue;
		}
		wchar_t root[4] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0' };
		const UINT drive_type = GetDriveTypeW(root);
		if (drive_type == DRIVE_NO_ROOT_DIR || drive_type == DRIVE_UNKNOWN) {
			continue;
		}

		wchar_t recycle_path[MAX_PATH] = {};
		swprintf_s(recycle_path, L"%c:\\$Recycle.Bin", L'A' + i);
		char label[128] = {};
		snprintf(label, sizeof(label), "%c: 回收筒", 'A' + i);
		TryAddPathIfDir(out_paths, out_labels, max_count, count, recycle_path, label);
	}
	return count;
}

size_t HCleanEnumerateSubdirsWithCache(const char* root_template, const char* const* subdir_names,
	size_t subdir_count, char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count,
	const char* label_prefix)
{
	if (root_template == nullptr || subdir_names == nullptr || subdir_count == 0
		|| out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t root[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide(root_template, root, MAX_PATH * 4) || !HCleanPathIsDirectory(root)) {
		return 0;
	}

	size_t count = 0;
	for (size_t i = 0; i < subdir_count && count < max_count; ++i) {
		if (subdir_names[i] == nullptr || subdir_names[i][0] == '\0') {
			continue;
		}
		wchar_t sub_wide[MAX_PATH * 4] = {};
		if (!WideFromUtf8(subdir_names[i], sub_wide, MAX_PATH * 4)) {
			continue;
		}
		std::wstring candidate = root;
		if (!candidate.empty() && candidate.back() != L'\\') {
			candidate += L'\\';
		}
		candidate += sub_wide;

		char label[128] = {};
		if (label_prefix != nullptr && label_prefix[0] != '\0') {
			snprintf(label, sizeof(label), "%s %s", label_prefix, subdir_names[i]);
		}
		else {
			strncpy_s(label, sizeof(label), subdir_names[i], _TRUNCATE);
		}
		TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
	}
	return count;
}

namespace {
	bool LooksLikeUnrealVersionDir(const wchar_t* name)
	{
		if (name == nullptr || name[0] == L'\0') {
			return false;
		}
		if (_wcsicmp(name, L"Common") == 0 || _wcsicmp(name, L"Engine") == 0) {
			return false;
		}
		if (wcsstr(name, L".") != nullptr) {
			return true;
		}
		return wcsncmp(name, L"UE_", 3) == 0 || wcsncmp(name, L"5.", 2) == 0 || wcsncmp(name, L"4.", 2) == 0;
	}

	void TryAddUnrealCacheSubdir(const std::wstring& base, const wchar_t* subpath, const char* label,
		char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count, size_t& count)
	{
		if (count >= max_count || subpath == nullptr) {
			return;
		}
		std::wstring candidate = base;
		if (!candidate.empty() && candidate.back() != L'\\') {
			candidate += L'\\';
		}
		candidate += subpath;
		TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
	}

	void EnumerateUnrealCommonSubdirs(const wchar_t* common_root,
		char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count, size_t& count)
	{
		if (!HCleanPathIsDirectory(common_root)) {
			return;
		}

		static const struct { const wchar_t* sub; const char* label; } k_common[] = {
			{ L"DerivedDataCache", "UE Common DDC" },
			{ L"Analytics", "UE Analytics" },
			{ L"Zen", "UE Zen 快取" },
		};
		std::wstring base = common_root;
		for (const auto& item : k_common) {
			TryAddUnrealCacheSubdir(base, item.sub, item.label, out_paths, out_labels, max_count, count);
		}

		std::wstring pattern = common_root;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return;
		}
		do {
			if (count >= max_count) {
				break;
			}
			if (ShouldSkipDirName(fd.cFileName)) {
				continue;
			}
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				continue;
			}
			if (_wcsicmp(fd.cFileName, L"DerivedDataCache") == 0
				|| _wcsicmp(fd.cFileName, L"Analytics") == 0
				|| _wcsicmp(fd.cFileName, L"Zen") == 0) {
				continue;
			}
			char sub_label[128] = {};
			Utf8FromWide(fd.cFileName, sub_label, sizeof(sub_label));
			char label[128] = {};
			snprintf(label, sizeof(label), "UE Common\\%s", sub_label);
			std::wstring sub_base = common_root;
			if (!sub_base.empty() && sub_base.back() != L'\\') {
				sub_base += L'\\';
			}
			sub_base += fd.cFileName;
			TryAddPathIfDir(out_paths, out_labels, max_count, count, sub_base.c_str(), label);
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}
}

size_t HCleanEnumerateUnrealEngineCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	size_t count = 0;

	wchar_t ue_root[MAX_PATH * 4] = {};
	if (HCleanExpandPathWide("%LOCALAPPDATA%\\UnrealEngine", ue_root, MAX_PATH * 4)
		&& HCleanPathIsDirectory(ue_root)) {
		std::wstring common = ue_root;
		common += L"\\Common";
		EnumerateUnrealCommonSubdirs(common.c_str(), out_paths, out_labels, max_count, count);

		std::wstring engine = ue_root;
		engine += L"\\Engine";
		TryAddUnrealCacheSubdir(engine, L"Intermediate", "UE Engine Intermediate",
			out_paths, out_labels, max_count, count);
		TryAddUnrealCacheSubdir(engine, L"Programs", "UE Engine Programs",
			out_paths, out_labels, max_count, count);

		std::wstring pattern = ue_root;
		pattern += L"\\*";
		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find != INVALID_HANDLE_VALUE) {
			do {
				if (count >= max_count) {
					break;
				}
				if (ShouldSkipDirName(fd.cFileName)) {
					continue;
				}
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					continue;
				}
				if (!LooksLikeUnrealVersionDir(fd.cFileName)) {
					continue;
				}

				char ver_label[128] = {};
				Utf8FromWide(fd.cFileName, ver_label, sizeof(ver_label));

				std::wstring ver_base = ue_root;
				ver_base += L'\\';
				ver_base += fd.cFileName;

				char label[128] = {};
				snprintf(label, sizeof(label), "UE %s DDC", ver_label);
				TryAddUnrealCacheSubdir(ver_base, L"DerivedDataCache", label,
					out_paths, out_labels, max_count, count);

				snprintf(label, sizeof(label), "UE %s ShaderCache", ver_label);
				TryAddUnrealCacheSubdir(ver_base, L"Saved\\ShaderCache", label,
					out_paths, out_labels, max_count, count);

				snprintf(label, sizeof(label), "UE %s Intermediate", ver_label);
				TryAddUnrealCacheSubdir(ver_base, L"Intermediate", label,
					out_paths, out_labels, max_count, count);
			} while (FindNextFileW(find, &fd));
			FindClose(find);
		}
	}

	wchar_t ue_roaming[MAX_PATH * 4] = {};
	if (HCleanExpandPathWide("%APPDATA%\\Unreal Engine", ue_roaming, MAX_PATH * 4)
		&& HCleanPathIsDirectory(ue_roaming)) {
		static const struct { const wchar_t* sub; const char* label; } k_roaming[] = {
			{ L"Saved\\ShaderCache", "UE Roaming ShaderCache" },
			{ L"Intermediate", "UE Roaming Intermediate" },
		};
		std::wstring base = ue_roaming;
		for (const auto& item : k_roaming) {
			TryAddUnrealCacheSubdir(base, item.sub, item.label, out_paths, out_labels, max_count, count);
		}
	}

	return count;
}

size_t HCleanEnumerateAdobeProductCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	size_t count = 0;

	static const struct { const char* path_template; const char* label; } k_static[] = {
		{ "%LOCALAPPDATA%\\Adobe\\Common\\Media Cache Files", "Adobe 媒體快取" },
		{ "%LOCALAPPDATA%\\Adobe\\Common\\Peak Files", "Adobe Peak 快取" },
		{ "%LOCALAPPDATA%\\Temp\\Creative Cloud", "Creative Cloud 暫存" },
		{ "%LOCALAPPDATA%\\Adobe\\Acrobat\\DC\\Cache", "Acrobat DC 快取" },
		{ "%APPDATA%\\Adobe\\Common", "Adobe 共用設定快取" },
	};
	for (const auto& item : k_static) {
		if (count >= max_count) {
			break;
		}
		wchar_t wide[MAX_PATH * 4] = {};
		if (!HCleanExpandPathWide(item.path_template, wide, MAX_PATH * 4)) {
			continue;
		}
		TryAddPathIfDir(out_paths, out_labels, max_count, count, wide, item.label);
	}

	wchar_t adobe_root[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Adobe", adobe_root, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(adobe_root)) {
		return count;
	}

	static const wchar_t* k_product_subs[] = {
		L"Media Cache Files",
		L"Peak Files",
		L"Cache",
		L"Logs",
	};

	std::wstring pattern = adobe_root;
	pattern += L"\\*";
	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return count;
	}

	do {
		if (count >= max_count) {
			break;
		}
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}
		if (_wcsicmp(fd.cFileName, L"Common") == 0 || _wcsicmp(fd.cFileName, L"Acrobat") == 0) {
			continue;
		}

		char product[128] = {};
		Utf8FromWide(fd.cFileName, product, sizeof(product));

		std::wstring product_dir = adobe_root;
		product_dir += L'\\';
		product_dir += fd.cFileName;

		for (const wchar_t* sub : k_product_subs) {
			if (count >= max_count) {
				break;
			}
			char label[128] = {};
			char sub_utf8[64] = {};
			Utf8FromWide(sub, sub_utf8, sizeof(sub_utf8));
			snprintf(label, sizeof(label), "Adobe %s\\%s", product, sub_utf8);
			std::wstring candidate = product_dir;
			if (!candidate.empty() && candidate.back() != L'\\') {
				candidate += L'\\';
			}
			candidate += sub;
			TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
		}
	} while (FindNextFileW(find, &fd));
	FindClose(find);

	return count;
}

size_t HCleanEnumerateExplorerThumbcachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t explorer[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer", explorer, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(explorer)) {
		return 0;
	}

	size_t count = 0;
	std::wstring pattern = explorer;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L"thumbcache_*.db";

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	do {
		if (count >= max_count) {
			break;
		}
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}
		std::wstring file_path = explorer;
		if (!file_path.empty() && file_path.back() != L'\\') {
			file_path += L'\\';
		}
		file_path += fd.cFileName;
		char label[128] = {};
		char name_utf8[64] = {};
		Utf8FromWide(fd.cFileName, name_utf8, sizeof(name_utf8));
		snprintf(label, sizeof(label), "縮圖 %s", name_utf8);
		TryAddPathIfFile(out_paths, out_labels, max_count, count, file_path.c_str(), label);
	} while (FindNextFileW(find, &fd));
	FindClose(find);
	return count;
}

size_t HCleanEnumerateExplorerIconcachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t explorer[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer", explorer, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(explorer)) {
		return 0;
	}

	size_t count = 0;
	std::wstring pattern = explorer;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L"iconcache_*.db";

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	do {
		if (count >= max_count) {
			break;
		}
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}
		std::wstring file_path = explorer;
		if (!file_path.empty() && file_path.back() != L'\\') {
			file_path += L'\\';
		}
		file_path += fd.cFileName;
		char label[128] = {};
		char name_utf8[64] = {};
		Utf8FromWide(fd.cFileName, name_utf8, sizeof(name_utf8));
		snprintf(label, sizeof(label), "圖示 %s", name_utf8);
		TryAddPathIfFile(out_paths, out_labels, max_count, count, file_path.c_str(), label);
	} while (FindNextFileW(find, &fd));
	FindClose(find);
	return count;
}

size_t HCleanEnumerateCommonUwpCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	static const wchar_t* k_package_globs[] = {
		L"Microsoft.MicrosoftPCManager*",
		L"Microsoft.Windows.Photos*",
		L"Microsoft.ZuneVideo*",
		L"MicrosoftWindows.Client.WebExperience*",
		L"Microsoft.YourPhone*",
		L"Microsoft.Windows.ContentDeliveryManager*",
		L"Microsoft.WindowsCalculator*",
	};

	static const wchar_t* k_subs[] = {
		L"LocalCache",
		L"TempState",
		L"AC\\Temp",
	};

	wchar_t packages[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Packages", packages, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(packages)) {
		return 0;
	}

	size_t count = 0;
	for (const wchar_t* pkg_glob : k_package_globs) {
		if (count >= max_count) {
			break;
		}
		std::wstring pattern = packages;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += pkg_glob;

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			continue;
		}
		do {
			if (count >= max_count) {
				break;
			}
			if (ShouldSkipDirName(fd.cFileName) || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				continue;
			}

			char pkg_label[96] = {};
			Utf8FromWide(fd.cFileName, pkg_label, sizeof(pkg_label));

			std::wstring pkg_dir = packages;
			pkg_dir += L'\\';
			pkg_dir += fd.cFileName;

			for (const wchar_t* sub : k_subs) {
				if (count >= max_count) {
					break;
				}
				std::wstring candidate = pkg_dir;
				if (!candidate.empty() && candidate.back() != L'\\') {
					candidate += L'\\';
				}
				candidate += sub;
				char label[128] = {};
				snprintf(label, sizeof(label), "UWP %s", pkg_label);
				TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
			}
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}
	return count;
}

size_t HCleanEnumerateMinecraftUwpCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t packages[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Packages", packages, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(packages)) {
		return 0;
	}

	size_t count = 0;
	std::wstring pattern = packages;
	pattern += L"\\Microsoft.Minecraft*";
	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	static const wchar_t* k_subs[] = {
		L"LocalCache",
		L"LocalState\\logs",
		L"TempState",
	};

	do {
		if (count >= max_count) {
			break;
		}
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		char pkg_label[96] = {};
		Utf8FromWide(fd.cFileName, pkg_label, sizeof(pkg_label));

		std::wstring pkg_dir = packages;
		pkg_dir += L'\\';
		pkg_dir += fd.cFileName;

		for (const wchar_t* sub : k_subs) {
			if (count >= max_count) {
				break;
			}
			char label[128] = {};
			char sub_utf8[64] = {};
			Utf8FromWide(sub, sub_utf8, sizeof(sub_utf8));
			snprintf(label, sizeof(label), "Minecraft UWP %s", sub_utf8);
			std::wstring candidate = pkg_dir;
			if (!candidate.empty() && candidate.back() != L'\\') {
				candidate += L'\\';
			}
			candidate += sub;
			TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
		}
	} while (FindNextFileW(find, &fd));
	FindClose(find);

	return count;
}

size_t HCleanEnumerateUwpPackagePaths(const char* package_name_glob,
	const wchar_t* const* rel_subpaths, size_t subpath_count, const char* label_prefix,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (package_name_glob == nullptr || rel_subpaths == nullptr || subpath_count == 0
		|| out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t packages[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Packages", packages, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(packages)) {
		return 0;
	}

	wchar_t glob_wide[MAX_PATH * 4] = {};
	if (!WideFromUtf8(package_name_glob, glob_wide, MAX_PATH * 4)) {
		return 0;
	}

	size_t count = 0;
	std::wstring pattern = packages;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += glob_wide;

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	do {
		if (count >= max_count) {
			break;
		}
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		std::wstring pkg_dir = packages;
		if (!pkg_dir.empty() && pkg_dir.back() != L'\\') {
			pkg_dir += L'\\';
		}
		pkg_dir += fd.cFileName;

		for (size_t i = 0; i < subpath_count && count < max_count; ++i) {
			if (rel_subpaths[i] == nullptr || rel_subpaths[i][0] == L'\0') {
				continue;
			}
			std::wstring candidate = pkg_dir;
			if (!candidate.empty() && candidate.back() != L'\\') {
				candidate += L'\\';
			}
			candidate += rel_subpaths[i];

			char label[128] = {};
			char sub_utf8[96] = {};
			Utf8FromWide(rel_subpaths[i], sub_utf8, sizeof(sub_utf8));
			if (label_prefix != nullptr && label_prefix[0] != '\0') {
				snprintf(label, sizeof(label), "%s %s", label_prefix, sub_utf8);
			}
			else {
				strncpy_s(label, sizeof(label), sub_utf8, _TRUNCATE);
			}
			TryAddPathIfDir(out_paths, out_labels, max_count, count, candidate.c_str(), label);
		}
	} while (FindNextFileW(find, &fd));
	FindClose(find);

	return count;
}

size_t HCleanEnumerateElectronAppCaches(const char* root_template, const char* label_prefix,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	static const char* k_subs[] = {
		"Cache", "Code Cache", "GPUCache", "logs", "Service Worker", "CachedData", "Crashpad",
	};
	return HCleanEnumerateSubdirsWithCache(root_template, k_subs, 7,
		out_paths, out_labels, max_count, label_prefix);
}

namespace {
	bool AppDataSubdirNameEquals(const wchar_t* name, const wchar_t* target)
	{
		return name != nullptr && target != nullptr && _wcsicmp(name, target) == 0;
	}

	bool SubdirNameContainsInsensitive(const wchar_t* name, const wchar_t* fragment)
	{
		if (name == nullptr || fragment == nullptr || fragment[0] == L'\0') {
			return false;
		}
		const size_t name_len = wcslen(name);
		const size_t frag_len = wcslen(fragment);
		if (frag_len == 0 || name_len < frag_len) {
			return false;
		}
		for (size_t i = 0; i + frag_len <= name_len; ++i) {
			if (_wcsnicmp(name + i, fragment, frag_len) == 0) {
				return true;
			}
		}
		return false;
	}

	bool IsKnownAppDataCacheSubdir(const wchar_t* name)
	{
		if (name == nullptr || name[0] == L'\0') {
			return false;
		}
		static const wchar_t* k_names[] = {
			L"Cache", L"cache", L"cache2", L"Caches", L"logs", L"Logs", L"Log", L"log",
			L"temp", L"Temp", L"tmp", L"Tmp", L"GPUCache", L"Code Cache", L"ShaderCache",
			L"webcache", L"webcache2", L"Crash Reports", L"CrashReports", L"Crashpad",
			L"Crash Dumps", L"CrashDumps", L"blob_storage", L"DawnCache", L"INetCache",
			L"Media Cache", L"Service Worker", L"Session Storage", L"Local Storage",
			L"workspaceStorage", L"IndexedDB", L"CachedData", L"CachedImages",
			L"Download", L"Downloads", L"download", L"downloads", L"update", L"updates",
			L"Update", L"Updates", L"installer", L"Installer", L"backup", L"Backup",
			L"Backups", L"thumbnails", L"Thumbnails", L"ThumbCache", L"metrics", L"Metrics",
			L"analytics", L"Analytics", L"journal", L"Journal", L"diagnostics", L"Diagnostics",
			L"Saved", L"saved", L"storage", L"Storage", L"compiled", L"Compiled",
			L"shader", L"Shader", L"offline", L"Offline", L"preview", L"Preview",
			L"RoamingState", L"LocalState", L"ACTemp", L"AC\\Temp",
		};
		for (const wchar_t* known : k_names) {
			if (AppDataSubdirNameEquals(name, known)) {
				return true;
			}
		}
		static const wchar_t* k_fragments[] = {
			L"cache", L"log", L"temp", L"dump", L"thumb", L"backup", L"update",
			L"download", L"shader", L"storage", L"metric", L"crash", L"blob",
			L"inet", L"session", L"worker", L"preview", L"journal", L"diagnostic",
		};
		for (const wchar_t* frag : k_fragments) {
			if (SubdirNameContainsInsensitive(name, frag)) {
				return true;
			}
		}
		return false;
	}

	bool ShouldSkipAppDataAppName(const wchar_t* app_name, const char* const* skip_names, size_t skip_count)
	{
		if (app_name == nullptr || skip_names == nullptr || skip_count == 0) {
			return false;
		}
		char app_utf8[128] = {};
		if (!Utf8FromWide(app_name, app_utf8, sizeof(app_utf8))) {
			return false;
		}
		for (size_t i = 0; i < skip_count; ++i) {
			if (skip_names[i] != nullptr && _stricmp(app_utf8, skip_names[i]) == 0) {
				return true;
			}
		}
		return false;
	}

	struct DiscoveryEntry {
		char path[MAX_PATH * 4]{};
		char label[128]{};
		wchar_t subdir_name[64]{};
		int64_t bytes = 0;
	};

	constexpr size_t kDiscoveryPoolMax = 384;
	const HCleanWalkLimits kDiscoveryMeasureLimits{ 12, 8000, 8.0 };

	bool DiscoveryPathExists(const char* path_utf8)
	{
		wchar_t wide[MAX_PATH * 4] = {};
		return path_utf8 != nullptr
			&& HCleanExpandPathWide(path_utf8, wide, MAX_PATH * 4)
			&& (HCleanPathIsDirectory(wide) || HCleanPathIsFile(wide));
	}

	bool TryPushDiscoveryEntry(DiscoveryEntry* pool, size_t pool_max, size_t& count, const wchar_t* path_wide,
		const char* label, const wchar_t* subdir_name)
	{
		if (pool == nullptr || path_wide == nullptr || label == nullptr || count >= pool_max) {
			return false;
		}
		if (!HCleanPathIsDirectory(path_wide)) {
			return false;
		}

		char path_utf8[MAX_PATH * 4] = {};
		if (!Utf8FromWide(path_wide, path_utf8, sizeof(path_utf8))) {
			return false;
		}

		for (size_t i = 0; i < count; ++i) {
			if (_stricmp(pool[i].path, path_utf8) == 0) {
				return false;
			}
		}

		strncpy_s(pool[count].path, path_utf8, _TRUNCATE);
		strncpy_s(pool[count].label, label, _TRUNCATE);
		if (subdir_name != nullptr) {
			wcsncpy_s(pool[count].subdir_name, subdir_name, _TRUNCATE);
		}
		pool[count].bytes = -1;
		++count;
		return true;
	}

	size_t CollectAppDataStyleCandidatesFromRoot(const wchar_t* root_wide, const char* const* skip_app_names,
		size_t skip_app_count, DiscoveryEntry* pool, size_t pool_max)
	{
		if (root_wide == nullptr || pool == nullptr || pool_max == 0 || !HCleanPathIsDirectory(root_wide)) {
			return 0;
		}

		size_t count = 0;
		std::wstring pattern = root_wide;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW app_fd = {};
		const HANDLE app_find = FindFirstFileW(pattern.c_str(), &app_fd);
		if (app_find == INVALID_HANDLE_VALUE) {
			return 0;
		}

		do {
			if (count >= pool_max) {
				break;
			}
			if (ShouldSkipDirName(app_fd.cFileName)) {
				continue;
			}
			if (!(app_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				continue;
			}
			if (ShouldSkipAppDataAppName(app_fd.cFileName, skip_app_names, skip_app_count)) {
				continue;
			}

			std::wstring app_dir = root_wide;
			if (!app_dir.empty() && app_dir.back() != L'\\') {
				app_dir += L'\\';
			}
			app_dir += app_fd.cFileName;

			std::wstring sub_pattern = app_dir;
			if (!sub_pattern.empty() && sub_pattern.back() != L'\\') {
				sub_pattern += L'\\';
			}
			sub_pattern += L'*';

			char app_label[96] = {};
			Utf8FromWide(app_fd.cFileName, app_label, sizeof(app_label));

			WIN32_FIND_DATAW sub_fd = {};
			const HANDLE sub_find = FindFirstFileW(sub_pattern.c_str(), &sub_fd);
			if (sub_find == INVALID_HANDLE_VALUE) {
				continue;
			}

			do {
				if (count >= pool_max) {
					break;
				}
				if (ShouldSkipDirName(sub_fd.cFileName)) {
					continue;
				}
				if (!(sub_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					continue;
				}
				if (!IsKnownAppDataCacheSubdir(sub_fd.cFileName)) {
					continue;
				}

				std::wstring candidate = app_dir;
				if (!candidate.empty() && candidate.back() != L'\\') {
					candidate += L'\\';
				}
				candidate += sub_fd.cFileName;

				char label[128] = {};
				char sub_utf8[64] = {};
				Utf8FromWide(sub_fd.cFileName, sub_utf8, sizeof(sub_utf8));
				snprintf(label, sizeof(label), "%s / %s", app_label, sub_utf8);
				TryPushDiscoveryEntry(pool, pool_max, count, candidate.c_str(), label, sub_fd.cFileName);
			} while (FindNextFileW(sub_find, &sub_fd));

			FindClose(sub_find);
		} while (FindNextFileW(app_find, &app_fd));

		FindClose(app_find);
		return count;
	}

	void MeasureAndSortDiscoveryPool(DiscoveryEntry* pool, size_t count)
	{
		if (pool == nullptr || count == 0) {
			return;
		}

		for (size_t i = 0; i < count; ++i) {
			wchar_t wide[MAX_PATH * 4] = {};
			if (HCleanExpandPathWide(pool[i].path, wide, MAX_PATH * 4)) {
				pool[i].bytes = HCleanMeasurePathBytes(wide, &kDiscoveryMeasureLimits, "discovery");
			}
			else {
				pool[i].bytes = 0;
			}
		}

		std::sort(pool, pool + count, [](const DiscoveryEntry& a, const DiscoveryEntry& b) {
			const int64_t av = a.bytes < 0 ? INT64_MAX / 4 : a.bytes;
			const int64_t bv = b.bytes < 0 ? INT64_MAX / 4 : b.bytes;
			return av > bv;
		});
	}

	size_t EmitSortedDiscoveryPool(const DiscoveryEntry* pool, size_t count, size_t max_out,
		char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], bool include_unknown_size)
	{
		if (pool == nullptr || out_paths == nullptr || out_labels == nullptr || max_out == 0) {
			return 0;
		}

		size_t out = 0;
		for (size_t i = 0; i < count && out < max_out; ++i) {
			if (pool[i].bytes == 0) {
				continue;
			}
			if (pool[i].bytes < 0 && !include_unknown_size) {
				continue;
			}
			strncpy_s(out_paths[out], MAX_PATH * 4, pool[i].path, _TRUNCATE);
			strncpy_s(out_labels[out], 128, pool[i].label, _TRUNCATE);
			++out;
		}
		return out;
	}

	size_t DiscoverCachesFromEnvRootSorted(const char* root_template, const char* const* skip_app_names,
		size_t skip_app_count, size_t max_out, char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128])
	{
		if (root_template == nullptr || out_paths == nullptr || out_labels == nullptr || max_out == 0) {
			return 0;
		}

		wchar_t root_wide[MAX_PATH * 4] = {};
		if (!HCleanExpandPathWide(root_template, root_wide, MAX_PATH * 4)
			|| !HCleanPathIsDirectory(root_wide)) {
			return 0;
		}

		thread_local static DiscoveryEntry pool[kDiscoveryPoolMax]{};
		const size_t pool_count = CollectAppDataStyleCandidatesFromRoot(root_wide, skip_app_names, skip_app_count,
			pool, kDiscoveryPoolMax);
		MeasureAndSortDiscoveryPool(pool, pool_count);
		return EmitSortedDiscoveryPool(pool, pool_count, max_out, out_paths, out_labels, false);
	}

	size_t CollectAllUwpCacheCandidates(DiscoveryEntry* pool, size_t pool_max)
	{
		if (pool == nullptr || pool_max == 0) {
			return 0;
		}

		wchar_t packages[MAX_PATH * 4] = {};
		if (!HCleanExpandPathWide("%LOCALAPPDATA%\\Packages", packages, MAX_PATH * 4)
			|| !HCleanPathIsDirectory(packages)) {
			return 0;
		}

		static const wchar_t* k_subs[] = {
			L"LocalCache",
			L"TempState",
			L"AC\\Temp",
			L"AC\\INetCache",
			L"INetCache",
			L"LocalState\\cache",
			L"LocalState\\logs",
			L"RoamingState\\cache",
			L"SharedLocal",
			L"SystemAppData\\Helium",
		};

		size_t count = 0;
		std::wstring pattern = packages;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return 0;
		}

		do {
			if (count >= pool_max) {
				break;
			}
			if (ShouldSkipDirName(fd.cFileName) || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				continue;
			}

			char pkg_label[96] = {};
			Utf8FromWide(fd.cFileName, pkg_label, sizeof(pkg_label));

			std::wstring pkg_dir = packages;
			if (!pkg_dir.empty() && pkg_dir.back() != L'\\') {
				pkg_dir += L'\\';
			}
			pkg_dir += fd.cFileName;

			for (const wchar_t* sub : k_subs) {
				if (count >= pool_max || sub == nullptr) {
					break;
				}
				std::wstring candidate = pkg_dir;
				if (!candidate.empty() && candidate.back() != L'\\') {
					candidate += L'\\';
				}
				candidate += sub;

				char label[128] = {};
				char sub_utf8[96] = {};
				Utf8FromWide(sub, sub_utf8, sizeof(sub_utf8));
				snprintf(label, sizeof(label), "UWP %s / %s", pkg_label, sub_utf8);
				TryPushDiscoveryEntry(pool, pool_max, count, candidate.c_str(), label, L"LocalCache");
			}
		} while (FindNextFileW(find, &fd));
		FindClose(find);
		return count;
	}

	void CollectInstallLocationFromUninstallKey(HKEY root, const wchar_t* subkey, DiscoveryEntry* pool,
		size_t pool_max, size_t& count)
	{
		if (root == nullptr || subkey == nullptr || pool == nullptr || count >= pool_max) {
			return;
		}

		HKEY hkey = nullptr;
		if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
			return;
		}

		wchar_t install_dir[MAX_PATH * 4] = {};
		DWORD install_size = static_cast<DWORD>(sizeof(install_dir));
		const LSTATUS install_status = RegQueryValueExW(hkey, L"InstallLocation", nullptr, nullptr,
			reinterpret_cast<LPBYTE>(install_dir), &install_size);
		RegCloseKey(hkey);
		if (install_status != ERROR_SUCCESS || install_dir[0] == L'\0' || !HCleanPathIsDirectory(install_dir)) {
			return;
		}

		static const wchar_t* k_subs[] = {
			L"Cache", L"cache", L"cache2", L"Logs", L"logs", L"Log", L"log",
			L"Temp", L"temp", L"tmp", L"CrashDumps", L"crash", L"backup", L"Backup",
			L"updates", L"Updates", L"Downloads", L"download", L"webcache", L"INetCache",
			L"GPUCache", L"ShaderCache",
		};
		char app_label[96] = {};
		Utf8FromWide(subkey, app_label, sizeof(app_label));

		for (const wchar_t* sub : k_subs) {
			if (count >= pool_max) {
				break;
			}
			std::wstring candidate = install_dir;
			if (!candidate.empty() && candidate.back() != L'\\') {
				candidate += L'\\';
			}
			candidate += sub;

			char label[128] = {};
			char sub_utf8[32] = {};
			Utf8FromWide(sub, sub_utf8, sizeof(sub_utf8));
			snprintf(label, sizeof(label), "安裝目錄 %s / %s", app_label, sub_utf8);
			TryPushDiscoveryEntry(pool, pool_max, count, candidate.c_str(), label, sub);
		}
	}

	size_t CollectInstallLocationCandidates(DiscoveryEntry* pool, size_t pool_max)
	{
		if (pool == nullptr || pool_max == 0) {
			return 0;
		}

		size_t count = 0;
		static const HKEY k_roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
		static const wchar_t* k_uninstall_paths[] = {
			L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
		};

		for (HKEY root : k_roots) {
			for (const wchar_t* uninstall_path : k_uninstall_paths) {
				HKEY hkey = nullptr;
				if (RegOpenKeyExW(root, uninstall_path, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
					continue;
				}

				DWORD index = 0;
				wchar_t subkey_name[256] = {};
				while (count < pool_max) {
					DWORD subkey_len = static_cast<DWORD>(sizeof(subkey_name) / sizeof(subkey_name[0]));
					const LSTATUS enum_status = RegEnumKeyExW(hkey, index++, subkey_name, &subkey_len,
						nullptr, nullptr, nullptr, nullptr);
					if (enum_status != ERROR_SUCCESS) {
						break;
					}
					wchar_t full_key[512] = {};
					_snwprintf_s(full_key, _TRUNCATE, L"%s\\%s", uninstall_path, subkey_name);
					CollectInstallLocationFromUninstallKey(root, full_key, pool, pool_max, count);
				}
				RegCloseKey(hkey);
			}
		}
		return count;
	}

	bool FileOlderThanDays(const WIN32_FIND_DATAW& fd, int min_age_days)
	{
		if (min_age_days <= 0) {
			return true;
		}
		FILETIME now_ft = {};
		GetSystemTimeAsFileTime(&now_ft);
		ULARGE_INTEGER now;
		now.LowPart = now_ft.dwLowDateTime;
		now.HighPart = now_ft.dwHighDateTime;

		ULARGE_INTEGER modified;
		modified.LowPart = fd.ftLastWriteTime.dwLowDateTime;
		modified.HighPart = fd.ftLastWriteTime.dwHighDateTime;

		const ULONGLONG age_100ns = now.QuadPart > modified.QuadPart ? now.QuadPart - modified.QuadPart : 0;
		const ULONGLONG min_age_100ns = static_cast<ULONGLONG>(min_age_days) * 24ULL * 3600ULL * 10000000ULL;
		return age_100ns >= min_age_100ns;
	}

	bool IsStaleUserFileName(const wchar_t* name)
	{
		if (name == nullptr) {
			return false;
		}
		const size_t len = wcslen(name);
		if (len >= 4 && _wcsicmp(name + len - 4, L".tmp") == 0) {
			return true;
		}
		if (len >= 4 && _wcsicmp(name + len - 4, L".log") == 0) {
			return true;
		}
		if (len >= 4 && _wcsicmp(name + len - 4, L".bak") == 0) {
			return true;
		}
		if (len >= 4 && _wcsicmp(name + len - 4, L".old") == 0) {
			return true;
		}
		if (len >= 4 && _wcsicmp(name + len - 4, L".dmp") == 0) {
			return true;
		}
		if (_wcsicmp(name, L"Thumbs.db") == 0) {
			return true;
		}
		return false;
	}

	void CollectStaleFilesInDir(const std::wstring& dir, int depth, int min_age_days, DiscoveryEntry* pool,
		size_t pool_max, size_t& count)
	{
		if (depth > 3 || count >= pool_max || dir.empty()) {
			return;
		}

		std::wstring pattern = dir;
		if (!pattern.empty() && pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return;
		}

		do {
			if (count >= pool_max) {
				break;
			}
			if (ShouldSkipDirName(fd.cFileName)) {
				continue;
			}

			std::wstring child = dir;
			if (!child.empty() && child.back() != L'\\') {
				child += L'\\';
			}
			child += fd.cFileName;

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
					CollectStaleFilesInDir(child, depth + 1, min_age_days, pool, pool_max, count);
				}
				continue;
			}

			if (!IsStaleUserFileName(fd.cFileName) || !FileOlderThanDays(fd, min_age_days)) {
				continue;
			}

			char path_utf8[MAX_PATH * 4] = {};
			if (!Utf8FromWide(child.c_str(), path_utf8, sizeof(path_utf8))) {
				continue;
			}
			bool dup = false;
			for (size_t i = 0; i < count; ++i) {
				if (_stricmp(pool[i].path, path_utf8) == 0) {
					dup = true;
					break;
				}
			}
			if (dup) {
				continue;
			}

			ULARGE_INTEGER sz;
			sz.LowPart = fd.nFileSizeLow;
			sz.HighPart = fd.nFileSizeHigh;

			char label[128] = {};
			char name_utf8[96] = {};
			Utf8FromWide(fd.cFileName, name_utf8, sizeof(name_utf8));
			snprintf(label, sizeof(label), "過期檔 %s", name_utf8);
			strncpy_s(pool[count].path, path_utf8, _TRUNCATE);
			strncpy_s(pool[count].label, label, _TRUNCATE);
			wcsncpy_s(pool[count].subdir_name, L"temp", _TRUNCATE);
			pool[count].bytes = static_cast<int64_t>(sz.QuadPart);
			++count;
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}

	size_t CollectStaleUserFileCandidates(int min_age_days, DiscoveryEntry* pool, size_t pool_max)
	{
		if (pool == nullptr || pool_max == 0) {
			return 0;
		}

		static const char* k_roots[] = {
			"%USERPROFILE%\\Downloads",
			"%USERPROFILE%\\Desktop",
			"%USERPROFILE%\\Documents",
			"%USERPROFILE%\\Pictures",
			"%USERPROFILE%\\Videos",
			"%USERPROFILE%\\Music",
			"%TEMP%",
			"%LOCALAPPDATA%\\Temp",
		};

		size_t count = 0;
		for (const char* root_template : k_roots) {
			if (count >= pool_max) {
				break;
			}
			wchar_t root_wide[MAX_PATH * 4] = {};
			if (!HCleanExpandPathWide(root_template, root_wide, MAX_PATH * 4)
				|| !HCleanPathIsDirectory(root_wide)) {
				continue;
			}
			CollectStaleFilesInDir(root_wide, 0, min_age_days, pool, pool_max, count);
		}

		std::sort(pool, pool + count, [](const DiscoveryEntry& a, const DiscoveryEntry& b) {
			return a.bytes > b.bytes;
		});
		return count;
	}
}

bool HCleanAppDataCacheSubdirDefaultSelected(const wchar_t* subdir_name)
{
	if (subdir_name == nullptr) {
		return false;
	}
	if (HCleanAppDataCacheSubdirDestructive(subdir_name)) {
		return false;
	}
	return AppDataSubdirNameEquals(subdir_name, L"Cache")
		|| AppDataSubdirNameEquals(subdir_name, L"cache")
		|| AppDataSubdirNameEquals(subdir_name, L"logs")
		|| AppDataSubdirNameEquals(subdir_name, L"Logs")
		|| AppDataSubdirNameEquals(subdir_name, L"Log")
		|| AppDataSubdirNameEquals(subdir_name, L"log")
		|| AppDataSubdirNameEquals(subdir_name, L"temp")
		|| AppDataSubdirNameEquals(subdir_name, L"Temp")
		|| AppDataSubdirNameEquals(subdir_name, L"tmp")
		|| AppDataSubdirNameEquals(subdir_name, L"GPUCache")
		|| AppDataSubdirNameEquals(subdir_name, L"Crash Reports")
		|| AppDataSubdirNameEquals(subdir_name, L"CrashReports")
		|| AppDataSubdirNameEquals(subdir_name, L"Crash Dumps")
		|| AppDataSubdirNameEquals(subdir_name, L"CrashDumps")
		|| AppDataSubdirNameEquals(subdir_name, L"Crashpad");
}

bool HCleanAppDataCacheSubdirDestructive(const wchar_t* subdir_name)
{
	if (subdir_name == nullptr) {
		return false;
	}
	return AppDataSubdirNameEquals(subdir_name, L"webcache")
		|| AppDataSubdirNameEquals(subdir_name, L"webcache2")
		|| AppDataSubdirNameEquals(subdir_name, L"Session Storage")
		|| AppDataSubdirNameEquals(subdir_name, L"workspaceStorage")
		|| AppDataSubdirNameEquals(subdir_name, L"IndexedDB")
		|| AppDataSubdirNameEquals(subdir_name, L"Service Worker")
		|| AppDataSubdirNameEquals(subdir_name, L"blob_storage");
}

size_t HCleanEnumerateAppDataCacheCandidates(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names, size_t skip_app_count)
{
	return HCleanDiscoverRoamingCachesSorted(out_paths, out_labels, max_count, skip_app_names, skip_app_count);
}

size_t HCleanDiscoverRoamingCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names, size_t skip_app_count)
{
	return DiscoverCachesFromEnvRootSorted("%APPDATA%", skip_app_names, skip_app_count, max_count, out_paths,
		out_labels);
}

size_t HCleanDiscoverLocalAppDataCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names, size_t skip_app_count)
{
	return DiscoverCachesFromEnvRootSorted("%LOCALAPPDATA%", skip_app_names, skip_app_count, max_count, out_paths,
		out_labels);
}

size_t HCleanDiscoverProgramDataCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, const char* const* skip_app_names, size_t skip_app_count)
{
	return DiscoverCachesFromEnvRootSorted("%PROGRAMDATA%", skip_app_names, skip_app_count, max_count, out_paths,
		out_labels);
}

size_t HCleanDiscoverAllUwpCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	thread_local static DiscoveryEntry pool[kDiscoveryPoolMax]{};
	const size_t pool_count = CollectAllUwpCacheCandidates(pool, kDiscoveryPoolMax);
	MeasureAndSortDiscoveryPool(pool, pool_count);
	return EmitSortedDiscoveryPool(pool, pool_count, max_count, out_paths, out_labels, false);
}

size_t HCleanDiscoverInstallLocationCachesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	thread_local static DiscoveryEntry pool[kDiscoveryPoolMax]{};
	const size_t pool_count = CollectInstallLocationCandidates(pool, kDiscoveryPoolMax);
	MeasureAndSortDiscoveryPool(pool, pool_count);
	return EmitSortedDiscoveryPool(pool, pool_count, max_count, out_paths, out_labels, false);
}

size_t HCleanDiscoverStaleUserFilesSorted(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count, int min_age_days)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	thread_local static DiscoveryEntry pool[kDiscoveryPoolMax]{};
	const size_t pool_count = CollectStaleUserFileCandidates(min_age_days, pool, kDiscoveryPoolMax);
	return EmitSortedDiscoveryPool(pool, pool_count, max_count, out_paths, out_labels, true);
}

size_t HCleanEnumerateJetBrainsIdeCachePaths(char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128],
	size_t max_count)
{
	if (out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t jetbrains_root[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide("%LOCALAPPDATA%\\JetBrains", jetbrains_root, MAX_PATH * 4)
		|| !HCleanPathIsDirectory(jetbrains_root)) {
		return 0;
	}

	static const char* k_subs[] = { "caches", "log", "tmp" };
	size_t count = 0;

	std::wstring pattern = jetbrains_root;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	do {
		if (count >= max_count) {
			break;
		}
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		char ide_name[96] = {};
		Utf8FromWide(fd.cFileName, ide_name, sizeof(ide_name));

		std::wstring ide_root = jetbrains_root;
		if (!ide_root.empty() && ide_root.back() != L'\\') {
			ide_root += L'\\';
		}
		ide_root += fd.cFileName;

		char ide_root_utf8[MAX_PATH * 4] = {};
		if (!Utf8FromWide(ide_root.c_str(), ide_root_utf8, sizeof(ide_root_utf8))) {
			continue;
		}

		char paths[8][MAX_PATH * 4] = {};
		char labels[8][128] = {};
		const size_t sub_count = HCleanEnumerateSubdirsWithCache(ide_root_utf8, k_subs, 3,
			paths, labels, 8, ide_name);
		for (size_t i = 0; i < sub_count && count < max_count; ++i) {
			strncpy_s(out_paths[count], MAX_PATH * 4, paths[i], _TRUNCATE);
			strncpy_s(out_labels[count], 128, labels[i], _TRUNCATE);
			++count;
		}
	} while (FindNextFileW(find, &fd));
	FindClose(find);

	return count;
}

size_t HCleanEnumerateWildcardChildSubdirs(const char* parent_template, const wchar_t* dir_name_glob,
	const char* const* subdir_names, size_t subdir_count, const char* label_prefix,
	char (*out_paths)[MAX_PATH * 4], char (*out_labels)[128], size_t max_count)
{
	if (parent_template == nullptr || dir_name_glob == nullptr || subdir_names == nullptr || subdir_count == 0
		|| out_paths == nullptr || out_labels == nullptr || max_count == 0) {
		return 0;
	}

	wchar_t parent[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide(parent_template, parent, MAX_PATH * 4) || !HCleanPathIsDirectory(parent)) {
		return 0;
	}

	size_t count = 0;
	std::wstring pattern = parent;
	if (!pattern.empty() && pattern.back() != L'\\') {
		pattern += L'\\';
	}
	pattern += dir_name_glob;

	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return 0;
	}

	do {
		if (count >= max_count) {
			break;
		}
		if (ShouldSkipDirName(fd.cFileName)) {
			continue;
		}
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		char child_name[128] = {};
		Utf8FromWide(fd.cFileName, child_name, sizeof(child_name));

		std::wstring child_root = parent;
		if (!child_root.empty() && child_root.back() != L'\\') {
			child_root += L'\\';
		}
		child_root += fd.cFileName;

		char child_root_utf8[MAX_PATH * 4] = {};
		if (!Utf8FromWide(child_root.c_str(), child_root_utf8, sizeof(child_root_utf8))) {
			continue;
		}

		char prefix[128] = {};
		if (label_prefix != nullptr && label_prefix[0] != '\0') {
			snprintf(prefix, sizeof(prefix), "%s %s", label_prefix, child_name);
		}
		else {
			strncpy_s(prefix, sizeof(prefix), child_name, _TRUNCATE);
		}

		const size_t added = HCleanEnumerateSubdirsWithCache(child_root_utf8, subdir_names, subdir_count,
			out_paths + count, out_labels + count, max_count - count, prefix);
		count += added;
	} while (FindNextFileW(find, &fd));
	FindClose(find);

	return count;
}

bool HCleanIsProcessRunning(const wchar_t* exe_name)
{
	if (exe_name == nullptr || exe_name[0] == L'\0') {
		return false;
	}

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return false;
	}

	PROCESSENTRY32W pe = {};
	pe.dwSize = sizeof(pe);
	bool found = false;
	if (Process32FirstW(snap, &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, exe_name) == 0) {
				found = true;
				break;
			}
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return found;
}

bool HCleanIsProcessRunningUtf8(const char* exe_name)
{
	if (exe_name == nullptr) {
		return false;
	}
	wchar_t wide[MAX_PATH] = {};
	if (!WideFromUtf8(exe_name, wide, MAX_PATH)) {
		return false;
	}
	return HCleanIsProcessRunning(wide);
}

bool HCleanShowProcessRunningPrompt(const char* app_display_name, const char* const* exe_names, size_t exe_count)
{
	if (exe_names == nullptr || exe_count == 0) {
		return true;
	}

	std::string running;
	for (size_t i = 0; i < exe_count; ++i) {
		if (HCleanIsProcessRunningUtf8(exe_names[i])) {
			if (!running.empty()) {
				running += ", ";
			}
			running += exe_names[i];
		}
	}
	if (running.empty()) {
		return true;
	}

	const std::wstring fmt = Hi18n::TrZhWide(
		u8"偵測到 %hs 相關程序仍在執行：%hs\n\n"
		u8"建議先關閉以取得最佳清理效果。\n\n"
		u8"仍要繼續清理嗎？");
	wchar_t msg[1024] = {};
	swprintf_s(msg, fmt.c_str(),
		app_display_name != nullptr ? app_display_name : I18N(u8"應用程式"),
		running.c_str());

	const int result = MessageBoxW(nullptr, msg, W18N(u8"HP CLEANER++ — 程序執行中"),
		MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
	return result == IDYES;
}

bool HCleanIsRunningAsAdmin()
{
	BOOL is_admin = FALSE;
	PSID admin_group = nullptr;
	SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
	if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
			DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
		CheckTokenMembership(nullptr, admin_group, &is_admin);
		FreeSid(admin_group);
	}
	return is_admin != FALSE;
}

bool HCleanRequestAdminElevation()
{
	return HAppShellRequestAdminElevation(false);
}

bool HCleanShowAdminElevationPrompt()
{
	return HAdminPrompt::TryGate(HAdminPrompt::Scene::Clean);
}

bool HCleanStopWindowsUpdateServices()
{
	bool ok = true;
	if (!StopServiceSimple(L"wuauserv")) {
		HLOG_WARN("Failed to stop wuauserv (may need admin)");
		ok = false;
	}
	if (!StopServiceSimple(L"bits")) {
		HLOG_WARN("Failed to stop bits (may need admin)");
		ok = false;
	}
	return ok;
}

bool HCleanStartWindowsUpdateServices()
{
	bool ok = true;
	if (!StartServiceSimple(L"bits")) {
		HLOG_WARN("Failed to start bits");
		ok = false;
	}
	if (!StartServiceSimple(L"wuauserv")) {
		HLOG_WARN("Failed to start wuauserv");
		ok = false;
	}
	return ok;
}

bool HCleanClearDeliveryOptimizationCache()
{
	wchar_t cache_path[MAX_PATH * 4] = L"C:\\ProgramData\\Microsoft\\Windows\\DeliveryOptimization\\Cache";
	if (!HCleanPathIsDirectory(cache_path)) {
		return false;
	}
	HCleanDeleteStats stats = {};
	const int64_t freed = HCleanSafeDeleteFilesDirShallow(cache_path, "delivery_optimization", &stats);
	HLOG_INFO("Delivery Optimization cache cleared: {} bytes", freed);
	return freed >= 0;
}

bool HCleanRefreshThumbnailIconCache()
{
	HCleanRunHiddenCommand(L"ie4uinit.exe -show", 15000);
	return true;
}

bool HCleanRunHiddenCommand(const wchar_t* command_line, DWORD timeout_ms)
{
	if (command_line == nullptr || command_line[0] == L'\0') {
		return false;
	}

	if (!HCleanIsRunningAsAdmin() && HElevationBroker::IsConnected()) {
		return HElevationBroker::RunHiddenCommand(command_line, timeout_ms, nullptr);
	}

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};

	std::wstring cmd = L"cmd.exe /C ";
	cmd += command_line;

	std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
	cmd_buf.push_back(L'\0');

	if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
			&pi)) {
		HLOG_WARN("CreateProcess failed for command");
		return false;
	}

	WaitForSingleObject(pi.hProcess, timeout_ms);
	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return exit_code == 0;
}

bool HCleanIsCliDetailPath(const char* path)
{
	return path != nullptr && std::strncmp(path, "@cli:", 5) == 0;
}

bool HCleanRunNpmCacheClean()
{
	return HCleanRunHiddenCommand(L"npm cache clean --force", 120000);
}

bool HCleanRunPnpmStorePrune()
{
	return HCleanRunHiddenCommand(L"pnpm store prune", 120000);
}

bool HCleanRunPnpmCacheClean()
{
	return HCleanRunHiddenCommand(L"pnpm cache clean", 120000);
}

bool HCleanRunNugetHttpCacheClear()
{
	return HCleanRunHiddenCommand(L"dotnet nuget locals http-cache --clear", 120000);
}

bool HCleanRunNugetGlobalPackagesClear()
{
	const int result = MessageBoxW(nullptr,
		W18N(u8"即將清除 NuGet 全域 packages 目錄（global-packages）。\n\n"
			u8"所有專案下次建置需重新還原 NuGet 套件，可能耗時較久。\n\n"
			u8"是否繼續？"),
		W18N(u8"HP CLEANER++ — NuGet packages 清理確認"),
		MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
	if (result != IDYES) {
		return false;
	}
	return HCleanRunHiddenCommand(L"dotnet nuget locals global-packages --clear", 120000);
}

bool HCleanRunDockerBuilderPrune()
{
	return HCleanRunHiddenCommand(L"docker builder prune -f", 180000);
}

bool HCleanRunDockerContainerPrune()
{
	const int result = MessageBoxW(nullptr,
		W18N(u8"即將執行 docker container prune 移除所有已停止的容器。\n\n"
			u8"若容器內有未備份資料，將無法復原。\n\n"
			u8"是否繼續？"),
		W18N(u8"HP CLEANER++ — Docker 容器清理確認"),
		MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
	if (result != IDYES) {
		return false;
	}
	return HCleanRunHiddenCommand(L"docker container prune -f", 180000);
}

bool HCleanRunDockerImagePrune()
{
	const int result = MessageBoxW(nullptr,
		W18N(u8"即將執行 docker image prune 移除 dangling（未標記）映像。\n\n"
			u8"不會刪除仍有容器使用的映像，但可能影響依賴這些映像的建置流程。\n\n"
			u8"是否繼續？"),
		W18N(u8"HP CLEANER++ — Docker 映像清理確認"),
		MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
	if (result != IDYES) {
		return false;
	}
	return HCleanRunHiddenCommand(L"docker image prune -f", 180000);
}

bool HCleanRunDockerVolumePrune()
{
	const int result = MessageBoxW(nullptr,
		W18N(u8"即將執行 docker volume prune 移除未使用的 Volume。\n\n"
			u8"警告：Volume 常存放資料庫與專案持久化資料，刪除後可能無法復原！\n\n"
			u8"是否仍要繼續？"),
		W18N(u8"HP CLEANER++ — Docker Volume 清理確認"),
		MB_ICONERROR | MB_YESNO | MB_DEFBUTTON2);
	if (result != IDYES) {
		return false;
	}
	return HCleanRunHiddenCommand(L"docker volume prune -f", 180000);
}

bool HCleanRunDockerSystemPruneAll()
{
	const int result = MessageBoxW(nullptr,
		W18N(u8"即將執行 docker system prune -a -f（完整清理）。\n\n"
			u8"將移除未使用的容器、網路、dangling 映像，以及所有未被容器使用的映像。\n"
			u8"可能嚴重影響進行中的開發專案！\n\n"
			u8"是否仍要繼續？"),
		W18N(u8"HP CLEANER++ — Docker 完整清理確認"),
		MB_ICONERROR | MB_YESNO | MB_DEFBUTTON2);
	if (result != IDYES) {
		return false;
	}
	return HCleanRunHiddenCommand(L"docker system prune -a -f", 180000);
}

bool HCleanIsRegistryDetailPath(const char* path)
{
	return path != nullptr && std::strncmp(path, "@registry:", 10) == 0;
}

bool HCleanCleanRegistryMuiCache()
{
	const wchar_t* key_path =
		L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache";
	const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, key_path);
	return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool HCleanCleanRegistryRecentDocs()
{
	const wchar_t* key_path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs";
	const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, key_path);
	return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool HCleanRunRegistryDetail(const char* path)
{
	if (!HCleanIsRegistryDetailPath(path)) {
		return false;
	}
	const char* id = path + 10;
	if (std::strcmp(id, "muicache") == 0) {
		return HCleanCleanRegistryMuiCache();
	}
	if (std::strcmp(id, "recentdocs") == 0) {
		return HCleanCleanRegistryRecentDocs();
	}
	return false;
}

bool HCleanRunCliDetail(const char* path)
{
	if (!HCleanIsCliDetailPath(path)) {
		return false;
	}
	const char* id = path + 5;
	if (std::strcmp(id, "dns-flush") == 0) {
		return HCleanRunHiddenCommand(L"ipconfig /flushdns", 30000);
	}
	if (std::strcmp(id, "docker-builder-prune") == 0) {
		return HCleanRunDockerBuilderPrune();
	}
	if (std::strcmp(id, "docker-container-prune") == 0) {
		return HCleanRunDockerContainerPrune();
	}
	if (std::strcmp(id, "docker-image-prune") == 0) {
		return HCleanRunDockerImagePrune();
	}
	if (std::strcmp(id, "docker-volume-prune") == 0) {
		return HCleanRunDockerVolumePrune();
	}
	if (std::strcmp(id, "docker-system-prune-all") == 0) {
		return HCleanRunDockerSystemPruneAll();
	}
	if (std::strcmp(id, "npm-cache-clean") == 0) {
		return HCleanRunNpmCacheClean();
	}
	if (std::strcmp(id, "pnpm-store-prune") == 0) {
		return HCleanRunPnpmStorePrune();
	}
	if (std::strcmp(id, "pnpm-cache-clean") == 0) {
		return HCleanRunPnpmCacheClean();
	}
	if (std::strcmp(id, "nuget-http-cache-clear") == 0) {
		return HCleanRunNugetHttpCacheClear();
	}
	if (std::strcmp(id, "nuget-global-packages-clear") == 0) {
		return HCleanRunNugetGlobalPackagesClear();
	}
	return false;
}

void HCleanRefreshDetailSizes(HCleanDetailEntry* entries, size_t count, int64_t& cached_bytes)
{
	if (entries == nullptr) {
		cached_bytes = 0;
		return;
	}

	wchar_t wide[MAX_PATH * 4] = {};
	for (size_t i = 0; i < count; ++i) {
		if (entries[i].path == nullptr) {
			continue;
		}
		if (HCleanIsCliDetailPath(entries[i].path) || HCleanIsRegistryDetailPath(entries[i].path)) {
			continue;
		}
		int64_t measured = 0;
		if (HCleanExpandPathWide(entries[i].path, wide, sizeof(wide) / sizeof(wide[0]))) {
			measured = HCleanMeasurePathBytes(wide, nullptr, "refresh");
		}
		entries[i].bytes = measured;
	}
	HCleanSumSelectedDetailBytes(entries, count, cached_bytes);
}

void HCleanDetailListTask::EnsureDetails() const
{
	if (details_ready_) {
		return;
	}
	if (real_scan_queued_) {
		return;
	}
	detail_count_ = 0;
	const_cast<HCleanDetailListTask*>(this)->BuildDetails();
	details_ready_ = true;
}

void HCleanDetailListTask::EnsurePathsOnly() const
{
	if (detail_count_ > 0) {
		return;
	}
	detail_count_ = 0;
	const_cast<HCleanDetailListTask*>(this)->BuildDetails();
}

void HCleanDetailListTask::RealScanDetails() const
{
	EnsureDetails();
	HCleanRefreshDetailSizes(const_cast<HCleanDetailEntry*>(details_), detail_count_,
		const_cast<int64_t&>(cached_bytes_));
}

void HCleanDetailListTask::RequestAsyncRealScan() const
{
	auto* self = const_cast<HCleanDetailListTask*>(this);
	if (real_scan_queued_ || path_build_queued_) {
		return;
	}
	self->path_build_queued_ = true;
	QueueTaskPathBuild(self);
}

void HCleanDetailListTask::RunPathBuildAndQueueScanOnWorker()
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	path_build_queued_ = false;
	if (real_scan_queued_) {
		return;
	}

	EnsurePathsOnly();

	if (detail_count_ == 0) {
		cached_bytes_ = 0;
		details_ready_ = true;
		scan_.state = HCleanScanState::Done;
		scan_.percent = 100.f;
		scan_.status_text = u8"掃描完成";
		return;
	}

	real_scan_queued_ = true;
	scan_.state = HCleanScanState::Scanning;
	scan_.percent = 0.f;
	scan_.status_text = u8"計算大小…";

	HCleanAsyncScanPathCopy paths[kMaxDetails]{};
	size_t path_count = 0;
	for (size_t i = 0; i < detail_count_; ++i) {
		if (details_[i].path == nullptr) {
			continue;
		}
		strncpy_s(paths[path_count].path, details_[i].path, _TRUNCATE);
		paths[path_count].selected = details_[i].selected;
		++path_count;
	}

	if (path_count == 0) {
		real_scan_queued_ = false;
		cached_bytes_ = 0;
		details_ready_ = true;
		scan_.state = HCleanScanState::Done;
		scan_.percent = 100.f;
		scan_.status_text = u8"掃描完成";
		return;
	}

	HCleanSubmitAsyncScanWork(this, paths, path_count);
}

void HCleanSubmitAsyncScanWork(HCleanDetailListTask* task, const HCleanAsyncScanPathCopy* paths, size_t path_count)
{
	if (task == nullptr || paths == nullptr || path_count == 0) {
		return;
	}

	std::vector<AsyncScanPathItem> copied;
	copied.reserve(path_count);
	for (size_t i = 0; i < path_count; ++i) {
		AsyncScanPathItem item;
		strncpy_s(item.path, paths[i].path, _TRUNCATE);
		item.selected = paths[i].selected;
		copied.push_back(item);
	}
	SubmitAsyncScanJob(task, copied.data(), copied.size());
}

void HCleanDetailListTask::ApplyMeasuredDetails(const int64_t* detail_bytes, size_t count, int64_t cached_bytes)
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	if (detail_bytes != nullptr && count > 0) {
		const size_t n = (std::min)(detail_count_, count);
		for (size_t i = 0; i < n; ++i) {
			details_[i].bytes = detail_bytes[i];
		}
	}
	cached_bytes_ = cached_bytes;
	details_ready_ = true;
	real_scan_queued_ = false;
	scan_result_pending_ = false;
	scan_.state = HCleanScanState::Done;
	scan_.percent = 100.f;
	scan_.status_text = u8"掃描完成";
	HLOG_INFO("Scan finished: task '{}', {} bytes", GetId(), cached_bytes_);
}

void HCleanDetailListTask::RefreshSize()
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	HLOG_INFO("Scan started: task '{}'", GetId());
	details_ready_ = false;
	real_scan_queued_ = false;
	path_build_queued_ = false;
	scan_result_pending_ = false;
	detail_count_ = 0;
	cached_bytes_ = -1;
	scan_.state = HCleanScanState::Scanning;
	scan_.percent = 0.f;
	scan_.status_text = u8"開始掃描…";
	RequestAsyncRealScan();
}

HCleanScanProgress HCleanDetailListTask::GetScanProgress() const
{
	bool async_pending = false;
	HCleanScanState state = HCleanScanState::Idle;
	float percent = 0.f;
	const char* status_text = scan_.status_text;

	{
		std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
		async_pending = real_scan_queued_ && !details_ready_;
		if (details_ready_ || !async_pending) {
			state = scan_.state;
			percent = scan_.percent;
			status_text = scan_.status_text;
		}
	}

	if (async_pending) {
		const float live_percent = HCleanGetTaskAsyncRealScanPercent(this);
		const char* live_status = HCleanGetTaskAsyncRealScanStatus(this);
		return { HCleanScanState::Scanning, live_percent, live_status };
	}

	return { state, percent, status_text };
}

HCleanSizeInfo HCleanDetailListTask::GetSize() const
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	return { cached_bytes_, cached_bytes_ >= 0 };
}

bool HCleanDetailListTask::ShouldShowInUI() const
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	if (scan_.state == HCleanScanState::Scanning || real_scan_queued_) {
		return true;
	}
	if (scan_.state != HCleanScanState::Done && cached_bytes_ < 0) {
		return true;
	}

	if (!details_ready_ && detail_count_ == 0) {
		return true;
	}

	if (detail_count_ == 0) {
		return false;
	}

	for (size_t i = 0; i < detail_count_; ++i) {
		if (details_[i].bytes > 0 || details_[i].bytes < 0) {
			return true;
		}
	}
	return false;
}

bool HCleanDetailListTask::CleanSelectedDetails()
{
	return CleanSelectedDetailsWithProgress(GetName());
}

bool HCleanDetailListTask::CleanSelectedDetailsWithProgress(const char* task_display_name)
{
	EnsureDetails();
	int64_t freed = 0;
	wchar_t wide[MAX_PATH * 4] = {};

	size_t selected_total = 0;
	for (size_t i = 0; i < detail_count_; ++i) {
		if (details_[i].selected && details_[i].path != nullptr) {
			++selected_total;
		}
	}

	size_t selected_done = 0;
	for (size_t i = 0; i < detail_count_; ++i) {
		if (!details_[i].selected || details_[i].path == nullptr) {
			continue;
		}

		HCleanDeleteStats stats = {};
		HCleanReportCleanProgress(task_display_name, details_[i].path, selected_done, selected_total, &stats);

		if (HCleanIsCliDetailPath(details_[i].path)) {
			if (HCleanRunCliDetail(details_[i].path)) {
				stats.files_deleted += 1;
			}
			++selected_done;
			HCleanReportCleanProgress(task_display_name, details_[i].path, selected_done, selected_total, &stats);
			continue;
		}

		if (HCleanIsRegistryDetailPath(details_[i].path)) {
			if (HCleanRunRegistryDetail(details_[i].path)) {
				stats.files_deleted += 1;
			}
			++selected_done;
			HCleanReportCleanProgress(task_display_name, details_[i].path, selected_done, selected_total, &stats);
			continue;
		}

		if (!HCleanExpandPathWide(details_[i].path, wide, sizeof(wide) / sizeof(wide[0]))) {
			++selected_done;
			continue;
		}

		const DWORD path_attrs = GetFileAttributesW(wide);
		if (path_attrs != INVALID_FILE_ATTRIBUTES && !(path_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			LARGE_INTEGER sz = {};
			HANDLE fh = CreateFileW(wide, FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (fh != INVALID_HANDLE_VALUE && GetFileSizeEx(fh, &sz)) {
				if (DeleteFileW(wide)) {
					freed += sz.QuadPart;
					++stats.files_deleted;
				}
				CloseHandle(fh);
			}
			else if (fh != INVALID_HANDLE_VALUE) {
				CloseHandle(fh);
			}
			++selected_done;
			HCleanReportCleanProgress(task_display_name, details_[i].path, selected_done, selected_total, &stats);
			continue;
		}

		const int64_t detail_freed = HCleanSafeDeleteFilesDirShallow(wide, GetId(), &stats, &walk_limits_);
		freed += detail_freed;

		HLOG_INFO("Clean detail: task '{}', path '{}', freed {} bytes, skip locked={} denied={} timeout={}",
			GetId(), details_[i].path, detail_freed, stats.skip_locked, stats.skip_access_denied, stats.skip_timeout);

		++selected_done;
		HCleanReportCleanProgress(task_display_name, details_[i].path, selected_done, selected_total, &stats);
	}

	last_freed_bytes_ = freed;
	for (size_t i = 0; i < detail_count_; ++i) {
		if (details_[i].path == nullptr) {
			continue;
		}
		int64_t remaining = 0;
		if (HCleanExpandPathWide(details_[i].path, wide, sizeof(wide) / sizeof(wide[0]))) {
			remaining = HCleanMeasurePathBytes(wide, &walk_limits_, GetId());
		}
		details_[i].bytes = remaining;
	}
	HCleanSumSelectedDetailBytes(details_, detail_count_, cached_bytes_);
	scan_.state = HCleanScanState::Done;
	scan_.percent = 100.f;
	scan_.status_text = u8"已清理";
	details_ready_ = true;
	HLOG_INFO("Clean succeeded: task '{}', freed {} bytes, remaining {} bytes",
		GetId(), freed, cached_bytes_);
	return true;
}

size_t HCleanDetailListTask::GetDetailEntryCount() const
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	return detail_count_;
}

HCleanDetailEntry* HCleanDetailListTask::GetDetailEntry(size_t index)
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	if (index >= detail_count_) {
		return nullptr;
	}
	return &details_[index];
}

void HCleanDetailListTask::ApplyDetailSelection()
{
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	if (!details_ready_ || detail_count_ == 0) {
		return;
	}
	HCleanSumSelectedDetailBytes(details_, detail_count_, cached_bytes_);
	HLOG_DEBUG("Detail selection applied: task '{}', {} bytes", GetId(), cached_bytes_);
}

bool HCleanDetailListTask::IsDestructiveTask() const
{
	if (!IsSelected()) {
		return false;
	}
	std::lock_guard<std::recursive_mutex> lock(g_detail_task_state_mutex);
	if (!details_ready_ || detail_count_ == 0) {
		return false;
	}
	for (size_t i = 0; i < detail_count_; ++i) {
		if (details_[i].selected && details_[i].destructive) {
			return true;
		}
	}
	return false;
}

void HCleanDetailListTask::AddFileDetailIfExists(const char* path_template, const char* label, int64_t fallback_bytes,
	bool selected, const char* usage, const char* impact, bool destructive) const
{
	if (detail_count_ >= kMaxDetails || path_template == nullptr) {
		return;
	}
	wchar_t wide[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide(path_template, wide, MAX_PATH * 4) || !HCleanPathIsFile(wide)) {
		return;
	}
	AddDetail(path_template, label, fallback_bytes, selected, usage, impact, destructive);
}

void HCleanDetailListTask::AddDetail(const char* path_template, const char* label, int64_t fallback_bytes,
	bool selected, const char* usage, const char* impact, bool destructive) const
{
	if (detail_count_ >= kMaxDetails) {
		return;
	}
	char* buf = path_bufs_[detail_count_];
	if (!HCleanExpandPathUtf8(path_template, buf, sizeof(path_bufs_[0]))) {
		strncpy_s(buf, sizeof(path_bufs_[0]), path_template, _TRUNCATE);
	}
	const int64_t initial_bytes = (path_template != nullptr && HCleanIsCliDetailPath(path_template))
		? fallback_bytes
		: 0;
	const char* label_ptr = label;
	if (label != nullptr) {
		strncpy_s(label_bufs_[detail_count_], sizeof(label_bufs_[0]), label, _TRUNCATE);
		label_ptr = label_bufs_[detail_count_];
	}
	details_[detail_count_] = { buf, label_ptr, usage, impact, initial_bytes, selected, destructive };
	++detail_count_;
}

void HCleanDetailListTask::AddDetailPath(const char* path_utf8, const char* label, int64_t fallback_bytes,
	bool selected, const char* usage, const char* impact, bool destructive) const
{
	if (detail_count_ >= kMaxDetails || path_utf8 == nullptr) {
		return;
	}
	char* buf = path_bufs_[detail_count_];
	strncpy_s(buf, sizeof(path_bufs_[0]), path_utf8, _TRUNCATE);
	(void)fallback_bytes;
	const char* label_ptr = label;
	if (label != nullptr) {
		strncpy_s(label_bufs_[detail_count_], sizeof(label_bufs_[0]), label, _TRUNCATE);
		label_ptr = label_bufs_[detail_count_];
	}
	else {
		label_ptr = buf;
	}
	details_[detail_count_] = { buf, label_ptr, usage, impact, 0, selected, destructive };
	++detail_count_;
}

void HCleanDetailListTask::AddChromiumProfileCaches(const char* user_data_template, bool default_selected) const
{
	char paths[kMaxDetails][MAX_PATH * 4] = {};
	char labels[kMaxDetails][128] = {};
	const size_t count = HCleanEnumerateChromiumProfileCaches(user_data_template, paths, labels, kMaxDetails);
	for (size_t i = 0; i < count; ++i) {
		AddDetailPath(paths[i], labels[i], 200LL * 1024 * 1024, default_selected && i < 8,
			"瀏覽器設定檔快取",
			"網站首次載入可能較慢；不影響書籤與密碼");
	}
}

void HCleanDetailListTask::AddFirefoxProfileCaches(bool default_selected) const
{
	char paths[kMaxDetails][MAX_PATH * 4] = {};
	char labels[kMaxDetails][128] = {};
	const size_t count = HCleanEnumerateFirefoxProfileCaches(paths, labels, kMaxDetails);
	for (size_t i = 0; i < count; ++i) {
		AddDetailPath(paths[i], labels[i], 520LL * 1024 * 1024, default_selected,
			"Firefox 設定檔中的快取與離線內容",
			"網站首次開啟可能重新下載資源");
	}
}

void HCleanDetailListTask::AddSteamLibraryDetails(bool include_downloading) const
{
	char lib_paths[kMaxDetails][MAX_PATH * 4] = {};
	char lib_labels[kMaxDetails][128] = {};
	const size_t lib_count = HCleanParseSteamLibraryPaths(lib_paths, lib_labels, kMaxDetails);

	for (size_t i = 0; i < lib_count; ++i) {
		char path[MAX_PATH * 4] = {};
		snprintf(path, sizeof(path), "%s\\steamapps\\shadercache", lib_paths[i]);
		AddDetailPath(path, "著色器快取", 800LL * 1024 * 1024, true,
			"Steam 著色器編譯快取",
			"首次進遊戲可能需重建著色器");

		if (include_downloading) {
			snprintf(path, sizeof(path), "%s\\steamapps\\downloading", lib_paths[i]);
			AddDetailPath(path, "下載中暫存", 400LL * 1024 * 1024, true,
				"進行中下載的暫存檔",
				"中斷中的下載需重新開始",
				true);
		}

		if (i == 0) {
			snprintf(path, sizeof(path), "%s\\appcache", lib_paths[i]);
			AddDetailPath(path, "應用快取", 220LL * 1024 * 1024, true,
				"Steam 用戶端 UI 快取",
				"商店頁可能重新載入");
		}
	}
}

void HCleanDetailListTask::AddDetailIfExists(const char* path_template, const char* label, int64_t fallback_bytes,
	bool selected, const char* usage, const char* impact, bool destructive) const
{
	if (detail_count_ >= kMaxDetails || path_template == nullptr) {
		return;
	}
	wchar_t wide[MAX_PATH * 4] = {};
	if (!HCleanExpandPathWide(path_template, wide, MAX_PATH * 4) || !HCleanPathIsDirectory(wide)) {
		return;
	}
	AddDetail(path_template, label, fallback_bytes, selected, usage, impact, destructive);
}

void HCleanDetailListTask::AddUnrealEngineCacheDetails(bool default_selected) const
{
	char paths[kMaxDetails][MAX_PATH * 4] = {};
	char labels[kMaxDetails][128] = {};
	const size_t room = kMaxDetails - detail_count_;
	const size_t count = HCleanEnumerateUnrealEngineCachePaths(paths, labels, room);
	for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
		const bool selected = default_selected && i < 6;
		AddDetailPath(paths[i], labels[i], 400LL * 1024 * 1024, selected,
			"Unreal 著色器 / DDC / 中介快取",
			"首次開啟專案或編輯器可能重新編譯");
	}
}

void HCleanDetailListTask::AddDiscoveredPathEntries(char paths[][MAX_PATH * 4], char labels[][128], size_t count,
	const char* usage, const char* impact_prefix) const
{
	for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
		const char* slash = std::strrchr(labels[i], '/');
		if (slash == nullptr) {
			slash = std::strrchr(labels[i], '\\');
		}
		wchar_t subdir[64] = L"Cache";
		if (slash != nullptr) {
			while (*slash == '/' || *slash == ' ') {
				++slash;
			}
			wchar_t wide[64] = {};
			MultiByteToWideChar(CP_UTF8, 0, slash, -1, wide, 64);
			wcsncpy_s(subdir, wide, _TRUNCATE);
		}

		const bool destructive = HCleanAppDataCacheSubdirDestructive(subdir);
		const bool selected = HCleanAppDataCacheSubdirDefaultSelected(subdir);
		char impact[128] = {};
		if (impact_prefix != nullptr) {
			snprintf(impact, sizeof(impact), "%s", destructive ? "可能影響登入或工作階段" : impact_prefix);
		}
		AddDetailPath(paths[i], labels[i], 180LL * 1024 * 1024, selected,
			usage != nullptr ? usage : "智慧發現的快取路徑", impact, destructive);
	}
}

void HCleanDetailListTask::AddSortedRoamingDiscoveryDetails(size_t max_slots, const char* const* skip_app_names,
	size_t skip_app_count) const
{
	if (max_slots == 0 || detail_count_ >= kMaxDetails) {
		return;
	}
	const size_t room = (std::min)(max_slots, kMaxDetails - detail_count_);
	thread_local static char paths[kMaxDetails][MAX_PATH * 4];
	thread_local static char labels[kMaxDetails][128];
	const size_t count = HCleanDiscoverRoamingCachesSorted(paths, labels, room, skip_app_names, skip_app_count);
	AddDiscoveredPathEntries(paths, labels, count, "動態發現 %APPDATA% 快取", "多為可重建快取");
}

void HCleanDetailListTask::AddSortedLocalDiscoveryDetails(size_t max_slots, const char* const* skip_app_names,
	size_t skip_app_count) const
{
	if (max_slots == 0 || detail_count_ >= kMaxDetails) {
		return;
	}
	const size_t room = (std::min)(max_slots, kMaxDetails - detail_count_);
	thread_local static char paths[kMaxDetails][MAX_PATH * 4];
	thread_local static char labels[kMaxDetails][128];
	const size_t count = HCleanDiscoverLocalAppDataCachesSorted(paths, labels, room, skip_app_names, skip_app_count);
	AddDiscoveredPathEntries(paths, labels, count, "動態發現 %LOCALAPPDATA% 快取", "多為可重建快取");
}

void HCleanDetailListTask::AddSortedProgramDataDiscoveryDetails(size_t max_slots, const char* const* skip_app_names,
	size_t skip_app_count) const
{
	if (max_slots == 0 || detail_count_ >= kMaxDetails) {
		return;
	}
	const size_t room = (std::min)(max_slots, kMaxDetails - detail_count_);
	thread_local static char paths[kMaxDetails][MAX_PATH * 4];
	thread_local static char labels[kMaxDetails][128];
	const size_t count = HCleanDiscoverProgramDataCachesSorted(paths, labels, room, skip_app_names, skip_app_count);
	AddDiscoveredPathEntries(paths, labels, count, "動態發現 %PROGRAMDATA% 快取", "多為共用程式快取");
}

void HCleanDetailListTask::AddSortedUwpDiscoveryDetails(size_t max_slots) const
{
	if (max_slots == 0 || detail_count_ >= kMaxDetails) {
		return;
	}
	const size_t room = (std::min)(max_slots, kMaxDetails - detail_count_);
	thread_local static char paths[kMaxDetails][MAX_PATH * 4];
	thread_local static char labels[kMaxDetails][128];
	const size_t count = HCleanDiscoverAllUwpCachesSorted(paths, labels, room);
	AddDiscoveredPathEntries(paths, labels, count, "已安裝 UWP 應用快取", "商店/UWP 應用可能重新載入");
}

void HCleanDetailListTask::AddSortedInstallLocationDiscoveryDetails(size_t max_slots) const
{
	if (max_slots == 0 || detail_count_ >= kMaxDetails) {
		return;
	}
	const size_t room = (std::min)(max_slots, kMaxDetails - detail_count_);
	thread_local static char paths[kMaxDetails][MAX_PATH * 4];
	thread_local static char labels[kMaxDetails][128];
	const size_t count = HCleanDiscoverInstallLocationCachesSorted(paths, labels, room);
	AddDiscoveredPathEntries(paths, labels, count, "登錄安裝路徑下的快取", "首次啟動可能略慢");
}

void HCleanDetailListTask::AddSortedStaleFileDiscoveryDetails(size_t max_slots) const
{
	if (max_slots == 0 || detail_count_ >= kMaxDetails) {
		return;
	}
	const size_t room = (std::min)(max_slots, kMaxDetails - detail_count_);
	thread_local static char paths[kMaxDetails][MAX_PATH * 4];
	thread_local static char labels[kMaxDetails][128];
	const size_t count = HCleanDiscoverStaleUserFilesSorted(paths, labels, room, 30);
	for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
		AddFileDetailIfExists(paths[i], labels[i], 48LL * 1024 * 1024, false,
			"超過 30 天未修改的暫存/日誌檔",
			"永久刪除檔案；請確認非重要資料",
			true);
	}
}

void HCleanDetailListTask::AddRecycleBinDetails(bool default_selected) const
{
	char paths[kMaxDetails][MAX_PATH * 4] = {};
	char labels[kMaxDetails][128] = {};
	const size_t count = HCleanEnumerateRecycleBinPaths(paths, labels, kMaxDetails);
	for (size_t i = 0; i < count; ++i) {
		AddDetailPath(paths[i], labels[i], 100LL * 1024 * 1024, default_selected && i == 0,
			"磁碟回收筒內容",
			"清理將永久刪除所有項目",
			true);
	}
}

void HCleanTickAsyncScanWorker()
{
	TickAsyncScanWorkerImpl();
}

void HCleanShutdownAsyncScanWorker()
{
	if (!g_async_scan_thread_started.load(std::memory_order_acquire)) {
		return;
	}

	g_async_scan_stop.store(true, std::memory_order_release);
	g_async_scan_cv.notify_all();

	if (g_async_scan_thread.joinable()) {
		g_async_scan_thread.join();
	}

	{
		std::lock_guard<std::mutex> lock(g_async_scan_mutex);
		g_path_build_queue.clear();
		g_async_scan_queue.clear();
		g_async_scan_results.clear();
	}

	g_async_scan_active_task.store(nullptr, std::memory_order_release);
	g_async_scan_thread_started.store(false, std::memory_order_release);
}

bool HCleanIsAsyncScanWorkerBusy()
{
	return IsAsyncScanWorkerBusyImpl();
}

bool HCleanIsTaskAsyncRealScanActive(const HCleanDetailListTask* task)
{
	if (task == nullptr) {
		return false;
	}
	if (task->IsAsyncRealScanPending()) {
		return true;
	}
	return GetAsyncScanActiveTaskImpl() == task;
}

float HCleanGetTaskAsyncRealScanPercent(const HCleanDetailListTask* task)
{
	if (task == nullptr || !task->IsAsyncRealScanPending()) {
		return 0.f;
	}
	if (task->IsScanResultPending()) {
		return 99.f;
	}
	if (GetAsyncScanActiveTaskImpl() != task) {
		return 0.f;
	}
	const size_t total = GetAsyncScanActivePathTotalImpl();
	if (total == 0) {
		return 0.f;
	}
	const size_t idx = GetAsyncScanActivePathIdxImpl();
	const int files_seen = GetAsyncScanActiveFilesSeenImpl();
	const int files_max = GetAsyncScanActiveFilesMaxImpl();
	float path_progress = 0.f;
	if (files_max > 0) {
		path_progress = static_cast<float>(files_seen) / static_cast<float>(files_max);
		if (path_progress > 1.f) {
			path_progress = 1.f;
		}
	}
	const float overall = (static_cast<float>(idx) + path_progress) / static_cast<float>(total);
	return overall * 100.f;
}

const char* HCleanGetTaskAsyncRealScanStatus(const HCleanDetailListTask* task)
{
	if (task == nullptr || !task->IsAsyncRealScanPending()) {
		return "開始掃描…";
	}
	if (task->IsScanResultPending()) {
		return "整理結果…";
	}
	if (GetAsyncScanActiveTaskImpl() != task) {
		return "排隊中…";
	}
	const size_t total = GetAsyncScanActivePathTotalImpl();
	const size_t idx = GetAsyncScanActivePathIdxImpl();
	static thread_local char status[64];
	if (total > 0) {
		snprintf(status, sizeof(status), "掃描中 明細 %zu/%zu", idx + 1, total);
		return status;
	}
	return "計算大小…";
}
