#include "HAppSingleInstance.h"
#include "HAppShell.h"
#include <cstring>

namespace {
	constexpr wchar_t kMutexName[] = L"Global\\HP_CLEANER_PLUSPLUS_SingleInstance_v1";
	constexpr wchar_t kWindowClassName[] = L"HP_CLEANER_WindowClass";
	HANDLE g_instance_mutex = nullptr;
}

bool HAppSingleInstanceAcquire()
{
	g_instance_mutex = ::CreateMutexW(nullptr, TRUE, kMutexName);
	if (g_instance_mutex == nullptr) {
		return true;
	}
	if (::GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND existing = ::FindWindowW(kWindowClassName, nullptr);
		if (existing != nullptr) {
			::PostMessageW(existing, WM_HP_SHOW_WINDOW, 0, 0);
		}
		::CloseHandle(g_instance_mutex);
		g_instance_mutex = nullptr;
		return false;
	}
	return true;
}

void HAppSingleInstanceRelease()
{
	if (g_instance_mutex != nullptr) {
		::ReleaseMutex(g_instance_mutex);
		::CloseHandle(g_instance_mutex);
		g_instance_mutex = nullptr;
	}
}
