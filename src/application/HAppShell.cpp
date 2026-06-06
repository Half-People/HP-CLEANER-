#include "HAppShell.h"
#include "HElevationBroker.h"
#include "HCleanTask.h"
#include "HCrashHandler.h"
#include "HAppSettings.h"
#include "HPage.h"
#include "Hi18n.h"

namespace {
	HWND g_main_hwnd = nullptr;
}

void HAppShellSetMainWindow(HWND hwnd)
{
	g_main_hwnd = hwnd;
}

HWND HAppShellGetMainWindow()
{
	return g_main_hwnd;
}

void HAppShellShowMainWindow()
{
	if (g_main_hwnd != nullptr) {
		::ShowWindow(g_main_hwnd, SW_SHOW);
		::SetForegroundWindow(g_main_hwnd);
	}
}

void HAppShellHideMainWindow()
{
	if (g_main_hwnd != nullptr) {
		::ShowWindow(g_main_hwnd, SW_HIDE);
	}
}

void HAppShellRequestApplicationQuit()
{
	HElevationBroker::Shutdown();
	HCrashMarkGracefulApplicationExit();
	::PostQuitMessage(0);
}

void HAppShellUpdateWindowTitle()
{
	if (g_main_hwnd == nullptr) {
		return;
	}

	const bool admin = HCleanIsRunningAsAdmin();
	const bool broker = HElevationBroker::IsConnected();
	const bool watchdog = HCrashIsWatchdogAlive();

	wchar_t title[320] = L"HP CLEANER++";
	if (admin) {
		wcscat_s(title, W18N(u8" · 管理員"));
	}
	else if (broker) {
		wcscat_s(title, W18N(u8" · 管理員代理"));
	}
	else {
		wcscat_s(title, W18N(u8" · 標準使用者"));
	}
	if (watchdog) {
		wcscat_s(title, W18N(u8" · 看門狗運行中"));
	}
	else {
		wcscat_s(title, W18N(u8" · 看門狗未運行"));
	}

	::SetWindowTextW(g_main_hwnd, title);
}

bool HAppShellRequestAdminElevation(bool exit_current_on_success)
{
	if (HCleanIsRunningAsAdmin() || HElevationBroker::IsConnected()) {
		HLOG_INFO("HAppShell: elevated access already available");
		return true;
	}

	const bool ok = HElevationBroker::RequestElevation();
	if (ok) {
		HLOG_INFO("HAppShell: background elevation broker connected");
		(void)HAppSettingsPromoteStartupToElevatedIfEnabled();
		HAppShellUpdateWindowTitle();
		return true;
	}

	HLOG_WARN("HAppShell: elevation broker failed");
	(void)exit_current_on_success;
	return false;
}