#include "HCrashReportUI.h"
#include "HCrashHandler.h"
#include "HAppLaunch.h"
#include "HAppPaths.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HPageStyle.h"
#include "HRC_Assets.h"
#include "resource.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
	std::string I18NStr(const char* text)
	{
		const char* translated = I18N(text);
		return (translated != nullptr) ? std::string(translated) : std::string();
	}

	const ImVec4 kCyanNeon = ImVec4(0.0f, 0.95f, 0.95f, 1.0f);
	const ImVec4 kCyanDark = ImVec4(0.0f, 0.45f, 0.45f, 1.0f);
	const ImVec4 kDanger = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
	const ImVec4 kWarn = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);

	struct CrashReportData {
		std::string report_file;
		std::string timestamp;
		std::string exception_code;
		std::string exception_description;
		std::string exception_address;
		std::string module;
		std::string module_offset;
		std::string dump_file;
		std::string log_file;
		std::string handler_log;
		std::string report_source;
		std::string os_version;
		std::string stack_trace;
		std::string process_id;
		std::string thread_id;
		std::string raw_json;
	};

	LPDIRECT3D9 g_pD3D = nullptr;
	LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
	bool g_DeviceLost = false;
	UINT g_ResizeWidth = 0;
	UINT g_ResizeHeight = 0;
	D3DPRESENT_PARAMETERS g_d3dpp = {};
	HRC::HTexture g_logo_texture;

	bool CreateDeviceD3D(HWND hWnd)
	{
		if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr) {
			return false;
		}

		ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
		g_d3dpp.Windowed = TRUE;
		g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
		g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
		g_d3dpp.EnableAutoDepthStencil = TRUE;
		g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
		g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
		return g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING,
			&g_d3dpp, &g_pd3dDevice) >= 0;
	}

	void CleanupDeviceD3D()
	{
		HRC::FreeTexture(g_logo_texture);
		if (g_pd3dDevice) {
			g_pd3dDevice->Release();
			g_pd3dDevice = nullptr;
		}
		if (g_pD3D) {
			g_pD3D->Release();
			g_pD3D = nullptr;
		}
	}

	void ResetDevice()
	{
		ImGui_ImplDX9_InvalidateDeviceObjects();
		g_pd3dDevice->Reset(&g_d3dpp);
		ImGui_ImplDX9_CreateDeviceObjects();
	}

	bool LoadLogoTexture()
	{
		g_logo_texture = HRC::LoadTextureFromDevice(g_pd3dDevice, MAKEINTRESOURCE(IDB_PNG1), TEXT("PNG"));
		return g_logo_texture.texture != 0;
	}

	std::string ExtractJsonString(const std::string& json, const char* key)
	{
		const std::string needle = std::string("\"") + key + "\": \"";
		const size_t pos = json.find(needle);
		if (pos == std::string::npos) {
			return {};
		}
		size_t i = pos + needle.size();
		std::string out;
		while (i < json.size()) {
			const char c = json[i++];
			if (c == '\\' && i < json.size()) {
				const char e = json[i++];
				if (e == 'n') {
					out += '\n';
				}
				else if (e == 'r') {
					out += '\r';
				}
				else if (e == 't') {
					out += '\t';
				}
				else if (e == '"' || e == '\\') {
					out += e;
				}
				else {
					out += '\\';
					out += e;
				}
				continue;
			}
			if (c == '"') {
				break;
			}
			out += c;
		}
		return out;
	}

	std::string ExtractJsonNumberField(const std::string& json, const char* key)
	{
		const std::string needle = std::string("\"") + key + "\": ";
		const size_t pos = json.find(needle);
		if (pos == std::string::npos) {
			return {};
		}
		const size_t start = pos + needle.size();
		const size_t end = json.find_first_of(",}\n\r", start);
		if (end == std::string::npos || end <= start) {
			return {};
		}
		return json.substr(start, end - start);
	}

	CrashReportData LoadCrashReport(const std::string& report_path)
	{
		CrashReportData data;
		data.report_file = report_path;
		std::ifstream in(report_path, std::ios::binary);
		if (!in) {
			data.raw_json = I18N(u8"無法讀取崩潰報告：\n") + report_path;
			return data;
		}

		std::ostringstream ss;
		ss << in.rdbuf();
		data.raw_json = ss.str();
		data.timestamp = ExtractJsonString(data.raw_json, "timestamp");
		data.exception_code = ExtractJsonString(data.raw_json, "exception_code");
		data.exception_description = ExtractJsonString(data.raw_json, "exception_description");
		data.exception_address = ExtractJsonString(data.raw_json, "exception_address");
		data.module = ExtractJsonString(data.raw_json, "module");
		data.module_offset = ExtractJsonString(data.raw_json, "module_offset");
		data.dump_file = ExtractJsonString(data.raw_json, "dump_file");
		data.log_file = ExtractJsonString(data.raw_json, "log_file");
		data.handler_log = ExtractJsonString(data.raw_json, "handler_log");
		data.report_source = ExtractJsonString(data.raw_json, "report_source");
		data.os_version = ExtractJsonString(data.raw_json, "os_version");
		data.stack_trace = ExtractJsonString(data.raw_json, "stack_trace");
		data.process_id = ExtractJsonNumberField(data.raw_json, "process_id");
		data.thread_id = ExtractJsonNumberField(data.raw_json, "thread_id");
		return data;
	}

	std::string ReadLogTail(const std::string& log_path, size_t max_lines)
	{
		if (log_path.empty()) {
			return I18N(u8"（日誌路徑為空）");
		}

		const int wide_len = MultiByteToWideChar(CP_UTF8, 0, log_path.c_str(), -1, nullptr, 0);
		if (wide_len <= 0) {
			return I18N(u8"（日誌路徑轉換失敗）\n") + log_path;
		}
		std::vector<wchar_t> wide(static_cast<size_t>(wide_len));
		MultiByteToWideChar(CP_UTF8, 0, log_path.c_str(), -1, wide.data(), wide_len);

		HANDLE file = CreateFileW(wide.data(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return I18N(u8"（無法讀取日誌檔，可能尚未建立或仍被佔用）\n") + log_path;
		}

		std::string content;
		char buffer[4096] = {};
		DWORD read = 0;
		while (ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0) {
			buffer[read] = '\0';
			content.append(buffer, buffer + read);
		}
		CloseHandle(file);

		if (content.empty()) {
			return I18N(u8"（日誌檔為空）\n") + log_path;
		}

		std::istringstream in(content);

		std::vector<std::string> lines;
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			lines.push_back(line);
		}

		if (lines.empty()) {
			return I18NStr(u8"（日誌檔為空）");
		}

		const size_t start = lines.size() > max_lines ? lines.size() - max_lines : 0;
		std::ostringstream ss;
		for (size_t i = start; i < lines.size(); ++i) {
			ss << lines[i] << '\n';
		}
		return ss.str();
	}

	std::string ExplainExceptionCode(const std::string& code_text)
	{
		unsigned long code = 0;
		if (sscanf_s(code_text.c_str(), "0x%lX", &code) != 1) {
			return I18NStr(u8"未知例外（報告未記錄完整代碼）");
		}
		switch (code) {
		case 0xC0000005u:
			return I18NStr(u8"存取違規 (ACCESS_VIOLATION)：常見於空指標、多執行緒同時讀寫資料、或記憶體已損壞");
		case 0xC00000FDu:
			return I18NStr(u8"堆疊溢出 (STACK_OVERFLOW)");
		case 0xE0000001u:
			return I18NStr(u8"std::terminate（未捕獲的 C++ 例外）");
		case 0xE0000002u:
			return "CRT invalid parameter";
		case 0xE0000003u:
			return I18NStr(u8"純虛擬函式呼叫 (purecall)");
		case 0xE0000004u:
			return I18NStr(u8"SIGABRT / abort()：程式主動中止（常見於堆積損壞、assert、或在崩潰處理中又觸發錯誤）");
		case 0xE0000010u:
			return I18NStr(u8"工作階段異常結束：程式未標記正常關閉（可能被強制結束、當機未寫入 dump，或關閉時未執行完整收尾）");
		default:
			if (code >= 0xE0000000u) {
				return I18NStr(u8"應用程式自訂異常碼（可能由 Watchdog 推斷，原始過濾器未完整寫入）");
			}
			return I18NStr(u8"系統或執行期例外");
		}
	}

	std::string BuildCopyableReportText(const CrashReportData& report, const std::string& log_tail)
	{
		std::ostringstream ss;
		ss << I18N(u8"========== HP CLEANER++ 崩潰報告 ==========\n");
		ss << I18N(u8"報告檔: ") << report.report_file << "\n";
		ss << I18N(u8"時間: ") << (report.timestamp.empty() ? I18N(u8"未知") : report.timestamp) << "\n";
		ss << I18N(u8"例外代碼: ") << (report.exception_code.empty() ? I18N(u8"未知") : report.exception_code) << "\n";
		const std::string explain = !report.exception_description.empty()
			? I18NStr(report.exception_description.c_str()) : ExplainExceptionCode(report.exception_code);
		ss << I18N(u8"說明: ") << explain << "\n";
		ss << I18N(u8"位址: ") << (report.exception_address.empty() ? I18N(u8"未知") : report.exception_address) << "\n";
		if (!report.process_id.empty()) {
			ss << I18N(u8"行程 ID: ") << report.process_id << I18N(u8" · 執行緒 ID: ")
				<< (report.thread_id.empty() ? I18N(u8"未知") : report.thread_id) << "\n";
		}
		if (!report.os_version.empty()) {
			ss << I18N(u8"系統: ") << report.os_version << "\n";
		}
		ss << I18N(u8"模組: ") << (report.module.empty() ? I18N(u8"未知") : report.module);
		if (!report.module_offset.empty()) {
			ss << " +" << report.module_offset;
		}
		ss << "\n";
		ss << "MiniDump: " << (report.dump_file.empty() ? I18N(u8"（無）") : report.dump_file) << "\n";
		ss << I18N(u8"日誌: ") << (report.log_file.empty() ? HAppPaths::GetLatestLogFilePath() : report.log_file) << "\n";
		ss << I18N(u8"來源: ") << (report.report_source.empty() ? I18N(u8"未知") : report.report_source) << "\n";
		if (!report.stack_trace.empty()) {
			ss << I18N(u8"---------- 堆疊追蹤 ----------\n") << report.stack_trace << "\n";
		}
		ss << I18N(u8"---------- 日誌尾端 ----------\n") << log_tail << "\n";
		ss << I18N(u8"---------- 原始 JSON ----------\n") << report.raw_json << "\n";
		return ss.str();
	}

	bool CopyTextToClipboard(const std::string& text)
	{
		if (text.empty()) {
			return false;
		}
		if (!OpenClipboard(nullptr)) {
			return false;
		}
		EmptyClipboard();

		const int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
		if (wide_len <= 0) {
			CloseClipboard();
			return false;
		}

		HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wide_len) * sizeof(wchar_t));
		if (mem == nullptr) {
			CloseClipboard();
			return false;
		}

		auto* dest = static_cast<wchar_t*>(GlobalLock(mem));
		if (dest == nullptr) {
			GlobalFree(mem);
			CloseClipboard();
			return false;
		}
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, dest, wide_len);
		GlobalUnlock(mem);
		SetClipboardData(CF_UNICODETEXT, mem);
		CloseClipboard();
		return true;
	}

	void DrawInfoRow(const char* label, const char* value, ImVec4 value_color)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled("%s", (label != nullptr) ? label : "");
		ImGui::TableSetColumnIndex(1);
		ImGui::TextColored(value_color, "%s", (value != nullptr) ? value : "");
	}

	bool LaunchMainApplication()
	{
		wchar_t exe_path[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
			return false;
		}

		std::wstring cmd = L"\"";
		cmd += exe_path;
		cmd += L"\" --mode=app";

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
		cmd_buf.push_back(L'\0');

		const BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
		if (ok) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		}
		return ok == TRUE;
	}

	void OpenPathInExplorer(const std::string& path_utf8)
	{
		if (path_utf8.empty()) {
			return;
		}
		const int needed = MultiByteToWideChar(CP_UTF8, 0, path_utf8.c_str(), -1, nullptr, 0);
		if (needed <= 0) {
			return;
		}
		std::vector<wchar_t> wide(static_cast<size_t>(needed));
		MultiByteToWideChar(CP_UTF8, 0, path_utf8.c_str(), -1, wide.data(), needed);
		ShellExecuteW(nullptr, L"open", wide.data(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
			return true;
		}

		switch (msg) {
		case WM_SIZE:
			if (wParam == SIZE_MINIMIZED) {
				return 0;
			}
			g_ResizeWidth = static_cast<UINT>(LOWORD(lParam));
			g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
}

int RunCrashReportApplication(const std::string& report_path)
{
	HInitLogging();
	HAppPaths::EnsureAppDataDirs();
	Hi18n::Init();

	std::string resolved_path = report_path;
	if (resolved_path.empty()) {
		resolved_path = HAppPaths::ReadPendingCrashReport();
	}
	HLOG_WARN("Crash report mode: {}", resolved_path.empty() ? "(no report path)" : resolved_path.c_str());

	if (resolved_path.empty()) {
		MessageBoxW(nullptr,
			W18N(u8"找不到崩潰報告檔案。\n請檢查 %APPDATA%\\HalfPeople\\HP CLEANER++\\crashes\\ 資料夾。"),
			W18N(u8"HP CLEANER++ - 崩潰報告"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
		return 1;
	}

	HCrashWriteReportUiLockCurrent();

	const CrashReportData report = LoadCrashReport(resolved_path);
	const std::string log_path = report.log_file.empty()
		? HAppPaths::GetLatestLogFilePath()
		: report.log_file;
	const std::string log_tail = ReadLogTail(log_path, 80);
	const std::string handler_path = HAppPaths::GetCrashesDir() + "\\handler.log";
	const std::string handler_tail = ReadLogTail(handler_path, 30);
	const std::string copy_text = BuildCopyableReportText(report, log_tail);
	const std::string explain = !report.exception_description.empty()
		? I18NStr(report.exception_description.c_str()) : ExplainExceptionCode(report.exception_code);

	ImGui_ImplWin32_EnableDpiAwareness();
	const float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
		MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	wchar_t window_class[64] = {};
	_snwprintf_s(window_class, _TRUNCATE, L"HP_CLEANER_CrashReport_%lu",
		static_cast<unsigned long>(GetCurrentProcessId()));
	const wchar_t* kWindowTitle = W18N(u8"HP CLEANER++ - 崩潰報告");

	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr,
		nullptr, window_class, nullptr };
	RegisterClassExW(&wc);
	HWND hwnd = CreateWindowW(window_class, kWindowTitle, WS_OVERLAPPEDWINDOW, 120, 120,
		static_cast<int>(980 * main_scale), static_cast<int>(720 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

	if (!CreateDeviceD3D(hwnd)) {
		CleanupDeviceD3D();
		UnregisterClassW(window_class, wc.hInstance);
		wchar_t wreport[MAX_PATH * 4] = {};
		MultiByteToWideChar(CP_UTF8, 0, resolved_path.c_str(), -1, wreport,
			static_cast<int>(sizeof(wreport) / sizeof(wreport[0])));
		wchar_t msg[768] = {};
		_snwprintf_s(msg, _TRUNCATE,
			W18N(u8"無法建立圖形裝置以顯示報告視窗。\n\n報告檔：\n%s\n\n請至 crashes 資料夾查看完整內容。"),
			wreport);
		MessageBoxW(nullptr, msg, W18N(u8"HP CLEANER++ - 崩潰報告"), MB_OK | MB_ICONERROR | MB_TOPMOST);
		HCrashClearReportUiLock();
		return 1;
	}

	LoadLogoTexture();

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	SetForegroundWindow(hwnd);
	FlashWindow(hwnd, TRUE);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;

	HPageStyleLoadFontsOnce();
	ImGuiStyle& style = HPageStyle();
	style.ScaleAllSizes(main_scale);
	style.FontScaleDpi = main_scale;

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX9_Init(g_pd3dDevice);

	bool done = false;
	bool request_restart = false;
	bool request_open_crashes = false;
	double copy_feedback_until = 0.0;

	while (!done) {
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) {
				done = true;
			}
		}
		if (done) {
			break;
		}

		if (g_DeviceLost) {
			const HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
			if (hr == D3DERR_DEVICELOST) {
				Sleep(10);
				continue;
			}
			if (hr == D3DERR_DEVICENOTRESET) {
				ResetDevice();
			}
			g_DeviceLost = false;
		}

		if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
			g_d3dpp.BackBufferWidth = g_ResizeWidth;
			g_d3dpp.BackBufferHeight = g_ResizeHeight;
			g_ResizeWidth = g_ResizeHeight = 0;
			ResetDevice();
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(io.DisplaySize);
		if (ImGui::Begin("crash_report", nullptr,
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
			const float logo_h = 56.0f;
			const float logo_w = g_logo_texture.texture != 0
				? logo_h * static_cast<float>(g_logo_texture.image_width)
				/ static_cast<float>(g_logo_texture.image_height)
				: 0.0f;

			if (g_logo_texture.texture != 0) {
				ImGui::Image((ImTextureID)(intptr_t)g_logo_texture.texture, ImVec2(logo_w, logo_h));
				ImGui::SameLine();
			}
			ImGui::BeginGroup();
			ImGui::TextColored(kCyanNeon, "%s", "HP CLEANER++");
			ImGui::TextColored(kDanger, "%s", I18N(u8"程式已異常結束"));
			ImGui::TextDisabled("%s", I18N(u8"請複製下方報告並回報開發者，或重新啟動後再試"));
			ImGui::EndGroup();

			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.08f, 0.08f, 0.85f));
			ImGui::BeginChild("alert_banner", ImVec2(0, 36), true, ImGuiWindowFlags_NoScrollbar);
			ImGui::TextColored(kDanger, "  !  ");
			ImGui::SameLine();
			if (report.report_source == "sigabrt") {
				ImGui::TextWrapped("%s",
					I18N_JOIN(
						u8"本次為 SIGABRT/abort（代碼 0xE0000004），多半不是「一般存取違規」，而是堆積損壞或 assert 後 CRT 中止。",
						u8"請一併提供 MiniDump 與日誌尾端。"));
			}
			else if (report.report_source == "watchdog_inferred"
				|| report.report_source == "watchdog_inferred_session") {
				ImGui::TextWrapped("%s",
					I18N_JOIN(
						u8"此報告由 Watchdog 在程式結束後補寫，且通常沒有 MiniDump。",
						u8"若您是用工作管理員結束、建置時強制關閉程式，或代碼為 0xC0000005 但無位址，",
						u8"可能是誤報而非真實存取違規；請查看 logs\\logger_日期.log 與 crashes\\handler.log。"));
			}
			else {
				ImGui::TextWrapped("%s",
					I18N(u8"偵測到未處理崩潰。若發生於「系統清理」掃描中，可能與 UI 與背景 BuildDetails 競爭有關；請用本版重測。"));
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();

			ImGui::Spacing();
			if (ImGui::BeginTable("crash_summary", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg, ImVec2(0, 0))) {
				ImGui::TableSetupColumn(I18N(u8"欄位"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn(I18N(u8"內容"), ImGuiTableColumnFlags_WidthStretch);
				DrawInfoRow(I18N(u8"時間"), report.timestamp.empty() ? I18N(u8"未知") : report.timestamp.c_str(), ImVec4(1, 1, 1, 1));
				DrawInfoRow(I18N(u8"例外代碼"), report.exception_code.empty() ? I18N(u8"未知") : report.exception_code.c_str(), kDanger);
				DrawInfoRow(I18N(u8"說明"), explain.c_str(), kWarn);
				if (!report.os_version.empty()) {
					DrawInfoRow(I18N(u8"系統"), report.os_version.c_str(), ImVec4(0.8f, 0.85f, 0.9f, 1.f));
				}
				if (!report.process_id.empty()) {
					char pid_line[64] = {};
					snprintf(pid_line, sizeof(pid_line), "%s / %s",
						report.process_id.c_str(),
						report.thread_id.empty() ? "?" : report.thread_id.c_str());
					DrawInfoRow("PID / TID", pid_line, ImVec4(0.75f, 0.8f, 0.85f, 1.f));
				}
				DrawInfoRow(I18N(u8"位址"), report.exception_address.empty() ? I18N(u8"未知") : report.exception_address.c_str(),
					ImVec4(0.85f, 0.85f, 1.0f, 1.0f));
				const std::string module_line = report.module.empty()
					? I18NStr(u8"未知")
					: report.module + (report.module_offset.empty() ? "" : " +" + report.module_offset);
				DrawInfoRow(I18N(u8"模組"), module_line.c_str(), kCyanNeon);
				DrawInfoRow("MiniDump", report.dump_file.empty() ? I18N(u8"（未產生）") : report.dump_file.c_str(),
					report.dump_file.empty() ? kWarn : ImVec4(0.7f, 1.0f, 0.7f, 1.0f));
				DrawInfoRow(I18N(u8"報告來源"), report.report_source.empty() ? I18N(u8"未知") : report.report_source.c_str(),
					ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
				DrawInfoRow(I18N(u8"報告檔"), report.report_file.c_str(), kCyanDark);
				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button(I18N(u8"一鍵複製完整報告"), ImVec2(200, 36))) {
				if (CopyTextToClipboard(copy_text)) {
					copy_feedback_until = ImGui::GetTime() + 3.0;
				}
			}
			if (ImGui::GetTime() < copy_feedback_until) {
				ImGui::SameLine();
				ImGui::TextColored(kCyanNeon, "%s", I18N(u8"已複製到剪貼簿"));
			}
			ImGui::SameLine();
			if (ImGui::Button(I18N(u8"重新啟動主程式"), ImVec2(160, 36))) {
				request_restart = true;
				done = true;
			}
			ImGui::SameLine();
			if (ImGui::Button(I18N(u8"開啟 crashes 資料夾"), ImVec2(180, 36))) {
				request_open_crashes = true;
			}
			ImGui::SameLine();
			if (ImGui::Button(I18N(u8"關閉"), ImVec2(100, 36))) {
				done = true;
			}

			const float bottom_h = ImGui::GetContentRegionAvail().y;
			const float stack_h = report.stack_trace.empty() ? 0.f : bottom_h * 0.22f;
			const float json_h = bottom_h * 0.22f;
			const float log_h = bottom_h - json_h - stack_h - (handler_tail.empty() ? 0.f : bottom_h * 0.14f) - 16.0f;

			ImGui::Spacing();
			ImGui::Separator();
			if (!report.stack_trace.empty()) {
				ImGui::TextColored(kCyanDark, "%s", I18N(u8"堆疊追蹤"));
				ImGui::BeginChild("crash_stack", ImVec2(0, stack_h), true);
				ImGui::TextWrapped("%s", report.stack_trace.c_str());
				ImGui::EndChild();
			}
			ImGui::TextColored(kCyanDark, "%s", I18N(u8"原始 JSON"));
			ImGui::BeginChild("crash_json", ImVec2(0, json_h), true);
			ImGui::TextWrapped("%s", report.raw_json.c_str());
			ImGui::EndChild();

			ImGui::TextColored(kCyanDark, I18NF(u8"日誌尾端（最近 80 行）  ·  %s"), log_path.c_str());
			ImGui::BeginChild("crash_log_tail", ImVec2(0, log_h > 80.f ? log_h : 80.f), true);
			ImGui::TextWrapped("%s", log_tail.c_str());
			ImGui::EndChild();
			if (!handler_tail.empty() && handler_tail.find("（") == std::string::npos) {
				ImGui::TextColored(kCyanDark, I18NF(u8"Handler 日誌尾端  ·  %s"), handler_path.c_str());
				const float handler_h = bottom_h * 0.14f;
				ImGui::BeginChild("crash_handler_tail",
					ImVec2(0, handler_h > 60.f ? handler_h : 60.f), true);
				ImGui::TextWrapped("%s", handler_tail.c_str());
				ImGui::EndChild();
			}
		}
		ImGui::End();

		ImGui::EndFrame();
		g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		const ImVec4 clear_color = ImVec4(0.10f, 0.11f, 0.13f, 1.f);
		const D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(
			static_cast<int>(clear_color.x * 255.f),
			static_cast<int>(clear_color.y * 255.f),
			static_cast<int>(clear_color.z * 255.f),
			static_cast<int>(clear_color.w * 255.f));
		g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.f, 0);
		if (g_pd3dDevice->BeginScene() >= 0) {
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			g_pd3dDevice->EndScene();
		}
		const HRESULT present = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
		if (present == D3DERR_DEVICELOST) {
			g_DeviceLost = true;
		}
	}

	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	CleanupDeviceD3D();
	DestroyWindow(hwnd);
	UnregisterClassW(window_class, wc.hInstance);

	if (request_open_crashes) {
		OpenPathInExplorer(HAppPaths::GetCrashesDir());
	}
	if (request_restart) {
		LaunchMainApplication();
	}

	HLOG_INFO("Crash report UI closed");
	HCrashClearReportUiLock();
	return 0;
}
