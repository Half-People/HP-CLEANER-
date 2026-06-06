#include "HUninstallUI.h"
#include "HAppRegistration.h"
#include "HAppPaths.h"
#include "HAppSettings.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HPageStyle.h"
#include "HRC_Assets.h"
#include "resource.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <imgui_internal.h>
#include <d3d9.h>
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
	constexpr float kRounding = 4.f;

	const ImVec4 kCyanNeon = ImVec4(0.0f, 0.95f, 0.95f, 1.0f);
	const ImVec4 kCyanDark = ImVec4(0.0f, 0.45f, 0.45f, 1.0f);
	const ImVec4 kDanger = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
	const ImVec4 kWarn = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
	const ImVec4 kOk = ImVec4(0.45f, 1.0f, 0.65f, 1.0f);

	ImVec4 panel_bg() { return ImVec4(0.06f, 0.09f, 0.11f, 0.94f); }
	ImVec4 card_bg() { return ImVec4(0.09f, 0.12f, 0.15f, 1.f); }
	ImVec4 header_bg() { return ImVec4(0.11f, 0.14f, 0.17f, 1.f); }
	ImVec4 hover_bg() { return ImVec4(0.14f, 0.18f, 0.22f, 1.f); }
	ImVec4 active_bg() { return ImVec4(0.08f, 0.22f, 0.24f, 1.f); }
	ImVec4 track_bg() { return ImVec4(0.12f, 0.14f, 0.16f, 1.f); }

	bool BeginCyberPanel(const char* id, float height = 0.f)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, panel_bg());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
		ImGuiChildFlags child_flags = ImGuiChildFlags_Borders;
		ImVec2 sz(0.f, height);
		if (height <= 0.f) {
			child_flags |= ImGuiChildFlags_AutoResizeY;
		}
		const bool open = ImGui::BeginChild(id, sz, child_flags);
		if (open) {
			const ImVec2 p0 = ImGui::GetWindowPos();
			const ImVec2 ws = ImGui::GetWindowSize();
			const ImVec2 p1(p0.x + ws.x, p0.y + ws.y);
			ImGui::GetWindowDrawList()->AddRect(p0, p1, ImGui::GetColorU32(kCyanDark), kRounding, 0, 1.f);
		}
		return open;
	}

	void EndCyberPanel()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	LPDIRECT3D9 g_pD3D = nullptr;
	LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
	bool g_DeviceLost = false;
	UINT g_ResizeWidth = 0;
	UINT g_ResizeHeight = 0;
	D3DPRESENT_PARAMETERS g_d3dpp = {};
	HRC::HTexture g_logo_texture;

	void DrawFramedLogo(const HRC::HTexture& tex, float height)
	{
		const float aspect = (tex.texture != 0 && tex.image_height > 0)
			? static_cast<float>(tex.image_width) / static_cast<float>(tex.image_height) : 1.f;
		const float width = height * aspect;
		const ImVec2 outer(width + 10.f, height + 10.f);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImRect frame(p0, ImVec2(p0.x + outer.x, p0.y + outer.y));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(frame.Min, frame.Max, ImGui::GetColorU32(card_bg()), kRounding);
		dl->AddRect(frame.Min, frame.Max, ImGui::GetColorU32(kCyanDark), kRounding, 0, 1.2f);
		if (tex.texture != 0) {
			const ImVec2 img_pos(frame.Min.x + 5.f, frame.Min.y + 5.f);
			dl->AddImage((ImTextureID)(intptr_t)tex.texture, img_pos,
				ImVec2(img_pos.x + width, img_pos.y + height));
		}
		else {
			const char* placeholder = "Logo";
			const ImVec2 ts = ImGui::CalcTextSize(placeholder);
			dl->AddText(
				ImVec2(frame.Min.x + (outer.x - ts.x) * 0.5f, frame.Min.y + (outer.y - ts.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), placeholder);
		}
		ImGui::Dummy(outer);
	}

	bool DrawPresetCard(const char* id, float width, float height, const char* title,
		const char* line1, const char* line2, bool accent)
	{
		ImGui::PushID(id);
		const bool pressed = ImGui::InvisibleButton("##card", ImVec2(width, height));
		const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const bool hovered = ImGui::IsItemHovered();
		const ImU32 bg = ImGui::GetColorU32(hovered ? hover_bg() : (accent ? active_bg() : card_bg()));
		dl->AddRectFilled(bb.Min, bb.Max, bg, kRounding);
		dl->AddRect(bb.Min, bb.Max,
			ImGui::GetColorU32(hovered ? kCyanNeon : (accent ? kCyanNeon : kCyanDark)),
			kRounding, 0, hovered ? 1.4f : 1.f);

		const float pad = 10.f;
		const ImU32 sub_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		dl->PushClipRect(bb.Min, bb.Max, true);
		const ImU32 title_col = accent ? ImGui::GetColorU32(kCyanNeon) : ImGui::GetColorU32(ImGuiCol_Text);
		dl->AddText(ImVec2(bb.Min.x + pad, bb.Min.y + 8.f), title_col, title);
		dl->AddText(ImVec2(bb.Min.x + pad, bb.Min.y + 30.f), sub_col, line1);
		dl->AddText(ImVec2(bb.Min.x + pad, bb.Min.y + 48.f), sub_col, line2);
		dl->PopClipRect();

		ImGui::PopID();
		return pressed;
	}

	void DrawMarqueePathTile(const ImRect& bb, const char* path_utf8)
	{
		const char* text = (path_utf8 != nullptr && path_utf8[0] != '\0')
			? path_utf8 : I18N(u8"（無法讀取安裝路徑）");
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(kCyanDark), kRounding, 0, 1.f);
		dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"安裝位置"));

		const ImVec2 text_size = ImGui::CalcTextSize(text);
		const float clip_pad = 10.f;
		const ImRect clip(bb.Min.x + clip_pad, bb.Min.y + 28.f, bb.Max.x - clip_pad, bb.Max.y - 8.f);
		const float text_y = clip.Max.y - text_size.y;
		const ImU32 value_col = ImGui::GetColorU32(kCyanNeon);

		if (text_size.x <= clip.GetWidth()) {
			dl->AddText(ImVec2(clip.Min.x, text_y), value_col, text);
			return;
		}

		const float gap = 48.f;
		const float cycle = text_size.x + gap;
		const float speed = 42.f;
		const float scroll = fmodf(static_cast<float>(ImGui::GetTime()) * speed, cycle);
		const float x0 = clip.Min.x - scroll;

		dl->PushClipRect(clip.Min, clip.Max, true);
		dl->AddText(ImVec2(x0, text_y), value_col, text);
		dl->AddText(ImVec2(x0 + cycle, text_y), value_col, text);
		dl->PopClipRect();
	}

	void DrawGradientHLine(ImDrawList* dl, ImVec2 p0, ImVec2 p1)
	{
		const ImU32 left = ImGui::GetColorU32(ImVec4(0.f, 0.35f, 0.35f, 0.f));
		const ImU32 mid = ImGui::GetColorU32(kCyanNeon);
		const ImU32 right = ImGui::GetColorU32(ImVec4(0.f, 0.35f, 0.35f, 0.f));
		const float mid_x = (p0.x + p1.x) * 0.5f;
		dl->AddRectFilledMultiColor(p0, ImVec2(mid_x, p1.y), left, mid, mid, left);
		dl->AddRectFilledMultiColor(ImVec2(mid_x, p0.y), p1, mid, right, right, mid);
	}

	bool CyberTextButton(const char* id_suffix, const ImRect& bb, const char* label, bool accent = false)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiID id = window->GetID(id_suffix);
		ImGui::ItemSize(bb);
		if (!ImGui::ItemAdd(bb, id)) {
			return false;
		}
		bool hovered = false;
		bool held = false;
		const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
		ImDrawList* dl = window->DrawList;
		const ImU32 border_col = ImGui::GetColorU32(hovered || held ? kCyanNeon : kCyanDark);
		const ImU32 bg_col = ImGui::GetColorU32(held ? active_bg() : (hovered ? hover_bg() : header_bg()));
		dl->AddRectFilled(bb.Min, bb.Max, bg_col, kRounding);
		dl->AddRect(bb.Min, bb.Max, border_col, kRounding, 0, 1.f);
		const ImVec2 ts = ImGui::CalcTextSize(label);
		const ImVec2 tp(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
			bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f);
		const ImU32 text_col = accent && hovered
			? ImGui::GetColorU32(kCyanNeon) : ImGui::GetColorU32(ImGuiCol_Text);
		dl->AddText(tp, text_col, label);
		return pressed;
	}

	bool CyberSwitch(const char* id_suffix, const ImRect& bb, bool on)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiID id = window->GetID(id_suffix);
		ImGui::ItemAdd(bb, id);
		bool hovered = false;
		bool held = false;
		const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
		ImDrawList* dl = window->DrawList;
		const float radius = bb.GetHeight() * 0.5f;
		const ImU32 track_off = ImGui::GetColorU32(ImVec4(0.14f, 0.16f, 0.18f, 1.f));
		const ImU32 track_on = ImGui::GetColorU32(ImVec4(0.08f, 0.28f, 0.3f, 1.f));
		dl->AddRectFilled(bb.Min, bb.Max, on ? track_on : track_off, radius);
		dl->AddRect(bb.Min, bb.Max,
			ImGui::GetColorU32(hovered || held ? kCyanNeon : kCyanDark), radius, 0, 1.2f);
		const float pad = 3.f;
		const float thumb_r = radius - pad;
		const float thumb_x = on ? (bb.Max.x - radius) : (bb.Min.x + radius);
		const ImVec2 thumb_center(thumb_x, bb.Min.y + radius);
		dl->AddCircleFilled(thumb_center, thumb_r,
			ImGui::GetColorU32(on ? kCyanNeon : ImVec4(0.55f, 0.6f, 0.65f, 1.f)));
		return pressed;
	}

	void DrawStatTile(const ImRect& bb, const char* title, const char* value, const ImVec4& value_color)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(kCyanDark), kRounding, 0, 1.f);
		dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
		const ImVec2 vs = ImGui::CalcTextSize(value);
		dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Max.y - vs.y - 10.f),
			ImGui::GetColorU32(value_color), value);
	}

	void DrawBadge(ImDrawList* dl, ImVec2 pos, const char* text, const ImVec4& bg, const ImVec4& fg)
	{
		if (text == nullptr || text[0] == '\0') {
			return;
		}
		const ImVec2 ts = ImGui::CalcTextSize(text);
		const ImRect pill(pos, ImVec2(pos.x + ts.x + 16.f, pos.y + ts.y + 8.f));
		dl->AddRectFilled(pill.Min, pill.Max, ImGui::GetColorU32(bg), 3.f);
		dl->AddRect(pill.Min, pill.Max, ImGui::GetColorU32(fg), 3.f, 0, 1.f);
		dl->AddText(ImVec2(pill.Min.x + 8.f, pill.Min.y + 4.f), ImGui::GetColorU32(fg), text);
	}

	struct UninstallOptionDef {
		const char* id;
		const char* icon;
		const char* title;
		const char* desc;
		const char* badge;
		ImVec4 badge_bg;
		ImVec4 badge_fg;
		bool* value;
	};

	bool DrawOptionCard(const UninstallOptionDef& opt, float card_h)
	{
		const float w = ImGui::GetContentRegionAvail().x;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImRect card_bb(p0, ImVec2(p0.x + w, p0.y + card_h));
		ImGui::ItemSize(card_bb);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const bool enabled = opt.value != nullptr && *opt.value;

		dl->AddRectFilled(card_bb.Min, card_bb.Max,
			ImGui::GetColorU32(enabled ? active_bg() : card_bg()), kRounding);
		dl->AddRect(card_bb.Min, card_bb.Max,
			ImGui::GetColorU32(enabled ? kCyanNeon : kCyanDark), kRounding, 0, enabled ? 1.4f : 1.f);

		const ImRect icon_bb(ImVec2(card_bb.Min.x + 12.f, card_bb.Min.y + 12.f),
			ImVec2(card_bb.Min.x + 44.f, card_bb.Min.y + 44.f));
		dl->AddCircleFilled(icon_bb.GetCenter(), 16.f, ImGui::GetColorU32(ImVec4(0.05f, 0.18f, 0.2f, 1.f)));
		dl->AddCircle(icon_bb.GetCenter(), 16.f, ImGui::GetColorU32(kCyanDark), 0, 1.2f);
		const ImVec2 isz = ImGui::CalcTextSize(opt.icon);
		dl->AddText(ImVec2(icon_bb.GetCenter().x - isz.x * 0.5f, icon_bb.GetCenter().y - isz.y * 0.5f),
			ImGui::GetColorU32(kCyanNeon), opt.icon);

		dl->AddText(ImVec2(card_bb.Min.x + 56.f, card_bb.Min.y + 12.f),
			ImGui::GetColorU32(ImGuiCol_Text), opt.title);
		ImGui::PushTextWrapPos(card_bb.Max.x - 90.f);
		dl->AddText(ImVec2(card_bb.Min.x + 56.f, card_bb.Min.y + 32.f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), opt.desc);
		ImGui::PopTextWrapPos();

		DrawBadge(dl, ImVec2(card_bb.Min.x + 56.f, card_bb.Max.y - 28.f),
			opt.badge, opt.badge_bg, opt.badge_fg);

		const ImRect sw_bb(ImVec2(card_bb.Max.x - 58.f, card_bb.Min.y + (card_h - 24.f) * 0.5f),
			ImVec2(card_bb.Max.x - 14.f, card_bb.Min.y + (card_h - 24.f) * 0.5f + 24.f));
		if (CyberSwitch(opt.id, sw_bb, enabled) && opt.value != nullptr) {
			*opt.value = !*opt.value;
			return true;
		}
		return false;
	}

	int CountEnabledOptions(const HAppUninstallOptions& opts)
	{
		int n = 0;
		if (opts.remove_shortcuts) ++n;
		if (opts.remove_startup) ++n;
		if (opts.remove_app_data) ++n;
		if (opts.remove_program_on_reboot) ++n;
		return n;
	}

	const char* RiskLabel(const HAppUninstallOptions& opts)
	{
		if (opts.remove_app_data || opts.remove_program_on_reboot) {
			return I18N(u8"高");
		}
		if (opts.remove_shortcuts || opts.remove_startup) {
			return I18N(u8"低");
		}
		return "—";
	}

	void BuildPreviewLines(const HAppUninstallOptions& opts, std::vector<std::string>& lines)
	{
		lines.clear();
		lines.push_back(I18N(u8"• 移除「設定 → 應用程式」中的 HP CLEANER++ 登錄（固定執行）"));
		if (opts.remove_shortcuts) {
			lines.push_back(I18N(u8"• 刪除桌面與開始功能表捷徑"));
		}
		if (opts.remove_startup) {
			lines.push_back(I18N(u8"• 關閉開機自動啟動（Run 登錄）"));
		}
		if (opts.remove_app_data) {
			lines.push_back(I18N(u8"• 刪除 %APPDATA%\\HalfPeople\\HP CLEANER++（日誌、設定、快取）"));
		}
		if (opts.remove_program_on_reboot) {
			lines.push_back(I18N(u8"• 重新開機後刪除程式 exe（安裝資料夾可能仍需手動清理）"));
		}
		if (lines.size() == 1) {
			lines.push_back(I18N(u8"• 僅移除系統登錄，不刪除捷徑與個人資料"));
		}
	}

	void ApplyPreset(const char* preset, HAppUninstallOptions& opts)
	{
		if (strcmp(preset, "light") == 0) {
			opts.remove_shortcuts = true;
			opts.remove_startup = true;
			opts.remove_app_data = false;
			opts.remove_program_on_reboot = false;
		}
		else if (strcmp(preset, "standard") == 0) {
			opts.remove_shortcuts = true;
			opts.remove_startup = true;
			opts.remove_app_data = true;
			opts.remove_program_on_reboot = false;
		}
		else if (strcmp(preset, "full") == 0) {
			opts.remove_shortcuts = true;
			opts.remove_startup = true;
			opts.remove_app_data = true;
			opts.remove_program_on_reboot = true;
		}
	}

	bool OptionsMatchPreset(const HAppUninstallOptions& opts, const char* preset)
	{
		HAppUninstallOptions ref = {};
		ApplyPreset(preset, ref);
		return opts.remove_shortcuts == ref.remove_shortcuts
			&& opts.remove_startup == ref.remove_startup
			&& opts.remove_app_data == ref.remove_app_data
			&& opts.remove_program_on_reboot == ref.remove_program_on_reboot;
	}

	const char* DetectActivePreset(const HAppUninstallOptions& opts)
	{
		if (OptionsMatchPreset(opts, "light")) {
			return "light";
		}
		if (OptionsMatchPreset(opts, "standard")) {
			return "standard";
		}
		if (OptionsMatchPreset(opts, "full")) {
			return "full";
		}
		return nullptr;
	}

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
		if (g_pd3dDevice->Reset(&g_d3dpp) == D3DERR_INVALIDCALL) {
			IM_ASSERT(0);
		}
		ImGui_ImplDX9_CreateDeviceObjects();
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

int RunUninstallApplication()
{
	HInitLogging();
	HAppPaths::EnsureAppDataDirs();
	HLOG_INFO("Uninstall mode started");

	ImGui_ImplWin32_EnableDpiAwareness();
	const float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
		MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	const HINSTANCE hinst = GetModuleHandle(nullptr);
	const HICON app_icon = static_cast<HICON>(
		LoadImageW(hinst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	const wchar_t* kClass = L"HP_CLEANER_UninstallClass";
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hinst, app_icon, nullptr, nullptr, nullptr,
		kClass, app_icon };
	RegisterClassExW(&wc);
	HWND hwnd = CreateWindowW(kClass, W18N(u8"HP CLEANER++ - 卸載"), WS_OVERLAPPEDWINDOW,
		120, 80, static_cast<int>(900 * main_scale), static_cast<int>(700 * main_scale),
		nullptr, nullptr, wc.hInstance, nullptr);
	if (app_icon != nullptr) {
		::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
		::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon));
	}

	if (!CreateDeviceD3D(hwnd)) {
		MessageBoxW(nullptr, W18N(u8"無法建立圖形裝置。"), L"HP CLEANER++", MB_OK | MB_ICONERROR);
		return 1;
	}

	HRC::SetRenderDevice(g_pd3dDevice);
	g_logo_texture = HRC::LoadTextureFromDevice(g_pd3dDevice, MAKEINTRESOURCE(IDB_PNG1), TEXT("PNG"));

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

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

	HAppUninstallOptions opts;
	opts.remove_shortcuts = true;
	opts.remove_startup = true;
	opts.remove_app_data = false;
	opts.remove_program_on_reboot = false;

	std::wstring result_msg;
	std::vector<std::string> preview_lines;
	bool done = false;
	bool uninstall_done = false;
	bool confirm_pending = false;

	const std::wstring install_dir = HAppRegistration::GetInstallDirectory();
	char install_utf8[512] = {};
	if (!install_dir.empty()) {
		WideCharToMultiByte(CP_UTF8, 0, install_dir.c_str(), -1, install_utf8, sizeof(install_utf8), nullptr, nullptr);
	}

	UninstallOptionDef option_defs[] = {
		{ "##u_shortcuts", "L", I18N(u8"捷徑與開始功能表"),
			I18N(u8"移除桌面圖示與「程式集」中的 HP CLEANER++ 項目。"),
			I18N(u8"建議"), ImVec4(0.08f, 0.22f, 0.18f, 1.f), kOk, &opts.remove_shortcuts },
		{ "##u_startup", "S", I18N(u8"開機自動啟動"),
			I18N(u8"清除登錄 Run 鍵中的自動啟動項目。"),
			I18N(u8"建議"), ImVec4(0.08f, 0.22f, 0.18f, 1.f), kOk, &opts.remove_startup },
		{ "##u_appdata", "D", I18N(u8"使用者資料"),
			I18N(u8"刪除日誌、設定、清理任務與快取（%APPDATA%）。"),
			I18N(u8"注意"), ImVec4(0.28f, 0.14f, 0.06f, 1.f), kWarn, &opts.remove_app_data },
		{ "##u_exe", "X", I18N(u8"程式檔案"),
			I18N(u8"於下次重新開機後刪除主程式 exe（無法立即刪除正在執行的檔案）。"),
			I18N(u8"進階"), ImVec4(0.22f, 0.08f, 0.08f, 1.f), kDanger, &opts.remove_program_on_reboot },
	};

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
		if (ImGui::Begin("uninstall", nullptr,
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {

			const float pad = 16.f;
			ImGui::SetCursorPos(ImVec2(pad, pad));

			if (BeginCyberPanel("##un_hero", 112.f)) {
				DrawFramedLogo(g_logo_texture, 72.f);
				ImGui::SameLine(0.f, 16.f);
				ImGui::BeginGroup();
				ImGui::TextColored(kCyanNeon, "HP CLEANER++");
				ImGui::TextColored(kDanger, "%s", I18N(u8"解除安裝精靈"));
				ImGui::TextDisabled(I18N(u8"選擇要清除的內容；右側即時預覽將執行的操作"));
				ImGui::TextDisabled(I18N(u8"版本 1.0.0.0  |  HalfPeople Studio"));
				ImGui::EndGroup();
			}
			EndCyberPanel();

			ImGui::Spacing();

			if (!uninstall_done) {
				const float tile_gap = 8.f;
				const float tile_w = (ImGui::GetContentRegionAvail().x - tile_gap * 2.f) / 3.f;
				const float tile_h = 58.f;
				const ImVec2 tile0 = ImGui::GetCursorScreenPos();
				char sel_buf[16] = {};
				snprintf(sel_buf, sizeof(sel_buf), I18N(u8"%d 項"), CountEnabledOptions(opts));
				DrawStatTile(ImRect(tile0, ImVec2(tile0.x + tile_w, tile0.y + tile_h)),
					I18N(u8"已選項目"), sel_buf, kCyanNeon);
				const ImVec2 tile1(tile0.x + tile_w + tile_gap, tile0.y);
				const bool high_risk = opts.remove_app_data || opts.remove_program_on_reboot;
				DrawStatTile(ImRect(tile1, ImVec2(tile1.x + tile_w, tile1.y + tile_h)),
					I18N(u8"影響程度"), RiskLabel(opts), high_risk ? kDanger : kOk);
				const ImVec2 tile2(tile1.x + tile_w + tile_gap, tile0.y);
				DrawMarqueePathTile(ImRect(tile2, ImVec2(tile2.x + tile_w, tile2.y + tile_h)), install_utf8);
				ImGui::Dummy(ImVec2(0, tile_h + 8.f));

				ImGui::Spacing();

				if (BeginCyberPanel("##un_preset", 0.f)) {
					ImGui::TextColored(kCyanNeon, "%s", I18N(u8"快速方案"));
					ImGui::SameLine();
					ImGui::TextDisabled(I18N(u8"點擊卡片套用預設組合"));
					ImGui::Spacing();
					const float card_gap = 8.f;
					const float card_w = (ImGui::GetContentRegionAvail().x - card_gap * 2.f) / 3.f;
					const float card_h = 86.f;
					const char* active_preset = DetectActivePreset(opts);
					if (DrawPresetCard("pre_light", card_w, card_h, I18N(u8"輕量卸載"),
						I18N(u8"捷徑 + 開機自啟"), I18N(u8"不刪除個人資料與程式檔"),
						active_preset != nullptr && strcmp(active_preset, "light") == 0)) {
						ApplyPreset("light", opts);
					}
					ImGui::SameLine(0.f, card_gap);
					if (DrawPresetCard("pre_std", card_w, card_h, I18N(u8"標準卸載"),
						I18N(u8"捷徑 + 自啟 + 使用者資料"), I18N(u8"建議大多數使用者"),
						active_preset != nullptr && strcmp(active_preset, "standard") == 0)) {
						ApplyPreset("standard", opts);
					}
					ImGui::SameLine(0.f, card_gap);
					if (DrawPresetCard("pre_full", card_w, card_h, I18N(u8"完全清除"),
						I18N(u8"含標準項 + 重開機刪 exe"), I18N(u8"徹底移除本機痕跡"),
						active_preset != nullptr && strcmp(active_preset, "full") == 0)) {
						ApplyPreset("full", opts);
					}
					ImGui::Dummy(ImVec2(0, 2.f));
				}
				EndCyberPanel();

				ImGui::Spacing();
				const float body_h = ImGui::GetContentRegionAvail().y;
				const float col_w = (ImGui::GetContentRegionAvail().x - 12.f) * 0.56f;
				const float left_footer_h = 40.f;
				ImGui::BeginChild("##un_left", ImVec2(col_w, body_h), ImGuiChildFlags_Borders);
				ImGui::TextColored(kCyanNeon, "%s", I18N(u8"卸載項目"));
				ImGui::TextDisabled(I18N(u8"點擊右側開關或卡片切換"));
				ImGui::Spacing();
				ImGui::BeginChild("##un_left_scroll", ImVec2(0.f, -left_footer_h), ImGuiChildFlags_Borders);
				for (const UninstallOptionDef& def : option_defs) {
					DrawOptionCard(def, 78.f);
					ImGui::Dummy(ImVec2(0, 8.f));
				}
				ImGui::EndChild();

				const float mini_h = 32.f;
				const float half = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
				const ImVec2 mp = ImGui::GetCursorScreenPos();
				if (CyberTextButton("##all_on", ImRect(mp, ImVec2(mp.x + half, mp.y + mini_h)), I18N(u8"全部啟用"))) {
					opts.remove_shortcuts = opts.remove_startup = opts.remove_app_data = opts.remove_program_on_reboot = true;
				}
				if (CyberTextButton("##all_off",
					ImRect(ImVec2(mp.x + half + 8.f, mp.y), ImVec2(mp.x + half + 8.f + half, mp.y + mini_h)),
					I18N(u8"全部關閉"))) {
					opts.remove_shortcuts = opts.remove_startup = opts.remove_app_data = opts.remove_program_on_reboot = false;
				}
				ImGui::Dummy(ImVec2(0.f, mini_h));
				ImGui::EndChild();

				ImGui::SameLine();
				const float act_h = 36.f;
				const float right_footer_h = confirm_pending ? (act_h + 56.f) : (act_h + 16.f);
				ImGui::BeginChild("##un_right", ImVec2(0, body_h), ImGuiChildFlags_Borders);
				ImGui::TextColored(kCyanNeon, "%s", I18N(u8"將會執行"));
				ImGui::Spacing();
				BuildPreviewLines(opts, preview_lines);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.08f, 0.1f, 1.f));
				ImGui::BeginChild("##preview_box", ImVec2(0.f, -right_footer_h), ImGuiChildFlags_Borders);
				for (const std::string& line : preview_lines) {
					ImGui::TextWrapped("%s", line.c_str());
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();

				if (confirm_pending) {
					ImGui::TextColored(kWarn, "%s", I18N(u8"請再次確認後執行卸載"));
				}
				const ImVec2 act_p = ImGui::GetCursorScreenPos();
				const float row_w = ImGui::GetContentRegionAvail().x;
				float bx = act_p.x;
				if (!confirm_pending) {
					const float go_w = 120.f;
					const float cancel_w = 100.f;
					const float cancel_x = act_p.x + row_w - cancel_w;
					if (CyberTextButton("##un_go", ImRect(act_p, ImVec2(act_p.x + go_w, act_p.y + act_h)), I18N(u8"開始卸載"), true)) {
						confirm_pending = true;
					}
					bx = cancel_x;
					if (CyberTextButton("##un_cancel",
						ImRect(ImVec2(bx, act_p.y), ImVec2(bx + cancel_w, act_p.y + act_h)), I18N(u8"取消"))) {
						done = true;
					}
				}
				else {
					const float confirm_w = 132.f;
					const float back_w = 96.f;
					const float cancel_w = 100.f;
					if (CyberTextButton("##un_confirm", ImRect(act_p, ImVec2(act_p.x + confirm_w, act_p.y + act_h)), I18N(u8"確認卸載"), true)) {
						if (HAppRegistration::PerformUninstall(opts, result_msg)) {
							uninstall_done = true;
							confirm_pending = false;
							HLOG_INFO("Uninstall completed");
						}
						else {
							result_msg = W18N(u8"卸載過程發生錯誤，請查看日誌。");
						}
					}
					bx = act_p.x + confirm_w + 8.f;
					if (CyberTextButton("##un_back",
						ImRect(ImVec2(bx, act_p.y), ImVec2(bx + back_w, act_p.y + act_h)), I18N(u8"返回"))) {
						confirm_pending = false;
					}
					bx = act_p.x + row_w - cancel_w;
					if (CyberTextButton("##un_cancel",
						ImRect(ImVec2(bx, act_p.y), ImVec2(bx + cancel_w, act_p.y + act_h)), I18N(u8"取消"))) {
						done = true;
					}
				}
				ImGui::Dummy(ImVec2(0.f, act_h));
				ImGui::EndChild();
			}
			else {
				if (BeginCyberPanel("##un_done", 0.f)) {
					ImGui::TextColored(kOk, "%s", I18N(u8"卸載完成"));
					ImGui::Spacing();
					char msg_utf8[1024] = {};
					WideCharToMultiByte(CP_UTF8, 0, result_msg.c_str(), -1, msg_utf8, sizeof(msg_utf8), nullptr, nullptr);
					ImGui::TextWrapped("%s", msg_utf8);
					ImGui::Spacing();
					const ImVec2 bb0 = ImGui::GetCursorScreenPos();
					const float bar_w = ImGui::GetContentRegionAvail().x;
					ImDrawList* dl = ImGui::GetWindowDrawList();
					const ImRect bar_bb(bb0, ImVec2(bb0.x + bar_w, bb0.y + 18.f));
					dl->AddRectFilled(bar_bb.Min, bar_bb.Max, ImGui::GetColorU32(track_bg()), kRounding);
					dl->AddRectFilled(bar_bb.Min, ImVec2(bar_bb.Max.x, bar_bb.Max.y),
						ImGui::GetColorU32(kCyanNeon), kRounding);
					dl->AddRect(bar_bb.Min, bar_bb.Max, ImGui::GetColorU32(kCyanDark), kRounding, 0, 1.f);
					dl->AddText(ImVec2(bar_bb.Min.x + 8.f, bar_bb.Min.y + 1.f),
						ImGui::GetColorU32(ImGuiCol_Text), "100%");
					ImGui::Dummy(ImVec2(0, 28.f));
					const ImVec2 cp = ImGui::GetCursorScreenPos();
					if (CyberTextButton("##un_close", ImRect(cp, ImVec2(cp.x + 120.f, cp.y + 36.f)), I18N(u8"關閉"), true)) {
						done = true;
					}
				}
				EndCyberPanel();
			}
		}
		ImGui::End();
		ImGui::EndFrame();

		g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
			D3DCOLOR_RGBA(12, 16, 22, 255), 1.0f, 0);
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
	UnregisterClassW(kClass, wc.hInstance);
	HShutdownLogging();
	return uninstall_done ? 0 : 1;
}
