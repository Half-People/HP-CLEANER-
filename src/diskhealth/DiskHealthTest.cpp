#include "DiskHealthTest.h"
#include "HAppPaths.h"
#include "HPage.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <winioctl.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace DiskHealthTest {
	namespace {
		constexpr uint32_t kMinSampleMb = 128;
		constexpr uint32_t kMaxSampleMb = 8192;
		constexpr DWORD kIoBlockBytes = 8u * 1024u * 1024u;
		constexpr DWORD kSectorBytes = 4096u;
		constexpr uint64_t kQuickScanPerEndGiB = 1024ull * 1024ull * 1024ull;
		constexpr uint64_t kDefaultScanSize = 2ull * 1024ull * 1024ull * 1024ull;
		const char* kPersistFileName = "disk_health_test_results.json";

		std::mutex g_mutex;
		JobState g_state;
		std::thread g_worker;
		std::atomic<bool> g_cancel{ false };
		std::atomic<bool> g_shutdown{ false };

		std::unordered_map<int, DriveTestHistory> g_history_by_pd;
		bool g_history_loaded = false;

		static int64_t NowUnixMs()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		}

		static std::string GetPersistFilePath()
		{
			const std::string dir = HAppPaths::GetConfigDir();
			if (dir.empty()) {
				return kPersistFileName;
			}
			return dir + kPersistFileName;
		}

		static nlohmann::json CurveToJson(const std::vector<float>& curve)
		{
			nlohmann::json arr = nlohmann::json::array();
			const size_t n = (std::min)(curve.size(), static_cast<size_t>(128));
			for (size_t i = 0; i < n; ++i) {
				arr.push_back(curve[i]);
			}
			return arr;
		}

		static void CurveFromJson(const nlohmann::json& arr, std::vector<float>& curve)
		{
			curve.clear();
			if (!arr.is_array()) {
				return;
			}
			for (const auto& v : arr) {
				if (v.is_number()) {
					curve.push_back(v.get<float>());
				}
			}
		}

		static nlohmann::json SpeedToJson(const SpeedTestResult& r)
		{
			nlohmann::json j;
			j["ok"] = r.ok;
			j["read_mbps"] = r.read_mbps;
			j["write_mbps"] = r.write_mbps;
			j["write_tested"] = r.write_tested;
			j["read_uncached"] = r.read_uncached;
			j["sample_mb"] = r.sample_mb;
			j["volume_letter"] = r.volume_letter ? std::string(1, r.volume_letter) : "";
			j["error_utf8"] = r.error_utf8;
			j["read_curve"] = CurveToJson(r.read_curve_mbps);
			j["write_curve"] = CurveToJson(r.write_curve_mbps);
			return j;
		}

		static void SpeedFromJson(const nlohmann::json& j, SpeedTestResult& r)
		{
			r = {};
			if (!j.is_object()) {
				return;
			}
			r.ok = j.value("ok", false);
			r.read_mbps = j.value("read_mbps", 0.0);
			r.write_mbps = j.value("write_mbps", 0.0);
			r.write_tested = j.value("write_tested", false);
			r.read_uncached = j.value("read_uncached", false);
			r.sample_mb = j.value("sample_mb", 0u);
			const std::string letter = j.value("volume_letter", std::string());
			r.volume_letter = letter.empty() ? '\0' : letter[0];
			const std::string err = j.value("error_utf8", std::string());
			strncpy_s(r.error_utf8, err.c_str(), _TRUNCATE);
			if (j.contains("read_curve")) {
				CurveFromJson(j["read_curve"], r.read_curve_mbps);
			}
			if (j.contains("write_curve")) {
				CurveFromJson(j["write_curve"], r.write_curve_mbps);
			}
		}

		static nlohmann::json BadToJson(const BadSectorResult& r)
		{
			nlohmann::json j;
			j["ok"] = r.ok;
			j["bytes_scanned"] = r.bytes_scanned;
			j["bytes_planned"] = r.bytes_planned;
			j["error_count"] = r.error_count;
			j["mode"] = static_cast<int>(r.mode);
			j["error_utf8"] = r.error_utf8;
			j["matrix_cols"] = r.matrix_cols;
			j["matrix_rows"] = r.matrix_rows;
			nlohmann::json cells = nlohmann::json::array();
			for (uint8_t c : r.matrix) {
				cells.push_back(c);
			}
			j["matrix"] = std::move(cells);
			return j;
		}

		static void BadFromJson(const nlohmann::json& j, BadSectorResult& r)
		{
			r = {};
			if (!j.is_object()) {
				return;
			}
			r.ok = j.value("ok", false);
			r.bytes_scanned = j.value("bytes_scanned", 0ull);
			r.bytes_planned = j.value("bytes_planned", 0ull);
			r.error_count = j.value("error_count", 0u);
			r.mode = static_cast<BadSectorMode>(j.value("mode", 0));
			const std::string err = j.value("error_utf8", std::string());
			strncpy_s(r.error_utf8, err.c_str(), _TRUNCATE);
			r.matrix_cols = j.value("matrix_cols", kMatrixCols);
			r.matrix_rows = j.value("matrix_rows", 0);
			if (j.contains("matrix") && j["matrix"].is_array()) {
				for (const auto& c : j["matrix"]) {
					if (c.is_number_unsigned()) {
						r.matrix.push_back(static_cast<uint8_t>(c.get<unsigned>()));
					}
				}
			}
		}

		static void PersistSpeedUnlocked(int physical_index, const SpeedTestResult& result)
		{
			DriveTestHistory& h = g_history_by_pd[physical_index];
			h.physical_index = physical_index;
			h.has_speed = result.ok;
			h.speed = result;
			h.speed_finished_unix_ms = NowUnixMs();
		}

		static void PersistBadUnlocked(int physical_index, const BadSectorResult& result)
		{
			DriveTestHistory& h = g_history_by_pd[physical_index];
			h.physical_index = physical_index;
			h.has_bad_sector = result.ok;
			h.bad_sector = result;
			h.bad_finished_unix_ms = NowUnixMs();
		}

		static void SetStatus(const char* text, float progress = -1.f)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (text != nullptr) {
				strncpy_s(g_state.status_utf8, text, _TRUNCATE);
			}
			if (progress >= 0.f) {
				g_state.progress = progress;
			}
		}

		static void FinishJobUnlocked()
		{
			g_state.running = false;
			g_state.progress = 1.f;
		}

		static void FinishJob()
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			FinishJobUnlocked();
		}

		static bool ParseFirstVolumeLetter(const char* volume_letters, wchar_t& letter)
		{
			if (volume_letters == nullptr || volume_letters[0] == '\0') {
				return false;
			}
			const char c = volume_letters[0];
			if (c >= 'A' && c <= 'Z') {
				letter = static_cast<wchar_t>(c);
				return true;
			}
			if (c >= 'a' && c <= 'z') {
				letter = static_cast<wchar_t>(c - 'a' + 'A');
				return true;
			}
			return false;
		}

		static uint64_t AlignDown(uint64_t value, uint64_t align)
		{
			if (align == 0) {
				return value;
			}
			return (value / align) * align;
		}

		static bool QueryDiskCapacityOnHandle(HANDLE device, uint64_t& out_bytes)
		{
			GET_LENGTH_INFORMATION len = {};
			DWORD bytes = 0;
			if (DeviceIoControl(device, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
				&len, sizeof(len), &bytes, nullptr) && len.Length.QuadPart > 0) {
				out_bytes = static_cast<uint64_t>(len.Length.QuadPart);
				return true;
			}
			std::vector<uint8_t> geo_buf(sizeof(DISK_GEOMETRY_EX) + 64, 0);
			bytes = 0;
			if (DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
				geo_buf.data(), static_cast<DWORD>(geo_buf.size()), &bytes, nullptr)
				&& bytes >= sizeof(DISK_GEOMETRY_EX)) {
				const auto* geo = reinterpret_cast<const DISK_GEOMETRY_EX*>(geo_buf.data());
				if (geo->DiskSize.QuadPart > 0) {
					out_bytes = static_cast<uint64_t>(geo->DiskSize.QuadPart);
					return true;
				}
			}
			return false;
		}

		static HANDLE OpenPhysicalForRead(int index)
		{
			wchar_t path[64] = {};
			_snwprintf_s(path, _TRUNCATE, L"\\\\.\\PhysicalDrive%d", index);
			const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
			HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, share,
				nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle != INVALID_HANDLE_VALUE) {
				return handle;
			}
			handle = CreateFileW(path, GENERIC_READ, share, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle != INVALID_HANDLE_VALUE) {
				return handle;
			}
			return CreateFileW(path, 0, share, nullptr, OPEN_EXISTING, 0, nullptr);
		}

		static double SecondsBetween(const LARGE_INTEGER& freq,
			const LARGE_INTEGER& start, const LARGE_INTEGER& end)
		{
			if (freq.QuadPart <= 0) {
				return 0.0;
			}
			return static_cast<double>(end.QuadPart - start.QuadPart)
				/ static_cast<double>(freq.QuadPart);
		}

		static bool VerifyOrReadChunk(HANDLE device, uint64_t offset, DWORD length)
		{
			VERIFY_INFORMATION verify = {};
			verify.StartingOffset.QuadPart = static_cast<LONGLONG>(offset);
			verify.Length = length;
			DWORD bytes = 0;
			if (DeviceIoControl(device, IOCTL_DISK_VERIFY, &verify, sizeof(verify),
				nullptr, 0, &bytes, nullptr)) {
				return true;
			}

			LARGE_INTEGER pos = {};
			pos.QuadPart = static_cast<LONGLONG>(offset);
			if (!SetFilePointerEx(device, pos, nullptr, FILE_BEGIN)) {
				return false;
			}
			std::vector<uint8_t> buffer(length);
			DWORD read = 0;
			return ReadFile(device, buffer.data(), length, &read, nullptr) && read == length;
		}

		static void PushSpeedSample(std::vector<float>& curve, double mbps)
		{
			if (curve.size() >= 256) {
				return;
			}
			curve.push_back(static_cast<float>(mbps));
		}

		static void RecordChunkSpeed(const LARGE_INTEGER& freq, LARGE_INTEGER& chunk_t0,
			uint64_t chunk_bytes, std::vector<float>& curve)
		{
			LARGE_INTEGER t1 = {};
			QueryPerformanceCounter(&t1);
			const double sec = SecondsBetween(freq, chunk_t0, t1);
			if (sec > 0.0 && chunk_bytes > 0) {
				const double mbps = (static_cast<double>(chunk_bytes) / (1024.0 * 1024.0)) / sec;
				PushSpeedSample(curve, mbps);
			}
			chunk_t0 = t1;
		}

		static bool AllocateBenchFileSize(HANDLE file, uint64_t total_bytes)
		{
			if (total_bytes == 0) {
				return false;
			}
			if (!SetFilePointerEx(file, { 0 }, nullptr, FILE_BEGIN)) {
				return false;
			}

			LARGE_INTEGER end = {};
			end.QuadPart = static_cast<LONGLONG>(total_bytes - 1);
			if (SetFilePointerEx(file, end, nullptr, FILE_BEGIN)) {
				DWORD wrote = 0;
				const uint8_t zero = 0;
				if (WriteFile(file, &zero, 1, &wrote, nullptr) && wrote > 0) {
					FlushFileBuffers(file);
					SetFilePointerEx(file, { 0 }, nullptr, FILE_BEGIN);
					return true;
				}
			}

			std::vector<char> zeros(kIoBlockBytes, 0);
			uint64_t written = 0;
			SetFilePointerEx(file, { 0 }, nullptr, FILE_BEGIN);
			while (written < total_bytes) {
				const uint64_t remain = total_bytes - written;
				const DWORD chunk = static_cast<DWORD>(
					(std::min)(static_cast<uint64_t>(kIoBlockBytes), remain));
				DWORD bytes = 0;
				if (!WriteFile(file, zeros.data(), chunk, &bytes, nullptr) || bytes == 0) {
					HLOG_WARN("AllocateBenchFile: chunk write at {} failed err={}",
						written, GetLastError());
					return false;
				}
				written += bytes;
			}
			FlushFileBuffers(file);
			SetFilePointerEx(file, { 0 }, nullptr, FILE_BEGIN);
			return true;
		}

		static bool QueryVolumeFreeBytes(wchar_t letter, uint64_t& out_free)
		{
			wchar_t root[8] = {};
			_snwprintf_s(root, _TRUNCATE, L"%c:\\", letter);
			ULARGE_INTEGER free_bytes = {};
			ULARGE_INTEGER total = {};
			ULARGE_INTEGER total_free = {};
			if (!GetDiskFreeSpaceExW(root, &free_bytes, &total, &total_free)) {
				return false;
			}
			out_free = free_bytes.QuadPart;
			return out_free > 0;
		}

		static HANDLE ReopenBenchForRead(const wchar_t* bench_path, bool& out_uncached)
		{
			out_uncached = false;
			HANDLE file = CreateFileW(bench_path, GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING,
				FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr);
			if (file != INVALID_HANDLE_VALUE) {
				out_uncached = true;
				return file;
			}
			file = CreateFileW(bench_path, GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			return file;
		}

		static void RunSpeedTest(int physical_index, const char* volume_letters,
			uint32_t sample_mb, bool include_write)
		{
			SpeedTestResult result = {};
			result.write_tested = include_write;
			result.sample_mb = sample_mb;

			wchar_t letter = L'\0';
			const bool has_volume = ParseFirstVolumeLetter(volume_letters, letter);
			if (!has_volume) {
				strncpy_s(result.error_utf8, "此硬碟無掛載磁碟區，無法進行檔案式速度測試", _TRUNCATE);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.speed = result;
					FinishJobUnlocked();
				}
				return;
			}

			result.volume_letter = static_cast<char>(letter);

			const uint32_t clamped_mb = (std::min)(kMaxSampleMb, (std::max)(kMinSampleMb, sample_mb));
			result.sample_mb = clamped_mb;
			uint64_t total_bytes = static_cast<uint64_t>(clamped_mb) * 1024ull * 1024ull;
			total_bytes = AlignDown(total_bytes, kSectorBytes);
			if (total_bytes < kSectorBytes) {
				total_bytes = kSectorBytes;
			}

			wchar_t bench_path[MAX_PATH] = {};
			_snwprintf_s(bench_path, _TRUNCATE, L"%c:\\HP_CLEANER++.bench.tmp", letter);

			SetStatus(include_write ? "建立測試檔（讀+寫）…" : "建立測試檔（僅讀取）…", 0.02f);

			HANDLE file = CreateFileW(bench_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				const DWORD err = GetLastError();
				HLOG_WARN("DiskHealthTest speed: create bench file {} err={}",
					static_cast<char>(letter), err);
				snprintf(result.error_utf8, sizeof(result.error_utf8),
					"無法建立測試檔（磁碟區 %c:，錯誤 %lu）",
					static_cast<char>(letter), err);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.speed = result;
					FinishJobUnlocked();
				}
				return;
			}

			uint64_t free_bytes = 0;
			const bool know_free = QueryVolumeFreeBytes(letter, free_bytes);
			if (know_free && free_bytes < total_bytes + (64ull * 1024ull * 1024ull)) {
				CloseHandle(file);
				DeleteFileW(bench_path);
				snprintf(result.error_utf8, sizeof(result.error_utf8),
					"磁碟區 %c: 可用空間不足（需約 %u MB，可用約 %llu MB）",
					static_cast<char>(letter), clamped_mb,
					static_cast<unsigned long long>(free_bytes / (1024ull * 1024ull)));
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.speed = result;
					FinishJobUnlocked();
				}
				return;
			}

			SetStatus(include_write ? "配置測試檔並寫入…" : "配置測試檔（僅讀取預備）…", 0.04f);
			if (!AllocateBenchFileSize(file, total_bytes)) {
				CloseHandle(file);
				DeleteFileW(bench_path);
				const DWORD err = GetLastError();
				snprintf(result.error_utf8, sizeof(result.error_utf8),
					"無法配置測試檔（%u MB，錯誤 %lu；請確認磁碟區可寫入且空間足夠）",
					clamped_mb, err);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.speed = result;
					FinishJobUnlocked();
				}
				return;
			}

			std::vector<char> buffer(kIoBlockBytes, static_cast<char>(0xA5));
			LARGE_INTEGER freq = {};
			QueryPerformanceFrequency(&freq);

			if (include_write) {
				SetStatus("寫入速度測試中…", 0.08f);
				uint64_t written = 0;
				LARGE_INTEGER t0 = {};
				LARGE_INTEGER t1 = {};
				LARGE_INTEGER chunk_t0 = {};
				QueryPerformanceCounter(&t0);
				QueryPerformanceCounter(&chunk_t0);
				result.write_curve_mbps.clear();
				while (written < total_bytes) {
					if (g_cancel.load(std::memory_order_relaxed)
						|| g_shutdown.load(std::memory_order_acquire)) {
						break;
					}
					const DWORD chunk = static_cast<DWORD>(
						(std::min)(static_cast<uint64_t>(kIoBlockBytes), total_bytes - written));
					DWORD bytes = 0;
					if (!WriteFile(file, buffer.data(), chunk, &bytes, nullptr) || bytes == 0) {
						snprintf(result.error_utf8, sizeof(result.error_utf8),
							"寫入失敗（%lu）", GetLastError());
						break;
					}
					written += bytes;
					RecordChunkSpeed(freq, chunk_t0, bytes, result.write_curve_mbps);
					const float p = 0.05f + 0.45f * static_cast<float>(written) / static_cast<float>(total_bytes);
					SetStatus("寫入速度測試中…", p);
				}
				FlushFileBuffers(file);
				QueryPerformanceCounter(&t1);
				const double write_sec = SecondsBetween(freq, t0, t1);
				if (write_sec > 0.0 && written > 0 && result.error_utf8[0] == '\0') {
					result.write_mbps = (static_cast<double>(written) / (1024.0 * 1024.0)) / write_sec;
				}
				if (g_cancel.load(std::memory_order_relaxed)) {
					CloseHandle(file);
					DeleteFileW(bench_path);
					FinishJob();
					return;
				}
			}

			CloseHandle(file);

			SetStatus(include_write ? "讀取測試（繞過系統快取）…" : "僅讀取測試（繞過系統快取）…",
				include_write ? 0.55f : 0.12f);

			bool uncached = false;
			file = ReopenBenchForRead(bench_path, uncached);
			if (file == INVALID_HANDLE_VALUE) {
				snprintf(result.error_utf8, sizeof(result.error_utf8),
					"無法重新開啟測試檔（%lu）", GetLastError());
				DeleteFileW(bench_path);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.speed = result;
					FinishJobUnlocked();
				}
				return;
			}
			result.read_uncached = uncached;

			void* aligned_buf = _aligned_malloc(kIoBlockBytes, kSectorBytes);
			if (aligned_buf == nullptr) {
				CloseHandle(file);
				DeleteFileW(bench_path);
				strncpy_s(result.error_utf8, "記憶體配置失敗", _TRUNCATE);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.speed = result;
					FinishJobUnlocked();
				}
				return;
			}

			uint64_t read_total = 0;
			LARGE_INTEGER t0 = {};
			LARGE_INTEGER t1 = {};
			LARGE_INTEGER chunk_t0 = {};
			QueryPerformanceCounter(&t0);
			QueryPerformanceCounter(&chunk_t0);
			result.read_curve_mbps.clear();

			while (read_total < total_bytes) {
				if (g_cancel.load(std::memory_order_relaxed)
					|| g_shutdown.load(std::memory_order_acquire)) {
					break;
				}
				DWORD chunk = static_cast<DWORD>(
					(std::min)(static_cast<uint64_t>(kIoBlockBytes), total_bytes - read_total));
				if (uncached) {
					chunk = static_cast<DWORD>(AlignDown(chunk, kSectorBytes));
					if (chunk == 0) {
						break;
					}
				}
				DWORD bytes = 0;
				if (!ReadFile(file, aligned_buf, chunk, &bytes, nullptr) || bytes == 0) {
					snprintf(result.error_utf8, sizeof(result.error_utf8),
						"讀取失敗（%lu）", GetLastError());
					break;
				}
				read_total += bytes;
				RecordChunkSpeed(freq, chunk_t0, bytes, result.read_curve_mbps);
				const float base = include_write ? 0.55f : 0.12f;
				const float span = include_write ? 0.43f : 0.86f;
				SetStatus(uncached ? "讀取測試（無快取）…" : "讀取測試中…",
					base + span * static_cast<float>(read_total) / static_cast<float>(total_bytes));
			}
			QueryPerformanceCounter(&t1);
			const double read_sec = SecondsBetween(freq, t0, t1);

			_aligned_free(aligned_buf);
			CloseHandle(file);
			DeleteFileW(bench_path);

			if (read_sec > 0.0 && read_total > 0 && result.error_utf8[0] == '\0') {
				result.read_mbps = (static_cast<double>(read_total) / (1024.0 * 1024.0)) / read_sec;
				result.ok = true;
			}
			else if (result.error_utf8[0] == '\0') {
				strncpy_s(result.error_utf8, "讀取測試未完成", _TRUNCATE);
			}

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_state.speed = result;
				if (result.ok) {
					PersistSpeedUnlocked(physical_index, result);
					snprintf(g_state.status_utf8, sizeof(g_state.status_utf8),
						"%s速度測試完成（PD%d / %c:，%u MB%s）",
						include_write ? "" : "僅讀取",
						physical_index, static_cast<char>(letter), clamped_mb,
						uncached ? "，無快取讀取" : "");
				}
				FinishJobUnlocked();
			}
			SavePersistedResults();
			HLOG_INFO("DiskHealthTest speed: read={} write={} MiB/s uncached={}",
				result.read_mbps, result.write_mbps, result.read_uncached);
		}

		static uint64_t MapCellToOffset(int cell, int total_cells, uint64_t disk_size,
			uint64_t bytes_planned, BadSectorMode mode)
		{
			if (total_cells <= 0 || bytes_planned == 0) {
				return 0;
			}
			const uint64_t pos = (static_cast<uint64_t>(cell) * bytes_planned)
				/ static_cast<uint64_t>(total_cells);
			if (mode != BadSectorMode::Quick) {
				return (std::min)(pos, disk_size > 0 ? disk_size - 1 : 0);
			}
			const uint64_t head = (std::min)(kQuickScanPerEndGiB, disk_size);
			const uint64_t tail = (disk_size > kQuickScanPerEndGiB * 2) ? kQuickScanPerEndGiB : 0;
			if (pos < head) {
				return pos;
			}
			if (tail > 0) {
				return disk_size - tail + (pos - head);
			}
			return pos;
		}

		static void RunBadSectorScan(int physical_index, uint64_t disk_size_bytes,
			BadSectorMode mode)
		{
			BadSectorResult result = {};
			result.mode = mode;
			result.matrix_cols = kMatrixCols;

			HANDLE device = OpenPhysicalForRead(physical_index);
			if (device == INVALID_HANDLE_VALUE) {
				const DWORD err = GetLastError();
				HLOG_WARN("DiskHealthTest bad sector: open PD{} err={}", physical_index, err);
				snprintf(result.error_utf8, sizeof(result.error_utf8),
					"無法開啟 PhysicalDrive%d（%lu）", physical_index, err);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.bad_sector = result;
					FinishJobUnlocked();
				}
				return;
			}

			uint64_t disk_size = disk_size_bytes;
			if (disk_size == 0) {
				QueryDiskCapacityOnHandle(device, disk_size);
			}
			if (disk_size == 0) {
				disk_size = kDefaultScanSize;
			}

			uint64_t bytes_planned = 0;
			if (mode == BadSectorMode::Quick) {
				const uint64_t head = (std::min)(kQuickScanPerEndGiB, disk_size);
				const uint64_t tail = (disk_size > kQuickScanPerEndGiB * 2)
					? kQuickScanPerEndGiB
					: 0;
				bytes_planned = head + tail;
			}
			else {
				bytes_planned = disk_size;
			}
			result.bytes_planned = bytes_planned;

			const int rows = (mode == BadSectorMode::Quick) ? 24 : kMatrixMaxRows;
			const int total_cells = kMatrixCols * rows;
			result.matrix_rows = rows;
			result.matrix.assign(static_cast<size_t>(total_cells),
				static_cast<uint8_t>(SectorCellState::Pending));

			const uint64_t chunk_bytes = (std::max)(65536ull, bytes_planned / static_cast<uint64_t>(total_cells));

			SetStatus("壞軌掃描準備中…", 0.01f);

			uint64_t bytes_scanned = 0;
			uint32_t error_count = 0;

			for (int cell = 0; cell < total_cells; ++cell) {
				if (g_cancel.load(std::memory_order_relaxed)
					|| g_shutdown.load(std::memory_order_acquire)) {
					break;
				}
				const uint64_t offset = MapCellToOffset(cell, total_cells, disk_size,
					bytes_planned, mode);
				const DWORD verify_len = static_cast<DWORD>(
					(std::min)(chunk_bytes, disk_size > offset ? disk_size - offset : chunk_bytes));
				const bool ok = VerifyOrReadChunk(device, offset, verify_len);
				result.matrix[static_cast<size_t>(cell)] = ok
					? static_cast<uint8_t>(SectorCellState::Ok)
					: static_cast<uint8_t>(SectorCellState::Bad);
				if (!ok) {
					++error_count;
				}
				bytes_scanned += verify_len;

				if (cell % 4 == 0) {
					const float p = static_cast<float>(cell + 1) / static_cast<float>(total_cells);
					char status[256] = {};
					snprintf(status, sizeof(status),
						"壞軌矩陣掃描 %.0f%%（問題格 %u）", static_cast<double>(p) * 100.0, error_count);
					SetStatus(status, (std::min)(0.99f, p));
					std::lock_guard<std::mutex> lock(g_mutex);
					g_state.bad_sector = result;
					g_state.bad_sector.bytes_scanned = bytes_scanned;
					g_state.bad_sector.error_count = error_count;
				}
			}

			CloseHandle(device);

			result.bytes_scanned = bytes_scanned;
			result.error_count = error_count;
			result.ok = !g_cancel.load(std::memory_order_relaxed);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_state.bad_sector = result;
				if (result.ok) {
					PersistBadUnlocked(physical_index, result);
					snprintf(g_state.status_utf8, sizeof(g_state.status_utf8),
						"壞軌掃描完成：掃描 %llu MB，疑似問題區 %u",
						static_cast<unsigned long long>(bytes_scanned / (1024ull * 1024ull)),
						error_count);
				}
				else {
					strncpy_s(g_state.status_utf8, "壞軌掃描已取消", _TRUNCATE);
				}
				FinishJobUnlocked();
			}
			SavePersistedResults();
			HLOG_INFO("DiskHealthTest bad sector: errors={} scanned={}",
				error_count, bytes_scanned);
		}

		static void StartWorker(JobKind kind, int physical_index,
			const char* volume_letters, uint32_t sample_mb, bool include_write,
			uint64_t disk_size, BadSectorMode bad_mode)
		{
			if (g_shutdown.load(std::memory_order_acquire)) {
				return;
			}
			if (g_worker.joinable()) {
				g_cancel.store(true, std::memory_order_release);
				g_worker.join();
				g_cancel.store(false, std::memory_order_release);
			}

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_state = {};
				g_state.kind = kind;
				g_state.running = true;
				g_state.physical_index = physical_index;
				g_state.progress = 0.f;
				strncpy_s(g_state.status_utf8, "準備中…", _TRUNCATE);
				g_state.speed = {};
				g_state.bad_sector = {};
			}

			g_worker = std::thread([=] {
				if (kind == JobKind::SpeedTest) {
					RunSpeedTest(physical_index, volume_letters, sample_mb, include_write);
				}
				else if (kind == JobKind::BadSectorScan) {
					RunBadSectorScan(physical_index, disk_size, bad_mode);
				}
				else {
					FinishJob();
				}
				});
		}
	}

	void LoadPersistedResults()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_history_loaded) {
			return;
		}
		g_history_loaded = true;
		g_history_by_pd.clear();

		HAppPaths::EnsureAppDataDirs();
		const std::string path = GetPersistFilePath();
		std::ifstream in(path);
		if (!in.is_open()) {
			HLOG_DEBUG("DiskHealthTest: no persist file '{}'", path);
			return;
		}
		try {
			nlohmann::json root;
			in >> root;
			if (!root.contains("drives") || !root["drives"].is_array()) {
				return;
			}
			for (const auto& item : root["drives"]) {
				if (!item.is_object()) {
					continue;
				}
				const int pd = item.value("physical_index", -1);
				if (pd < 0) {
					continue;
				}
				DriveTestHistory h;
				h.physical_index = pd;
				h.speed_finished_unix_ms = item.value("speed_finished_unix_ms", 0ll);
				h.bad_finished_unix_ms = item.value("bad_finished_unix_ms", 0ll);
				if (item.contains("speed")) {
					SpeedFromJson(item["speed"], h.speed);
					h.has_speed = h.speed.ok;
				}
				if (item.contains("bad_sector")) {
					BadFromJson(item["bad_sector"], h.bad_sector);
					h.has_bad_sector = h.bad_sector.ok;
				}
				g_history_by_pd[pd] = std::move(h);
			}
			HLOG_INFO("DiskHealthTest: loaded {} drive histories", g_history_by_pd.size());
		}
		catch (const std::exception& ex) {
			HLOG_WARN("DiskHealthTest: parse '{}' failed: {}", path, ex.what());
		}
	}

	void SavePersistedResults()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		HAppPaths::EnsureAppDataDirs();
		nlohmann::json root;
		root["version"] = 1;
		nlohmann::json drives = nlohmann::json::array();
		for (const auto& pair : g_history_by_pd) {
			const DriveTestHistory& h = pair.second;
			nlohmann::json item;
			item["physical_index"] = h.physical_index;
			item["speed_finished_unix_ms"] = h.speed_finished_unix_ms;
			item["bad_finished_unix_ms"] = h.bad_finished_unix_ms;
			if (h.has_speed) {
				item["speed"] = SpeedToJson(h.speed);
			}
			if (h.has_bad_sector) {
				item["bad_sector"] = BadToJson(h.bad_sector);
			}
			drives.push_back(std::move(item));
		}
		root["drives"] = std::move(drives);

		const std::string path = GetPersistFilePath();
		std::ofstream out(path, std::ios::trunc);
		if (!out.is_open()) {
			HLOG_WARN("DiskHealthTest: cannot write '{}'", path);
			return;
		}
		out << root.dump(2);
		HLOG_DEBUG("DiskHealthTest: saved persist to '{}'", path);
	}

	DriveTestHistory GetDriveHistory(int physical_index)
	{
		LoadPersistedResults();
		std::lock_guard<std::mutex> lock(g_mutex);
		const auto it = g_history_by_pd.find(physical_index);
		if (it != g_history_by_pd.end()) {
			return it->second;
		}
		DriveTestHistory empty = {};
		empty.physical_index = physical_index;
		return empty;
	}

	void Shutdown()
	{
		g_shutdown.store(true, std::memory_order_release);
		g_cancel.store(true, std::memory_order_release);
		if (g_worker.joinable()) {
			g_worker.join();
		}
		g_shutdown.store(false, std::memory_order_release);
		g_cancel.store(false, std::memory_order_release);
		std::lock_guard<std::mutex> lock(g_mutex);
		g_state = {};
	}

	void Cancel()
	{
		g_cancel.store(true, std::memory_order_release);
	}

	JobState GetState()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_state;
	}

	void RequestSpeedTest(int physical_index, const char* volume_letters,
		uint32_t sample_mb, bool include_write)
	{
		StartWorker(JobKind::SpeedTest, physical_index, volume_letters,
			sample_mb, include_write, 0, BadSectorMode::Quick);
	}

	void RequestBadSectorScan(int physical_index, uint64_t disk_size_bytes,
		BadSectorMode mode)
	{
		StartWorker(JobKind::BadSectorScan, physical_index, nullptr, 0, false,
			disk_size_bytes, mode);
	}

	void BuildBadSectorReport(const BadSectorResult& result, char* out, size_t out_size)
	{
		if (out == nullptr || out_size == 0) {
			return;
		}
		const int ok_cells = static_cast<int>(std::count(result.matrix.begin(),
			result.matrix.end(), static_cast<uint8_t>(SectorCellState::Ok)));
		const int bad_cells = static_cast<int>(std::count(result.matrix.begin(),
			result.matrix.end(), static_cast<uint8_t>(SectorCellState::Bad)));
		const int pending_cells = static_cast<int>(std::count(result.matrix.begin(),
			result.matrix.end(), static_cast<uint8_t>(SectorCellState::Pending)));
		snprintf(out, out_size,
			"【壞軌掃描報告】\n"
			"模式：%s（快速＝抽樣碟首/碟尾；完整＝覆蓋整碟抽樣格）\n"
			"矩陣：%d × %d\n"
			"  灰＝未掃  綠＝讀取/驗證正常  紅＝異常  青框＝目前掃描位置\n"
			"計畫掃描量：%llu MB\n"
			"已掃描：%llu MB\n"
			"正常格：%d  異常格：%d  未掃：%d\n"
			"說明：此為抽樣檢測，異常格代表該區塊讀取/驗證失敗，建議備份後以廠商工具複檢。\n"
			"%s\n",
			result.mode == BadSectorMode::Quick ? "快速（首末區域）" : "完整",
			result.matrix_cols, result.matrix_rows,
			static_cast<unsigned long long>(result.bytes_planned / (1024ull * 1024ull)),
			static_cast<unsigned long long>(result.bytes_scanned / (1024ull * 1024ull)),
			ok_cells, bad_cells, pending_cells,
			result.error_utf8[0] ? result.error_utf8 : "");
	}

	void BuildSpeedReport(const SpeedTestResult& result, char* out, size_t out_size)
	{
		if (out == nullptr || out_size == 0) {
			return;
		}
		char write_line[128] = {};
		if (result.write_tested) {
			snprintf(write_line, sizeof(write_line), "%.1f MB/s（%zu 段採樣）",
				result.write_mbps, result.write_curve_mbps.size());
		}
		else {
			strncpy_s(write_line, "未執行（僅讀取模式）", _TRUNCATE);
		}
		snprintf(out, out_size,
			"【速度測試報告】\n"
			"磁碟區：%c:\n"
			"樣本大小：%u MB（建議 ≥1024 MB 以減少 RAM 快取影響）\n"
			"模式：%s\n"
			"讀取：%.1f MB/s（%zu 段採樣，%s）\n"
			"寫入：%s\n"
			"%s\n",
			result.volume_letter ? result.volume_letter : '?',
			result.sample_mb,
			result.write_tested ? "讀取 + 寫入" : "僅讀取",
			result.read_mbps, result.read_curve_mbps.size(),
			result.read_uncached ? "關閉系統讀取快取" : "一般讀取",
			write_line,
			result.error_utf8[0] ? result.error_utf8 : "測試完成");
	}

}
