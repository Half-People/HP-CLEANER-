#include "HAppTray.h"
#include "HAppShell.h"
#include "HPage.h"
#include "HAppPaths.h"
#include "OptimizeScan.h"
#include "DiskHealthScan.h"
#include "MainPageMemory.h"
#include "MainPageDiskScan.h"
#include "HElevationBroker.h"
#include "HAppSettings.h"
#include "Hi18n.h"
#include <shellapi.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
	constexpr UINT kTrayCallbackMsg = WM_APP + 41;
	constexpr UINT kTrayIconId = 1;
	constexpr UINT kMenuShow = 1001;
	constexpr UINT kMenuQuit = 1002;
	constexpr UINT kMenuClean = 1010;
	constexpr UINT kMenuOptimize = 1011;
	constexpr UINT kMenuDiskHealth = 1012;
	constexpr UINT kMenuElevate = 1013;
	constexpr UINT kMenuFlushDns = 1014;
	constexpr UINT kMenuToggleConsole = 1015;
	constexpr UINT kMenuReleaseMemory = 1016;
	constexpr UINT kMenuDiskRescan = 1017;
	constexpr UINT kMenuOpenLogs = 1018;
	constexpr UINT kMenuRunAtStartup = 1019;

	NOTIFYICONDATAW g_nid = {};
	bool g_tray_added = false;
	HMENU g_tray_menu = nullptr;

	void ShowTrayBalloon(const wchar_t* text)
	{
		if (!g_tray_added) {
			return;
		}
		NOTIFYICONDATAW nid = g_nid;
		nid.uFlags = NIF_INFO;
		wcsncpy_s(nid.szInfoTitle, L"HP CLEANER++", _TRUNCATE);
		if (text != nullptr) {
			wcsncpy_s(nid.szInfo, text, _TRUNCATE);
		}
		nid.dwInfoFlags = NIIF_INFO;
		::Shell_NotifyIconW(NIM_MODIFY, &nid);
	}

	void RebuildTrayMenu()
	{
		if (g_tray_menu != nullptr) {
			::DestroyMenu(g_tray_menu);
			g_tray_menu = nullptr;
		}
		g_tray_menu = ::CreatePopupMenu();
		if (g_tray_menu == nullptr) {
			return;
		}
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuShow, W18N(u8"顯示主視窗"));
		::AppendMenuW(g_tray_menu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuClean, W18N(u8"開啟系統清理"));
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuOptimize, W18N(u8"優化掃描"));
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuDiskHealth, W18N(u8"硬碟健康檢查"));
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuFlushDns, W18N(u8"刷新 DNS"));
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuReleaseMemory, W18N(u8"釋放記憶體"));
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuDiskRescan, W18N(u8"重新掃描磁碟儲存"));
		if (!HCleanHasElevatedAccess()) {
			::AppendMenuW(g_tray_menu, MF_STRING, kMenuElevate, W18N(u8"UAC 提權（背景）"));
		}
		::AppendMenuW(g_tray_menu, MF_SEPARATOR, 0, nullptr);
		const UINT console_flags = MF_STRING | (HLoggingIsConsoleEnabled() ? MF_CHECKED : 0);
		::AppendMenuW(g_tray_menu, console_flags, kMenuToggleConsole, W18N(u8"顯示日志控制台"));
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuOpenLogs, W18N(u8"開啟日誌資料夾"));
		const UINT startup_flags = MF_STRING | (HAppSettingsGetRunAtStartup() ? MF_CHECKED : 0);
		::AppendMenuW(g_tray_menu, startup_flags, kMenuRunAtStartup, W18N(u8"開機自動啟動"));
		::AppendMenuW(g_tray_menu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(g_tray_menu, MF_STRING, kMenuQuit, W18N(u8"退出"));
	}
}

void HAppTrayRebuildMenu()
{
	RebuildTrayMenu();
}

bool HAppTrayInit(HWND hwnd, HICON icon)
{
	if (hwnd == nullptr) {
		return false;
	}

	ZeroMemory(&g_nid, sizeof(g_nid));
	g_nid.cbSize = sizeof(NOTIFYICONDATAW);
	g_nid.hWnd = hwnd;
	g_nid.uID = kTrayIconId;
	g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	g_nid.uCallbackMessage = kTrayCallbackMsg;
	g_nid.hIcon = icon != nullptr ? icon : ::LoadIconW(nullptr, IDI_APPLICATION);
	wcsncpy_s(g_nid.szTip, W18N(u8"HP CLEANER++ — 右鍵快速操作"), _TRUNCATE);

	RebuildTrayMenu();

	g_tray_added = ::Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
	return g_tray_added;
}

void HAppTrayShutdown()
{
	if (g_tray_added) {
		::Shell_NotifyIconW(NIM_DELETE, &g_nid);
		g_tray_added = false;
	}
	if (g_tray_menu != nullptr) {
		::DestroyMenu(g_tray_menu);
		g_tray_menu = nullptr;
	}
}

void HAppTrayShowMainWindow(HWND hwnd)
{
	if (hwnd == nullptr) {
		return;
	}
	MainPageDiskScan::NotifyMainWindowVisible();
	::ShowWindow(hwnd, SW_SHOW);
	if (::IsIconic(hwnd)) {
		::ShowWindow(hwnd, SW_RESTORE);
	}
	::SetForegroundWindow(hwnd);
}

void HAppTrayHideMainWindow(HWND hwnd)
{
	if (hwnd == nullptr) {
		return;
	}
	::ShowWindow(hwnd, SW_HIDE);
	ShowTrayBalloon(W18N(u8"已縮至系統匣。左鍵顯示視窗，右鍵可快速操作。"));
}

void HAppTrayRequestQuit(HWND hwnd)
{
	(void)hwnd;
	HAppTrayShutdown();
	HAppShellRequestApplicationQuit();
}

bool HAppTrayExecuteCommand(UINT command_id)
{
	HWND hwnd = HAppShellGetMainWindow();

	switch (command_id) {
	case kMenuShow:
		HAppTrayShowMainWindow(hwnd);
		return true;
	case kMenuClean:
		open_page("ClearPage");
		HAppTrayShowMainWindow(hwnd);
		ShowTrayBalloon(W18N(u8"已開啟系統清理"));
		return true;
	case kMenuOptimize:
		open_page("OptimizePage");
		OptimizeScan::RequestScan();
		HAppTrayShowMainWindow(hwnd);
		ShowTrayBalloon(W18N(u8"已開始優化掃描"));
		return true;
	case kMenuDiskHealth:
		open_page("DiskHealthPage");
		DiskHealthScan::RequestRescan();
		HAppTrayShowMainWindow(hwnd);
		ShowTrayBalloon(W18N(u8"已開始硬碟健康檢查"));
		return true;
	case kMenuFlushDns:
		if (OptimizeScan::FlushDnsCache()) {
			ShowTrayBalloon(W18N(u8"DNS 快取已刷新"));
		}
		else {
			ShowTrayBalloon(W18N(u8"DNS 刷新失敗"));
		}
		return true;
	case kMenuElevate:
		if (HAppShellRequestAdminElevation(false)) {
			HAppShellUpdateWindowTitle();
			RebuildTrayMenu();
			if (HAppSettingsGetRunAtStartupElevated()) {
				ShowTrayBalloon(W18N(u8"管理員代理已連線；開機自啟已改為管理員模式"));
			}
			else {
				ShowTrayBalloon(W18N(u8"管理員代理已連線"));
			}
		}
		else {
			ShowTrayBalloon(W18N(u8"UAC 提權失敗或已取消"));
		}
		return true;
	case kMenuToggleConsole: {
		const bool enable = !HLoggingIsConsoleEnabled();
		HLoggingToggleConsole(enable);
		RebuildTrayMenu();
		ShowTrayBalloon(enable
			? W18N(u8"日誌控制台已開啟（設定已保存）")
			: W18N(u8"日誌控制台已關閉（設定已保存）"));
		return true;
	}
	case kMenuRunAtStartup: {
		const bool enable = !HAppSettingsGetRunAtStartup();
		if (HAppSettingsSetRunAtStartup(enable)) {
			RebuildTrayMenu();
			if (enable && HAppSettingsGetRunAtStartupElevated()) {
				ShowTrayBalloon(W18N(u8"已啟用開機自動啟動（管理員權限）"));
			}
			else {
				ShowTrayBalloon(enable
					? W18N(u8"已啟用開機自動啟動")
					: W18N(u8"已關閉開機自動啟動"));
			}
		}
		else {
			ShowTrayBalloon(W18N(u8"開機自動啟動設定失敗"));
		}
		return true;
	}
	case kMenuReleaseMemory:
		MainPageMemory::RequestRelease();
		ShowTrayBalloon(W18N(u8"已開始釋放記憶體"));
		return true;
	case kMenuDiskRescan:
		MainPageDiskScan::RequestRescan();
		ShowTrayBalloon(W18N(u8"已開始重新掃描磁碟儲存"));
		return true;
	case kMenuOpenLogs: {
		const std::string logs_dir = HAppPaths::GetLogsDir();
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, logs_dir.c_str(), -1, nullptr, 0);
		if (wlen > 0) {
			std::vector<wchar_t> wide(static_cast<size_t>(wlen));
			MultiByteToWideChar(CP_UTF8, 0, logs_dir.c_str(), -1, wide.data(), wlen);
			::ShellExecuteW(nullptr, L"explore", wide.data(), nullptr, nullptr, SW_SHOWNORMAL);
			ShowTrayBalloon(W18N(u8"已開啟日誌資料夾"));
		}
		return true;
	}
	case kMenuQuit:
		HAppTrayRequestQuit(hwnd);
		return true;
	default:
		return false;
	}
}

bool HAppTrayHandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg != kTrayCallbackMsg) {
		return false;
	}
	switch (LOWORD(lParam)) {
	case WM_LBUTTONUP:
		HAppTrayShowMainWindow(hwnd);
		return true;
	case WM_RBUTTONUP: {
		POINT pt;
		::GetCursorPos(&pt);
		::SetForegroundWindow(hwnd);
		if (g_tray_menu != nullptr) {
			const UINT cmd = ::TrackPopupMenu(g_tray_menu,
				TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
				pt.x, pt.y, 0, hwnd, nullptr);
			if (cmd != 0) {
				HAppTrayExecuteCommand(cmd);
			}
		}
		::PostMessageW(hwnd, WM_NULL, 0, 0);
		return true;
	}
	default:
		return false;
	}
}
