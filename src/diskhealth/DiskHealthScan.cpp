#include "DiskHealthScan.h"
#include "HPage.h"
#include "HCleanTask.h"
#include "HAdminPrompt.h"
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <nvme.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include "Hi18n.h"

namespace DiskHealthScan {
	namespace {
		constexpr int kMaxPhysicalDrives = 32;

#pragma pack(push, 1)
		struct Ideregs {
			uint8_t bFeaturesReg;
			uint8_t bSectorCountReg;
			uint8_t bSectorNumberReg;
			uint8_t bCylLowReg;
			uint8_t bCylHighReg;
			uint8_t bDriveHeadReg;
			uint8_t bCommandReg;
			uint8_t bReserved;
		};

		struct SendCmdInParams {
			uint32_t cBufferSize;
			Ideregs irDriveRegs;
			uint8_t bDriveNumber;
			uint8_t bReserved[3];
			uint32_t dwReserved[4];
			uint8_t bBuffer[1];
		};

		struct DriverStatus {
			uint8_t bDriverError;
			uint8_t bIDEError;
			uint8_t bReserved[2];
			uint32_t dwReserved[2];
		};

		struct SendCmdOutParams {
			uint32_t cBufferSize;
			DriverStatus DriverStatus;
			uint8_t bBuffer[512];
		};

		struct SmartAttributeRaw {
			uint8_t id;
			uint16_t flags;
			uint8_t current;
			uint8_t worst;
			uint8_t raw[6];
			uint8_t reserved;
		};
#pragma pack(pop)

		constexpr uint8_t kSmartCommand = 0xB0;
		constexpr uint8_t kSmartFeatureReadData = 0xD0;
		constexpr uint8_t kSmartCylLow = 0x4F;
		constexpr uint8_t kSmartCylHigh = 0xC2;

		std::mutex g_mutex;
		Snapshot g_snapshot;
		std::thread g_worker;
		std::thread g_live_worker;
		std::atomic<bool> g_scanning{ false };
		std::atomic<bool> g_cancel{ false };
		std::atomic<bool> g_shutdown{ false };
		std::atomic<bool> g_live_enabled{ false };
		bool g_init_done = false;

		constexpr int kLiveRefreshIntervalSec = 3;
		// 內建碟合理上限
		constexpr uint64_t kMaxReasonableDiskBytes = 64ull * 1024ull * 1024ull * 1024ull * 1024ull;
		// USB 外接碟：12TB+ 實碟常見；僅拒絕明顯離譜值（如 ~11PiB 的錯誤 IOCTL）
		constexpr uint64_t kMaxUsbDiskBytes = 32ull * 1024ull * 1024ull * 1024ull * 1024ull;

		static bool IsReasonableDiskCapacity(uint64_t bytes, bool usb)
		{
			if (bytes == 0) {
				return false;
			}
			const uint64_t cap = usb ? kMaxUsbDiskBytes : kMaxReasonableDiskBytes;
			return bytes <= cap;
		}

		static const char* Win32ErrorName(DWORD err)
		{
			switch (err) {
			case ERROR_INVALID_FUNCTION: return "ERROR_INVALID_FUNCTION";
			case ERROR_NOT_SUPPORTED: return "ERROR_NOT_SUPPORTED";
			case ERROR_ACCESS_DENIED: return "ERROR_ACCESS_DENIED";
			case ERROR_INVALID_PARAMETER: return "ERROR_INVALID_PARAMETER";
			default: return "WIN32";
			}
		}

		static bool ShouldSkipAtaSmart(STORAGE_BUS_TYPE bus, DWORD err)
		{
			if (bus == BusTypeUsb) {
				return true;
			}
			return err == ERROR_INVALID_FUNCTION || err == ERROR_NOT_SUPPORTED;
		}

		static bool Utf8FromWide(const wchar_t* wide, char* out, size_t out_size)
		{
			if (wide == nullptr || out == nullptr || out_size == 0) {
				return false;
			}
			const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
			return n > 0;
		}

		static void TrimTrailingSpaces(char* s)
		{
			if (s == nullptr) {
				return;
			}
			size_t len = strlen(s);
			while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
				s[--len] = '\0';
			}
		}

		static STORAGE_BUS_TYPE BusTypeFromUtf8(const char* bus_utf8)
		{
			if (bus_utf8 == nullptr || bus_utf8[0] == '\0') {
				return BusTypeUnknown;
			}
			if (strcmp(bus_utf8, "NVMe") == 0) {
				return BusTypeNvme;
			}
			if (strcmp(bus_utf8, "SATA") == 0 || strcmp(bus_utf8, "ATA") == 0) {
				return BusTypeSata;
			}
			if (strcmp(bus_utf8, "USB") == 0) {
				return BusTypeUsb;
			}
			return BusTypeUnknown;
		}

		static void LogDriveSnapshot(const char* phase, const DriveInfo& drive)
		{
			HLOG_INFO(
				"DiskHealthScan [{}] PD{} bus={} cap={} smart={} temp={}C poh={} attrs={} note={}",
				phase, drive.physical_index,
				drive.bus_type_utf8[0] ? drive.bus_type_utf8 : "?",
				drive.size_bytes,
				drive.smart_available ? "ok" : "no",
				drive.temperature_c,
				drive.power_on_hours,
				drive.smart_attributes.size(),
				drive.status_note[0] ? drive.status_note : "-");
		}

		static const char* BusTypeToUtf8(STORAGE_BUS_TYPE bus)
		{
			if (bus == BusTypeNvme) {
				return "NVMe";
			}
			if (bus == BusTypeSata) {
				return "SATA";
			}
			if (bus == BusTypeAta) {
				return "ATA";
			}
			if (bus == BusTypeUsb) {
				return "USB";
			}
			if (bus == BusTypeSas) {
				return "SAS";
			}
			if (bus == BusTypeScsi) {
				return "SCSI";
			}
			if (bus == BusTypeSd) {
				return "SD";
			}
			if (bus == BusTypeMmc) {
				return "MMC";
			}
			return u8"未知";
		}

		static const char* SmartIdName(uint8_t id)
		{
			switch (id) {
			case 1: return u8"讀取錯誤率";
			case 2: return u8"吞吐效能";
			case 3: return u8"啟停次數";
			case 4: return u8"啟停時間";
			case 5: return u8"重配置扇區計數";
			case 7: return u8"尋道錯誤率";
			case 8: return u8"尋道時間效能";
			case 9: return u8"通電時數";
			case 10: return u8"啟動次數";
			case 12: return u8"通電週期";
			case 170: return u8"保留可用區塊";
			case 171: return u8"程式失敗區塊";
			case 172: return u8"擦除失敗區塊";
			case 173: return u8"平均擦除次數";
			case 174: return u8"意外斷電";
			case 175: return u8"電源損失保護";
			case 177: return u8"磨損指示";
			case 180: return u8"未備份區塊";
			case 181: return u8"程式失敗計數";
			case 182: return u8"擦除失敗計數";
			case 183: return u8"執行時壞塊";
			case 184: return u8"端到端錯誤";
			case 187: return u8"無法修正錯誤";
			case 188: return u8"命令逾時";
			case 190: return u8"氣流溫度";
			case 194: return u8"溫度 (攝氏)";
			case 196: return u8"重新配置事件";
			case 197: return u8"待處理扇區";
			case 198: return u8"離線無法修正";
			case 199: return u8"UltraDMA CRC 錯誤";
			case 200: return u8"寫入錯誤率";
			case 201: return u8"媒體錯誤 (NVMe)";
			case 218: return u8"SATA 下調";
			case 220: return u8"磁碟偏移";
			case 222: return u8"總寫入時間";
			case 225: return u8"主機寫入量";
			case 226: return u8"工作負載磁區";
			case 227: return u8"磁頭飛行小時";
			case 228: return u8"斷電重試計數";
			case 230: return u8"磁碟壽命";
			case 231: return u8"SSD 壽命已用 %";
			case 232: return u8"可用備用 %";
			case 233: return u8"媒體磨損指標";
			case 234: return u8"平均擦除次數 (最大)";
			case 235: return u8"良好區塊計數";
			case 241: return u8"總寫入量 (GB)";
			case 242: return u8"總讀取量 (GB)";
			case 243: return u8"總寫入量 (TB)";
			case 244: return u8"總讀取量 (TB)";
			default: return nullptr;
			}
		}

		static void SnprintfI18n(char* buf, size_t buf_size, const char* zh_fmt_key, ...)
		{
			const std::string fmt = Hi18n::I18NStr(zh_fmt_key);
			va_list ap;
			va_start(ap, zh_fmt_key);
			vsnprintf(buf, buf_size, fmt.c_str(), ap);
			va_end(ap);
		}

		static const char* HealthLevelKey(HealthLevel level)
		{
			switch (level) {
			case HealthLevel::Good: return u8"良好";
			case HealthLevel::Caution: return u8"注意";
			case HealthLevel::Bad: return u8"不良";
			case HealthLevel::Unavailable: return u8"無資料";
			default: return u8"未知";
			}
		}

		static void SmartIdNameFallback(uint8_t id, char* out, size_t out_size)
		{
			const char* known = SmartIdName(id);
			if (known != nullptr) {
				strncpy_s(out, out_size, known, _TRUNCATE);
				return;
			}
			SnprintfI18n(out, out_size, u8"SMART 屬性 #%u", static_cast<unsigned>(id));
		}

		static uint64_t RawFromSmart(const SmartAttributeRaw& attr)
		{
			uint64_t raw = 0;
			for (int i = 0; i < 6; ++i) {
				raw |= static_cast<uint64_t>(attr.raw[i]) << (8 * i);
			}
			return raw;
		}

		static void SetHealth(DriveInfo& drive, HealthLevel level)
		{
			drive.health = level;
			strncpy_s(drive.health_text, HealthLevelKey(level), _TRUNCATE);
		}

		static void ComputeHealth(DriveInfo& drive)
		{
			if (!drive.smart_available) {
				if (drive.temperature_c >= 60) {
					SetHealth(drive, HealthLevel::Caution);
				}
				else if (drive.temperature_c >= 0) {
					SetHealth(drive, HealthLevel::Good);
				}
				else if (drive.model_utf8[0] != '\0') {
					SetHealth(drive, HealthLevel::Unknown);
				}
				else {
					SetHealth(drive, HealthLevel::Unavailable);
				}
				return;
			}

			HealthLevel level = HealthLevel::Good;
			if (drive.reallocated_sectors > 0 || drive.pending_sectors > 0 || drive.uncorrectable_errors > 0) {
				level = HealthLevel::Caution;
			}
			if (drive.pending_sectors > 10 || drive.reallocated_sectors > 50 || drive.uncorrectable_errors > 0) {
				level = HealthLevel::Bad;
			}
			for (const auto& attr : drive.smart_attributes) {
				if (attr.id == 231 && attr.raw >= 90) {
					level = HealthLevel::Caution;
				}
				if (attr.id == 231 && attr.raw >= 100) {
					level = HealthLevel::Bad;
				}
			}
			if (drive.status_note[0] != '\0' && strstr(drive.status_note, I18N(u8"關鍵警告")) != nullptr) {
				level = HealthLevel::Bad;
			}
			if (drive.temperature_c >= 60) {
				level = (level == HealthLevel::Good) ? HealthLevel::Caution : level;
			}
			if (drive.temperature_c >= 70) {
				level = HealthLevel::Bad;
			}
			SetHealth(drive, level);
		}

		static HANDLE TryOpenPhysicalDrive(int index, DWORD desired_access)
		{
			wchar_t path[64] = {};
			_snwprintf_s(path, _TRUNCATE, L"\\\\.\\PhysicalDrive%d", index);
			const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
			return CreateFileW(path, desired_access, share, nullptr, OPEN_EXISTING, 0, nullptr);
		}

		// IOCTL_DISK_GET_LENGTH_INFO / SMART 需要 GENERIC_READ（或讀寫）；勿優先使用 access=0
		static HANDLE OpenPhysicalDriveForQuery(int index)
		{
			HANDLE handle = TryOpenPhysicalDrive(index, GENERIC_READ);
			if (handle != INVALID_HANDLE_VALUE) {
				return handle;
			}
			handle = TryOpenPhysicalDrive(index, GENERIC_READ | GENERIC_WRITE);
			if (handle != INVALID_HANDLE_VALUE) {
				return handle;
			}
			return TryOpenPhysicalDrive(index, 0);
		}

		static HANDLE OpenPhysicalDriveForSmart(int index)
		{
			HANDLE handle = TryOpenPhysicalDrive(index, GENERIC_READ | GENERIC_WRITE);
			if (handle != INVALID_HANDLE_VALUE) {
				return handle;
			}
			return TryOpenPhysicalDrive(index, GENERIC_READ);
		}

		static void CollectPhysicalDriveIndices(std::vector<int>& out_indices)
		{
			std::set<int> found;

			const DWORD logical_mask = GetLogicalDrives();
			for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
				const DWORD bit = 1u << (letter - L'A');
				if ((logical_mask & bit) == 0) {
					continue;
				}
				wchar_t vol_path[16] = {};
				_snwprintf_s(vol_path, _TRUNCATE, L"\\\\.\\%c:", static_cast<char>(letter));
				HANDLE vol = CreateFileW(vol_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, OPEN_EXISTING, 0, nullptr);
				if (vol == INVALID_HANDLE_VALUE) {
					continue;
				}
				STORAGE_DEVICE_NUMBER num = {};
				DWORD bytes = 0;
				if (DeviceIoControl(vol, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
					&num, sizeof(num), &bytes, nullptr)) {
					found.insert(static_cast<int>(num.DeviceNumber));
				}
				CloseHandle(vol);
			}

			for (int i = 0; i < kMaxPhysicalDrives; ++i) {
				HANDLE probe = OpenPhysicalDriveForQuery(i);
				if (probe != INVALID_HANDLE_VALUE) {
					found.insert(i);
					CloseHandle(probe);
				}
			}

			out_indices.assign(found.begin(), found.end());
			std::sort(out_indices.begin(), out_indices.end());
		}

		static bool QueryStorageDescriptor(HANDLE device, DriveInfo& drive)
		{
			STORAGE_PROPERTY_QUERY query = {};
			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;

			STORAGE_DESCRIPTOR_HEADER header = {};
			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
				&header, sizeof(header), &bytes, nullptr)) {
				return false;
			}

			std::vector<uint8_t> buffer(header.Size);
			if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
				buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
				return false;
			}

			const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
			if (desc->VendorIdOffset != 0 && desc->VendorIdOffset < buffer.size()) {
				const char* vendor = reinterpret_cast<const char*>(buffer.data() + desc->VendorIdOffset);
				strncpy_s(drive.model_utf8, vendor, _TRUNCATE);
			}
			if (desc->ProductIdOffset != 0 && desc->ProductIdOffset < buffer.size()) {
				const char* product = reinterpret_cast<const char*>(buffer.data() + desc->ProductIdOffset);
				if (drive.model_utf8[0] != '\0') {
					strncat_s(drive.model_utf8, " ", _TRUNCATE);
				}
				strncat_s(drive.model_utf8, product, _TRUNCATE);
			}
			TrimTrailingSpaces(drive.model_utf8);

			if (desc->SerialNumberOffset != 0 && desc->SerialNumberOffset < buffer.size()) {
				const char* serial = reinterpret_cast<const char*>(buffer.data() + desc->SerialNumberOffset);
				strncpy_s(drive.serial_utf8, serial, _TRUNCATE);
				TrimTrailingSpaces(drive.serial_utf8);
			}
			if (desc->ProductRevisionOffset != 0 && desc->ProductRevisionOffset < buffer.size()) {
				const char* fw = reinterpret_cast<const char*>(buffer.data() + desc->ProductRevisionOffset);
				strncpy_s(drive.firmware_utf8, fw, _TRUNCATE);
				TrimTrailingSpaces(drive.firmware_utf8);
			}

			strncpy_s(drive.bus_type_utf8, BusTypeToUtf8(desc->BusType), _TRUNCATE);
			return true;
		}

		static bool QueryDiskCapacityGeometry(HANDLE device, uint64_t& out_bytes)
		{
			std::vector<uint8_t> buffer(sizeof(DISK_GEOMETRY_EX) + 64, 0);
			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
				buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)
				|| bytes < sizeof(DISK_GEOMETRY_EX)) {
				return false;
			}
			const auto* geo = reinterpret_cast<const DISK_GEOMETRY_EX*>(buffer.data());
			if (geo->DiskSize.QuadPart <= 0) {
				return false;
			}
			out_bytes = static_cast<uint64_t>(geo->DiskSize.QuadPart);
			return IsReasonableDiskCapacity(out_bytes, false);
		}

		static bool QueryDiskCapacityFromLayout(HANDLE device, uint64_t& out_bytes, bool usb)
		{
			DWORD needed = 0;
			if (!DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0,
				nullptr, 0, &needed, nullptr) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
				return false;
			}
			if (needed < sizeof(DRIVE_LAYOUT_INFORMATION_EX)) {
				needed = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 256;
			}
			std::vector<uint8_t> buffer(needed, 0);
			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0,
				buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
				return false;
			}
			const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(buffer.data());
			uint64_t extent_end = 0;
			for (DWORD i = 0; i < layout->PartitionCount; ++i) {
				const PARTITION_INFORMATION_EX& part = layout->PartitionEntry[i];
				const uint64_t end = static_cast<uint64_t>(part.StartingOffset.QuadPart)
					+ static_cast<uint64_t>(part.PartitionLength.QuadPart);
				if (end > extent_end) {
					extent_end = end;
				}
			}
			if (!IsReasonableDiskCapacity(extent_end, usb)) {
				return false;
			}
			out_bytes = extent_end;
			return true;
		}

		static bool VolumeBelongsToPhysical(int physical_index, wchar_t letter)
		{
			wchar_t vol_path[16] = {};
			_snwprintf_s(vol_path, _TRUNCATE, L"\\\\.\\%c:", static_cast<char>(letter));
			HANDLE vol = CreateFileW(vol_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, 0, nullptr);
			if (vol == INVALID_HANDLE_VALUE) {
				return false;
			}
			STORAGE_DEVICE_NUMBER num = {};
			DWORD bytes = 0;
			const bool match = DeviceIoControl(vol, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
				&num, sizeof(num), &bytes, nullptr)
				&& static_cast<int>(num.DeviceNumber) == physical_index;
			CloseHandle(vol);
			return match;
		}

		static bool QueryCapacityFromVolumePath(const wchar_t* root_path, bool usb,
			uint64_t& out_cap)
		{
			auto try_length_on_handle = [&](HANDLE vol) -> bool {
				GET_LENGTH_INFORMATION len = {};
				DWORD bytes = 0;
				if (!DeviceIoControl(vol, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
					&len, sizeof(len), &bytes, nullptr) || len.Length.QuadPart <= 0) {
					return false;
				}
				const uint64_t cap = static_cast<uint64_t>(len.Length.QuadPart);
				if (IsReasonableDiskCapacity(cap, usb)) {
					out_cap = cap;
					return true;
				}
				return false;
			};

			const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
			const DWORD backup_flags = FILE_FLAG_BACKUP_SEMANTICS;
			const wchar_t* paths[2] = { root_path, nullptr };
			wchar_t device_path[16] = {};
			if (root_path != nullptr && root_path[0] != L'\0' && root_path[1] == L':'
				&& (root_path[2] == L'\\' || root_path[2] == L'\0')) {
				_snwprintf_s(device_path, _TRUNCATE, L"\\\\.\\%c:", static_cast<wchar_t>(root_path[0]));
				paths[1] = device_path;
			}

			for (const wchar_t* path : paths) {
				if (path == nullptr) {
					continue;
				}
				HANDLE vol = CreateFileW(path, 0, share, nullptr, OPEN_EXISTING, 0, nullptr);
				if (vol != INVALID_HANDLE_VALUE) {
					if (try_length_on_handle(vol)) {
						CloseHandle(vol);
						return true;
					}
					CloseHandle(vol);
				}

				vol = CreateFileW(path, GENERIC_READ, share, nullptr, OPEN_EXISTING,
					backup_flags, nullptr);
				if (vol != INVALID_HANDLE_VALUE) {
					if (try_length_on_handle(vol)) {
						CloseHandle(vol);
						return true;
					}
					CloseHandle(vol);
				}
			}

			const wchar_t* free_path = root_path;
			if (free_path != nullptr && free_path[0] != L'\0' && free_path[1] == L':'
				&& free_path[2] != L'\\') {
				_snwprintf_s(device_path, _TRUNCATE, L"%c:\\", free_path[0]);
				free_path = device_path;
			}
			ULARGE_INTEGER free_bytes = {};
			ULARGE_INTEGER total_bytes = {};
			ULARGE_INTEGER total_free = {};
			if (GetDiskFreeSpaceExW(free_path, &free_bytes, &total_bytes, &total_free)
				&& total_bytes.QuadPart > 0) {
				if (IsReasonableDiskCapacity(total_bytes.QuadPart, usb)) {
					out_cap = total_bytes.QuadPart;
					return true;
				}
				if (usb && total_bytes.QuadPart >= 512ull * 1024ull * 1024ull) {
					out_cap = total_bytes.QuadPart;
					HLOG_INFO("USB capacity from partition size {} (visible volume)",
						static_cast<unsigned long long>(out_cap));
					return true;
				}
			}
			return false;
		}

		static bool QueryUsbCapacity(int physical_index, uint64_t& out_bytes)
		{
			const DWORD mask = GetLogicalDrives();
			uint64_t best = 0;
			bool any = false;
			for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
				const DWORD bit = 1u << (letter - L'A');
				if ((mask & bit) == 0 || !VolumeBelongsToPhysical(physical_index, letter)) {
					continue;
				}
				wchar_t root[8] = {};
				_snwprintf_s(root, _TRUNCATE, L"%c:\\", static_cast<char>(letter));
				uint64_t cap = 0;
				if (!QueryCapacityFromVolumePath(root, true, cap)) {
					continue;
				}
				if (!any || cap > best) {
					best = cap;
					any = true;
				}
				HLOG_INFO("PD{} USB volume {} -> {} bytes", physical_index,
					static_cast<char>(letter), cap);
			}
			if (!any) {
				return false;
			}
			out_bytes = best;
			return true;
		}

		static bool QueryCapacityFromVolumes(int physical_index, uint64_t& out_bytes)
		{
			const DWORD mask = GetLogicalDrives();
			uint64_t best = 0;
			bool any = false;
			for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
				const DWORD bit = 1u << (letter - L'A');
				if ((mask & bit) == 0 || !VolumeBelongsToPhysical(physical_index, letter)) {
					continue;
				}
				wchar_t root[8] = {};
				_snwprintf_s(root, _TRUNCATE, L"%c:\\", static_cast<char>(letter));
				uint64_t cap = 0;
				if (!QueryCapacityFromVolumePath(root, false, cap)) {
					continue;
				}
				if (!any || cap > best) {
					best = cap;
					any = true;
				}
			}
			if (!any) {
				return false;
			}
			out_bytes = best;
			return true;
		}

		static bool QueryDiskCapacity(HANDLE device, int physical_index, uint64_t& out_bytes,
			bool usb)
		{
			if (usb && QueryUsbCapacity(physical_index, out_bytes)) {
				return true;
			}
			if (!usb && QueryCapacityFromVolumes(physical_index, out_bytes)) {
				HLOG_INFO("PD{} capacity from volume -> {} bytes", physical_index, out_bytes);
				return true;
			}

			GET_LENGTH_INFORMATION len = {};
			DWORD bytes = 0;
			if (DeviceIoControl(device, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
				&len, sizeof(len), &bytes, nullptr) && len.Length.QuadPart > 0) {
				const uint64_t cap = static_cast<uint64_t>(len.Length.QuadPart);
				if (IsReasonableDiskCapacity(cap, usb)) {
					out_bytes = cap;
					HLOG_DEBUG("PD{} GET_LENGTH_INFO -> {} bytes", physical_index, out_bytes);
					return true;
				}
				HLOG_WARN("PD{} GET_LENGTH_INFO {} bytes rejected (usb={}), trying fallbacks",
					physical_index, cap, usb);
			}
			const DWORD err_len = GetLastError();
			if (QueryDiskCapacityGeometry(device, out_bytes)
				&& IsReasonableDiskCapacity(out_bytes, usb)) {
				HLOG_DEBUG("PD{} GEOMETRY_EX -> {} bytes", physical_index, out_bytes);
				return true;
			}
			out_bytes = 0;
			if (QueryDiskCapacityFromLayout(device, out_bytes, usb)) {
				HLOG_INFO("PD{} capacity from partition layout -> {} bytes", physical_index, out_bytes);
				return true;
			}
			if (QueryCapacityFromVolumes(physical_index, out_bytes)) {
				HLOG_INFO("PD{} capacity from volume (fallback) -> {} bytes", physical_index, out_bytes);
				return true;
			}
			if (usb && QueryUsbCapacity(physical_index, out_bytes)) {
				return true;
			}
			HLOG_DEBUG("PD{} QueryDiskCapacity failed, length err {}", physical_index, err_len);
			return false;
		}

		static bool QueryCapacityFromFirstVolumeLetter(const char* letters, bool usb,
			uint64_t& out_bytes)
		{
			if (letters == nullptr || letters[0] == '\0') {
				return false;
			}
			for (size_t i = 0; letters[i] != '\0'; ++i) {
				const char ch = letters[i];
				if (ch < 'A' || ch > 'Z') {
					continue;
				}
				wchar_t root[8] = {};
				_snwprintf_s(root, _TRUNCATE, L"%c:\\", ch);
				if (QueryCapacityFromVolumePath(root, usb, out_bytes)) {
					HLOG_INFO("USB capacity from volume {}: -> {} bytes", ch, out_bytes);
					return true;
				}
			}
			return false;
		}

		static bool ReadStorageDeviceTemperature(HANDLE device, int& out_temp_c,
			STORAGE_PROPERTY_ID property_id)
		{
			STORAGE_PROPERTY_QUERY query = {};
			query.PropertyId = property_id;
			query.QueryType = PropertyStandardQuery;

			STORAGE_TEMPERATURE_DATA_DESCRIPTOR header = {};
			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
				&header, sizeof(header), &bytes, nullptr)) {
				HLOG_DEBUG("Temperature IOCTL prop={} err={}",
					static_cast<int>(property_id), GetLastError());
				return false;
			}

			DWORD buf_size = header.Size;
			if (buf_size < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) {
				buf_size = sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)
					+ sizeof(STORAGE_TEMPERATURE_INFO) * 4;
			}
			std::vector<uint8_t> buffer(buf_size, 0);
			bytes = 0;
			if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
				buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
				HLOG_DEBUG("Temperature IOCTL (full) prop={} err={}",
					static_cast<int>(property_id), GetLastError());
				return false;
			}

			const auto* desc = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(
				buffer.data());
			for (WORD i = 0; i < desc->InfoCount; ++i) {
				const SHORT t = desc->TemperatureInfo[i].Temperature;
				if (t == STORAGE_TEMPERATURE_VALUE_NOT_REPORTED) {
					continue;
				}
				if (t > -60 && t < 150) {
					out_temp_c = static_cast<int>(t);
					HLOG_DEBUG("Temperature prop={} sensor={} -> {} C",
						static_cast<int>(property_id), i, out_temp_c);
					return true;
				}
			}
			HLOG_DEBUG("Temperature prop={}: no valid sensor (count={})",
				static_cast<int>(property_id), desc->InfoCount);
			return false;
		}

		static bool TryReadTemperatureOnly(HANDLE device, DriveInfo& drive)
		{
			int temp = -1;
			if (ReadStorageDeviceTemperature(device, temp, StorageDeviceTemperatureProperty)) {
				drive.temperature_c = temp;
				return true;
			}
			if (ReadStorageDeviceTemperature(device, temp, StorageAdapterTemperatureProperty)) {
				drive.temperature_c = temp;
				return true;
			}
			return false;
		}

		// NVMe 健康日誌合成屬性（勿佔用 ATA id 5 重配置扇區）
		static constexpr uint8_t kNvmeSyntheticMediaErrors = 201;

		static bool AttrNameHas(const char* name, const char* token)
		{
			return name != nullptr && token != nullptr && strstr(name, token) != nullptr;
		}

		static int SmartAttrCounterValue(const SmartAttribute& attr)
		{
			if (attr.id == 194 || attr.id == 190) {
				return -1;
			}
			uint64_t r = attr.raw;
			if (r == 0) {
				return -1;
			}
			if (attr.id == 5 || attr.id == 196 || attr.id == 197 || attr.id == 187
				|| attr.id == 198 || attr.id == kNvmeSyntheticMediaErrors) {
				const uint64_t low16 = r & 0xFFFF;
				if (low16 > 0 && (r >> 16) == 0) {
					return static_cast<int>((std::min)(low16, static_cast<uint64_t>(INT_MAX)));
				}
				if (r <= static_cast<uint64_t>(INT_MAX)) {
					return static_cast<int>(r);
				}
				const uint64_t low32 = r & 0xFFFFFFFF;
				if (low32 > 0 && low32 <= static_cast<uint64_t>(INT_MAX)) {
					return static_cast<int>(low32);
				}
				return INT_MAX;
			}
			if (r <= static_cast<uint64_t>(INT_MAX)) {
				return static_cast<int>(r);
			}
			return INT_MAX;
		}

		static void SyncSectorSummaryFromAttributes(DriveInfo& drive)
		{
			bool saw_realloc = (drive.reallocated_sectors >= 0);
			bool saw_pending = (drive.pending_sectors >= 0);
			bool saw_unc = (drive.uncorrectable_errors >= 0);

			auto apply = [&](int& slot, bool& saw, int value) {
				if (value < 0) {
					return;
				}
				if (!saw || value > slot) {
					slot = value;
				}
				saw = true;
			};

			for (const auto& attr : drive.smart_attributes) {
				const int v = SmartAttrCounterValue(attr);
				if (v < 0) {
					continue;
				}
				switch (attr.id) {
				case 5:
					if (AttrNameHas(attr.name_utf8, u8"媒體")) {
						apply(drive.uncorrectable_errors, saw_unc, v);
					}
					else {
						apply(drive.reallocated_sectors, saw_realloc, v);
					}
					break;
				case 196:
					apply(drive.reallocated_sectors, saw_realloc, v);
					break;
				case 197:
					apply(drive.pending_sectors, saw_pending, v);
					break;
				case 187:
				case 198:
					apply(drive.uncorrectable_errors, saw_unc, v);
					break;
				case kNvmeSyntheticMediaErrors:
					apply(drive.uncorrectable_errors, saw_unc, v);
					break;
				default:
					if (AttrNameHas(attr.name_utf8, u8"重配置")
						|| AttrNameHas(attr.name_utf8, u8"重新配置")) {
						apply(drive.reallocated_sectors, saw_realloc, v);
					}
					else if (AttrNameHas(attr.name_utf8, u8"待處理")) {
						apply(drive.pending_sectors, saw_pending, v);
					}
					else if (AttrNameHas(attr.name_utf8, u8"無法修正")
						|| AttrNameHas(attr.name_utf8, u8"不可修正")
						|| AttrNameHas(attr.name_utf8, u8"離線無法")) {
						apply(drive.uncorrectable_errors, saw_unc, v);
					}
					break;
				}
			}

			const bool has_smart_table = drive.smart_available || !drive.smart_attributes.empty();
			if (has_smart_table) {
				if (!saw_realloc) {
					drive.reallocated_sectors = 0;
				}
				if (!saw_pending) {
					drive.pending_sectors = 0;
				}
				if (!saw_unc) {
					drive.uncorrectable_errors = 0;
				}
			}
		}

		static int TemperatureFromSmartAttr(uint8_t id, uint8_t current, uint64_t raw)
		{
			if (id != 194 && id != 190) {
				return -1;
			}
			if (raw > 0 && raw <= 120) {
				return static_cast<int>(raw);
			}
			if (current > 0 && current <= 120) {
				return static_cast<int>(current);
			}
			const int low = static_cast<int>(raw & 0xFF);
			if (low > 0 && low <= 120) {
				return low;
			}
			return -1;
		}

		static void ApplySmartAttribute(DriveInfo& drive, const SmartAttributeRaw& raw)
		{
			SmartAttribute attr = {};
			attr.id = raw.id;
			SmartIdNameFallback(raw.id, attr.name_utf8, sizeof(attr.name_utf8));
			attr.current = raw.current;
			attr.worst = raw.worst;
			attr.raw = RawFromSmart(raw);
			attr.threshold = static_cast<uint32_t>(raw.worst);
			attr.prefailure = (raw.flags & 0x1) != 0;
			drive.smart_attributes.push_back(attr);

			switch (raw.id) {
			case 5:
				drive.reallocated_sectors = static_cast<int>(attr.raw);
				break;
			case 9:
				drive.power_on_hours = static_cast<int>(attr.raw);
				break;
			case 194:
			case 190: {
				const int temp = TemperatureFromSmartAttr(raw.id, raw.current, attr.raw);
				if (temp >= 0) {
					drive.temperature_c = temp;
				}
				break;
			}
			case 197:
				drive.pending_sectors = static_cast<int>(attr.raw);
				break;
			case 187:
			case 198:
				drive.uncorrectable_errors = static_cast<int>(attr.raw);
				break;
			default:
				break;
			}
		}

		static void FinalizeSmartDerivedFields(DriveInfo& drive)
		{
			for (const auto& attr : drive.smart_attributes) {
				const int temp = TemperatureFromSmartAttr(attr.id, attr.current, attr.raw);
				if (temp >= 0) {
					drive.temperature_c = temp;
				}
				if (attr.id == 9 && attr.raw > 0 && attr.raw < 10000000ull) {
					drive.power_on_hours = static_cast<int>(attr.raw);
				}
			}
			SyncSectorSummaryFromAttributes(drive);
			ComputeHealth(drive);
		}

		static bool ParseAtaSmartBuffer(const uint8_t* data512, size_t data_len, DriveInfo& drive)
		{
			if (data512 == nullptr || data_len < 362) {
				return false;
			}
			drive.smart_attributes.clear();
			bool any = false;
			for (int i = 0; i < 30; ++i) {
				const auto* raw = reinterpret_cast<const SmartAttributeRaw*>(&data512[2 + i * 12]);
				if (raw->id == 0) {
					continue;
				}
				any = true;
				ApplySmartAttribute(drive, *raw);
			}
			if (any) {
				drive.smart_supported = true;
				drive.smart_available = true;
				FinalizeSmartDerivedFields(drive);
			}
			return any;
		}

		static bool ReadSmartViaSmartRcv(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note);
		static bool ReadSmartViaScsiSatAll(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note, bool try_lun_scan = false);
		static bool ReadSmartViaAtaPassThrough(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note);

		static void MergeSupplementalSmartAttributes(DriveInfo& drive, const DriveInfo& extra)
		{
			size_t added = 0;
			for (const auto& attr : extra.smart_attributes) {
				bool exists = false;
				for (const auto& existing : drive.smart_attributes) {
					if (existing.id == attr.id) {
						exists = true;
						break;
					}
				}
				if (!exists) {
					drive.smart_attributes.push_back(attr);
					++added;
				}
			}
			if (added > 0) {
				FinalizeSmartDerivedFields(drive);
			}
		}

		static void TryMergeNvmeSupplementalSmart(HANDLE device, DriveInfo& drive,
			bool& needs_admin, bool log_info)
		{
			const size_t before = drive.smart_attributes.size();

			DriveInfo extra = {};
			extra.physical_index = drive.physical_index;
			if (ReadSmartViaSmartRcv(device, extra, needs_admin, false, false)
				&& !extra.smart_attributes.empty()) {
				MergeSupplementalSmartAttributes(drive, extra);
			}

			extra = {};
			extra.physical_index = drive.physical_index;
			if (ReadSmartViaScsiSatAll(device, extra, needs_admin, false, false)
				&& !extra.smart_attributes.empty()) {
				MergeSupplementalSmartAttributes(drive, extra);
			}

			extra = {};
			extra.physical_index = drive.physical_index;
			if (ReadSmartViaAtaPassThrough(device, extra, needs_admin, false, false)
				&& !extra.smart_attributes.empty()) {
				MergeSupplementalSmartAttributes(drive, extra);
			}

			if (log_info) {
				if (drive.smart_attributes.size() > before) {
					HLOG_INFO("PD{} NVMe: merged supplemental SMART +{} (total {})",
						drive.physical_index,
						drive.smart_attributes.size() - before,
						drive.smart_attributes.size());
				}
				else {
					HLOG_DEBUG("PD{} NVMe: no supplemental ATA SMART (RCV/SAT/ATA unavailable)",
						drive.physical_index);
				}
			}
		}

		static void NoteSmartError(DriveInfo& drive, bool& needs_admin, DWORD err,
			const char* ui_ctx, const char* log_op, bool warn_log, bool write_status_note)
		{
			if (err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD) {
				needs_admin = true;
			}
			if (write_status_note) {
				if (err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD) {
					snprintf(drive.status_note, sizeof(drive.status_note),
						I18N(u8"%s：存取遭拒（請以系統管理員身分執行本程式）"), ui_ctx);
				}
				else {
					snprintf(drive.status_note, sizeof(drive.status_note),
						I18N(u8"%s（錯誤碼 %lu）"), ui_ctx, err);
				}
			}
			if (warn_log) {
				HLOG_WARN("SMART PD{}: {} {} ({})",
					drive.physical_index, log_op, Win32ErrorName(err), err);
			}
		}

		static void SetUsbSmartFailureStatus(DriveInfo& drive, bool needs_admin,
			DWORD err_primary, DWORD err_secondary)
		{
			const bool denied = (err_primary == ERROR_ACCESS_DENIED
				|| err_secondary == ERROR_ACCESS_DENIED
				|| err_primary == ERROR_PRIVILEGE_NOT_HELD
				|| err_secondary == ERROR_PRIVILEGE_NOT_HELD);
			const bool ioctl_unsupported = (err_primary == ERROR_INVALID_FUNCTION
				|| err_secondary == ERROR_INVALID_FUNCTION
				|| err_primary == ERROR_NOT_SUPPORTED
				|| err_secondary == ERROR_NOT_SUPPORTED);
			if (denied && needs_admin) {
				strncpy_s(drive.status_note, u8"USB：無法讀取 SMART（請以系統管理員身分執行本程式）", _TRUNCATE);
			}
			else if (denied) {
				snprintf(drive.status_note, sizeof(drive.status_note),
					I18N(u8"USB：無法讀取 SMART（存取遭拒%s）"),
					needs_admin ? I18N(u8"，建議以系統管理員執行") : "");
			}
			else if (ioctl_unsupported) {
				strncpy_s(drive.status_note, u8"USB：此橋接不支援 Windows 內建 SMART IOCTL（已嘗試 SCSI 轉發仍失敗）", _TRUNCATE);
			}
			else {
				strncpy_s(drive.status_note, u8"USB：未能讀取 SMART（橋接晶片或驅動限制）", _TRUNCATE);
			}
		}

		static uint64_t LeBytesToU64(const uint8_t* bytes, size_t count)
		{
			uint64_t value = 0;
			const size_t n = (std::min)(count, static_cast<size_t>(8));
			for (size_t i = 0; i < n; ++i) {
				value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
			}
			return value;
		}

		static void AppendNvmeSmartAttr(DriveInfo& drive, uint8_t id, const char* name, uint64_t raw)
		{
			SmartAttribute attr = {};
			attr.id = id;
			strncpy_s(attr.name_utf8, name, _TRUNCATE);
			attr.raw = raw;
			attr.worst = 100;
			if (raw <= 255) {
				attr.current = static_cast<uint8_t>(raw);
			}
			else if (id == 9 || id == 10 || id == 12) {
				attr.current = static_cast<uint8_t>((std::min)(raw, 255ull));
			}
			drive.smart_attributes.push_back(attr);
		}

		static bool ParseNvmeHealthLogBuffer(const uint8_t* log, size_t log_len, DriveInfo& drive)
		{
			if (log == nullptr || log_len < offsetof(NVME_HEALTH_INFO_LOG, Temperature) + 2) {
				return false;
			}
			const auto* health = reinterpret_cast<const NVME_HEALTH_INFO_LOG*>(log);

			const uint16_t temp_k = static_cast<uint16_t>(health->Temperature[0])
				| (static_cast<uint16_t>(health->Temperature[1]) << 8);
			if (temp_k > 0 && temp_k < 400) {
				drive.temperature_c = static_cast<int>(temp_k) - 273;
			}

			const uint64_t poh = LeBytesToU64(health->PowerOnHours, 8);
			if (poh > 0 && poh < 10000000ull) {
				drive.power_on_hours = static_cast<int>(poh);
			}

			const uint64_t media_errors = LeBytesToU64(health->MediaErrors, 8);
			if (media_errors > 0) {
				drive.uncorrectable_errors = static_cast<int>(
					(std::min)(media_errors, static_cast<uint64_t>(INT_MAX)));
			}

			drive.smart_supported = true;
			drive.smart_available = true;
			drive.smart_attributes.clear();
			AppendNvmeSmartAttr(drive, 194, u8"溫度 (攝氏)", static_cast<uint64_t>(
				drive.temperature_c >= 0 ? drive.temperature_c : 0));
			AppendNvmeSmartAttr(drive, 9, u8"通電時數", poh);
			AppendNvmeSmartAttr(drive, 231, u8"已使用壽命 %", health->PercentageUsed);
			AppendNvmeSmartAttr(drive, 232, u8"可用備用 %", health->AvailableSpare);
			AppendNvmeSmartAttr(drive, 233, u8"備用門檻 %", health->AvailableSpareThreshold);
			AppendNvmeSmartAttr(drive, 1, u8"關鍵警告", health->CriticalWarning.AsUchar);
			AppendNvmeSmartAttr(drive, kNvmeSyntheticMediaErrors, u8"媒體錯誤", media_errors);
			AppendNvmeSmartAttr(drive, 198, u8"錯誤日誌筆數",
				LeBytesToU64(health->ErrorInfoLogEntryCount, 8));
			AppendNvmeSmartAttr(drive, 12, u8"不安全關機",
				LeBytesToU64(health->UnsafeShutdowns, 8));
			AppendNvmeSmartAttr(drive, 10, u8"電源週期",
				LeBytesToU64(health->PowerCycle, 8));
			const uint64_t du_read = LeBytesToU64(health->DataUnitRead, 8);
			const uint64_t du_write = LeBytesToU64(health->DataUnitWritten, 8);
			AppendNvmeSmartAttr(drive, 241, u8"資料讀取 (512B 單位×1000)",
				du_read);
			AppendNvmeSmartAttr(drive, 242, u8"資料寫入 (512B 單位×1000)",
				du_write);
			AppendNvmeSmartAttr(drive, 225, u8"主機讀取命令",
				LeBytesToU64(health->HostReadCommands, 8));
			AppendNvmeSmartAttr(drive, 226, u8"主機寫入命令",
				LeBytesToU64(health->HostWrittenCommands, 8));
			AppendNvmeSmartAttr(drive, 227, u8"控制器忙碌時間(分)",
				LeBytesToU64(health->ControllerBusyTime, 8));
			AppendNvmeSmartAttr(drive, 228, u8"複合溫度警示(分)",
				health->WarningCompositeTemperatureTime);
			AppendNvmeSmartAttr(drive, 229, u8"複合溫度嚴重(分)",
				health->CriticalCompositeTemperatureTime);

			const USHORT* const sensors = &health->TemperatureSensor1;
			for (int si = 0; si < 8; ++si) {
				const USHORT raw = sensors[si];
				if (raw == 0 || raw == 0xFFFF) {
					continue;
				}
				int temp_c = -1;
				if (raw > 200 && raw < 400) {
					temp_c = static_cast<int>(raw) - 273;
				}
				else if (raw < 150) {
					temp_c = static_cast<int>(raw);
				}
				if (temp_c >= 0) {
					char name[48] = {};
					SnprintfI18n(name, sizeof(name), u8"溫度感測器 %d", si + 1);
					AppendNvmeSmartAttr(drive, static_cast<uint8_t>(240 + si), name,
						static_cast<uint64_t>(temp_c));
				}
			}

			if (health->CriticalWarning.AsUchar != 0) {
				snprintf(drive.status_note, sizeof(drive.status_note),
					I18N(u8"NVMe 關鍵警告 0x%02X"), health->CriticalWarning.AsUchar);
			}
			else {
				strncpy_s(drive.status_note, "NVMe SMART OK", _TRUNCATE);
			}
			return true;
		}

		static bool ReadSmartViaSmartRcv(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note)
		{
			SendCmdInParams in = {};
			in.cBufferSize = sizeof(SendCmdOutParams) - 1;
			in.bDriveNumber = 0;
			in.irDriveRegs.bFeaturesReg = kSmartFeatureReadData;
			in.irDriveRegs.bSectorCountReg = 1;
			in.irDriveRegs.bSectorNumberReg = 1;
			in.irDriveRegs.bCylLowReg = kSmartCylLow;
			in.irDriveRegs.bCylHighReg = kSmartCylHigh;
			in.irDriveRegs.bCommandReg = kSmartCommand;

			SendCmdOutParams out = {};
			DWORD bytes = 0;
			if (!DeviceIoControl(device, SMART_RCV_DRIVE_DATA, &in, sizeof(SendCmdInParams) - 1,
				&out, sizeof(out), &bytes, nullptr)) {
				NoteSmartError(drive, needs_admin, GetLastError(),
					I18N(u8"SMART 讀取"), "SMART_RCV_DRIVE_DATA", warn_log, write_status_note);
				return false;
			}
			if (out.DriverStatus.bDriverError != 0 || out.DriverStatus.bIDEError != 0) {
				HLOG_DEBUG("SMART_RCV PD{} driverErr={} ideErr={}",
					drive.physical_index, out.DriverStatus.bDriverError, out.DriverStatus.bIDEError);
				strncpy_s(drive.status_note, u8"SMART 命令未由磁碟回應", _TRUNCATE);
				return false;
			}
			HLOG_DEBUG("SMART_RCV_DRIVE_DATA PD{} ok, bytes={}", drive.physical_index, bytes);
			return ParseAtaSmartBuffer(out.bBuffer, sizeof(out.bBuffer), drive);
		}

		static void FillSatSmartCdb(UCHAR* cdb, int cdb_len, bool use_cdb16)
		{
			memset(cdb, 0, static_cast<size_t>(cdb_len));
			if (use_cdb16) {
				cdb[0] = 0xA1;
				cdb[1] = 0x08;
				cdb[2] = 0x0E;
				cdb[3] = kSmartFeatureReadData;
				cdb[4] = 1;
				cdb[5] = 1;
				cdb[6] = kSmartCylLow;
				cdb[7] = kSmartCylHigh;
				cdb[9] = kSmartCommand;
			}
			else {
				cdb[0] = 0x85;
				cdb[1] = 0x08;
				cdb[2] = 0x0E;
				cdb[3] = kSmartFeatureReadData;
				cdb[4] = 1;
				cdb[5] = 1;
				cdb[6] = kSmartCylLow;
				cdb[7] = kSmartCylHigh;
				cdb[9] = kSmartCommand;
			}
		}

		static bool ReadSmartViaScsiSatDirect(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note, bool use_cdb16, UCHAR lun)
		{
			constexpr DWORD data_len = 512;
			constexpr DWORD sense_len = 32;
			const DWORD spt_size = sizeof(SCSI_PASS_THROUGH_DIRECT) + sense_len;
			const DWORD buf_size = spt_size + data_len;
			std::vector<uint8_t> buffer(buf_size, 0);
			auto* spt = reinterpret_cast<SCSI_PASS_THROUGH_DIRECT*>(buffer.data());
			uint8_t* data_buf = buffer.data() + spt_size;

			spt->Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
			spt->PathId = 0;
			spt->TargetId = 0;
			spt->Lun = lun;
			spt->SenseInfoLength = static_cast<UCHAR>(sense_len);
			spt->DataIn = SCSI_IOCTL_DATA_IN;
			spt->DataTransferLength = data_len;
			spt->TimeOutValue = 30;
			spt->DataBuffer = data_buf;
			spt->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH_DIRECT);
			spt->CdbLength = use_cdb16 ? 16 : 12;
			FillSatSmartCdb(spt->Cdb, static_cast<int>(spt->CdbLength), use_cdb16);

			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_SCSI_PASS_THROUGH_DIRECT,
				buffer.data(), buf_size, buffer.data(), buf_size, &bytes, nullptr)) {
				const DWORD err = GetLastError();
				NoteSmartError(drive, needs_admin, err,
					use_cdb16 ? "SCSI SAT-16" : "SCSI SAT-12",
					use_cdb16 ? "SCSI_PASS_16" : "SCSI_PASS_12",
					warn_log, write_status_note);
				return false;
			}
			if (spt->ScsiStatus != 0) {
				SetLastError(ERROR_GEN_FAILURE);
				if (warn_log) {
					HLOG_DEBUG("SCSI SAT PD{} status={} cdb16={} lun={}",
						drive.physical_index, spt->ScsiStatus, use_cdb16,
						static_cast<unsigned>(lun));
				}
				return false;
			}
			if (warn_log) {
				HLOG_DEBUG("SCSI SAT PD{} ok cdb16={} lun={}",
					drive.physical_index, use_cdb16, static_cast<unsigned>(lun));
			}
			return ParseAtaSmartBuffer(data_buf, data_len, drive);
		}

#pragma pack(push, 1)
		struct ScsiPassThroughBuffer {
			SCSI_PASS_THROUGH spt;
			ULONG filler;
			UCHAR sense[32];
			UCHAR data[512];
		};
#pragma pack(pop)

		static bool ReadSmartViaScsiSatIndirect(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note, bool use_cdb16, UCHAR lun)
		{
			ScsiPassThroughBuffer sptb = {};
			SCSI_PASS_THROUGH& spt = sptb.spt;
			spt.Length = sizeof(SCSI_PASS_THROUGH);
			spt.PathId = 0;
			spt.TargetId = 0;
			spt.Lun = lun;
			spt.SenseInfoLength = sizeof(sptb.sense);
			spt.DataIn = SCSI_IOCTL_DATA_IN;
			spt.DataTransferLength = sizeof(sptb.data);
			spt.TimeOutValue = 30;
			spt.SenseInfoOffset =
				static_cast<ULONG>(reinterpret_cast<UCHAR*>(&sptb.sense) - reinterpret_cast<UCHAR*>(&spt));
			spt.DataBufferOffset =
				static_cast<ULONG>(reinterpret_cast<UCHAR*>(&sptb.data) - reinterpret_cast<UCHAR*>(&spt));
			spt.CdbLength = use_cdb16 ? 16 : 12;
			FillSatSmartCdb(spt.Cdb, static_cast<int>(spt.CdbLength), use_cdb16);

			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_SCSI_PASS_THROUGH,
				&sptb, sizeof(sptb), &sptb, sizeof(sptb), &bytes, nullptr)) {
				const DWORD err = GetLastError();
				NoteSmartError(drive, needs_admin, err,
					use_cdb16 ? "SCSI SAT-16 (buffered)" : "SCSI SAT-12 (buffered)",
					"SCSI_PASS_THROUGH", warn_log, write_status_note);
				return false;
			}
			if (spt.ScsiStatus != 0) {
				SetLastError(ERROR_GEN_FAILURE);
				if (warn_log) {
					HLOG_DEBUG("SCSI SAT buffered PD{} status={} cdb16={} lun={}",
						drive.physical_index, spt.ScsiStatus, use_cdb16,
						static_cast<unsigned>(lun));
				}
				return false;
			}
			if (warn_log) {
				HLOG_DEBUG("SCSI SAT buffered PD{} ok cdb16={} lun={}",
					drive.physical_index, use_cdb16, static_cast<unsigned>(lun));
			}
			return ParseAtaSmartBuffer(sptb.data, sizeof(sptb.data), drive);
		}

		static bool ReadSmartViaScsiSat(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note, bool use_cdb16)
		{
			return ReadSmartViaScsiSatDirect(device, drive, needs_admin, warn_log,
				write_status_note, use_cdb16, 0);
		}

		static bool ReadSmartViaScsiSatAll(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note, bool try_lun_scan)
		{
			const auto try_once = [&](bool use_cdb16, bool indirect, UCHAR lun) -> bool {
				if (indirect) {
					return ReadSmartViaScsiSatIndirect(device, drive, needs_admin, warn_log,
						write_status_note, use_cdb16, lun);
				}
				return ReadSmartViaScsiSatDirect(device, drive, needs_admin, warn_log,
					write_status_note, use_cdb16, lun);
			};

			const UCHAR max_lun = try_lun_scan ? static_cast<UCHAR>(4) : static_cast<UCHAR>(1);
			for (UCHAR lun = 0; lun < max_lun; ++lun) {
				const bool modes[4][2] = { {true, false}, {true, true}, {false, false}, {false, true} };
				for (const auto& mode : modes) {
					drive.smart_attributes.clear();
					drive.smart_supported = false;
					drive.smart_available = false;
					if (try_once(mode[0], mode[1], lun)) {
						return true;
					}
				}
			}
			SetLastError(ERROR_GEN_FAILURE);
			return false;
		}

		static void CopySmartResultToDrive(DriveInfo& drive, const DriveInfo& from)
		{
			drive.temperature_c = from.temperature_c;
			drive.power_on_hours = from.power_on_hours;
			drive.reallocated_sectors = from.reallocated_sectors;
			drive.pending_sectors = from.pending_sectors;
			drive.uncorrectable_errors = from.uncorrectable_errors;
			drive.smart_supported = from.smart_supported;
			drive.smart_available = from.smart_available;
			drive.smart_attributes = from.smart_attributes;
			drive.skip_ata_smart = drive.skip_ata_smart || from.skip_ata_smart;
			if (from.status_note[0] != '\0') {
				strncpy_s(drive.status_note, from.status_note, _TRUNCATE);
			}
		}

		static bool TrySmartReadOnVolumeHandle(HANDLE vol, char letter, DriveInfo& drive,
			bool& needs_admin, bool warn_log);

		static HANDLE OpenVolumePathForSmart(const wchar_t* vol_path)
		{
			if (vol_path == nullptr || vol_path[0] == L'\0') {
				return INVALID_HANDLE_VALUE;
			}
			const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
			const DWORD flags = FILE_ATTRIBUTE_NORMAL;
			const DWORD accesses[] = {
				GENERIC_READ | GENERIC_WRITE,
				GENERIC_READ,
				0u,
			};
			for (DWORD access : accesses) {
				HANDLE vol = CreateFileW(vol_path, access, share, nullptr, OPEN_EXISTING, flags, nullptr);
				if (vol != INVALID_HANDLE_VALUE) {
					return vol;
				}
			}
			return INVALID_HANDLE_VALUE;
		}

		static HANDLE OpenVolumeForSmart(wchar_t letter)
		{
			wchar_t vol_path[32] = {};
			_snwprintf_s(vol_path, _TRUNCATE, L"\\\\.\\%c:", static_cast<char>(letter));
			HANDLE vol = OpenVolumePathForSmart(vol_path);
			if (vol != INVALID_HANDLE_VALUE) {
				return vol;
			}
			_snwprintf_s(vol_path, _TRUNCATE, L"\\\\?\\%c:", static_cast<char>(letter));
			return OpenVolumePathForSmart(vol_path);
		}

		static bool VolumeHandleMatchesPhysical(HANDLE vol, int physical_index)
		{
			STORAGE_DEVICE_NUMBER num = {};
			DWORD bytes = 0;
			if (!DeviceIoControl(vol, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
				&num, sizeof(num), &bytes, nullptr)) {
				return false;
			}
			return static_cast<int>(num.DeviceNumber) == physical_index;
		}

		static void MarkUsbVolumeSatUnreachable(DriveInfo& drive, char letter, bool warn_log)
		{
			drive.skip_ata_smart = true;
			const char vol = (letter >= 'A' && letter <= 'Z') ? letter : '*';
			snprintf(drive.status_note, sizeof(drive.status_note),
				I18N(u8"USB [%c:]：此裝置未提供 SMART（常見於隨身碟或簡易 USB 儲存）"),
				vol);
			if (warn_log) {
				HLOG_INFO("PD{}: volume {} SAT unreachable, skip SMART retries on live refresh",
					drive.physical_index, vol);
			}
		}

		static bool ReadSmartOnEnumeratedVolumes(DriveInfo& drive, bool& needs_admin, bool warn_log)
		{
			if (drive.skip_ata_smart) {
				return false;
			}
			wchar_t vol_name[MAX_PATH] = {};
			HANDLE find = FindFirstVolumeW(vol_name, static_cast<DWORD>(std::size(vol_name)));
			if (find == INVALID_HANDLE_VALUE) {
				return false;
			}
			bool any_tried = false;
			bool ok = false;
			do {
				size_t len = wcslen(vol_name);
				if (len > 0 && vol_name[len - 1] == L'\\') {
					vol_name[len - 1] = L'\0';
				}
				HANDLE vol = OpenVolumePathForSmart(vol_name);
				if (vol == INVALID_HANDLE_VALUE) {
					continue;
				}
				if (!VolumeHandleMatchesPhysical(vol, drive.physical_index)) {
					CloseHandle(vol);
					continue;
				}
				any_tried = true;
				if (TrySmartReadOnVolumeHandle(vol, '*', drive, needs_admin, warn_log)) {
					ok = true;
					CloseHandle(vol);
					break;
				}
				CloseHandle(vol);
			} while (FindNextVolumeW(find, vol_name, static_cast<DWORD>(std::size(vol_name))));
			FindVolumeClose(find);
			if (any_tried && warn_log && !ok) {
				HLOG_DEBUG("PD{}: enumerated volume paths did not return SMART",
					drive.physical_index);
			}
			return ok;
		}

		static bool TrySmartReadOnVolumeHandle(HANDLE vol, char letter, DriveInfo& drive,
			bool& needs_admin, bool warn_log)
		{
			DriveInfo tmp = drive;
			tmp.smart_attributes.clear();
			tmp.smart_supported = false;
			tmp.smart_available = false;

			if (ReadSmartViaSmartRcv(vol, tmp, needs_admin, warn_log, false)) {
				CopySmartResultToDrive(drive, tmp);
				HLOG_INFO("SMART PD{} via volume {} SMART_RCV ({} attrs)",
					drive.physical_index, letter, drive.smart_attributes.size());
				return true;
			}
			const DWORD err_rcv = GetLastError();

			tmp = drive;
			tmp.smart_attributes.clear();
			tmp.smart_supported = false;
			tmp.smart_available = false;
			if (ReadSmartViaScsiSatAll(vol, tmp, needs_admin, warn_log, false, true)) {
				CopySmartResultToDrive(drive, tmp);
				HLOG_INFO("SMART PD{} via volume {} SCSI SAT ({} attrs)",
					drive.physical_index, letter, drive.smart_attributes.size());
				return true;
			}
			const DWORD err_sat = GetLastError();

			tmp = drive;
			tmp.smart_attributes.clear();
			if (ReadSmartViaAtaPassThrough(vol, tmp, needs_admin, warn_log, false)) {
				CopySmartResultToDrive(drive, tmp);
				HLOG_INFO("SMART PD{} via volume {} ATA Pass-Through ({} attrs)",
					drive.physical_index, letter, drive.smart_attributes.size());
				return true;
			}
			const DWORD err_ata = GetLastError();

			const bool all_unsupported = (err_rcv == ERROR_NOT_SUPPORTED
				|| err_rcv == ERROR_INVALID_FUNCTION)
				&& (err_sat == ERROR_NOT_SUPPORTED || err_sat == ERROR_INVALID_FUNCTION)
				&& (err_ata == ERROR_NOT_SUPPORTED || err_ata == ERROR_INVALID_FUNCTION);
			if (all_unsupported) {
				drive.skip_ata_smart = true;
				snprintf(drive.status_note, sizeof(drive.status_note),
					I18N(u8"USB [%c:]：橋接器不支援 Windows SMART（非管理員權限問題）"),
					letter);
				if (warn_log) {
					HLOG_INFO("PD{}: volume {} IOCTL not supported, skip SMART retries",
						drive.physical_index, letter);
				}
			}
			else if (err_sat == ERROR_GEN_FAILURE
				|| (err_rcv == ERROR_NOT_SUPPORTED && err_ata == ERROR_NOT_SUPPORTED)) {
				MarkUsbVolumeSatUnreachable(drive, letter, warn_log);
			}
			else if (warn_log) {
				HLOG_WARN("SMART PD{} volume {}: RCV({}) SAT({}) ATA({})",
					drive.physical_index, letter,
					Win32ErrorName(err_rcv), Win32ErrorName(err_sat), Win32ErrorName(err_ata));
			}
			else {
				HLOG_DEBUG("SMART PD{} volume {}: RCV({}) SAT({}) ATA({})",
					drive.physical_index, letter,
					Win32ErrorName(err_rcv), Win32ErrorName(err_sat), Win32ErrorName(err_ata));
			}
			return false;
		}

		static bool ReadSmartOnVolumeLetters(DriveInfo& drive, bool& needs_admin, bool warn_log)
		{
			if (drive.skip_ata_smart) {
				return false;
			}
			if (ReadSmartOnEnumeratedVolumes(drive, needs_admin, warn_log)) {
				return true;
			}
			if (drive.skip_ata_smart) {
				return false;
			}

			const char* letters = drive.volume_letters;
			if (letters == nullptr || letters[0] == '\0') {
				return false;
			}
			for (size_t i = 0; letters[i] != '\0'; ++i) {
				char ch = letters[i];
				if (ch == ',' || ch == ' ' || ch == '\t') {
					continue;
				}
				if (ch >= 'a' && ch <= 'z') {
					ch = static_cast<char>(ch - 'a' + 'A');
				}
				if (ch < 'A' || ch > 'Z') {
					continue;
				}
				HANDLE vol = OpenVolumeForSmart(static_cast<wchar_t>(ch));
				if (vol == INVALID_HANDLE_VALUE) {
					const DWORD err = GetLastError();
					if (warn_log) {
						HLOG_WARN("SMART PD{} volume {}: open failed ({})",
							drive.physical_index, ch, Win32ErrorName(err));
					}
					else {
						HLOG_DEBUG("SMART volume {}: open err={}", ch, err);
					}
					continue;
				}
				const bool ok = TrySmartReadOnVolumeHandle(vol, ch, drive, needs_admin, warn_log);
				CloseHandle(vol);
				if (ok) {
					return true;
				}
			}
			return false;
		}

		static bool ReadSmartViaAtaPassThrough(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool warn_log, bool write_status_note)
		{
			constexpr DWORD data_len = 512;
			std::vector<uint8_t> buffer(sizeof(ATA_PASS_THROUGH_DIRECT) + data_len);
			auto* apt = reinterpret_cast<ATA_PASS_THROUGH_DIRECT*>(buffer.data());
			apt->Length = sizeof(ATA_PASS_THROUGH_DIRECT);
			apt->AtaFlags = ATA_FLAGS_DATA_IN;
			apt->DataTransferLength = data_len;
			apt->TimeOutValue = 15;
			uint8_t* data_buf = buffer.data() + sizeof(ATA_PASS_THROUGH_DIRECT);
			apt->DataBuffer = data_buf;
			apt->CurrentTaskFile[0] = kSmartFeatureReadData;
			apt->CurrentTaskFile[1] = 1;
			apt->CurrentTaskFile[2] = 1;
			apt->CurrentTaskFile[3] = kSmartCylLow;
			apt->CurrentTaskFile[4] = kSmartCylHigh;
			apt->CurrentTaskFile[6] = kSmartCommand;

			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_ATA_PASS_THROUGH_DIRECT,
				apt, static_cast<DWORD>(buffer.size()),
				apt, static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
				NoteSmartError(drive, needs_admin, GetLastError(),
					I18N(u8"ATA 直通讀取"), "ATA_PASS_THROUGH", warn_log, write_status_note);
				return false;
			}
			return ParseAtaSmartBuffer(data_buf, data_len, drive);
		}

		static bool ReadNvmeHealthLogWithProperty(HANDLE device, DriveInfo& drive,
			STORAGE_PROPERTY_ID property_id, bool& needs_admin, bool log_success_info)
		{
			constexpr DWORD kLogSize = 512;
			const DWORD buffer_length = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters)
				+ sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + kLogSize;

			std::vector<uint8_t> buffer(buffer_length, 0);
			auto* query = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buffer.data());
			auto* desc = reinterpret_cast<STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buffer.data());
			auto* proto = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(query->AdditionalParameters);

			query->PropertyId = property_id;
			query->QueryType = PropertyStandardQuery;
			proto->ProtocolType = ProtocolTypeNvme;
			proto->DataType = NVMeDataTypeLogPage;
			proto->ProtocolDataRequestValue = NVME_LOG_PAGE_HEALTH_INFO;
			proto->ProtocolDataRequestSubValue = 0;
			proto->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
			proto->ProtocolDataLength = sizeof(NVME_HEALTH_INFO_LOG);

			DWORD bytes = 0;
			if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, buffer.data(), buffer_length,
				buffer.data(), buffer_length, &bytes, nullptr)) {
				HLOG_DEBUG("NVMe health IOCTL prop={} err={}",
					static_cast<int>(property_id), GetLastError());
				return false;
			}

			if (desc->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)
				|| desc->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) {
				HLOG_WARN("NVMe health PD{} prop={}: header ver={} size={} returned={}",
					drive.physical_index, static_cast<int>(property_id),
					desc->Version, desc->Size, bytes);
			}

			const STORAGE_PROTOCOL_SPECIFIC_DATA& out_proto = desc->ProtocolSpecificData;
			if (out_proto.ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)
				|| out_proto.ProtocolDataLength < sizeof(NVME_HEALTH_INFO_LOG)) {
				HLOG_WARN("NVMe health PD{} prop={}: bad offset={} len={}",
					drive.physical_index, static_cast<int>(property_id),
					out_proto.ProtocolDataOffset, out_proto.ProtocolDataLength);
				return false;
			}

			const auto* log = reinterpret_cast<const uint8_t*>(&out_proto)
				+ out_proto.ProtocolDataOffset;
			if (reinterpret_cast<uintptr_t>(log + out_proto.ProtocolDataLength)
				> reinterpret_cast<uintptr_t>(buffer.data() + buffer.size())) {
				return false;
			}
			const bool ok = ParseNvmeHealthLogBuffer(log, out_proto.ProtocolDataLength, drive);
			if (ok) {
				if (log_success_info) {
					HLOG_INFO("NVMe health PD{} prop={} temp={}C poh={} attrs={}",
						drive.physical_index, static_cast<int>(property_id),
						drive.temperature_c, drive.power_on_hours, drive.smart_attributes.size());
				}
				else {
					HLOG_DEBUG("NVMe health PD{} temp={}C poh={}",
						drive.physical_index, drive.temperature_c, drive.power_on_hours);
				}
			}
			return ok;
		}

		static bool ReadNvmeHealthLog(HANDLE device, DriveInfo& drive, bool& needs_admin,
			bool log_success_info, bool warn_on_fail)
		{
			if (ReadNvmeHealthLogWithProperty(device, drive,
				StorageDeviceProtocolSpecificProperty, needs_admin, log_success_info)) {
				return true;
			}
			const DWORD err1 = GetLastError();
			HLOG_DEBUG("NVMe device health failed PD{} err={}, try adapter", drive.physical_index, err1);
			if (ReadNvmeHealthLogWithProperty(device, drive,
				StorageAdapterProtocolSpecificProperty, needs_admin, log_success_info)) {
				return true;
			}
			NoteSmartError(drive, needs_admin, err1,
				I18N(u8"NVMe 健康日誌"), "NVMe health log", warn_on_fail, warn_on_fail);
			return false;
		}

		static bool RefreshTemperatureOnly(HANDLE device, DriveInfo& drive)
		{
			const int prev = drive.temperature_c;
			if (!TryReadTemperatureOnly(device, drive)) {
				return false;
			}
			if (drive.temperature_c != prev) {
				HLOG_DEBUG("PD{} temperature {} -> {} C",
					drive.physical_index, prev, drive.temperature_c);
			}
			ComputeHealth(drive);
			return drive.temperature_c >= 0;
		}

		static void MergeSmartFields(DriveInfo& dst, const DriveInfo& src)
		{
			dst.temperature_c = src.temperature_c;
			dst.power_on_hours = src.power_on_hours;
			dst.reallocated_sectors = src.reallocated_sectors;
			dst.pending_sectors = src.pending_sectors;
			dst.uncorrectable_errors = src.uncorrectable_errors;
			dst.smart_supported = src.smart_supported;
			dst.smart_available = src.smart_available;
			dst.smart_attributes = src.smart_attributes;
			dst.skip_ata_smart = dst.skip_ata_smart || src.skip_ata_smart;
			if (src.status_note[0] != '\0') {
				strncpy_s(dst.status_note, src.status_note, _TRUNCATE);
			}
			ComputeHealth(dst);
		}

		static bool ReadSmartAttributes(HANDLE device, DriveInfo& drive, bool& needs_admin,
			STORAGE_BUS_TYPE bus, bool live_quiet)
		{
			const bool warn_log = !live_quiet;
			const bool log_nvme_ok = !live_quiet;

			drive.smart_attributes.clear();
			drive.smart_supported = false;
			drive.smart_available = false;

			if (bus == BusTypeUnknown) {
				bus = BusTypeFromUtf8(drive.bus_type_utf8);
			}

			const bool is_usb = (bus == BusTypeUsb) || (strcmp(drive.bus_type_utf8, "USB") == 0);

			if (drive.skip_ata_smart) {
				if (TryReadTemperatureOnly(device, drive) && drive.temperature_c >= 0) {
					drive.smart_supported = true;
					strncpy_s(drive.status_note, u8"僅溫度（此碟已標記不支援 SMART）", _TRUNCATE);
					return true;
				}
				return false;
			}

			HLOG_DEBUG("ReadSmart PD{} bus={} usb={} live_quiet={}",
				drive.physical_index, static_cast<int>(bus), is_usb, live_quiet);

			const bool try_nvme = !is_usb
				&& ((bus == BusTypeNvme) || (strcmp(drive.bus_type_utf8, "NVMe") == 0));
			if (try_nvme) {
				if (ReadNvmeHealthLog(device, drive, needs_admin, log_nvme_ok, warn_log)) {
					TryMergeNvmeSupplementalSmart(device, drive, needs_admin, log_nvme_ok);
					strncpy_s(drive.status_note, u8"NVMe 健康日誌 + 補充 SMART", _TRUNCATE);
					return true;
				}
			}

			const bool write_err_status = !is_usb;
			DWORD err_sat = 0;
			DWORD err_ata = 0;
			DWORD err_rcv = 0;

			if (live_quiet && is_usb) {
				if (drive.skip_ata_smart) {
					const int prev_temp = drive.temperature_c;
					if (TryReadTemperatureOnly(device, drive) && drive.temperature_c >= 0) {
						drive.smart_supported = true;
						return true;
					}
					drive.temperature_c = prev_temp;
					return false;
				}
				if (ReadSmartOnVolumeLetters(drive, needs_admin, false)) {
					return true;
				}
				const int prev_temp = drive.temperature_c;
				if (TryReadTemperatureOnly(device, drive) && drive.temperature_c >= 0) {
					drive.smart_supported = true;
					return true;
				}
				drive.temperature_c = prev_temp;
				return false;
			}

			if (is_usb) {
				if (ReadSmartOnVolumeLetters(drive, needs_admin, warn_log)) {
					if (drive.status_note[0] == '\0') {
						strncpy_s(drive.status_note, u8"USB：透過磁碟區讀取 SMART", _TRUNCATE);
					}
					return true;
				}
				err_sat = GetLastError();
				drive.smart_attributes.clear();
				drive.smart_supported = false;
				drive.smart_available = false;

				if (!drive.skip_ata_smart) {
					if (ReadSmartViaScsiSatAll(device, drive, needs_admin, warn_log, false)) {
						strncpy_s(drive.status_note, u8"USB：透過 SCSI SAT 讀取 SMART", _TRUNCATE);
						return true;
					}
					err_sat = GetLastError();
					drive.smart_attributes.clear();
					drive.smart_supported = false;
					drive.smart_available = false;

					if (ReadSmartViaAtaPassThrough(device, drive, needs_admin, warn_log, false)) {
						strncpy_s(drive.status_note, u8"USB：透過 ATA Pass-Through 讀取 SMART", _TRUNCATE);
						return true;
					}
					err_ata = GetLastError();
					drive.smart_attributes.clear();

					if (ReadSmartViaSmartRcv(device, drive, needs_admin, warn_log, false)) {
						strncpy_s(drive.status_note, u8"USB：透過 SMART IOCTL 讀取", _TRUNCATE);
						return true;
					}
					err_rcv = GetLastError();
					drive.smart_attributes.clear();

					if (err_sat == ERROR_GEN_FAILURE
						&& drive.volume_letters[0] != '\0' && !drive.skip_ata_smart) {
						MarkUsbVolumeSatUnreachable(drive, drive.volume_letters[0], warn_log);
					}
				}
			}
			else {
				if (ReadSmartViaSmartRcv(device, drive, needs_admin, warn_log, write_err_status)) {
					return true;
				}
				err_rcv = GetLastError();
				drive.smart_attributes.clear();
				drive.smart_supported = false;
				drive.smart_available = false;

				if (ReadSmartViaAtaPassThrough(device, drive, needs_admin, warn_log, write_err_status)) {
					return true;
				}
				err_ata = GetLastError();
				drive.smart_attributes.clear();

				if (ReadSmartViaScsiSatAll(device, drive, needs_admin, warn_log, false)) {
					strncpy_s(drive.status_note, u8"透過 SCSI SAT 讀取 SMART", _TRUNCATE);
					return true;
				}
				err_sat = GetLastError();
				drive.smart_attributes.clear();
			}

			if (!is_usb && (ShouldSkipAtaSmart(bus, err_rcv) || ShouldSkipAtaSmart(bus, err_ata))) {
				drive.skip_ata_smart = true;
				strncpy_s(drive.status_note, u8"此介面不支援 SMART（橋接或驅動限制）", _TRUNCATE);
				if (!live_quiet) {
					HLOG_INFO("PD{}: permanent SMART skip ({}/{})",
						drive.physical_index, Win32ErrorName(err_rcv), Win32ErrorName(err_ata));
				}
			}
			else if (is_usb && !live_quiet) {
				HLOG_INFO("PD{} USB: SMART failed SAT({}) ATA({}) RCV({}), fallback temperature",
					drive.physical_index, Win32ErrorName(err_sat), Win32ErrorName(err_ata),
					Win32ErrorName(err_rcv));
			}

			if (try_nvme && ReadNvmeHealthLog(device, drive, needs_admin, log_nvme_ok, warn_log)) {
				return true;
			}

			const int temp_only = drive.temperature_c;
			if (TryReadTemperatureOnly(device, drive) && drive.temperature_c >= 0) {
				drive.smart_supported = true;
				drive.smart_available = false;
				if (is_usb) {
					strncpy_s(drive.status_note, u8"USB：橋接器未提供完整 SMART（僅溫度）", _TRUNCATE);
				}
				else {
					strncpy_s(drive.status_note, u8"僅溫度感測器（無完整 SMART）", _TRUNCATE);
				}
				if (!live_quiet) {
					HLOG_INFO("PD{} temperature-only -> {} C",
						drive.physical_index, drive.temperature_c);
				}
				return true;
			}
			drive.temperature_c = temp_only;

			if (is_usb && !drive.skip_ata_smart) {
				SetUsbSmartFailureStatus(drive, needs_admin, err_sat, err_ata);
				if (drive.volume_letters[0] != '\0'
					&& strstr(drive.status_note, I18N(u8"已嘗試")) == nullptr) {
					char extra[96] = {};
					snprintf(extra, sizeof(extra), I18N(u8"（已嘗試 [%s]）"), drive.volume_letters);
					strncat_s(drive.status_note, extra, _TRUNCATE);
				}
			}
			else if (drive.status_note[0] == '\0') {
				strncpy_s(drive.status_note, u8"無法讀取 SMART（請確認權限或介面支援）", _TRUNCATE);
			}

			if (warn_log) {
				HLOG_WARN("ReadSmart PD{}: all paths failed (see status_note)", drive.physical_index);
			}
			return false;
		}

		static void CollectVolumeLetters(int physical_index, DriveInfo& drive);

		static bool RefreshDriveSmartOnly(DriveInfo& drive, bool& needs_admin)
		{
			const int index = drive.physical_index;
			if (drive.volume_letters[0] == '\0') {
				CollectVolumeLetters(index, drive);
			}

			HANDLE handle = OpenPhysicalDriveForSmart(index);
			if (handle == INVALID_HANDLE_VALUE) {
				handle = OpenPhysicalDriveForQuery(index);
			}
			if (handle == INVALID_HANDLE_VALUE) {
				HLOG_DEBUG("LiveRefresh: open PD{} failed err={}", index, GetLastError());
				return false;
			}

			if (drive.skip_ata_smart) {
				const bool ok = RefreshTemperatureOnly(handle, drive);
				CloseHandle(handle);
				return ok;
			}

			STORAGE_BUS_TYPE bus = BusTypeFromUtf8(drive.bus_type_utf8);
			if (bus == BusTypeUnknown) {
				STORAGE_PROPERTY_QUERY query = {};
				query.PropertyId = StorageDeviceProperty;
				query.QueryType = PropertyStandardQuery;
				STORAGE_DEVICE_DESCRIPTOR desc = {};
				DWORD bytes = 0;
				if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
					&desc, sizeof(desc), &bytes, nullptr) && desc.Version > 0) {
					bus = desc.BusType;
				}
			}

			DriveInfo fresh = drive;
			fresh.physical_index = index;
			const bool ok = ReadSmartAttributes(handle, fresh, needs_admin, bus, true);
			CloseHandle(handle);

			if (ok) {
				MergeSmartFields(drive, fresh);
			}
			else {
				if (fresh.temperature_c >= 0 && fresh.temperature_c != drive.temperature_c) {
					drive.temperature_c = fresh.temperature_c;
					ComputeHealth(drive);
				}
				if (fresh.skip_ata_smart) {
					drive.skip_ata_smart = true;
					strncpy_s(drive.status_note, fresh.status_note, _TRUNCATE);
				}
			}
			return ok;
		}

		static void CollectVolumeLetters(int physical_index, DriveInfo& drive)
		{
			const DWORD mask = GetLogicalDrives();
			char letters[32] = {};
			size_t pos = 0;
			for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
				const DWORD bit = 1u << (letter - L'A');
				if ((mask & bit) == 0) {
					continue;
				}
				wchar_t vol_path[16] = {};
				_snwprintf_s(vol_path, _TRUNCATE, L"\\\\.\\%c:", static_cast<char>(letter));
				HANDLE vol = CreateFileW(vol_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, OPEN_EXISTING, 0, nullptr);
				if (vol == INVALID_HANDLE_VALUE) {
					continue;
				}
				STORAGE_DEVICE_NUMBER num = {};
				DWORD bytes = 0;
				const bool ok = DeviceIoControl(vol, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
					&num, sizeof(num), &bytes, nullptr)
					&& static_cast<int>(num.DeviceNumber) == physical_index;
				CloseHandle(vol);
				if (!ok) {
					continue;
				}
				if (pos + 4 < sizeof(letters)) {
					if (pos > 0) {
						letters[pos++] = ',';
						letters[pos++] = ' ';
					}
					letters[pos++] = static_cast<char>(letter);
					letters[pos++] = ':';
				}
			}
			strncpy_s(drive.volume_letters, letters, _TRUNCATE);
		}

		static void ScanOneDrive(int index, DriveInfo& drive, bool& needs_admin)
		{
			drive.physical_index = index;
			_snwprintf_s(drive.device_path, _TRUNCATE, L"\\\\.\\PhysicalDrive%d", index);

			HLOG_INFO("ScanOneDrive PD{} begin", index);
			HANDLE handle = OpenPhysicalDriveForSmart(index);
			if (handle == INVALID_HANDLE_VALUE) {
				handle = OpenPhysicalDriveForQuery(index);
			}
			if (handle == INVALID_HANDLE_VALUE) {
				const DWORD open_err = GetLastError();
				SetHealth(drive, HealthLevel::Unavailable);
				snprintf(drive.status_note, sizeof(drive.status_note),
					I18N(u8"無法開啟裝置（錯誤 %lu）"), open_err);
				snprintf(drive.model_utf8, sizeof(drive.model_utf8), "PhysicalDrive%d", index);
				HLOG_ERROR("ScanOneDrive PD{}: open failed err={}", index, open_err);
				return;
			}
			HLOG_DEBUG("ScanOneDrive PD{}: handle opened", index);

			STORAGE_BUS_TYPE bus = BusTypeUnknown;
			{
				STORAGE_PROPERTY_QUERY query = {};
				query.PropertyId = StorageDeviceProperty;
				query.QueryType = PropertyStandardQuery;
				STORAGE_DEVICE_DESCRIPTOR desc = {};
				DWORD bytes = 0;
				if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
					&desc, sizeof(desc), &bytes, nullptr) && desc.Version > 0) {
					bus = desc.BusType;
				}
			}

			QueryStorageDescriptor(handle, drive);
			if (bus == BusTypeUnknown && drive.bus_type_utf8[0] != '\0') {
				bus = BusTypeFromUtf8(drive.bus_type_utf8);
			}

			const bool is_usb = (bus == BusTypeUsb) || (strcmp(drive.bus_type_utf8, "USB") == 0);

			CollectVolumeLetters(index, drive);

			uint64_t capacity = 0;
			if (QueryDiskCapacity(handle, index, capacity, is_usb)) {
				drive.size_bytes = capacity;
			}
			else if (is_usb && drive.volume_letters[0] != '\0'
				&& QueryCapacityFromFirstVolumeLetter(drive.volume_letters, true, capacity)) {
				drive.size_bytes = capacity;
			}
			else if (is_usb) {
				HLOG_WARN("PD{} USB: could not resolve capacity (letters=[{}])",
					index,
					drive.volume_letters[0] != '\0' ? drive.volume_letters : "none");
			}

			if (!ReadSmartAttributes(handle, drive, needs_admin, bus, false)) {
				if (drive.status_note[0] == '\0') {
					strncpy_s(drive.status_note, u8"無法讀取 SMART（外接/USB 橋接器可能不支援）", _TRUNCATE);
				}
			}

			CloseHandle(handle);

			if (drive.size_bytes == 0 && drive.volume_letters[0] != '\0') {
				uint64_t cap_retry = 0;
				if (QueryCapacityFromFirstVolumeLetter(drive.volume_letters, is_usb, cap_retry)) {
					drive.size_bytes = cap_retry;
				}
			}

			if (is_usb && !drive.smart_available) {
				if (drive.temperature_c >= 0 || drive.size_bytes > 0) {
					SetHealth(drive, HealthLevel::Unknown);
				}
				else {
					drive.smart_supported = false;
					SetHealth(drive, HealthLevel::Unavailable);
				}
			}
			else {
				ComputeHealth(drive);
			}

			if (drive.model_utf8[0] == '\0') {
				snprintf(drive.model_utf8, sizeof(drive.model_utf8), "PhysicalDrive%d", index);
			}
			LogDriveSnapshot("scan-done", drive);
		}

		static void FormatNowLocal(char* buf, size_t buf_size)
		{
			SYSTEMTIME st = {};
			GetLocalTime(&st);
			snprintf(buf, buf_size, "%04u-%02u-%02u %02u:%02u:%02u",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}

		static void RunLiveRefreshLoop()
		{
			HLOG_INFO("DiskHealthScan live refresh thread started (interval {}s)",
				kLiveRefreshIntervalSec);
			while (!g_shutdown.load(std::memory_order_acquire)) {
				if (!g_live_enabled.load(std::memory_order_relaxed)) {
					std::this_thread::sleep_for(std::chrono::milliseconds(400));
					continue;
				}
				if (g_scanning.load(std::memory_order_relaxed)) {
					std::this_thread::sleep_for(std::chrono::milliseconds(400));
					continue;
				}

				bool have_drives = false;
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					have_drives = !g_snapshot.drives.empty();
				}
				if (!have_drives) {
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				bool needs_admin = false;
				char time_buf[64] = {};
				FormatNowLocal(time_buf, sizeof(time_buf));

				std::vector<DriveInfo> drives_work;
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					drives_work = g_snapshot.drives;
					g_snapshot.live_refreshing = true;
				}
				for (auto& drive : drives_work) {
					RefreshDriveSmartOnly(drive, needs_admin);
				}
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					for (size_t i = 0; i < drives_work.size() && i < g_snapshot.drives.size(); ++i) {
						MergeSmartFields(g_snapshot.drives[i], drives_work[i]);
					}
					g_snapshot.live_refreshing = false;
					g_snapshot.needs_admin_hint = g_snapshot.needs_admin_hint || needs_admin;
					strncpy_s(g_snapshot.last_live_update_time, time_buf, _TRUNCATE);
					g_snapshot.live_refresh_interval_sec = kLiveRefreshIntervalSec;
				}

				HLOG_DEBUG("DiskHealthScan live refresh at {}", time_buf);

				for (int i = 0; i < kLiveRefreshIntervalSec * 10; ++i) {
					if (g_shutdown.load(std::memory_order_acquire)) {
						break;
					}
					if (!g_live_enabled.load(std::memory_order_relaxed)) {
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}
			HLOG_INFO("DiskHealthScan live refresh thread stopped");
		}

		static void StartLiveWorker()
		{
			if (g_live_worker.joinable()) {
				return;
			}
			g_live_worker = std::thread([] { RunLiveRefreshLoop(); });
		}

		static void RunScan()
		{
			g_scanning.store(true, std::memory_order_release);
			g_cancel.store(false, std::memory_order_release);

			bool needs_admin = false;

			std::vector<int> physical_indices;
			CollectPhysicalDriveIndices(physical_indices);
			HLOG_INFO("DiskHealthScan RunScan: {} physical drive(s), admin={}",
				physical_indices.size(), HCleanIsRunningAsAdmin() ? "yes" : "no");

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.drives.clear();
			}

			const size_t total = physical_indices.empty() ? 1 : physical_indices.size();
			size_t step = 0;
			if (physical_indices.empty()) {
				std::lock_guard<std::mutex> lock(g_mutex);
				strncpy_s(g_snapshot.status_text, u8"未找到實體硬碟", _TRUNCATE);
			}

			for (int index : physical_indices) {
				if (g_cancel.load(std::memory_order_relaxed) || g_shutdown.load(std::memory_order_acquire)) {
					break;
				}

				++step;
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					SnprintfI18n(g_snapshot.status_text, sizeof(g_snapshot.status_text),
						u8"掃描 PhysicalDrive%d… (%zu/%zu)", index, step, total);
					g_snapshot.progress = static_cast<float>(step - 1) / static_cast<float>(total);
				}

				DriveInfo drive = {};
				ScanOneDrive(index, drive, needs_admin);
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					g_snapshot.drives.push_back(drive);
					g_snapshot.progress = static_cast<float>(step) / static_cast<float>(total);
				}
			}

			SYSTEMTIME st = {};
			GetLocalTime(&st);
			char time_buf[64] = {};
			snprintf(time_buf, sizeof(time_buf), "%04u-%02u-%02u %02u:%02u:%02u",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.scanning = false;
				g_snapshot.progress = 1.f;
				g_snapshot.needs_admin_hint = needs_admin;
				strncpy_s(g_snapshot.last_scan_time, time_buf, _TRUNCATE);
				SnprintfI18n(g_snapshot.status_text, sizeof(g_snapshot.status_text),
					u8"完成：%zu 顆實體磁碟", g_snapshot.drives.size());
			}

			g_scanning.store(false, std::memory_order_release);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				HLOG_INFO("DiskHealthScan finished: {} drives", g_snapshot.drives.size());
			}
		}

		static void StartWorker()
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
				g_snapshot.scanning = true;
				g_snapshot.progress = 0.f;
				strncpy_s(g_snapshot.status_text, u8"掃描硬碟中…", _TRUNCATE);
			}
			g_scanning.store(true, std::memory_order_release);
			g_worker = std::thread([] { RunScan(); });
		}
	}

	const char* HealthLevelLabel(HealthLevel level)
	{
		switch (level) {
		case HealthLevel::Good: return I18N(u8"良好");
		case HealthLevel::Caution: return I18N(u8"注意");
		case HealthLevel::Bad: return I18N(u8"不良");
		case HealthLevel::Unavailable: return I18N(u8"無資料");
		default: return I18N(u8"未知");
		}
	}

	void Init()
	{
		if (g_init_done) {
			return;
		}
		g_init_done = true;
		g_shutdown.store(false, std::memory_order_release);
		g_live_enabled.store(false, std::memory_order_release);
		// 不在 init 同步啟動全碟 SMART 掃描，改由 UI 首幀 RequestRescan 延後觸發
		HLOG_INFO("DiskHealthScan Init ready (scan deferred until UI requests)");
	}

	void Shutdown()
	{
		HLOG_INFO("DiskHealthScan Shutdown");
		g_shutdown.store(true, std::memory_order_release);
		g_live_enabled.store(false, std::memory_order_release);
		g_cancel.store(true, std::memory_order_release);
		if (g_worker.joinable()) {
			g_worker.join();
		}
		if (g_live_worker.joinable()) {
			g_live_worker.join();
		}
		g_scanning.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot = {};
		}
		g_init_done = false;
	}

	void SetLiveRefreshEnabled(bool enabled)
	{
		g_live_enabled.store(enabled, std::memory_order_release);
		if (enabled) {
			if (g_init_done && !g_live_worker.joinable() && !g_shutdown.load(std::memory_order_acquire)) {
				StartLiveWorker();
			}
			HLOG_INFO("DiskHealthScan live refresh ENABLED (every {}s)", kLiveRefreshIntervalSec);
		}
		else {
			HLOG_INFO("DiskHealthScan live refresh DISABLED");
		}
	}

	bool IsLiveRefreshEnabled()
	{
		return g_live_enabled.load(std::memory_order_relaxed);
	}

	int GetLiveRefreshIntervalSec()
	{
		return kLiveRefreshIntervalSec;
	}

	void RequestRescan()
	{
		HLOG_INFO("DiskHealthScan RequestRescan (full scan, static info + SMART)");
		StartWorker();
	}

	void EnrichDriveSummary(DriveInfo& drive)
	{
		FinalizeSmartDerivedFields(drive);
	}

	void GetSectorCounters(const DriveInfo& drive, int& reallocated, int& pending,
		int& uncorrectable)
	{
		DriveInfo work = drive;
		FinalizeSmartDerivedFields(work);
		reallocated = work.reallocated_sectors;
		pending = work.pending_sectors;
		uncorrectable = work.uncorrectable_errors;
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		Snapshot snap = g_snapshot;
		for (auto& drive : snap.drives) {
			EnrichDriveSummary(drive);
		}
		return snap;
	}

	bool IsRunningAsAdmin()
	{
		return HCleanIsRunningAsAdmin();
	}

	bool PromptAdminElevationIfNeeded()
	{
		return HAdminPrompt::TryGate(HAdminPrompt::Scene::DiskHealth);
	}

	void BuildDriveReport(const DriveInfo& drive, char* out, size_t out_size)
	{
		if (out == nullptr || out_size == 0) {
			return;
		}
		char cap[48] = {};
		char temp[32] = {};
		char poh[32] = {};
		char re[32] = {};
		char pend[32] = {};
		char unc[32] = {};

		if (drive.size_bytes > 0) {
			const double gb = static_cast<double>(drive.size_bytes) / (1024.0 * 1024.0 * 1024.0);
			snprintf(cap, sizeof(cap), "%.2f GiB", gb);
		}
		else {
			strncpy_s(cap, u8"未知", _TRUNCATE);
		}
		if (drive.temperature_c >= 0) {
			snprintf(temp, sizeof(temp), "%d °C", drive.temperature_c);
		}
		else {
			strncpy_s(temp, "—", _TRUNCATE);
		}
		if (drive.power_on_hours >= 0) {
			snprintf(poh, sizeof(poh), I18N(u8"%d 小時"), drive.power_on_hours);
		}
		else {
			strncpy_s(poh, "—", _TRUNCATE);
		}
		snprintf(re, sizeof(re), "%d", drive.reallocated_sectors >= 0 ? drive.reallocated_sectors : -1);
		if (drive.reallocated_sectors < 0) {
			strncpy_s(re, "—", _TRUNCATE);
		}
		snprintf(pend, sizeof(pend), "%d", drive.pending_sectors >= 0 ? drive.pending_sectors : -1);
		if (drive.pending_sectors < 0) {
			strncpy_s(pend, "—", _TRUNCATE);
		}
		snprintf(unc, sizeof(unc), "%d", drive.uncorrectable_errors >= 0 ? drive.uncorrectable_errors : -1);
		if (drive.uncorrectable_errors < 0) {
			strncpy_s(unc, "—", _TRUNCATE);
		}

		if (!drive.smart_available) {
			const bool is_usb = (strcmp(drive.bus_type_utf8, "USB") == 0);
			snprintf(out, out_size,
				I18N(u8"【儲存裝置報告】\n"
					u8"實體編號：PhysicalDrive%d\n"
					u8"型號：%s\n"
					u8"序號：%s\n"
					u8"介面：%s\n"
					u8"容量：%s\n"
					u8"掛載槽：%s\n"
					u8"資料監測：未提供 SMART%s\n"
					u8"說明：%s\n"
					u8"備註：%s\n"),
				drive.physical_index,
				drive.model_utf8[0] ? drive.model_utf8 : "—",
				drive.serial_utf8[0] ? drive.serial_utf8 : "—",
				drive.bus_type_utf8[0] ? drive.bus_type_utf8 : "—",
				cap,
				drive.volume_letters[0] ? drive.volume_letters : u8"無",
				is_usb ? u8"（USB 儲存裝置）" : "",
				is_usb
					? u8"此類裝置通常無法透過 Windows 讀取健康屬性，仍可使用下方速度／壞軌抽樣檢測。"
					: u8"此碟未回傳 SMART 資料，建議以廠商工具或更換連接方式複檢。",
				drive.status_note[0] ? drive.status_note : "—");
			return;
		}

		snprintf(out, out_size,
			I18N(u8"【硬碟健康報告】\n"
				u8"實體編號：PhysicalDrive%d\n"
				u8"型號：%s\n"
				u8"序號：%s\n"
				u8"韌體：%s\n"
				u8"介面：%s\n"
				u8"容量：%s\n"
				u8"掛載槽：%s\n"
				u8"健康評估：%s\n"
				u8"溫度：%s\n"
				u8"通電時數：%s\n"
				u8"重配置扇區：%s\n"
				u8"待處理扇區：%s\n"
				u8"不可修正錯誤：%s\n"
				u8"SMART：可讀取\n"
				u8"SMART 屬性數：%zu\n"
				u8"備註：%s\n"),
			drive.physical_index,
			drive.model_utf8[0] ? drive.model_utf8 : "—",
			drive.serial_utf8[0] ? drive.serial_utf8 : "—",
			drive.firmware_utf8[0] ? drive.firmware_utf8 : "—",
			drive.bus_type_utf8[0] ? drive.bus_type_utf8 : "—",
			cap,
			drive.volume_letters[0] ? drive.volume_letters : u8"無",
			HealthLevelLabel(drive.health),
			temp, poh, re, pend, unc,
			drive.smart_attributes.size(),
			drive.status_note[0] ? drive.status_note : "—");
	}
}