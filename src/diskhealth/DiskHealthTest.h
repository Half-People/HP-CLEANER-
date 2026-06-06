#pragma once

#include <cstdint>
#include <vector>

namespace DiskHealthTest {

	enum class JobKind : uint8_t {
		None = 0,
		SpeedTest = 1,
		BadSectorScan = 2,
	};

	enum class BadSectorMode : uint8_t {
		Quick = 0,
		Full = 1,
	};

	enum class SectorCellState : uint8_t {
		Pending = 0,
		Ok = 1,
		Bad = 2,
	};

	static constexpr int kMatrixCols = 80;
	static constexpr int kMatrixMaxRows = 48;

	struct SpeedTestResult {
		bool ok = false;
		double read_mbps = 0.0;
		double write_mbps = 0.0;
		bool write_tested = false;
		bool read_uncached = false;
		uint32_t sample_mb = 0;
		char volume_letter = '\0';
		char error_utf8[256] = {};
		std::vector<float> read_curve_mbps;
		std::vector<float> write_curve_mbps;
	};

	struct BadSectorResult {
		bool ok = false;
		uint64_t bytes_scanned = 0;
		uint64_t bytes_planned = 0;
		uint32_t error_count = 0;
		BadSectorMode mode = BadSectorMode::Quick;
		char error_utf8[256] = {};
		int matrix_cols = kMatrixCols;
		int matrix_rows = 0;
		std::vector<uint8_t> matrix;
	};

	struct JobState {
		JobKind kind = JobKind::None;
		bool running = false;
		float progress = 0.f;
		char status_utf8[256] = {};
		int physical_index = -1;
		SpeedTestResult speed;
		BadSectorResult bad_sector;
	};

	struct DriveTestHistory {
		int physical_index = -1;
		bool has_speed = false;
		bool has_bad_sector = false;
		int64_t speed_finished_unix_ms = 0;
		int64_t bad_finished_unix_ms = 0;
		SpeedTestResult speed;
		BadSectorResult bad_sector;
	};

	void Shutdown();
	void Cancel();
	JobState GetState();

	void LoadPersistedResults();
	void SavePersistedResults();
	DriveTestHistory GetDriveHistory(int physical_index);

	void RequestSpeedTest(int physical_index, const char* volume_letters,
		uint32_t sample_mb, bool include_write);
	void RequestBadSectorScan(int physical_index, uint64_t disk_size_bytes,
		BadSectorMode mode);

	void BuildBadSectorReport(const BadSectorResult& result, char* out, size_t out_size);
	void BuildSpeedReport(const SpeedTestResult& result, char* out, size_t out_size);

}
