#include "MainPageDiskScan.h"
#include "HPage.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MainPageDiskScan {
	namespace {
		constexpr int kCategoryCount = static_cast<int>(Category::Count);
		constexpr double kScanTimeoutSec = 12.0;
		constexpr int kMaxWalkDepth = 3;
		constexpr int kMaxFilesPerDir = 800;

		static const char* kLabels[kCategoryCount] = {
			"系統", "程式", "使用者", "暫存", "其他", "可用"
		};

		static const char* kStatusLabels[kCategoryCount] = {
			"掃描：系統", "掃描：程式", "掃描：使用者", "掃描：暫存", "掃描：其他", "掃描：可用"
		};

		std::mutex g_mutex;
		Snapshot g_snapshot = {};
		std::thread g_worker;
		std::atomic<bool> g_scanning{ false };
		std::atomic<bool> g_cancel{ false };
		std::atomic<bool> g_shutdown{ false };
		std::atomic<float> g_progress{ 0.f };
		char g_status_text[64] = "準備掃描…";
		bool g_init_done = false;
		bool g_defer_worker = false;

		static void SetStatusText(const char* text, float progress)
		{
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				strncpy_s(g_status_text, text, _TRUNCATE);
				g_snapshot.status_text[0] = '\0';
				strncpy_s(g_snapshot.status_text, text, _TRUNCATE);
				g_snapshot.progress = progress;
			}
			g_progress.store(progress, std::memory_order_release);
		}

		static std::wstring GetWindowsDriveRoot()
		{
			wchar_t win_dir[MAX_PATH] = {};
			const UINT n = GetWindowsDirectoryW(win_dir, MAX_PATH);
			if (n == 0 || n >= MAX_PATH || win_dir[1] != L':') {
				return L"C:\\";
			}
			return std::wstring(1, win_dir[0]) + L":\\";
		}

		static bool IsNetworkDrive(const std::wstring& root)
		{
			return GetDriveTypeW(root.c_str()) == DRIVE_REMOTE;
		}

		static bool ShouldSkipDirName(const wchar_t* name)
		{
			if (name == nullptr || name[0] == L'\0') {
				return true;
			}
			if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
				return true;
			}
			if (_wcsicmp(name, L"$Recycle.Bin") == 0) {
				return true;
			}
			if (_wcsicmp(name, L"System Volume Information") == 0) {
				return true;
			}
			if (_wcsicmp(name, L"$WinREAgent") == 0) {
				return true;
			}
			return false;
		}

		static uint64_t DirSizeShallow(const std::wstring& path, int depth,
			int& files_seen,
			const std::chrono::steady_clock::time_point& deadline)
		{
			if (g_cancel.load(std::memory_order_relaxed)) {
				return 0;
			}
			if (depth > kMaxWalkDepth) {
				return 0;
			}
			if (files_seen >= kMaxFilesPerDir) {
				return 0;
			}
			if (std::chrono::steady_clock::now() >= deadline) {
				return 0;
			}

			const DWORD attrs = GetFileAttributesW(path.c_str());
			if (attrs != INVALID_FILE_ATTRIBUTES
				&& (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
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

			uint64_t total = 0;
			do {
				if (g_cancel.load(std::memory_order_relaxed)) {
					break;
				}
				if (std::chrono::steady_clock::now() >= deadline) {
					break;
				}
				if (files_seen >= kMaxFilesPerDir) {
					break;
				}

				const wchar_t* name = fd.cFileName;
				if (ShouldSkipDirName(name)) {
					continue;
				}

				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
						continue;
					}
					std::wstring sub = path;
					if (!sub.empty() && sub.back() != L'\\') {
						sub += L'\\';
					}
					sub += name;
					total += DirSizeShallow(sub, depth + 1, files_seen, deadline);
				}
				else {
					++files_seen;
					ULARGE_INTEGER file_size;
					file_size.LowPart = fd.nFileSizeLow;
					file_size.HighPart = fd.nFileSizeHigh;
					total += file_size.QuadPart;
				}
			} while (FindNextFileW(find, &fd));

			FindClose(find);
			return total;
		}

		static uint64_t MeasurePath(const std::wstring& path, const char* status,
			const std::chrono::steady_clock::time_point& deadline, float progress_after)
		{
			SetStatusText(status, progress_after);

			const DWORD attrs = GetFileAttributesW(path.c_str());
			if (attrs == INVALID_FILE_ATTRIBUTES) {
				return 0;
			}

			int files_seen = 0;
			return DirSizeShallow(path, 0, files_seen, deadline);
		}

		static void FillVolumeLabel(const std::wstring& root, char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}
			out[0] = '\0';

			wchar_t vol_name[MAX_PATH + 1] = {};
			wchar_t fs_name[MAX_PATH + 1] = {};
			if (GetVolumeInformationW(root.c_str(), vol_name, MAX_PATH,
				nullptr, nullptr, nullptr, fs_name, MAX_PATH)) {
				WideCharToMultiByte(CP_UTF8, 0, vol_name, -1, out,
					static_cast<int>(out_size), nullptr, nullptr);
			}
		}

		static void BuildSnapshotLocked(const std::wstring& root,
			uint64_t total_bytes, uint64_t free_bytes,
			uint64_t system_b, uint64_t programs_b, uint64_t users_b,
			uint64_t temp_b, bool failed)
		{
			g_snapshot = {};
			wcsncpy_s(g_snapshot.drive_root, root.c_str(), _TRUNCATE);
			FillVolumeLabel(root, g_snapshot.volume_label, sizeof(g_snapshot.volume_label));

			g_snapshot.total_bytes = total_bytes;
			g_snapshot.free_bytes = free_bytes;
			g_snapshot.failed = failed;
			g_snapshot.scanning = false;
			g_snapshot.progress = 1.f;
			strncpy_s(g_snapshot.status_text, "掃描完成", _TRUNCATE);

			if (total_bytes == 0) {
				g_snapshot.valid = false;
				g_snapshot.segment_count = 0;
				return;
			}

			const uint64_t used = (total_bytes > free_bytes) ? (total_bytes - free_bytes) : 0;
			uint64_t other = 0;
			const uint64_t measured = system_b + programs_b + users_b + temp_b;
			if (used > measured) {
				other = used - measured;
			}
			else if (measured > used && measured > 0) {
				const double scale = static_cast<double>(used) / static_cast<double>(measured);
				system_b = static_cast<uint64_t>(static_cast<double>(system_b) * scale);
				programs_b = static_cast<uint64_t>(static_cast<double>(programs_b) * scale);
				users_b = static_cast<uint64_t>(static_cast<double>(users_b) * scale);
				temp_b = static_cast<uint64_t>(static_cast<double>(temp_b) * scale);
				other = 0;
			}

			const uint64_t cat_bytes[kCategoryCount] = {
				system_b, programs_b, users_b, temp_b, other, free_bytes
			};

			g_snapshot.segment_count = kCategoryCount;
			for (int i = 0; i < kCategoryCount; ++i) {
				g_snapshot.segments[i].label = kLabels[i];
				g_snapshot.segments[i].bytes = cat_bytes[i];
				g_snapshot.segments[i].fraction =
					static_cast<float>(static_cast<double>(cat_bytes[i])
						/ static_cast<double>(total_bytes));
			}
			g_snapshot.valid = true;
		}

		static void RunScanWorker()
		{
			HLOG_INFO("MainPage disk storage scan started");
			g_scanning.store(true, std::memory_order_release);
			g_cancel.store(false, std::memory_order_release);
			g_progress.store(0.f, std::memory_order_release);
			SetStatusText("準備掃描…", 0.f);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.scanning = true;
				g_snapshot.valid = false;
				g_snapshot.failed = false;
				g_snapshot.progress = 0.f;
				g_snapshot.segment_count = 0;
			}

			const std::wstring root = GetWindowsDriveRoot();
			if (IsNetworkDrive(root)) {
				HLOG_WARN("MainPage disk scan skipped: Windows drive is a network volume");
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.scanning = false;
				g_snapshot.failed = true;
				strncpy_s(g_snapshot.status_text, "略過：網路磁碟", _TRUNCATE);
				g_scanning.store(false, std::memory_order_release);
				return;
			}

			ULARGE_INTEGER free_bytes{}, total_bytes{}, total_free{};
			if (!GetDiskFreeSpaceExW(root.c_str(), &free_bytes, &total_bytes, &total_free)) {
				HLOG_ERROR("MainPage disk scan: GetDiskFreeSpaceExW failed for drive");
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.scanning = false;
				g_snapshot.failed = true;
				strncpy_s(g_snapshot.status_text, "掃描失敗", _TRUNCATE);
				g_scanning.store(false, std::memory_order_release);
				return;
			}

			const std::chrono::steady_clock::time_point deadline =
				std::chrono::steady_clock::now()
				+ std::chrono::milliseconds(static_cast<int>(kScanTimeoutSec * 1000.0));

			const std::wstring windows_dir = root + L"Windows";
			const std::wstring pf = root + L"Program Files";
			const std::wstring pf86 = root + L"Program Files (x86)";
			const std::wstring users = root + L"Users";
			const std::wstring win_temp = root + L"Windows\\Temp";

			std::vector<std::wstring> temp_paths = { win_temp };
			wchar_t env_temp[MAX_PATH] = {};
			if (GetEnvironmentVariableW(L"TEMP", env_temp, MAX_PATH) > 0) {
				temp_paths.emplace_back(env_temp);
			}

			const uint64_t system_b = MeasurePath(windows_dir, kStatusLabels[0], deadline, 0.15f);
			const uint64_t programs_b = MeasurePath(pf, kStatusLabels[1], deadline, 0.35f)
				+ MeasurePath(pf86, kStatusLabels[1], deadline, 0.45f);
			const uint64_t users_b = MeasurePath(users, kStatusLabels[2], deadline, 0.65f);

			uint64_t temp_b = 0;
			for (size_t i = 0; i < temp_paths.size(); ++i) {
				const float p = 0.72f + 0.22f * static_cast<float>(i + 1)
					/ static_cast<float>(temp_paths.size());
				temp_b += MeasurePath(temp_paths[i], kStatusLabels[3], deadline, p);
			}

			SetStatusText(kStatusLabels[4], 0.92f);

			const bool timed_out = std::chrono::steady_clock::now() >= deadline;
			const bool cancelled = g_cancel.load(std::memory_order_relaxed);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				BuildSnapshotLocked(root, total_bytes.QuadPart, free_bytes.QuadPart,
					system_b, programs_b, users_b, temp_b,
					timed_out || cancelled);
			}

			if (timed_out) {
				HLOG_WARN("MainPage disk storage scan finished (timeout, partial results)");
			}
			else if (cancelled) {
				HLOG_INFO("MainPage disk storage scan cancelled");
			}
			else {
				HLOG_INFO("MainPage disk storage scan complete");
			}

			g_scanning.store(false, std::memory_order_release);
		}

		static void StartWorkerIfNeeded()
		{
			if (g_shutdown.load(std::memory_order_acquire)) {
				return;
			}
			if (g_scanning.load(std::memory_order_acquire)) {
				return;
			}
			if (g_worker.joinable()) {
				g_worker.join();
			}
			g_worker = std::thread(RunScanWorker);
		}
	}

	void SetDeferUntilMainWindowVisible(bool defer)
	{
		g_defer_worker = defer;
	}

	void NotifyMainWindowVisible()
	{
		if (g_defer_worker) {
			g_defer_worker = false;
			StartWorkerIfNeeded();
		}
	}

	void Init()
	{
		if (g_init_done) {
			return;
		}
		g_init_done = true;
		g_shutdown.store(false, std::memory_order_release);
		if (!g_defer_worker) {
			StartWorkerIfNeeded();
		}
	}

	void Shutdown()
	{
		g_shutdown.store(true, std::memory_order_release);
		g_cancel.store(true, std::memory_order_release);
		if (g_worker.joinable()) {
			g_worker.join();
		}
		g_scanning.store(false, std::memory_order_release);
	}

	void RequestRescan()
	{
		if (g_shutdown.load(std::memory_order_acquire)) {
			return;
		}
		if (g_scanning.load(std::memory_order_acquire)) {
			g_cancel.store(true, std::memory_order_release);
			if (g_worker.joinable()) {
				g_worker.join();
			}
			g_cancel.store(false, std::memory_order_release);
		}
		else if (g_worker.joinable()) {
			g_worker.join();
		}
		StartWorkerIfNeeded();
	}

	Snapshot GetSnapshot()
	{
		Snapshot copy = {};
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			copy = g_snapshot;
		}
		if (g_scanning.load(std::memory_order_acquire)) {
			copy.scanning = true;
			copy.progress = g_progress.load(std::memory_order_acquire);
			if (copy.status_text[0] == '\0') {
				strncpy_s(copy.status_text, g_status_text, _TRUNCATE);
			}
		}
		return copy;
	}
}
