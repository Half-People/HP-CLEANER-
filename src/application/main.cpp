#include "HPageStyle.h"
#include "HPage.h"
#include "HCleanTaskCommon.h"
#include "HAppLaunch.h"
#include "HCrashHandler.h"
#include "HCrashReportUI.h"
#include "HAppPaths.h"
#include "HAppShell.h"
#include "HAppTray.h"
#include "HAppSingleInstance.h" // WM_HP_SHOW_WINDOW
#include "HAppSettings.h"
#include "MainPageDiskScan.h"
#include "Hi18n.h"
#include "HAppRegistration.h"
#include "HElevationBroker.h"
#include "HUninstallUI.h"
#include "HLogCmdConsole.h"
#include "resource.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <implot.h>
#include <d3d9.h>
#include <tchar.h>
const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


HRC::HTexture HRC::LoadTexture(LPCWSTR resourceName, LPCWSTR resourceType)
{
	return HRC::LoadTextureFromDevice(g_pd3dDevice, resourceName, resourceType);
}


int RunMainApplication(bool start_to_tray)
{
    if (!HAppSingleInstanceAcquire()) {
        HLOG_INFO("HP CLEANER++ second instance ignored; focusing existing window");
        return 0;
    }

    HLOG_INFO("HP CLEANER++ starting");
    HAppRegistration::EnsureRegisteredOnStartup();
    const wchar_t* kWindowClassName = L"HP_CLEANER_WindowClass";
    const wchar_t* kWindowTitle = L"HP CLEANER++";
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    HINSTANCE hinst = GetModuleHandle(nullptr);
    HICON app_icon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hinst, app_icon, nullptr, nullptr, nullptr, kWindowClassName, app_icon };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, kWindowTitle, WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);
    HAppShellSetMainWindow(hwnd);
    HAppShellUpdateWindowTitle();
    if (app_icon != nullptr) {
        ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
        ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon));
    }
    HAppTrayInit(hwnd, app_icon);


    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        HLOG_ERROR("Failed to create Direct3D 9 device");
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    HLOG_INFO("Direct3D 9 device created");
    HRC::SetRenderDevice(g_pd3dDevice);

    if (start_to_tray) {
        HAppTrayHideMainWindow(hwnd);
    }
    else {
        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hwnd);
    }


    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    HPageStyleLoadFontsOnce();
    ImGuiStyle& style = HPageStyle();


    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    MainPage::BeginMainPage();
    if (start_to_tray) {
        MainPageDiskScan::SetDeferUntilMainWindowVisible(true);
    }

	bool done = false;
    static DWORD s_last_crash_filter_reinstall = 0;
    while (!done){
		MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)){
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) {
            HCrashMarkGracefulApplicationExit();
            break;
        }

        if (!::IsWindowVisible(hwnd)) {
            ::Sleep(100);
            continue;
        }

        const DWORD now_tick = GetTickCount();
        if (now_tick - s_last_crash_filter_reinstall > 2000) {
            HCrashHandlerReinstallFilter();
            s_last_crash_filter_reinstall = now_tick;
        }

        // Handle lost D3D9 device
        if (g_DeviceLost)
        {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST){
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET)
                ResetDevice();
            g_DeviceLost = false;
        }

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }


        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();


		MainPage::RenderMainPage();
        HAppShellUpdateWindowTitle();

        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST)
            g_DeviceLost = true;
    }

	HCrashMarkGracefulApplicationExit();
	HLOG_DEBUG("Shutdown: stop async scan worker");
	HCleanShutdownAsyncScanWorker();
	HLOG_DEBUG("Shutdown: EndMainPage (textures/pages)");
	MainPage::EndMainPage();
    HLOG_INFO("HP CLEANER++ shutting down");

    HLOG_DEBUG("Shutdown: invalidate DX9 device objects");
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HLOG_DEBUG("Shutdown: ImGui_ImplDX9_Shutdown");
    ImGui_ImplDX9_Shutdown();
    HLOG_DEBUG("Shutdown: ImGui_ImplWin32_Shutdown");
    ImGui_ImplWin32_Shutdown();
    HLOG_DEBUG("Shutdown: ImPlot::DestroyContext");
    ImPlot::DestroyContext();
    HLOG_DEBUG("Shutdown: ImGui::DestroyContext");
    ImGui::DestroyContext();

	HLOG_DEBUG("Shutdown: log cmd");
	HLogCmdConsoleShutdown();
	HLOG_DEBUG("Shutdown: spdlog");
	HShutdownLogging();

    HAppTrayShutdown();
    HElevationBroker::Shutdown();
    HAppSingleInstanceRelease();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    HCrashMarkGracefulApplicationExit();
    return 0;
}

int main(int, char**)
{
    HAppLaunchOptions launch = HAppParseCommandLine();

    // GUI 子系統進程不會因 CREATE_NEW_CONSOLE 自動附加控制台，需優先進入此模式。
    if (launch.mode == HAppRunMode::LogConsole) {
        return RunLogConsoleApplication(launch.log_console_path, launch.watchdog_parent_pid, launch.log_read_handle);
    }

    HCrashHandlerInstall();

    if (launch.mode == HAppRunMode::Help) {
        Hi18n::Init();
        ::MessageBoxW(nullptr,
            W18N(u8"HP CLEANER++ 命令列模式\n\n"
                u8"(無參數) / --mode=app          正常 GUI 主程式\n"
                u8"--mode=crash-report --report=<json>   顯示崩潰報告視窗\n"
                u8"--mode=uninstall                開啟卸載視窗\n"
                u8"--mode=logconsole --log=<path>  彩色日誌控制台（內部使用）\n"
                u8"--mode=test-crash               觸發測試崩潰\n"
                u8"--tray                          啟動後縮至系統匣\n"
                u8"--help                          顯示此說明"),
            W18N(u8"HP CLEANER++"), MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (launch.mode == HAppRunMode::TestCrash) {
        HInitLogging();
        HCrashWatchdogSpawn();
        HLOG_ERROR("Test crash requested via --mode=test-crash");
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 1;
    }

    if (launch.mode == HAppRunMode::Watchdog) {
        return HCrashWatchdogRun(launch.watchdog_parent_pid);
    }

    if (launch.mode == HAppRunMode::ElevBroker) {
        return HElevationBroker::RunBrokerMain(
            launch.elev_broker_parent_pid,
            launch.elev_broker_pipe.empty() ? nullptr : launch.elev_broker_pipe.c_str(),
            launch.elev_broker_ready_event.empty() ? nullptr : launch.elev_broker_ready_event.c_str());
    }

    if (launch.mode == HAppRunMode::Uninstall) {
        return RunUninstallApplication();
    }

    if (launch.mode == HAppRunMode::CrashReport) {
        if (launch.crash_report_path.empty()) {
            launch.crash_report_path = HAppPaths::ReadPendingCrashReport();
        }
        const int rc = RunCrashReportApplication(launch.crash_report_path);
        if (rc == 0) {
            HAppPaths::ClearPendingCrashReport();
        }
        return rc;
    }

    const std::string pending = HAppPaths::ReadPendingCrashReport();
    if (!pending.empty() && HCrashShouldShowPendingReportOnStartup()) {
        const int rc = RunCrashReportApplication(pending);
        if (rc == 0) {
            HAppPaths::ClearPendingCrashReport();
        }
        return rc;
    }

    HInitLogging();
    HAppSettingsLoad();
    Hi18n::Init();
    if (HAppSettingsGetConsoleLogger()) {
        HLoggingToggleConsole(true);
    }
    HCrashWatchdogSpawn();
    return RunMainApplication(launch.start_to_tray);
}



bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
    //g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}


void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}


// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (HAppTrayHandleMessage(hWnd, msg, wParam, lParam))
        return 0;

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_HP_SHOW_WINDOW:
        HAppTrayShowMainWindow(hWnd);
        return 0;
    case WM_HP_CONSOLE_CLOSED:
        HLoggingOnConsoleClosedByUser();
        HAppTrayRebuildMenu();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_CLOSE:
        HAppTrayHideMainWindow(hWnd);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        HAppTrayShutdown();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}