#include "AboutDeviceInfo.h"
#include "HCleanTask.h"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include "Hi18n.h"

namespace {
	AboutDeviceInfoSnapshot g_info;
	bool g_collected = false;

	using RtlGetVersionFn = LONG(WINAPI*)(void* lpVersionInformation);

	void CopyWideToUtf8(const wchar_t* wide, char* out, size_t out_size)
	{
		if (out == nullptr || out_size == 0) {
			return;
		}
		out[0] = '\0';
		if (wide == nullptr || wide[0] == L'\0') {
			return;
		}
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
		out[out_size - 1] = '\0';
	}

	void QueryOsVersion()
	{
		struct OsVersionInfo {
			ULONG dwOSVersionInfoSize;
			ULONG dwMajorVersion;
			ULONG dwMinorVersion;
			ULONG dwBuildNumber;
			ULONG dwPlatformId;
			wchar_t szCSDVersion[128];
		} ver{};
		ver.dwOSVersionInfoSize = sizeof(ver);

		const auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
			GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
		if (rtl_get_version != nullptr && rtl_get_version(&ver) == 0) {
			snprintf(g_info.os_version, sizeof(g_info.os_version),
				"%lu.%lu (Build %lu)", ver.dwMajorVersion, ver.dwMinorVersion, ver.dwBuildNumber);

			if (ver.dwMajorVersion == 10 && ver.dwBuildNumber >= 22000) {
				strncpy_s(g_info.os_product, "Windows 11", _TRUNCATE);
			}
			else if (ver.dwMajorVersion == 10) {
				strncpy_s(g_info.os_product, "Windows 10", _TRUNCATE);
			}
			else {
				snprintf(g_info.os_product, sizeof(g_info.os_product),
					"Windows %lu.%lu", ver.dwMajorVersion, ver.dwMinorVersion);
			}
		}
		else {
			strncpy_s(g_info.os_product, "Windows", _TRUNCATE);
			strncpy_s(g_info.os_version, "—", _TRUNCATE);
		}

		wchar_t display_ver[96] = {};
		DWORD disp_size = static_cast<DWORD>(sizeof(display_ver));
		if (RegGetValueW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
			L"DisplayVersion", RRF_RT_REG_SZ, nullptr, display_ver, &disp_size) == ERROR_SUCCESS
			&& display_ver[0] != L'\0') {
			CopyWideToUtf8(display_ver, g_info.os_display_version, sizeof(g_info.os_display_version));
		}
		else {
			wchar_t release_id[32] = {};
			DWORD rel_size = sizeof(release_id);
			if (RegGetValueW(HKEY_LOCAL_MACHINE,
				L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
				L"ReleaseId", RRF_RT_REG_SZ, nullptr, release_id, &rel_size) == ERROR_SUCCESS) {
				CopyWideToUtf8(release_id, g_info.os_display_version, sizeof(g_info.os_display_version));
			}
		}
	}

	void QueryCpu()
	{
		wchar_t name[256] = {};
		DWORD name_size = sizeof(name);
		if (RegGetValueW(HKEY_LOCAL_MACHINE,
			L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
			L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &name_size) == ERROR_SUCCESS) {
			CopyWideToUtf8(name, g_info.cpu_name, sizeof(g_info.cpu_name));
		}
		else {
			strncpy_s(g_info.cpu_name, "—", _TRUNCATE);
		}

		SYSTEM_INFO si = {};
		GetNativeSystemInfo(&si);
		snprintf(g_info.cpu_topology, sizeof(g_info.cpu_topology),
			I18N(u8"%lu 邏輯處理器"), static_cast<unsigned long>(si.dwNumberOfProcessors));
	}

	void QueryRam()
	{
		MEMORYSTATUSEX ms = {};
		ms.dwLength = sizeof(ms);
		if (!GlobalMemoryStatusEx(&ms)) {
			strncpy_s(g_info.ram_summary, "—", _TRUNCATE);
			return;
		}
		char total[32];
		char avail[32];
		FormatCleanSize(static_cast<int64_t>(ms.ullTotalPhys), total, sizeof(total));
		FormatCleanSize(static_cast<int64_t>(ms.ullAvailPhys), avail, sizeof(avail));
		snprintf(g_info.ram_summary, sizeof(g_info.ram_summary),
			I18N(u8"共 %s · 可用 %s (%.0f%% 使用中)"),
			total, avail, static_cast<double>(ms.dwMemoryLoad));
	}

	void QueryGpu()
	{
		DISPLAY_DEVICEW dd = {};
		dd.cb = sizeof(dd);
		for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
			if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) {
				continue;
			}
			if (dd.DeviceString[0] != L'\0') {
				CopyWideToUtf8(dd.DeviceString, g_info.gpu_name, sizeof(g_info.gpu_name));
				if (g_info.gpu_name[0] != '\0') {
					return;
				}
			}
		}
		strncpy_s(g_info.gpu_name, "—", _TRUNCATE);
	}

	void QueryMisc()
	{
		wchar_t comp[MAX_COMPUTERNAME_LENGTH + 1] = {};
		DWORD comp_len = MAX_COMPUTERNAME_LENGTH + 1;
		if (GetComputerNameW(comp, &comp_len)) {
			CopyWideToUtf8(comp, g_info.computer_name, sizeof(g_info.computer_name));
		}

		wchar_t user[256] = {};
		DWORD user_len = 256;
		if (GetUserNameW(user, &user_len)) {
			CopyWideToUtf8(user, g_info.user_name, sizeof(g_info.user_name));
		}

		SYSTEM_INFO si = {};
		GetNativeSystemInfo(&si);
		switch (si.wProcessorArchitecture) {
		case PROCESSOR_ARCHITECTURE_AMD64:
			strncpy_s(g_info.system_arch, I18N(u8"x64 (64 位元)"), _TRUNCATE);
			break;
		case PROCESSOR_ARCHITECTURE_ARM64:
			strncpy_s(g_info.system_arch, "ARM64", _TRUNCATE);
			break;
		default:
			snprintf(g_info.system_arch, sizeof(g_info.system_arch), "Arch %u", si.wProcessorArchitecture);
			break;
		}

		strncpy_s(g_info.admin_status,
			HCleanIsRunningAsAdmin() ? I18N(u8"系統管理員（已提升）") : I18N(u8"標準使用者"),
			_TRUNCATE);

		wchar_t sys_dir[MAX_PATH] = {};
		if (GetWindowsDirectoryW(sys_dir, MAX_PATH) > 0 && sys_dir[1] == L':') {
			snprintf(g_info.system_drive, sizeof(g_info.system_drive), "%c:",
				static_cast<char>(sys_dir[0]));
		}
		else {
			strncpy_s(g_info.system_drive, "C:", _TRUNCATE);
		}

		wchar_t machine_guid[128] = {};
		DWORD guid_size = sizeof(machine_guid);
		if (RegGetValueW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Cryptography",
			L"MachineGuid", RRF_RT_REG_SZ, nullptr, machine_guid, &guid_size) == ERROR_SUCCESS) {
			CopyWideToUtf8(machine_guid, g_info.machine_id, sizeof(g_info.machine_id));
		}
	}
}

namespace AboutDeviceInfo {

void Refresh()
{
	g_info = {};
	QueryOsVersion();
	QueryCpu();
	QueryRam();
	QueryGpu();
	QueryMisc();
	g_collected = true;
}

const AboutDeviceInfoSnapshot& Get()
{
	if (!g_collected) {
		Refresh();
	}
	return g_info;
}

}