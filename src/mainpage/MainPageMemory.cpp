#include "MainPageMemory.h"
#include "HCleanTask.h"
#include "HPage.h"
#include "Hi18n.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdio>

#pragma comment(lib, "psapi.lib")

namespace {
	std::mutex g_mutex;
	MainPageMemoryStatus g_status;
	std::atomic<bool> g_running{ false };

	using NtSetSystemInformationFn = LONG(WINAPI*)(ULONG, void*, ULONG);

	enum : ULONG {
		SystemMemoryListInformation = 80,
	};
	enum SYSTEM_MEMORY_LIST_COMMAND : ULONG {
		MemoryEmptyWorkingSets = 2,
		MemoryPurgeStandbyList = 4,
	};

	int64_t QueryAvailPhysBytes()
	{
		MEMORYSTATUSEX ms = {};
		ms.dwLength = sizeof(ms);
		if (!GlobalMemoryStatusEx(&ms)) {
			return 0;
		}
		return static_cast<int64_t>(ms.ullAvailPhys);
	}

	bool EnableDebugPrivilege()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
			return false;
		}

		LUID luid = {};
		if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
			CloseHandle(token);
			return false;
		}

		TOKEN_PRIVILEGES tp = {};
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
		const DWORD err = GetLastError();
		CloseHandle(token);
		return err == ERROR_SUCCESS;
	}

	void TrimProcess(DWORD pid, int* trimmed, int* failed)
	{
		if (pid == 0 || pid == 4) {
			return;
		}

		HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pid);
		if (process == nullptr) {
			++(*failed);
			return;
		}

		BOOL ok = EmptyWorkingSet(process);
		if (!ok) {
			ok = SetProcessWorkingSetSize(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
		}
		if (ok) {
			++(*trimmed);
		}
		else {
			++(*failed);
		}
		CloseHandle(process);
	}

	bool PurgeSystemMemoryCaches()
	{
		const auto nt_set = reinterpret_cast<NtSetSystemInformationFn>(
			GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation"));
		if (nt_set == nullptr) {
			return false;
		}

		bool any_ok = false;
		ULONG cmd = MemoryEmptyWorkingSets;
		if (nt_set(SystemMemoryListInformation, &cmd, sizeof(cmd)) == 0) {
			any_ok = true;
		}
		cmd = MemoryPurgeStandbyList;
		if (nt_set(SystemMemoryListInformation, &cmd, sizeof(cmd)) == 0) {
			any_ok = true;
		}
		return any_ok;
	}

	void FormatResultMessage(MainPageMemoryStatus& st)
	{
		const int64_t delta = st.avail_after - st.avail_before;
		if (delta > 512 * 1024) {
			char freed[32];
			FormatCleanSize(delta, freed, sizeof(freed));
			snprintf(st.message, sizeof(st.message),
				I18N(u8"可用記憶體增加約 %s"), freed);
		}
		else if (st.system_purge_ok) {
			snprintf(st.message, sizeof(st.message),
				I18N(u8"已整理 %d 個程序並清理系統快取"), st.processes_trimmed);
		}
		else {
			snprintf(st.message, sizeof(st.message),
				I18N(u8"已整理 %d 個程序工作集"), st.processes_trimmed);
		}
	}

	void ReleaseWorker()
	{
		MainPageMemoryStatus result = {};
		result.running = true;
		result.avail_before = QueryAvailPhysBytes();

		EnableDebugPrivilege();

		const DWORD self_pid = GetCurrentProcessId();
		TrimProcess(self_pid, &result.processes_trimmed, &result.processes_failed);

		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap != INVALID_HANDLE_VALUE) {
			PROCESSENTRY32W pe = {};
			pe.dwSize = sizeof(pe);
			if (Process32FirstW(snap, &pe)) {
				do {
					if (pe.th32ProcessID != self_pid) {
						TrimProcess(pe.th32ProcessID, &result.processes_trimmed, &result.processes_failed);
					}
				} while (Process32NextW(snap, &pe));
			}
			CloseHandle(snap);
		}

		if (HCleanIsRunningAsAdmin()) {
			result.system_purge_ok = PurgeSystemMemoryCaches();
		}

		Sleep(250);
		result.avail_after = QueryAvailPhysBytes();
		result.freed_bytes = result.avail_after - result.avail_before;
		if (result.freed_bytes < 0) {
			result.freed_bytes = 0;
		}
		result.running = false;
		result.has_result = true;
		result.finished_tick = GetTickCount64();
		FormatResultMessage(result);

		HLOG_INFO("MainPageMemory: trimmed {} process(es), failed {}, avail {} -> {} bytes, admin_purge={}",
			result.processes_trimmed, result.processes_failed,
			result.avail_before, result.avail_after, result.system_purge_ok);

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_status = result;
		}
		g_running.store(false, std::memory_order_release);
	}
}

namespace MainPageMemory {

void RequestRelease()
{
	bool expected = false;
	if (!g_running.compare_exchange_strong(expected, true)) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_status = {};
		g_status.running = true;
	}

	std::thread(ReleaseWorker).detach();
}

bool IsRunning()
{
	return g_running.load(std::memory_order_acquire);
}

MainPageMemoryStatus GetStatus()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_status;
}

}
