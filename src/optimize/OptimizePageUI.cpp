#define IMGUI_DEFINE_MATH_OPERATORS
#include "OptimizePageUI.h"
#include "OptimizeScan.h"
#include "OptimizeNetworkScan.h"
#include "OptimizeStartupIcon.h"
#include "ClearPageUI.h"
#include "HCleanTask.h"
#include "HAdminPrompt.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

namespace OptimizePageUI {
	namespace {
		using namespace HUiTheme;

		static const char* UiTxt(const char* zh_tw)
		{
			if (zh_tw == nullptr || zh_tw[0] == '\0') {
				return "";
			}
			return I18N(zh_tw);
		}

		constexpr float kRounding = 4.f;
		constexpr float kSubTabH = 34.f;
		constexpr float kTopBarH = 52.f;
		constexpr float kSwitchW = 52.f;
		constexpr float kSwitchH = 26.f;
		constexpr float kStartupIconSz = 40.f;
		constexpr float kSvcActionW = 76.f;
		constexpr float kListRowH = 76.f;
		constexpr float kListPad = 10.f;
		constexpr float kListIcon = 44.f;
		constexpr float kListActionW = 82.f;
		constexpr float kListTagW = 112.f;

		enum class SubTab : int {
			Overview = 0,
			Startup,
			Services,
			System,
			Network,
			Storage,
			Count
		};

		static int g_active_subtab = 0;
		static char g_pending_apply_preset[32] = {};
		static bool g_show_apply_confirm = false;
		static bool g_apply_create_restore_point = true;
		static bool g_did_initial_scan = false;
		static char g_startup_filter[96] = {};
		static int g_startup_sort = 0;
		static bool g_startup_only_enabled = false;
		static char g_service_filter[96] = {};
		static int g_service_sort = 0;
		static bool g_service_only_recommended = false;

		static bool g_sys_open_status = true;
		static bool g_sys_open_power = true;
		static bool g_sys_open_visual = true;
		static bool g_sys_open_game = true;
		static bool g_sys_open_input = true;
		static bool g_sys_open_adv = false;
		static bool g_sys_open_bg = true;
		static bool g_sys_open_tune = false;
		static bool g_sys_open_tools = false;

		static bool g_sys_info_popup_open = false;
		static char g_sys_info_popup_title[80] = {};
		static char g_sys_info_popup_body[640] = {};

		enum class NetSubTab : int {
			Overview = 0,
			Monitor,
			Dns,
			SpeedTest,
			Tools,
			Processes,
			Count
		};

		static int g_net_subtab = 0;
		static bool g_net_did_request = false;
		static bool g_net_did_dns_bench = false;
		static char g_net_proc_filter[72] = {};
		static bool g_net_proc_hot_only = false;
		static char g_custom_dns_label[40] = {};
		static char g_custom_dns_primary[48] = {};
		static char g_custom_dns_secondary[48] = {};
		static char g_dns_test_domain[64] = "www.msftconnecttest.com";
		static bool g_dns_test_domain_inited = false;
		static float g_net_auto_refresh = 0.f;

		static bool g_store_open_work = true;
		static bool g_store_open_local = true;
		static bool g_store_open_space = true;
		static bool g_store_open_maintain = true;
		static bool g_store_open_tools = false;

		enum class PendingStorageOp : int { None = 0, Analyze, Optimize };
		static PendingStorageOp g_pending_storage_op = PendingStorageOp::None;

		static void TryRunPendingStorageOp()
		{
			if (g_pending_storage_op == PendingStorageOp::None) {
				return;
			}
			if (!HCleanHasElevatedAccess()) {
				return;
			}
			if (OptimizeScan::IsDiskOptimizationRunning()) {
				return;
			}
			const OptimizeScan::StorageWorkSnapshot work =
				OptimizeScan::GetStorageWorkSnapshot();
			if (work.running) {
				return;
			}
			const PendingStorageOp op = g_pending_storage_op;
			g_pending_storage_op = PendingStorageOp::None;
			if (op == PendingStorageOp::Analyze) {
				HLOG_INFO("StoragePage: 提權後自動執行待處理磁碟分析");
				OptimizeScan::RequestDiskOptimizationAnalyze();
			}
			else if (op == PendingStorageOp::Optimize) {
				HLOG_INFO("StoragePage: 提權後自動執行待處理硬碟最佳化");
				OptimizeScan::RequestDiskOptimizationRun();
			}
		}

		static const char* NetSubTabLabel(NetSubTab t)
		{
			switch (t) {
			case NetSubTab::Overview: return I18N(u8"總覽");
			case NetSubTab::Monitor: return I18N(u8"監控");
			case NetSubTab::Dns: return "DNS";
			case NetSubTab::SpeedTest: return I18N(u8"網路測試");
			case NetSubTab::Tools: return I18N(u8"設定");
			case NetSubTab::Processes: return I18N(u8"程式");
			default: return "";
			}
		}

		enum class StartupSortMode : int {
			ImpactDesc = 0,
			NameAsc,
			EnabledFirst,
		};

		static bool StartupPassesFilter(const OptimizeScan::StartupEntry& e)
		{
			if (g_startup_only_enabled && !e.enabled) {
				return false;
			}
			if (g_startup_filter[0] == '\0') {
				return true;
			}
			auto contains = [&](const char* s) {
				if (s == nullptr || s[0] == '\0') {
					return false;
				}
				return strstr(s, g_startup_filter) != nullptr
					|| _stricmp(s, g_startup_filter) == 0;
			};
			if (strstr(e.name_utf8, g_startup_filter) != nullptr) {
				return true;
			}
			return contains(e.product_utf8) || contains(e.publisher_utf8)
				|| contains(e.file_description_utf8) || contains(e.command_utf8);
		}

		static bool ServicePassesFilter(const OptimizeScan::ServiceEntry& s)
		{
			if (g_service_only_recommended && !s.recommended_disable) {
				return false;
			}
			if (g_service_filter[0] == '\0') {
				return true;
			}
			return strstr(s.display_name, g_service_filter) != nullptr
				|| strstr(s.service_name, g_service_filter) != nullptr
				|| strstr(s.description_utf8, g_service_filter) != nullptr;
		}

		static void SortServiceIndices(const OptimizeScan::Snapshot& snap,
			std::vector<size_t>& indices)
		{
			indices.clear();
			for (size_t i = 0; i < snap.services.size(); ++i) {
				if (ServicePassesFilter(snap.services[i])) {
					indices.push_back(i);
				}
			}
			std::sort(indices.begin(), indices.end(),
				[&](size_t a, size_t b) {
					const auto& sa = snap.services[a];
					const auto& sb = snap.services[b];
					if (g_service_sort == 0) {
						if (sa.recommended_disable != sb.recommended_disable) {
							return sa.recommended_disable > sb.recommended_disable;
						}
					}
					else if (g_service_sort == 1) {
						if (sa.running != sb.running) {
							return sa.running > sb.running;
						}
					}
					return std::strcmp(sa.display_name, sb.display_name) < 0;
				});
		}

		static void SortStartupIndices(const OptimizeScan::Snapshot& snap,
			std::vector<size_t>& indices)
		{
			indices.clear();
			for (size_t i = 0; i < snap.startups.size(); ++i) {
				if (StartupPassesFilter(snap.startups[i])) {
					indices.push_back(i);
				}
			}
			const auto mode = static_cast<StartupSortMode>(g_startup_sort);
			std::sort(indices.begin(), indices.end(),
				[&](size_t a, size_t b) {
					const auto& ea = snap.startups[a];
					const auto& eb = snap.startups[b];
					if (mode == StartupSortMode::EnabledFirst) {
						if (ea.enabled != eb.enabled) {
							return ea.enabled > eb.enabled;
						}
					}
					if (mode == StartupSortMode::ImpactDesc) {
						if (ea.impact_tier != eb.impact_tier) {
							return ea.impact_tier > eb.impact_tier;
						}
					}
					return std::strcmp(ea.name_utf8, eb.name_utf8) < 0;
				});
		}

		static const char* SubTabLabel(SubTab t)
		{
			switch (t) {
			case SubTab::Overview: return I18N(u8"概覽");
			case SubTab::Startup: return I18N(u8"啟動項");
			case SubTab::Services: return I18N(u8"服務");
			case SubTab::System: return I18N(u8"系統效能");
			case SubTab::Network: return I18N(u8"網路");
			case SubTab::Storage: return I18N(u8"儲存設定");
			default: return "";
			}
		}

		static const char* VisualFxLabel(int setting)
		{
			switch (setting) {
			case 0: return I18N(u8"由系統決定");
			case 1: return I18N(u8"最佳外觀");
			case 2: return I18N(u8"最佳效能");
			case 3: return I18N(u8"自訂");
			default: return I18N(u8"未知");
			}
		}

		static const char* ServiceStartLabel(uint32_t start_type)
		{
			switch (start_type) {
			case SERVICE_AUTO_START: return I18N(u8"自動");
			case SERVICE_DEMAND_START: return I18N(u8"手動");
			case SERVICE_DISABLED: return I18N(u8"已停用");
			case 5: return I18N(u8"延遲自動");
			default: return "—";
			}
		}

		static const char* CleanPresetDisplayLabel(const char* preset_id)
		{
			if (preset_id == nullptr) {
				return I18N(u8"一般");
			}
			if (std::strcmp(preset_id, "gamer") == 0) {
				return I18N(u8"玩家");
			}
			if (std::strcmp(preset_id, "developer") == 0) {
				return I18N(u8"開發者");
			}
			if (std::strcmp(preset_id, "general") == 0) {
				return I18N(u8"一般");
			}
			if (std::strcmp(preset_id, "advanced") == 0 || std::strcmp(preset_id, "bleachbit") == 0) {
				return I18N(u8"進階");
			}
			return preset_id;
		}

		static void DrawGradientHLine(ImDrawList* dl, ImVec2 p0, ImVec2 p1)
		{
			const ImU32 left = ImGui::GetColorU32(ImVec4(0.f, 0.35f, 0.35f, 0.f));
			const ImU32 mid = ImGui::GetColorU32(cyan_neon());
			const ImU32 right = ImGui::GetColorU32(ImVec4(0.f, 0.35f, 0.35f, 0.f));
			const float mid_x = (p0.x + p1.x) * 0.5f;
			dl->AddRectFilledMultiColor(p0, ImVec2(mid_x, p1.y), left, mid, mid, left);
			dl->AddRectFilledMultiColor(ImVec2(mid_x, p0.y), p1, mid, right, right, mid);
		}

		static bool CyberTextButton(const char* id_suffix, const ImRect& bb, const char* label,
			bool accent = false, bool advance_layout = true)
		{
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window->SkipItems) {
				return false;
			}
			const ImGuiID id = window->GetID(id_suffix);
			if (!ImGui::ItemAdd(bb, id)) {
				if (advance_layout) {
					ImGui::ItemSize(bb.GetSize());
				}
				return false;
			}
			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
			ImDrawList* dl = window->DrawList;
			const ImU32 border_col = ImGui::GetColorU32(hovered || held ? cyan_neon() : cyan_dark());
			const ImU32 bg_col = ImGui::GetColorU32(held ? active_bg()
				: (hovered ? hover_bg() : header_bg()));
			dl->AddRectFilled(bb.Min, bb.Max, bg_col, kRounding);
			dl->AddRect(bb.Min, bb.Max, border_col, kRounding, 0, 1.f);
			const ImVec2 ts = ImGui::CalcTextSize(label);
			const ImVec2 tp(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
				bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f);
			const ImU32 text_col = (accent && hovered)
				? ImGui::GetColorU32(cyan_neon()) : ImGui::GetColorU32(ImGuiCol_Text);
			dl->AddText(tp, text_col, label);
			if (advance_layout) {
				ImGui::ItemSize(bb.GetSize());
			}
			return pressed;
		}

		static bool CyberStartupSwitch(const char* id_suffix, const ImRect& bb, bool on)
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
				ImGui::GetColorU32(hovered || held ? cyan_neon() : cyan_dark()), radius, 0, 1.2f);
			const float pad = 3.f;
			const float thumb_r = radius - pad;
			const float thumb_x = on
				? (bb.Max.x - radius)
				: (bb.Min.x + radius);
			const ImVec2 thumb_center(thumb_x, bb.Min.y + radius);
			dl->AddCircleFilled(thumb_center, thumb_r,
				ImGui::GetColorU32(on ? cyan_neon() : ImVec4(0.55f, 0.6f, 0.65f, 1.f)));
			if (on) {
				dl->AddCircleFilled(thumb_center, thumb_r + 2.f,
					ImGui::GetColorU32(ImVec4(0.f, 0.85f, 0.9f, 0.25f)));
			}
			return pressed;
		}

		static void DrawRowIcon(ImDrawList* dl, const ImRect& icon_bb, unsigned long long tex)
		{
			if (tex != 0) {
				dl->AddImage((ImTextureID)(intptr_t)tex, icon_bb.Min, icon_bb.Max);
				return;
			}
			if (Logo::HP_Cleaner_Logo.texture != 0) {
				dl->AddImage((ImTextureID)(intptr_t)Logo::HP_Cleaner_Logo.texture,
					icon_bb.Min, icon_bb.Max);
				return;
			}
			dl->AddRectFilled(icon_bb.Min, icon_bb.Max,
				ImGui::GetColorU32(ImVec4(0.12f, 0.14f, 0.16f, 1.f)), kRounding);
		}

		static void BeginCyberTooltip()
		{
			ImGui::PushStyleColor(ImGuiCol_PopupBg, panel_bg());
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding);
			ImGui::BeginTooltip();
		}

		static void EndCyberTooltip()
		{
			ImGui::EndTooltip();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
		}

		static void TooltipTitle(const char* text)
		{
			ImGui::TextColored(cyan_neon(), "%s", text);
			ImGui::Spacing();
		}

		static void TooltipSection(const char* heading, const char* body)
		{
			if (body == nullptr || body[0] == '\0') {
				return;
			}
			ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.95f, 1.f), "%s", heading);
			ImGui::PushTextWrapPos(460.f);
			ImGui::TextUnformatted(UiTxt(body));
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
		}

		static void TooltipKv(const char* key, const char* value)
		{
			if (value == nullptr || value[0] == '\0') {
				return;
			}
			ImGui::TextDisabled("%s", key);
			ImGui::SameLine(120.f);
			ImGui::TextUnformatted(UiTxt(value));
		}

		static void DrawStartupCyberTooltip(const OptimizeScan::StartupEntry& e)
		{
			BeginCyberTooltip();
			const char* title = e.file_description_utf8[0] != '\0'
				? e.file_description_utf8 : e.name_utf8;
			TooltipTitle(title);
			char impact_line[160] = {};
			snprintf(impact_line, sizeof(impact_line), "%s — %s",
				I18N(OptimizeScan::StartupImpactLabel(e.impact_tier)),
				e.boot_impact_utf8[0] ? UiTxt(e.boot_impact_utf8) : "—");
			TooltipSection(I18N(u8"啟動速度影響"), impact_line);
			TooltipSection(I18N(u8"一般影響"), e.impact_utf8);
			TooltipSection(I18N(u8"操作"), e.how_to_utf8);
			ImGui::Separator();
			TooltipKv(I18N(u8"登錄名稱"), e.name_utf8);
			TooltipKv(I18N(u8"來源"), e.source_label);
			TooltipKv(I18N(u8"產品"), e.product_utf8);
			TooltipKv(I18N(u8"發行者"), e.publisher_utf8);
			TooltipKv(I18N(u8"狀態"), e.enabled ? I18N(u8"開機啟動") : I18N(u8"已停用"));
			TooltipKv(I18N(u8"路徑"), e.exe_path_utf8);
			EndCyberTooltip();
		}

		static void DrawServiceCyberTooltip(const OptimizeScan::ServiceEntry& s)
		{
			BeginCyberTooltip();
			char title[192] = {};
			snprintf(title, sizeof(title), "%s（%s）", s.display_name, s.service_name);
			TooltipTitle(title);
			if (s.recommended_disable && s.role_utf8[0] != '\0') {
				ImGui::TextColored(ImVec4(1.f, 0.65f, 0.4f, 1.f), I18N(u8"★ 常見可停用（詳見下方說明）"));
				ImGui::Spacing();
			}
			TooltipSection(I18N(u8"用途"), s.role_utf8);
			TooltipSection(I18N(u8"停用後"), s.disable_effect_utf8);
			TooltipSection(I18N(u8"注意"), s.risk_note_utf8);
			TooltipSection(I18N(u8"開機／效能"), s.boot_impact_utf8);
			TooltipSection(I18N(u8"操作"), s.how_to_utf8);
			ImGui::Separator();
			const char* run = s.exists ? (s.running ? I18N(u8"執行中") : I18N(u8"已停止")) : I18N(u8"未安裝");
			TooltipKv(I18N(u8"狀態"), run);
			TooltipKv(I18N(u8"啟動類型"), ServiceStartLabel(s.start_type));
			TooltipKv(I18N(u8"發行者"), s.publisher_utf8);
			TooltipKv(I18N(u8"執行檔"), s.binary_path_utf8);
			EndCyberTooltip();
		}

		static ImVec2 DrawBadgeRow(ImDrawList* dl, ImVec2 pos, const char* text,
			const ImVec4& bg, const ImVec4& fg)
		{
			if (text == nullptr || text[0] == '\0') {
				return ImVec2(0.f, 0.f);
			}
			const ImVec2 ts = ImGui::CalcTextSize(text);
			const ImRect bb(pos, ImVec2(pos.x + ts.x + 16.f, pos.y + ts.y + 8.f));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bg), 3.f);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(fg), 3.f, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 4.f), ImGui::GetColorU32(fg), text);
			return ImVec2(bb.GetWidth() + 6.f, bb.GetHeight());
		}

		static bool CyberSubTabButton(const char* id_suffix, const ImRect& bb, const char* label,
			bool selected)
		{
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImGuiID id = window->GetID(id_suffix);
			ImGui::ItemAdd(bb, id);
			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
			ImDrawList* dl = window->DrawList;
			const ImVec4 bg_col = selected ? active_bg()
				: (hovered ? hover_bg() : ImVec4(0.f, 0.f, 0.f, 0.f));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bg_col), kRounding);
			if (selected || hovered) {
				dl->AddRect(bb.Min, bb.Max,
					ImGui::GetColorU32(selected ? cyan_neon() : cyan_dark()), kRounding, 0, 1.f);
			}
			const ImVec2 ts = ImGui::CalcTextSize(label);
			const ImVec2 tp(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
				bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f);
			const ImU32 text_col = selected
				? ImGui::GetColorU32(cyan_neon()) : ImGui::GetColorU32(ImGuiCol_Text);
			dl->AddText(tp, text_col, label);
			if (selected) {
				const float y = bb.Max.y - 2.f;
				DrawGradientHLine(dl, ImVec2(bb.Min.x + 6.f, y), ImVec2(bb.Max.x - 6.f, y + 2.f));
			}
			return pressed;
		}

		static void CyberProgressBar(const ImRect& bb, float fraction, const char* overlay)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(track_bg()), kRounding);
			const float f = ImClamp(fraction, 0.f, 1.f);
			if (f > 0.001f) {
				const ImVec2 fill_max(bb.Min.x + bb.GetWidth() * f, bb.Max.y);
				dl->AddRectFilled(bb.Min, fill_max, ImGui::GetColorU32(cyan_mid()), kRounding);
			}
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			if (overlay != nullptr && overlay[0] != '\0') {
				const ImVec2 ts = ImGui::CalcTextSize(overlay);
				dl->AddText(ImVec2(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
					bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f),
					ImGui::GetColorU32(ImGuiCol_Text), overlay);
			}
		}

		static bool BeginCyberPanel(const char* id, float height = 0.f)
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, panel_bg());
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
			ImGuiChildFlags child_flags = ImGuiChildFlags_Borders;
			ImVec2 sz(0.f, height);
			if (height <= 0.f) {
				child_flags |= ImGuiChildFlags_AutoResizeY;
				sz = ImVec2(0.f, 0.f);
			}
			const bool open = ImGui::BeginChild(id, sz, child_flags);
			if (open) {
				const ImVec2 p0 = ImGui::GetWindowPos();
				const ImVec2 p1 = p0 + ImGui::GetWindowSize();
				ImGui::GetWindowDrawList()->AddRect(p0, p1, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			}
			return open;
		}

		static void EndCyberPanel()
		{
			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		static float StatTileContentHeight(const char* value, float col_w)
		{
			const float pad = 8.f;
			const float inner_w = ImMax(20.f, col_w - pad * 2.f);
			const ImVec2 val_sz = ImGui::CalcTextSize(value, nullptr, true, inner_w);
			return pad + ImGui::GetTextLineHeight() + 4.f + val_sz.y + pad;
		}

		static void DrawStatTile(const ImRect& bb, const char* title,
			const char* value, const ImVec4& value_color)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);

			const float pad = 8.f;
			const ImVec2 title_pos(bb.Min.x + pad, bb.Min.y + pad);
			dl->AddText(title_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), title);

			const ImVec2 val_min(bb.Min.x + pad, title_pos.y + ImGui::GetTextLineHeight() + 4.f);
			const ImVec2 val_max(bb.Max.x - pad, bb.Max.y - pad);
			ImGui::PushClipRect(val_min, val_max, true);
			ImGui::PushStyleColor(ImGuiCol_Text, value_color);
			ImGui::RenderTextClipped(val_min, val_max, value, nullptr, nullptr, ImVec2(0.f, 0.f), nullptr);
			ImGui::PopStyleColor();
			ImGui::PopClipRect();
		}

		static const char* TagIf(bool cond, const char* tag)
		{
			return cond ? tag : nullptr;
		}

		static void RecommendTagStyle(const char* tag, ImVec4& bg, ImVec4& fg)
		{
			bg = ImVec4(0.14f, 0.16f, 0.18f, 1.f);
			fg = ImVec4(0.75f, 0.8f, 0.85f, 1.f);
			if (tag == nullptr || tag[0] == '\0') {
				return;
			}
			if (strcmp(tag, u8"建議關閉") == 0) {
				bg = ImVec4(0.28f, 0.14f, 0.06f, 1.f);
				fg = ImVec4(1.f, 0.72f, 0.38f, 1.f);
			}
			else if (strcmp(tag, u8"建議開啟") == 0) {
				bg = ImVec4(0.08f, 0.22f, 0.18f, 1.f);
				fg = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			}
			else if (strcmp(tag, u8"遊戲建議") == 0) {
				bg = ImVec4(0.06f, 0.20f, 0.22f, 1.f);
				fg = ImVec4(0.0f, 0.90f, 0.90f, 1.f);
			}
			else if (strcmp(tag, u8"省電建議") == 0) {
				bg = ImVec4(0.20f, 0.18f, 0.06f, 1.f);
				fg = ImVec4(1.f, 0.90f, 0.40f, 1.f);
			}
			else if (strcmp(tag, u8"辦公建議") == 0) {
				bg = ImVec4(0.10f, 0.14f, 0.22f, 1.f);
				fg = ImVec4(0.70f, 0.82f, 1.f, 1.f);
			}
			else if (strcmp(tag, u8"可選調校") == 0) {
				bg = ImVec4(0.16f, 0.18f, 0.22f, 1.f);
				fg = ImVec4(0.82f, 0.86f, 0.92f, 1.f);
			}
		}

		static float DrawBadgeStacked(ImDrawList* dl, ImVec2 pos, const char* text,
			const ImVec4& bg, const ImVec4& fg)
		{
			if (text == nullptr || text[0] == '\0') {
				return 0.f;
			}
			const ImVec2 ts = ImGui::CalcTextSize(text);
			const float h = ts.y + 8.f;
			const ImRect pill(pos, ImVec2(pos.x + ts.x + 16.f, pos.y + h));
			dl->AddRectFilled(pill.Min, pill.Max, ImGui::GetColorU32(bg), 3.f);
			dl->AddRect(pill.Min, pill.Max, ImGui::GetColorU32(fg), 3.f, 0, 1.f);
			dl->AddText(ImVec2(pill.Min.x + 8.f, pill.Min.y + 4.f),
				ImGui::GetColorU32(fg), text);
			return h + 5.f;
		}

		static void DrawKeyValueRow(const char* key, const char* value, const ImVec4* value_col = nullptr)
		{
			const float w = ImGui::GetContentRegionAvail().x;
			const float row_h = 26.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), key);
			const ImVec2 vs = ImGui::CalcTextSize(value);
			const ImU32 vc = value_col != nullptr
				? ImGui::GetColorU32(*value_col) : ImGui::GetColorU32(ImGuiCol_Text);
			dl->AddText(ImVec2(pos.x + w - vs.x, pos.y), vc, value);
		}

		static const char* OnOffText(bool on)
		{
			return on ? I18N(u8"開啟") : I18N(u8"關閉");
		}

		static ImVec4 OnOffColor(bool on)
		{
			return on
				? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
				: ImVec4(0.65f, 0.68f, 0.72f, 1.f);
		}

		static void DrawSystemStatusStrip(const OptimizeScan::Snapshot& snap)
		{
			struct StatusItem {
				const char* title;
				char value[40];
				ImVec4 color;
			};
			StatusItem items[12] = {};
			strncpy_s(items[0].value, snap.power_plan_name[0] ? UiTxt(snap.power_plan_name) : "—",
				_TRUNCATE);
			items[0].title = I18N(u8"電源");
			items[0].color = cyan_neon();

			strncpy_s(items[1].value, OnOffText(snap.game_mode_on), _TRUNCATE);
			items[1].title = I18N(u8"遊戲模式");
			items[1].color = ImVec4(0.85f, 0.95f, 1.f, 1.f);

			strncpy_s(items[2].value, VisualFxLabel(snap.visual_fx_setting), _TRUNCATE);
			items[2].title = I18N(u8"視覺效果");
			items[2].color = ImVec4(0.75f, 0.9f, 1.f, 1.f);

			snprintf(items[3].value, sizeof(items[3].value), I18N(u8"透明%s 動畫%s"),
				OnOffText(snap.transparency_on), OnOffText(snap.animations_on));
			items[3].title = I18N(u8"介面動畫");
			items[3].color = ImVec4(0.75f, 0.9f, 1.f, 1.f);

			snprintf(items[4].value, sizeof(items[4].value), "%s / GPU%s",
				snap.processor_foreground ? I18N(u8"前景優先") : I18N(u8"系統預設"),
				OnOffText(snap.gpu_scheduling_on));
			items[4].title = I18N(u8"處理器");
			items[4].color = ImVec4(1.f, 0.78f, 0.45f, 1.f);

			snprintf(items[5].value, sizeof(items[5].value), I18N(u8"快啟%s 錄影%s"),
				OnOffText(snap.fast_startup_on), OnOffText(snap.game_dvr_on));
			items[5].title = I18N(u8"開機錄影");
			items[5].color = ImVec4(0.55f, 0.9f, 1.f, 1.f);

			if (snap.system_drive_used_percent >= 0.f) {
				snprintf(items[6].value, sizeof(items[6].value), I18N(u8"C: %.0f%% 已用"),
					snap.system_drive_used_percent);
			}
			else {
				strncpy_s(items[6].value, "—", _TRUNCATE);
			}
			items[6].title = I18N(u8"系統碟");
			items[6].color = snap.system_drive_used_percent >= 85.f
				? ImVec4(1.f, 0.45f, 0.35f, 1.f) : ImVec4(0.45f, 0.95f, 0.75f, 1.f);

			strncpy_s(items[7].value, OnOffText(snap.background_apps_on), _TRUNCATE);
			items[7].title = I18N(u8"背景應用");
			items[7].color = OnOffColor(snap.background_apps_on);

			strncpy_s(items[8].value,
				snap.power_throttling_on ? I18N(u8"節流中") : I18N(u8"已關閉"), _TRUNCATE);
			items[8].title = I18N(u8"電源節流");
			items[8].color = snap.power_throttling_on
				? ImVec4(0.65f, 0.68f, 0.72f, 1.f)
				: ImVec4(0.45f, 0.95f, 0.75f, 1.f);

			const float gap = 6.f;
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float col_w = ImMax(80.f, (w - gap * 2.f) / 3.f);
			strncpy_s(items[9].value, OnOffText(snap.delivery_p2p_on), _TRUNCATE);
			items[9].title = I18N(u8"更新 P2P");
			items[9].color = OnOffColor(!snap.delivery_p2p_on);

			strncpy_s(items[10].value, OnOffText(snap.search_highlights_on), _TRUNCATE);
			items[10].title = I18N(u8"搜尋亮點");
			items[10].color = OnOffColor(!snap.search_highlights_on);

			strncpy_s(items[11].value, snap.fast_menu_delay ? I18N(u8"即時") : I18N(u8"預設"), _TRUNCATE);
			items[11].title = I18N(u8"選單延遲");
			items[11].color = snap.fast_menu_delay
				? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
				: ImVec4(0.65f, 0.68f, 0.72f, 1.f);

			const int item_count = 12;
			const int cols = 3;
			const int rows = (item_count + cols - 1) / cols;

			for (int row = 0; row < rows; ++row) {
				if (row > 0) {
					ImGui::Dummy(ImVec2(0.f, gap));
				}
				float row_h = 0.f;
				for (int c = 0; c < cols; ++c) {
					const int idx = row * cols + c;
					if (idx >= item_count) {
						break;
					}
					row_h = ImMax(row_h, StatTileContentHeight(items[idx].value, col_w));
				}
				for (int c = 0; c < cols; ++c) {
					const int idx = row * cols + c;
					if (idx >= item_count) {
						break;
					}
					if (c > 0) {
						ImGui::SameLine(0.f, gap);
					}
					const ImVec2 p = ImGui::GetCursorScreenPos();
					DrawStatTile(ImRect(p, ImVec2(p.x + col_w, p.y + row_h)),
						items[idx].title, items[idx].value, items[idx].color);
					ImGui::Dummy(ImVec2(col_w, row_h));
				}
			}
		}

		struct SystemActionBtn {
			const char* id;
			const char* label;
			bool accent;
		};

		static bool DrawSystemToolbarButton(const char* id, const char* label, bool accent,
			float width = 92.f)
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const ImRect bb(p, ImVec2(p.x + width, p.y + 28.f));
			return CyberTextButton(id, bb, label, accent);
		}

		static int DrawSystemSettingCard(const char* title, const char* hint,
			const char* status_text, const ImVec4& status_color,
			const SystemActionBtn* buttons, int button_count,
			const char* recommend_tag = nullptr)
		{
			const float outer_w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, card_bg());
			ImGui::PushStyleColor(ImGuiCol_Border, cyan_dark());
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 6.f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 3.f));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kRounding);
			ImGui::PushID(title);
			ImGui::BeginChild("##card", ImVec2(0.f, 0.f),
				ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

			const float row_start_x = ImGui::GetCursorPosX();
			const float row_avail_w = ImGui::GetContentRegionAvail().x;
			ImGui::TextColored(cyan_neon(), "%s", title);
			if (recommend_tag != nullptr && recommend_tag[0] != '\0') {
				ImGui::SameLine(0.f, 8.f);
				ImVec4 tag_bg;
				ImVec4 tag_fg;
				RecommendTagStyle(recommend_tag, tag_bg, tag_fg);
				const char* tag_txt = UiTxt(recommend_tag);
				const ImVec2 tag_ts = ImGui::CalcTextSize(tag_txt);
				const float tag_w = tag_ts.x + 14.f;
				const float tag_h = tag_ts.y + 6.f;
				const ImVec2 tag_pos = ImGui::GetCursorScreenPos();
				DrawBadgeStacked(ImGui::GetWindowDrawList(), tag_pos, tag_txt, tag_bg, tag_fg);
				ImGui::Dummy(ImVec2(tag_w, tag_h));
			}
			if (status_text != nullptr && status_text[0] != '\0') {
				const char* status_txt = UiTxt(status_text);
				const ImVec2 badge_ts = ImGui::CalcTextSize(status_txt);
				const float badge_w = badge_ts.x + 16.f;
				const float badge_h = badge_ts.y + 8.f;
				ImGui::SameLine(0.f, 0.f);
				ImGui::SetCursorPosX(row_start_x + row_avail_w - badge_w);
				const ImVec2 badge_pos = ImGui::GetCursorScreenPos();
				DrawBadgeStacked(ImGui::GetWindowDrawList(), badge_pos, status_txt,
					ImVec4(0.12f, 0.18f, 0.2f, 1.f), status_color);
				ImGui::Dummy(ImVec2(badge_w, badge_h));
			}

			ImGui::PushTextWrapPos(row_start_x + row_avail_w);
			ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", hint);
			ImGui::PopTextWrapPos();

			int max_cols = button_count;
			if (outer_w < 380.f) {
				max_cols = ImMin(2, button_count);
			}
			else if (outer_w < 560.f) {
				max_cols = ImMin(3, button_count);
			}
			max_cols = ImMax(1, max_cols);

			const float btn_h = 26.f;
			const float btn_gap = 6.f;
			const float inner_w = ImGui::GetContentRegionAvail().x;
			float btn_w = (inner_w - static_cast<float>(max_cols - 1) * btn_gap)
				/ static_cast<float>(max_cols);
			btn_w = ImClamp(btn_w, 68.f, 120.f);

			int clicked = -1;
			for (int i = 0; i < button_count; ++i) {
				if (i > 0 && (i % max_cols) != 0) {
					ImGui::SameLine(0.f, btn_gap);
				}
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const ImRect btn_bb(p, ImVec2(p.x + btn_w, p.y + btn_h));
				if (CyberTextButton(buttons[i].id, btn_bb, buttons[i].label, buttons[i].accent)) {
					clicked = i;
				}
			}

			ImGui::EndChild();
			ImGui::PopID();
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(2);
			ImGui::Dummy(ImVec2(0.f, 3.f));
			return clicked;
		}

		static void OpenSysInfoPopup(const char* title, const char* body)
		{
			if (title != nullptr) {
				strncpy_s(g_sys_info_popup_title, title, _TRUNCATE);
			}
			else {
				g_sys_info_popup_title[0] = '\0';
			}
			if (body != nullptr) {
				strncpy_s(g_sys_info_popup_body, body, _TRUNCATE);
			}
			else {
				g_sys_info_popup_body[0] = '\0';
			}
			g_sys_info_popup_open = true;
		}

		static bool SysTryAdmin(const char* action_label)
		{
			if (HCleanHasElevatedAccess()) {
				return true;
			}
			if (HAdminPrompt::TryGate(HAdminPrompt::Scene::Optimize)) {
				return true;
			}
			if (HAdminPrompt::IsModalOpen()) {
				return false;
			}
			char body[512] = {};
			char head[160] = {};
			snprintf(head, sizeof(head), I18N(u8"「%s」需要系統管理員權限。\n\n"),
				action_label != nullptr ? action_label : I18N(u8"此操作"));
			snprintf(body, sizeof(body), "%s%s%s", head,
				I18N(u8"原因：Windows 不允許一般使用者直接修改電源計畫、休眠、處理器排程等系統設定。\n\n"),
				I18N(u8"請在權限視窗選擇「以管理員重新啟動」，或以管理員身分執行 HP CLEANER++。"));
			OpenSysInfoPopup(I18N(u8"需要管理員權限"), body);
			return false;
		}

		static bool SysReportResult(const char* action_label, bool ok)
		{
			if (ok) {
				return true;
			}
			const char* msg = OptimizeScan::GetLastActionMessage();
			HLOG_WARN("OptimizePage: '{}' 失敗：{}",
				action_label != nullptr ? action_label : I18N(u8"操作"),
				(msg != nullptr && msg[0] != '\0') ? msg : I18N(u8"系統拒絕變更或目前環境不支援"));
			char body[640] = {};
			snprintf(body, sizeof(body),
				I18N(u8"「%s」未能完成。\n\n原因：%s"),
				action_label != nullptr ? action_label : I18N(u8"操作"),
				(msg != nullptr && msg[0] != '\0') ? UiTxt(msg) : I18N(u8"系統拒絕變更或目前環境不支援。"));
			OpenSysInfoPopup(I18N(u8"無法完成操作"), body);
			return false;
		}

		static void SysSetAllSectionsOpen(bool open)
		{
			g_sys_open_status = open;
			g_sys_open_power = open;
			g_sys_open_visual = open;
			g_sys_open_game = open;
			g_sys_open_input = open;
			g_sys_open_adv = open;
			g_sys_open_bg = open;
			g_sys_open_tune = open;
			g_sys_open_tools = open;
		}

		static bool BeginSysCollapsibleSection(const char* id, const char* title, bool* open)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, card_bg());
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hover_bg());
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, active_bg());
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 6.f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kRounding);
			const bool expanded = ImGui::CollapsingHeader(title, open, ImGuiTreeNodeFlags_FramePadding);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
			if (!expanded) {
				return false;
			}
			ImGui::PushID(id);
			return true;
		}

		static void EndSysCollapsibleSection()
		{
			ImGui::PopID();
		}

		static void CenterModalButtons(float total_width)
		{
			const float region_w = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.f, (region_w - total_width) * 0.5f));
		}

		static void DrawSysInfoPopupModal()
		{
			if (g_sys_info_popup_open) {
				ImGui::OpenPopup("##sys_info_popup");
			}
			const ImVec2 disp = ImGui::GetIO().DisplaySize;
			const float max_w = ImMin(440.f, disp.x - 64.f);
			ImGui::SetNextWindowSize(ImVec2(max_w, 0.f), ImGuiCond_Always);
			ImGui::SetNextWindowPos(disp * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal("##sys_info_popup", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
				return;
			}
			ImGui::TextColored(cyan_neon(), "%s",
				g_sys_info_popup_title[0] ? g_sys_info_popup_title : I18N(u8"提示"));
			ImGui::Spacing();
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + max_w - 32.f);
			ImGui::TextWrapped("%s",
				g_sys_info_popup_body[0] ? g_sys_info_popup_body : I18N(u8"目前無法執行此操作。"));
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			const float ok_w = 108.f;
			CenterModalButtons(ok_w);
			if (ImGui::Button(I18N(u8"知道了"), ImVec2(ok_w, 28.f))) {
				g_sys_info_popup_open = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		static ImVec2 DrawBadgePill(ImDrawList* dl, ImVec2 pos, const char* text,
			const ImVec4& bg, const ImVec4& fg)
		{
			const ImVec2 ts = ImGui::CalcTextSize(text);
			const ImRect bb(pos, ImVec2(pos.x + ts.x + 16.f, pos.y + ts.y + 8.f));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bg), 3.f);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(fg), 3.f, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 4.f), ImGui::GetColorU32(fg), text);
			return ImVec2(bb.GetWidth() + 6.f, 0.f);
		}

		static void ImpactBadgeStyle(int tier, ImVec4& bg, ImVec4& fg, char* label, size_t label_sz)
		{
			bg = ImVec4(0.2f, 0.22f, 0.25f, 1.f);
			fg = ImVec4(0.75f, 0.8f, 0.85f, 1.f);
			strncpy_s(label, label_sz, I18N(u8"影響 ?"), _TRUNCATE);
			switch (static_cast<OptimizeScan::StartupImpactTier>(tier)) {
			case OptimizeScan::StartupImpactTier::High:
				bg = ImVec4(0.35f, 0.12f, 0.08f, 1.f);
				fg = ImVec4(1.f, 0.55f, 0.35f, 1.f);
				strncpy_s(label, label_sz, I18N(u8"高影響"), _TRUNCATE);
				break;
			case OptimizeScan::StartupImpactTier::Medium:
				bg = ImVec4(0.28f, 0.22f, 0.06f, 1.f);
				fg = ImVec4(1.f, 0.85f, 0.35f, 1.f);
				strncpy_s(label, label_sz, I18N(u8"中影響"), _TRUNCATE);
				break;
			case OptimizeScan::StartupImpactTier::Low:
				bg = ImVec4(0.08f, 0.22f, 0.18f, 1.f);
				fg = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				strncpy_s(label, label_sz, I18N(u8"低影響"), _TRUNCATE);
				break;
			case OptimizeScan::StartupImpactTier::None:
				bg = ImVec4(0.14f, 0.16f, 0.18f, 1.f);
				fg = ImVec4(0.6f, 0.65f, 0.7f, 1.f);
				strncpy_s(label, label_sz, I18N(u8"無影響"), _TRUNCATE);
				break;
			default:
				break;
			}
		}

		static void DrawCyberSearchField(const char* id, const char* hint, char* buf, size_t buf_sz)
		{
			const float w = ImGui::GetContentRegionAvail().x;
			const ImVec2 box_min = ImGui::GetCursorScreenPos();
			const float box_h = 36.f;
			const ImRect box_bb(box_min, ImVec2(box_min.x + w, box_min.y + box_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(box_bb.Min, box_bb.Max, ImGui::GetColorU32(header_bg()), kRounding);
			dl->AddRect(box_bb.Min, box_bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.2f);
			dl->AddText(ImVec2(box_bb.Min.x + 10.f, box_bb.Min.y + 10.f),
				ImGui::GetColorU32(cyan_neon()), I18N(u8"搜"));
			ImGui::SetCursorScreenPos(ImVec2(box_bb.Min.x + 34.f, box_bb.Min.y + 7.f));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::SetNextItemWidth(w - 44.f);
			ImGui::InputTextWithHint(id, hint, buf, buf_sz);
			ImGui::PopStyleColor(2);
			ImGui::SetCursorScreenPos(ImVec2(box_min.x, box_min.y + box_h + 8.f));
		}

		static void DrawSubTabBar()
		{
			const float gap = 6.f;
			const int tab_count = static_cast<int>(SubTab::Count);
			const float bar_h = kSubTabH + 8.f;
			const float avail_w = ImGui::GetContentRegionAvail().x;
			const ImVec2 outer_start = ImGui::GetCursorScreenPos();

			if (ImGui::BeginChild("##opt_subtab_scroll", ImVec2(avail_w, bar_h), false,
				ImGuiWindowFlags_HorizontalScrollbar)) {
				float x = 0.f;
				for (int i = 0; i < tab_count; ++i) {
					const char* label = SubTabLabel(static_cast<SubTab>(i));
					const ImVec2 ts = ImGui::CalcTextSize(label);
					const float tw = ts.x + 28.f;
					const ImVec2 row_start = ImGui::GetCursorScreenPos();
					const ImRect bb(ImVec2(row_start.x + x, row_start.y),
						ImVec2(row_start.x + x + tw, row_start.y + kSubTabH));
					char id[48] = {};
					snprintf(id, sizeof(id), "##opt_subtab_%d", i);
					if (CyberSubTabButton(id, bb, label, g_active_subtab == i)) {
						g_active_subtab = i;
					}
					x += tw + gap;
				}
				ImGui::Dummy(ImVec2(x, kSubTabH));
			}
			ImGui::EndChild();

			const ImVec2 line_y(outer_start.x, outer_start.y + kSubTabH + 4.f);
			DrawGradientHLine(ImGui::GetWindowDrawList(), line_y,
				ImVec2(outer_start.x + avail_w, line_y.y + 2.f));
			ImGui::Spacing();
		}

		static void DrawTopStatusBar(OptimizeScan::Snapshot& snap)
		{
			const float avail_w = ImGui::GetContentRegionAvail().x;
			const ImVec2 row_start = ImGui::GetCursorScreenPos();
			const float scan_w = 108.f;
			const float prog_w = ImClamp(avail_w - scan_w - 140.f, 120.f, 280.f);
			const bool narrow = avail_w < 480.f;
			const float bar_h = narrow ? (kTopBarH + 18.f) : kTopBarH;
			ImGui::Dummy(ImVec2(avail_w, bar_h));

			const ImRect scan_bb(row_start, ImVec2(row_start.x + scan_w, row_start.y + 30.f));
			if (CyberTextButton("##opt_scan", scan_bb, I18N(u8"優化掃描"), true, false)) {
				OptimizeScan::RequestScan();
			}

			const ImRect prog_bb(ImVec2(scan_bb.Max.x + 10.f, row_start.y),
				ImVec2(scan_bb.Max.x + 10.f + prog_w, row_start.y + 30.f));
			char prog_overlay[128] = {};
			if (snap.scanning) {
				snprintf(prog_overlay, sizeof(prog_overlay), "%.0f%% %s",
					snap.progress * 100.f, UiTxt(snap.status_text));
				CyberProgressBar(prog_bb, snap.progress, prog_overlay);
			}
			else if (snap.valid) {
				CyberProgressBar(prog_bb, 1.f, UiTxt(snap.status_text));
			}
			else {
				CyberProgressBar(prog_bb, 0.f, I18N(u8"尚未掃描"));
			}

			char pending_buf[32] = {};
			snprintf(pending_buf, sizeof(pending_buf), I18N(u8"待處理 %d"), snap.pending_suggestions);
			const ImVec2 ps = ImGui::CalcTextSize(pending_buf);
			const float pill_x = narrow ? row_start.x : (prog_bb.Max.x + 12.f);
			const float pill_y = narrow ? (row_start.y + 34.f) : (row_start.y + 4.f);
			const ImRect pill_bb(
				ImVec2(pill_x, pill_y),
				ImVec2(pill_x + ps.x + 20.f, pill_y + 22.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec4 pill_bg = snap.pending_suggestions > 0
				? ImVec4(0.12f, 0.28f, 0.28f, 1.f) : header_bg();
			dl->AddRectFilled(pill_bb.Min, pill_bb.Max, ImGui::GetColorU32(pill_bg), kRounding);
			dl->AddRect(pill_bb.Min, pill_bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			const ImU32 pill_text = snap.pending_suggestions > 0
				? ImGui::GetColorU32(cyan_neon()) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
			dl->AddText(ImVec2(pill_bb.Min.x + 10.f, pill_bb.Min.y + 4.f), pill_text, pending_buf);

			const char* last = OptimizeScan::GetLastActionMessage();
			if (last != nullptr && last[0] != '\0') {
				const float msg_y = narrow ? (pill_bb.Max.y + 4.f) : (row_start.y + 34.f);
				ImGui::SetCursorScreenPos(ImVec2(row_start.x, msg_y));
				ImGui::PushTextWrapPos(row_start.x + avail_w);
				ImGui::TextColored(cyan_mid(), "%s", UiTxt(last));
				ImGui::PopTextWrapPos();
			}
			ImGui::Spacing();
		}

		static void DrawApplyConfirmModal()
		{
			if (g_show_apply_confirm) {
				ImGui::OpenPopup("##optimize_apply_preset");
			}
			const ImVec2 disp = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowSize(ImVec2(ImMin(480.f, disp.x - 48.f), 0.f), ImGuiCond_Always);
			ImGui::SetNextWindowPos(disp * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal("##optimize_apply_preset", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
				return;
			}
			const OptimizeScan::PresetInfo* pinfo = nullptr;
			for (size_t i = 0; i < OptimizeScan::GetPresetCount(); ++i) {
				const OptimizeScan::PresetInfo* p = OptimizeScan::GetPreset(i);
				if (p != nullptr && std::strcmp(p->id, g_pending_apply_preset) == 0) {
					pinfo = p;
					break;
				}
			}
			ImGui::TextColored(cyan_neon(), "%s", I18N(u8"套用優化預設"));
			ImGui::Spacing();
			ImGui::Text(I18N(u8"方案：%s"), pinfo != nullptr ? pinfo->label : g_pending_apply_preset);
			ImGui::PushTextWrapPos(440.f);
			ImGui::TextWrapped("%s", UiTxt(OptimizeScan::GetPresetDescription(g_pending_apply_preset)));
			ImGui::TextWrapped(I18N(u8"不會自動勾選清理任務；服務變更需管理員權限。套用後可於概覽「還原上次優化」。"));
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			ImGui::Checkbox(I18N(u8"套用前建立系統還原點"), &g_apply_create_restore_point);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			const float ok_w = 112.f;
			const float cancel_w = 80.f;
			const float btn_gap = 10.f;
			CenterModalButtons(ok_w + btn_gap + cancel_w);
			if (ImGui::Button(I18N(u8"確認套用"), ImVec2(ok_w, 28.f))) {
				if (HAdminPrompt::TryGate(HAdminPrompt::Scene::Optimize)) {
					OptimizeScan::ApplyPresetWithOptions(g_pending_apply_preset,
						g_apply_create_restore_point);
					OptimizeScan::RequestScan();
					g_show_apply_confirm = false;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine(0, btn_gap);
			if (ImGui::Button(I18N(u8"取消"), ImVec2(cancel_w, 28.f))) {
				g_show_apply_confirm = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		static void DrawOverviewPresetCards()
		{
			const size_t count = OptimizeScan::GetPresetCount();
			const float card_h = 120.f;
			const int cols = ImGui::GetContentRegionAvail().x < 560.f ? 2 : 3;

			ImGui::TextColored(cyan_neon(), "%s", I18N(u8"一鍵預設方案"));
			ImGui::TextDisabled(I18N(u8"套用前可勾選建立還原點；建議先閱讀各方案說明"));
			ImGui::Spacing();

			if (!ImGui::BeginTable("##opt_preset_tbl", cols,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
				return;
			}
			for (size_t i = 0; i < count; ++i) {
				if (i % static_cast<size_t>(cols) == 0) {
					ImGui::TableNextRow(ImGuiTableRowFlags_None, card_h);
				}
				ImGui::TableNextColumn();
				const OptimizeScan::PresetInfo* p = OptimizeScan::GetPreset(i);
				if (p == nullptr) {
					continue;
				}
				const ImVec2 p0 = ImGui::GetCursorScreenPos();
				const float cw = ImMax(40.f, ImGui::GetContentRegionAvail().x);
				const ImRect bb(p0, ImVec2(p0.x + cw, p0.y + card_h - 4.f));
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
				dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
				dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
					ImGui::GetColorU32(cyan_neon()), UiTxt(p->label));
				dl->PushClipRect(ImVec2(bb.Min.x + 10.f, bb.Min.y + 28.f),
					ImVec2(bb.Max.x - 10.f, bb.Max.y - 40.f), true);
				dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 28.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), UiTxt(p->description));
				dl->PopClipRect();
				const ImRect apply_bb(
					ImVec2(bb.Min.x + 10.f, bb.Max.y - 34.f),
					ImVec2(bb.Max.x - 10.f, bb.Max.y - 8.f));
				char btn_id[64] = {};
				snprintf(btn_id, sizeof(btn_id), "##opt_ov_apply_%s", p->id);
				if (CyberTextButton(btn_id, apply_bb, I18N(u8"套用"), true, false)) {
					strncpy_s(g_pending_apply_preset, p->id, _TRUNCATE);
					g_show_apply_confirm = true;
				}
			}
			ImGui::EndTable();
		}

		static bool DrawOverviewActionCard(const ImRect& bb, const char* btn_id,
			const char* title, const char* sub, const char* btn_label, bool accent)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 12.f, bb.Min.y + 10.f),
				ImGui::GetColorU32(cyan_neon()), title);
			const ImVec2 sub_pos(bb.Min.x + 12.f, bb.Min.y + 30.f);
			dl->PushClipRect(sub_pos, ImVec2(bb.Max.x - 12.f, bb.Max.y - 40.f), true);
			dl->AddText(sub_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
			dl->PopClipRect();
			const ImRect btn_bb(
				ImVec2(bb.Min.x + 12.f, bb.Max.y - 36.f),
				ImVec2(bb.Max.x - 12.f, bb.Max.y - 10.f));
			return CyberTextButton(btn_id, btn_bb, btn_label, accent, false);
		}

		static void DrawOverviewKpiGrid(OptimizeScan::Snapshot& snap)
		{
			constexpr int kCols = 3;
			const float tile_h = 72.f;

			int running_svc = 0;
			int enabled_startup = 0;
			for (const auto& s : snap.services) {
				if (s.running) {
					++running_svc;
				}
			}
			for (const auto& e : snap.startups) {
				if (e.enabled) {
					++enabled_startup;
				}
			}
			const int hi_startup = OptimizeScan::CountHighImpactEnabledStartups(snap);
			const int pending_svc = OptimizeScan::CountRecommendedServicesPending(snap);
			const int est_sec = OptimizeScan::EstimateBootSavingsSeconds(snap);

			struct TileDef {
				const char* title;
				char value[32];
				ImVec4 color;
			};
			TileDef tiles[6] = {};
			snprintf(tiles[0].value, sizeof(tiles[0].value), "%d", snap.pending_suggestions);
			tiles[0].title = I18N(u8"建議待處理");
			tiles[0].color = cyan_neon();

			snprintf(tiles[1].value, sizeof(tiles[1].value), "%d / %zu",
				enabled_startup, snap.startups.size());
			tiles[1].title = I18N(u8"啟動項（已啟用）");
			tiles[1].color = ImVec4(0.85f, 0.95f, 1.f, 1.f);

			snprintf(tiles[2].value, sizeof(tiles[2].value), "%d", hi_startup);
			tiles[2].title = I18N(u8"高影響啟動");
			tiles[2].color = ImVec4(1.f, 0.55f, 0.35f, 1.f);

			snprintf(tiles[3].value, sizeof(tiles[3].value), "%d", pending_svc);
			tiles[3].title = I18N(u8"建議停用服務");
			tiles[3].color = ImVec4(1.f, 0.78f, 0.35f, 1.f);

			snprintf(tiles[4].value, sizeof(tiles[4].value), "%d", running_svc);
			tiles[4].title = I18N(u8"執行中服務");
			tiles[4].color = ImVec4(0.55f, 0.9f, 1.f, 1.f);

			snprintf(tiles[5].value, sizeof(tiles[5].value), I18N(u8"~%d 秒"), est_sec);
			tiles[5].title = I18N(u8"估計可縮短");
			tiles[5].color = ImVec4(0.45f, 0.95f, 0.75f, 1.f);

			const ImGuiTableFlags tbl_flags = ImGuiTableFlags_SizingStretchSame
				| ImGuiTableFlags_PadOuterX;
			if (!ImGui::BeginTable("##opt_kpi_tbl", kCols, tbl_flags)) {
				return;
			}
			for (int i = 0; i < 6; ++i) {
				if (i % kCols == 0) {
					ImGui::TableNextRow(ImGuiTableRowFlags_None, tile_h);
				}
				ImGui::TableNextColumn();
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float cw = ImMax(40.f, ImGui::GetContentRegionAvail().x);
				const ImRect cell_bb(p, ImVec2(p.x + cw, p.y + tile_h - 4.f));
				DrawStatTile(cell_bb, tiles[i].title, tiles[i].value, tiles[i].color);
			}
			ImGui::EndTable();
		}

		static void HandleOverviewAction(const char* btn_id)
		{
			if (btn_id == nullptr) {
				return;
			}
			if (std::strcmp(btn_id, "##ov_svc") == 0) {
				if (HAdminPrompt::TryGate(HAdminPrompt::Scene::Optimize)) {
					OptimizeScan::ApplyRecommendedServices();
				}
			}
			else if (std::strcmp(btn_id, "##ov_startup") == 0) {
				g_active_subtab = static_cast<int>(SubTab::Startup);
			}
			else if (std::strcmp(btn_id, "##ov_store") == 0) {
				g_active_subtab = static_cast<int>(SubTab::Storage);
			}
			else if (std::strcmp(btn_id, "##ov_scan") == 0) {
				OptimizeScan::RequestScan();
			}
			else if (std::strcmp(btn_id, "##ov_revert") == 0) {
				if (HAdminPrompt::TryGate(HAdminPrompt::Scene::Optimize)) {
					OptimizeScan::RevertLastApply();
				}
			}
		}

		static void DrawTabOverview(OptimizeScan::Snapshot& snap)
		{
			const int hi_startup = OptimizeScan::CountHighImpactEnabledStartups(snap);
			const int pending_svc = OptimizeScan::CountRecommendedServicesPending(snap);
			const float action_card_h = 96.f;

			if (BeginCyberPanel("##opt_panel_overview_hero")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"系統優化概覽"));
				ImGui::SameLine(0, 12);
				if (snap.valid && !OptimizeScan::IsScanning()) {
					ImGui::TextDisabled(I18N(u8"· 上次掃描：%zu 啟動 / %zu 服務"),
						snap.startups.size(), snap.services.size());
				}
				else if (OptimizeScan::IsScanning()) {
					ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), I18N(u8"· 正在掃描…"));
				}
				ImGui::TextWrapped(
					I18N(u8"以下為可採取的優化建議；向下捲動可查看快速操作、系統狀態與預設方案。"));
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_overview_kpi")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"關鍵指標"));
				ImGui::Spacing();
				DrawOverviewKpiGrid(snap);
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_overview_actions")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"快速操作"));
				ImGui::Spacing();

				char sub_svc[96] = {};
				snprintf(sub_svc, sizeof(sub_svc), I18N(u8"一鍵停用 %d 項建議背景服務"), pending_svc);
				char sub_su[96] = {};
				snprintf(sub_su, sizeof(sub_su), I18N(u8"檢視 %zu 項 · 高影響 %d 項"),
					snap.startups.size(), hi_startup);

				struct ActionCardDef {
					const char* id;
					const char* title;
					const char* sub;
					const char* btn;
					bool accent;
				};
				ActionCardDef cards[4] = {};
				int card_count = 0;
				cards[card_count++] = { "##ov_svc", I18N(u8"背景服務"),
					pending_svc > 0 ? sub_svc : I18N(u8"目前無需停用"), I18N(u8"立即停用"), pending_svc > 0 };
				cards[card_count++] = { "##ov_startup", I18N(u8"啟動項"), sub_su, I18N(u8"前往管理"), true };

				if (snap.suggested_clean_bytes > 50 * 1024 * 1024) {
					char size_buf[48] = {};
					FormatCleanSize(snap.suggested_clean_bytes, size_buf, sizeof(size_buf));
					char sub_st[96] = {};
					snprintf(sub_st, sizeof(sub_st), I18N(u8"估計可釋放 %s"), size_buf);
					cards[card_count++] = { "##ov_store", I18N(u8"磁碟清理"), sub_st, I18N(u8"前往清理"), false };
				}
				else {
					cards[card_count++] = { "##ov_scan", I18N(u8"重新掃描"),
						I18N(u8"更新啟動項與服務清單"), I18N(u8"掃描"), false };
				}
				if (OptimizeScan::HasLastApplyRevert()) {
					cards[card_count++] = { "##ov_revert", I18N(u8"還原上次優化"),
						I18N(u8"還原預設套用的服務與電源設定"), I18N(u8"一鍵還原"), true };
				}

				if (ImGui::BeginTable("##opt_action_tbl", 2,
					ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
					for (int i = 0; i < card_count; ++i) {
						if (i % 2 == 0) {
							ImGui::TableNextRow(ImGuiTableRowFlags_None, action_card_h);
						}
						ImGui::TableNextColumn();
						const ImVec2 p0 = ImGui::GetCursorScreenPos();
						const float cw = ImMax(40.f, ImGui::GetContentRegionAvail().x);
						const ImRect bb(p0, ImVec2(p0.x + cw, p0.y + action_card_h - 4.f));
						if (DrawOverviewActionCard(bb, cards[i].id, cards[i].title,
							cards[i].sub, cards[i].btn, cards[i].accent)) {
							HandleOverviewAction(cards[i].id);
						}
					}
					ImGui::EndTable();
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_overview_sys")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"系統狀態"));
				DrawKeyValueRow(I18N(u8"電源"), snap.power_plan_name[0] ? UiTxt(snap.power_plan_name) : "—");
				DrawKeyValueRow(I18N(u8"遊戲模式"), snap.game_mode_on ? I18N(u8"開啟") : I18N(u8"關閉"));
				DrawKeyValueRow(I18N(u8"視覺效果"), VisualFxLabel(snap.visual_fx_setting));
				if (snap.system_drive_used_percent >= 0.f) {
					char pct[32] = {};
					snprintf(pct, sizeof(pct), "%.1f%%", snap.system_drive_used_percent);
					DrawKeyValueRow(I18N(u8"系統碟 C:"), pct);
				}
				const char* msg = OptimizeScan::GetLastActionMessage();
				if (msg != nullptr && msg[0] != '\0') {
					DrawKeyValueRow(I18N(u8"最近操作"), UiTxt(msg));
				}
				ImGui::Dummy(ImVec2(0, 4.f));
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_overview_presets")) {
				DrawOverviewPresetCards();
			}
			EndCyberPanel();
		}

		static void DrawStartupRow(const OptimizeScan::StartupEntry& e, int index)
		{
			(void)index;
			const float w = ImGui::GetContentRegionAvail().x;
			const float row_h = kListRowH;
			const float action_w = e.can_toggle ? kSwitchW : 108.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 6.f));
			const ImRect bb(pos, ImVec2(pos.x + w, pos.y + row_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();

			const float action_left = bb.Max.x - kListPad - action_w;
			const float tag_left = action_left - kListTagW - 6.f;
			const float text_left = bb.Min.x + kListPad + kListIcon + 8.f;
			const float text_right = tag_left - 4.f;
			const ImRect icon_bb(
				ImVec2(bb.Min.x + kListPad, bb.Min.y + (row_h - kListIcon) * 0.5f),
				ImVec2(bb.Min.x + kListPad + kListIcon, bb.Min.y + (row_h + kListIcon) * 0.5f));

			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			DrawRowIcon(dl, icon_bb, OptimizeStartupIcon::GetStartupIconTextureId(e));

			const char* title = e.file_description_utf8[0] != '\0'
				? e.file_description_utf8 : e.name_utf8;
			dl->PushClipRect(ImVec2(text_left, bb.Min.y), ImVec2(text_right, bb.Max.y), true);
			dl->AddText(ImVec2(text_left, bb.Min.y + 12.f),
				ImGui::GetColorU32(cyan_neon()), title);
			char line2[256] = {};
			snprintf(line2, sizeof(line2), "%s · %s", e.name_utf8, UiTxt(e.source_label));
			dl->AddText(ImVec2(text_left, bb.Min.y + 32.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), line2);
			if (e.publisher_utf8[0] != '\0') {
				dl->AddText(ImVec2(text_left, bb.Min.y + 48.f),
					ImGui::GetColorU32(ImGuiCol_Text), e.publisher_utf8);
			}
			dl->PopClipRect();

			char impact_lbl[16] = {};
			ImVec4 impact_bg;
			ImVec4 impact_fg;
			ImpactBadgeStyle(e.impact_tier, impact_bg, impact_fg, impact_lbl, sizeof(impact_lbl));
			const char* status = e.enabled ? I18N(u8"開機") : I18N(u8"停用");
			const ImVec4 st_bg = e.enabled
				? ImVec4(0.1f, 0.28f, 0.22f, 1.f) : ImVec4(0.18f, 0.18f, 0.2f, 1.f);
			const ImVec4 st_fg = e.enabled
				? ImVec4(0.4f, 0.95f, 0.75f, 1.f) : ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			float tag_y = bb.Min.y + 14.f;
			tag_y += DrawBadgeStacked(dl, ImVec2(tag_left, tag_y), impact_lbl, impact_bg, impact_fg);
			DrawBadgeStacked(dl, ImVec2(tag_left, tag_y), status, st_bg, st_fg);

			const ImRect text_hit(text_left - 2.f, bb.Min.y, text_right, bb.Max.y);
			ImGui::SetCursorScreenPos(text_hit.Min);
			char row_id[80] = {};
			snprintf(row_id, sizeof(row_id), "##su_hit_%s", e.id);
			ImGui::InvisibleButton(row_id, text_hit.GetSize());
			if (ImGui::IsItemHovered()) {
				dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_neon()), kRounding, 0, 1.2f);
				DrawStartupCyberTooltip(e);
				if (e.can_toggle && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					OptimizeScan::SetStartupEnabled(e.id, !e.enabled);
				}
			}

			if (e.can_toggle) {
				const ImRect sw_bb(
					ImVec2(action_left, bb.Min.y + (row_h - kSwitchH) * 0.5f),
					ImVec2(action_left + kSwitchW, bb.Min.y + (row_h + kSwitchH) * 0.5f));
				char sw_id[80] = {};
				snprintf(sw_id, sizeof(sw_id), "##su_sw_%s", e.id);
				bool on = e.enabled;
				if (CyberStartupSwitch(sw_id, sw_bb, on)) {
					OptimizeScan::SetStartupEnabled(e.id, !on);
				}
			}
			else {
				const ImRect tm_bb(
					ImVec2(action_left, bb.Min.y + (row_h - 30.f) * 0.5f),
					ImVec2(bb.Max.x - kListPad, bb.Min.y + (row_h + 30.f) * 0.5f));
				char tm_id[80] = {};
				snprintf(tm_id, sizeof(tm_id), "##su_tm_%s", e.id);
				if (CyberTextButton(tm_id, tm_bb, I18N(u8"工作管理員"), false, false)) {
					OptimizeScan::OpenTaskManagerStartup();
				}
			}
		}

		static void DrawTabStartup(OptimizeScan::Snapshot& snap)
		{
			if (BeginCyberPanel("##opt_panel_startup_tools", 0.f)) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"啟動項"));
				ImGui::TextDisabled(I18N(u8"管理開機自動執行的程式"));
				ImGui::Spacing();
				DrawCyberSearchField("##su_filter", I18N(u8"輸入名稱、產品或發行者…"),
					g_startup_filter, sizeof(g_startup_filter));
				const float half = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
				ImGui::TextDisabled(I18N(u8"排序"));
				ImGui::SameLine(48.f);
				ImGui::SetNextItemWidth(half);
				const char* sort_items[] = { I18N(u8"影響（高→低）"), I18N(u8"名稱"), I18N(u8"已啟用優先") };
				ImGui::Combo("##su_sort", &g_startup_sort, sort_items, 3);
				ImGui::SameLine(0, 8);
				ImGui::Checkbox(I18N(u8"只顯示已啟用"), &g_startup_only_enabled);
				std::vector<size_t> indices;
				SortStartupIndices(snap, indices);
				ImGui::TextColored(cyan_mid(), I18NF(u8"顯示 %zu / %zu 項"),
					indices.size(), snap.startups.size());
				ImGui::Dummy(ImVec2(0, 2.f));
			}
			EndCyberPanel();
			ImGui::Spacing();

			const float list_h = ImMax(120.f, ImGui::GetContentRegionAvail().y);
			if (BeginCyberPanel("##opt_panel_startup_list", list_h)) {
				if (snap.startups.empty()) {
					ImGui::TextDisabled(I18N(u8"尚無啟動項資料，請先執行優化掃描。"));
				}
				else {
					std::vector<size_t> indices;
					SortStartupIndices(snap, indices);
					if (indices.empty()) {
						ImGui::TextDisabled(I18N(u8"沒有符合篩選條件的啟動項。"));
					}
					for (size_t idx : indices) {
						DrawStartupRow(snap.startups[idx], static_cast<int>(idx));
					}
				}
			}
			EndCyberPanel();
		}

		static void DrawServiceRow(const OptimizeScan::ServiceEntry& s, int index)
		{
			(void)index;
			const float w = ImGui::GetContentRegionAvail().x;
			const float row_h = kListRowH;
			const bool is_disabled = (s.start_type == SERVICE_DISABLED);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 6.f));
			const ImRect bb(pos, ImVec2(pos.x + w, pos.y + row_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();

			const float action_left = bb.Max.x - kListPad - kListActionW;
			const float tag_left = action_left - kListTagW - 6.f;
			const float text_left = bb.Min.x + kListPad + kListIcon + 8.f;
			const float text_right = tag_left - 4.f;
			const ImRect icon_bb(
				ImVec2(bb.Min.x + kListPad, bb.Min.y + (row_h - kListIcon) * 0.5f),
				ImVec2(bb.Min.x + kListPad + kListIcon, bb.Min.y + (row_h + kListIcon) * 0.5f));

			const ImVec4 row_bg = s.recommended_disable
				? ImVec4(0.16f, 0.1f, 0.08f, 0.98f) : card_bg();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(row_bg), kRounding);
			dl->AddRect(bb.Min, bb.Max,
				ImGui::GetColorU32(s.recommended_disable
					? ImVec4(1.f, 0.45f, 0.28f, 0.9f) : cyan_dark()),
				kRounding, 0, 1.f);
			DrawRowIcon(dl, icon_bb, OptimizeStartupIcon::GetServiceIconTextureId(s));

			dl->PushClipRect(ImVec2(text_left, bb.Min.y), ImVec2(text_right, bb.Max.y), true);
			dl->AddText(ImVec2(text_left, bb.Min.y + 12.f),
				ImGui::GetColorU32(cyan_neon()), s.display_name);
			dl->AddText(ImVec2(text_left, bb.Min.y + 30.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), s.service_name);
			const char* brief = s.role_utf8[0] != '\0' ? UiTxt(s.role_utf8) : UiTxt(s.description_utf8);
			if (brief[0] != '\0') {
				dl->AddText(ImVec2(text_left, bb.Min.y + 46.f),
					ImGui::GetColorU32(ImGuiCol_Text), brief);
			}
			dl->PopClipRect();

			const char* run = s.exists ? (s.running ? I18N(u8"執行中") : I18N(u8"已停止")) : I18N(u8"未安裝");
			char status_lbl[48] = {};
			snprintf(status_lbl, sizeof(status_lbl), "%s · %s", run,
				ServiceStartLabel(s.start_type));
			float tag_y = bb.Min.y + 12.f;
			if (s.recommended_disable && s.role_utf8[0] != '\0') {
				tag_y += DrawBadgeStacked(dl, ImVec2(tag_left, tag_y), I18N(u8"常見可停用"),
					ImVec4(0.35f, 0.12f, 0.08f, 1.f),
					ImVec4(1.f, 0.55f, 0.35f, 1.f));
			}
			DrawBadgeStacked(dl, ImVec2(tag_left, tag_y), status_lbl,
				ImVec4(0.14f, 0.18f, 0.2f, 1.f), ImVec4(0.7f, 0.85f, 0.95f, 1.f));

			if (s.exists) {
				const char* action = is_disabled ? I18N(u8"啟用") : I18N(u8"停用");
				const ImRect act_bb(
					ImVec2(action_left, bb.Min.y + (row_h - 30.f) * 0.5f),
					ImVec2(bb.Max.x - kListPad, bb.Min.y + (row_h + 30.f) * 0.5f));
				ImGui::SetCursorScreenPos(act_bb.Min);
				char btn_id[64] = {};
				snprintf(btn_id, sizeof(btn_id), "##svc_act_%s", s.service_name);
				if (CyberTextButton(btn_id, act_bb, action, !is_disabled, false)) {
					HAdminPrompt::TryGate(HAdminPrompt::Scene::Optimize);
					OptimizeScan::SetServiceDisabled(s.service_name, !is_disabled);
				}
			}

			const ImRect text_hit(text_left - 2.f, bb.Min.y, action_left - 4.f, bb.Max.y);
			ImGui::SetCursorScreenPos(text_hit.Min);
			char row_id[80] = {};
			snprintf(row_id, sizeof(row_id), "##svc_hit_%s", s.service_name);
			ImGui::InvisibleButton(row_id, text_hit.GetSize());
			if (ImGui::IsItemHovered()) {
				dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_neon()), kRounding, 0, 1.2f);
				DrawServiceCyberTooltip(s);
			}
		}

		static void DrawTabServices(OptimizeScan::Snapshot& snap)
		{
			if (BeginCyberPanel("##opt_panel_svc_hdr", 0.f)) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"背景服務"));
				ImGui::TextDisabled(I18N(u8"自動啟動與執行中的系統服務（最多 80 項）"));
				if (!HCleanIsRunningAsAdmin()) {
					ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f),
						I18N(u8"變更服務建議以管理員執行"));
				}
				ImGui::Spacing();
				DrawCyberSearchField("##svc_filter", I18N(u8"輸入服務名稱或顯示名稱…"),
					g_service_filter, sizeof(g_service_filter));
				const float half = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
				ImGui::TextDisabled(I18N(u8"排序"));
				ImGui::SameLine(48.f);
				ImGui::SetNextItemWidth(half);
				const char* sort_items[] = { I18N(u8"建議優先"), I18N(u8"執行中優先"), I18N(u8"名稱") };
				ImGui::Combo("##svc_sort", &g_service_sort, sort_items, 3);
				ImGui::SameLine(0, 8);
				ImGui::Checkbox(I18N(u8"只顯示常見可停用"), &g_service_only_recommended);
				std::vector<size_t> indices;
				SortServiceIndices(snap, indices);
				ImGui::TextColored(cyan_mid(), I18NF(u8"顯示 %zu / %zu 項"),
					indices.size(), snap.services.size());
				if (!HCleanIsRunningAsAdmin()) {
					ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f),
						I18N(u8"停用／啟用服務需以管理員執行本程式"));
				}
				ImGui::Dummy(ImVec2(0, 2.f));
			}
			EndCyberPanel();
			ImGui::Spacing();

			const float list_h = ImMax(120.f, ImGui::GetContentRegionAvail().y);
			if (BeginCyberPanel("##opt_panel_svc_list", list_h)) {
				std::vector<size_t> indices;
				SortServiceIndices(snap, indices);
				if (indices.empty()) {
					ImGui::TextDisabled(I18N(u8"沒有符合篩選條件的服務。"));
				}
				for (size_t idx : indices) {
					DrawServiceRow(snap.services[idx], static_cast<int>(idx));
				}
			}
			EndCyberPanel();
		}

		static void DrawTabSystem(OptimizeScan::Snapshot& snap)
		{
			if (BeginCyberPanel("##opt_panel_sys_hdr")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"系統效能調校"));
				const char* msg = OptimizeScan::GetLastActionMessage();
				if (msg != nullptr && msg[0] != '\0') {
					ImGui::SameLine(0.f, 12.f);
					ImGui::TextColored(cyan_mid(), I18NF(u8"最近：%s"), UiTxt(msg));
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##sys_quick_game", I18N(u8"遊戲調校"), true)) {
					SysReportResult(I18N(u8"一鍵遊戲調校"), OptimizeScan::ApplyQuickGamingTune());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##sys_quick_office", I18N(u8"辦公調校"), false)) {
					SysReportResult(I18N(u8"一鍵辦公調校"), OptimizeScan::ApplyQuickOfficeTune());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##sys_quick_batt", I18N(u8"省電調校"), false)) {
					SysReportResult(I18N(u8"一鍵省電調校"), OptimizeScan::ApplyQuickBatteryTune());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##sys_quick_resp", I18N(u8"極致響應"), true, 88.f)) {
					SysReportResult(I18N(u8"極致響應調校"), OptimizeScan::ApplyQuickResponsiveTune());
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				if (DrawSystemToolbarButton("##sys_refresh", I18N(u8"重新讀取"), false, 88.f)) {
					OptimizeScan::RefreshSystemSettings();
				}
				if (!snap.ultimate_plan_available) {
					ImGui::SameLine(0.f, 6.f);
					if (DrawSystemToolbarButton("##sys_ult_enable", I18N(u8"終極效能"), false, 88.f)) {
						if (SysTryAdmin(I18N(u8"建立終極效能電源計畫"))) {
							SysReportResult(I18N(u8"建立終極效能"), OptimizeScan::EnsureUltimatePowerPlan());
						}
					}
				}
				ImGui::SameLine(0.f, 12.f);
				if (DrawSystemToolbarButton("##sys_expand_all", I18N(u8"全部展開"), false, 80.f)) {
					SysSetAllSectionsOpen(true);
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##sys_collapse_all", I18N(u8"全部收起"), false, 80.f)) {
					SysSetAllSectionsOpen(false);
				}
				ImGui::PopStyleVar();
			}
			EndCyberPanel();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 2.f));
			if (BeginSysCollapsibleSection("status", I18N(u8"① 目前狀態"), &g_sys_open_status)) {
				DrawSystemStatusStrip(snap);
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("power", I18N(u8"② 電源與開機"), &g_sys_open_power)) {
				char pwr_status[64] = {};
				snprintf(pwr_status, sizeof(pwr_status), "%s",
					snap.power_plan_name[0] ? UiTxt(snap.power_plan_name) : I18N(u8"未知"));
				const char* active_pwr = OptimizeScan::GetActivePowerPlanKind();
				const SystemActionBtn pwr_btns[] = {
					{ "##pwr_hi", I18N(u8"高效能"), strcmp(active_pwr, "high") == 0 },
					{ "##pwr_bal", I18N(u8"平衡"), strcmp(active_pwr, "balanced") == 0 },
					{ "##pwr_sav", I18N(u8"省電"), strcmp(active_pwr, "saver") == 0 },
					{ "##pwr_ult", I18N(u8"終極效能"), strcmp(active_pwr, "ultimate") == 0 },
				};
				const int pwr_pick = DrawSystemSettingCard(
					I18N(u8"電源計畫"), I18N(u8"影響 CPU 最高頻率與休眠策略；遊戲建議高效能或終極效能。"),
					pwr_status, cyan_neon(), pwr_btns, 4,
					TagIf(strcmp(active_pwr, "high") != 0
						&& strcmp(active_pwr, "ultimate") != 0, I18N(u8"遊戲建議")));
				if (pwr_pick >= 0) {
					const char* kinds[] = { "high", "balanced", "saver", "ultimate" };
					const bool need_admin = (pwr_pick == 3);
					if (!need_admin || SysTryAdmin(I18N(u8"切換終極效能電源計畫"))) {
						SysReportResult(I18N(u8"切換電源計畫"),
							OptimizeScan::SetPowerPlanByKind(kinds[pwr_pick]));
					}
				}

				char hib_status[16] = {};
				snprintf(hib_status, sizeof(hib_status), "%s", OnOffText(snap.hibernate_on));
				static const SystemActionBtn hib_btns[] = {
					{ "##hib_on", I18N(u8"啟用"), false },
					{ "##hib_off", I18N(u8"關閉"), true },
				};
				const int hib_pick = DrawSystemSettingCard(
					I18N(u8"休眠"), I18N(u8"關閉可釋放與記憶體等大的磁碟空間（hiberfil.sys）；啟用需管理員。"),
					hib_status, OnOffColor(snap.hibernate_on), hib_btns, 2,
					TagIf(snap.hibernate_on, u8"建議關閉"));
				if (hib_pick >= 0) {
					const bool enabling = (hib_pick == 0);
					if (!enabling || SysTryAdmin(I18N(u8"啟用休眠"))) {
						SysReportResult(I18N(u8"休眠設定"), OptimizeScan::SetHibernateEnabled(enabling));
					}
				}

				char fs_status[16] = {};
				strncpy_s(fs_status, OnOffText(snap.fast_startup_on), _TRUNCATE);
				static const SystemActionBtn fs_btns[] = {
					{ "##fs_on", I18N(u8"啟用"), false },
					{ "##fs_off", I18N(u8"關閉"), true },
				};
				const int fs_pick = DrawSystemSettingCard(
					I18N(u8"快速啟動"), I18N(u8"關閉可讓關機更徹底；變更需管理員權限。"),
					fs_status, OnOffColor(snap.fast_startup_on), fs_btns, 2,
					TagIf(snap.fast_startup_on, u8"建議關閉"));
				if (fs_pick >= 0 && SysTryAdmin(I18N(u8"變更快速啟動"))) {
					SysReportResult(I18N(u8"快速啟動"), OptimizeScan::SetFastStartup(fs_pick == 0));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("visual", I18N(u8"③ 視覺與動畫"), &g_sys_open_visual)) {
				char vfx_status[32] = {};
				strncpy_s(vfx_status, VisualFxLabel(snap.visual_fx_setting), _TRUNCATE);
				static const SystemActionBtn vfx_btns[] = {
					{ "##vfx_perf", I18N(u8"最佳效能"), true },
					{ "##vfx_bal", I18N(u8"由系統決定"), false },
					{ "##vfx_app", I18N(u8"最佳外觀"), false },
				};
				const int vfx_pick = DrawSystemSettingCard(
					I18N(u8"視覺效果"), I18N(u8"「最佳效能」可減少陰影與動畫，提升操作流暢度。"),
					vfx_status, cyan_mid(), vfx_btns, 3,
					TagIf(snap.visual_fx_setting != 2, u8"遊戲建議"));
				if (vfx_pick >= 0) {
					const int modes[] = { 2, 0, 1 };
					SysReportResult(I18N(u8"視覺效果"), OptimizeScan::SetVisualEffects(modes[vfx_pick]));
				}

				char anim_status[16] = {};
				snprintf(anim_status, sizeof(anim_status), "%s", OnOffText(snap.animations_on));
				static const SystemActionBtn anim_btns[] = {
					{ "##anim_off", I18N(u8"關閉動畫"), true },
					{ "##anim_on", I18N(u8"啟用動畫"), false },
				};
				const int anim_pick = DrawSystemSettingCard(
					I18N(u8"視窗動畫"), I18N(u8"關閉選單／視窗動畫可略為提升反應速度。"),
					anim_status, OnOffColor(snap.animations_on), anim_btns, 2,
					TagIf(snap.animations_on, u8"建議關閉"));
				if (anim_pick >= 0) {
					SysReportResult(I18N(u8"視窗動畫"), OptimizeScan::SetAnimationsEnabled(anim_pick == 1));
				}

				char trans_status[16] = {};
				snprintf(trans_status, sizeof(trans_status), "%s", OnOffText(snap.transparency_on));
				static const SystemActionBtn trans_btns[] = {
					{ "##trans_off", I18N(u8"關閉透明"), true },
					{ "##trans_on", I18N(u8"啟用透明"), false },
				};
				const int trans_pick = DrawSystemSettingCard(
					I18N(u8"視窗透明"), I18N(u8"關閉透明效果可減少合成負擔。"),
					trans_status, OnOffColor(snap.transparency_on), trans_btns, 2,
					TagIf(snap.transparency_on, u8"建議關閉"));
				if (trans_pick >= 0) {
					SysReportResult(I18N(u8"視窗透明"), OptimizeScan::SetTransparencyEffects(trans_pick == 1));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("game", I18N(u8"④ 遊戲與錄影"), &g_sys_open_game)) {
				char gm_status[16] = {};
				snprintf(gm_status, sizeof(gm_status), "%s", OnOffText(snap.game_mode_on));
				static const SystemActionBtn gm_btns[] = {
					{ "##gm_on", I18N(u8"開啟"), true },
					{ "##gm_off", I18N(u8"關閉"), false },
				};
				const int gm_pick = DrawSystemSettingCard(
					I18N(u8"遊戲模式"), I18N(u8"讓 Windows 優先遊戲程序資源。"),
					gm_status, OnOffColor(snap.game_mode_on), gm_btns, 2,
					TagIf(!snap.game_mode_on, u8"建議開啟"));
				if (gm_pick >= 0) {
					SysReportResult(I18N(u8"遊戲模式"), OptimizeScan::SetGameModeEnabled(gm_pick == 0));
				}

				char dvr_status[16] = {};
				snprintf(dvr_status, sizeof(dvr_status), "%s", OnOffText(snap.game_dvr_on));
				static const SystemActionBtn dvr_btns[] = {
					{ "##dvr_off", I18N(u8"關閉錄影"), true },
					{ "##dvr_on", I18N(u8"啟用錄影"), false },
				};
				const int dvr_pick = DrawSystemSettingCard(
					I18N(u8"Xbox 背景錄影"), I18N(u8"關閉可釋放 CPU／GPU，遊戲幀數通常更穩定。"),
					dvr_status, OnOffColor(snap.game_dvr_on), dvr_btns, 2,
					TagIf(snap.game_dvr_on, u8"建議關閉"));
				if (dvr_pick >= 0) {
					SysReportResult(I18N(u8"Xbox 背景錄影"), OptimizeScan::SetGameDvrEnabled(dvr_pick == 1));
				}

				char fso_status[16] = {};
				snprintf(fso_status, sizeof(fso_status), "%s", OnOffText(snap.fullscreen_opt_on));
				static const SystemActionBtn fso_btns[] = {
					{ "##fso_on", I18N(u8"啟用"), false },
					{ "##fso_off", I18N(u8"關閉"), true },
				};
				const int fso_pick = DrawSystemSettingCard(
					I18N(u8"全螢幕最佳化"), I18N(u8"部分遊戲關閉後可減少卡頓；若遊戲異常再改回啟用。"),
					fso_status, OnOffColor(snap.fullscreen_opt_on), fso_btns, 2,
					TagIf(snap.fullscreen_opt_on, u8"可選調校"));
				if (fso_pick >= 0) {
					SysReportResult(I18N(u8"全螢幕最佳化"),
						OptimizeScan::SetFullscreenOptimizations(fso_pick == 0));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("input", I18N(u8"⑤ 輸入與回應"), &g_sys_open_input)) {
				char mouse_status[16] = {};
				snprintf(mouse_status, sizeof(mouse_status), "%s", OnOffText(snap.mouse_accel_on));
				static const SystemActionBtn mouse_btns[] = {
					{ "##mouse_off", I18N(u8"關閉加速"), true },
					{ "##mouse_on", I18N(u8"啟用加速"), false },
				};
				const int mouse_pick = DrawSystemSettingCard(
					I18N(u8"滑鼠加速"), I18N(u8"「增強指標精確度」；FPS 遊戲建議關閉。"),
					mouse_status, OnOffColor(snap.mouse_accel_on), mouse_btns, 2,
					TagIf(snap.mouse_accel_on, u8"遊戲建議"));
				if (mouse_pick >= 0) {
					SysReportResult(I18N(u8"滑鼠加速"),
						OptimizeScan::SetMouseAccelerationEnabled(mouse_pick == 1));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("adv", I18N(u8"⑥ 進階硬體排程"), &g_sys_open_adv)) {
				char cpu_status[24] = {};
				strncpy_s(cpu_status, snap.processor_foreground ? I18N(u8"前景優先") : I18N(u8"系統預設"), _TRUNCATE);
				static const SystemActionBtn cpu_btns[] = {
					{ "##proc_fg", I18N(u8"前景優先"), true },
					{ "##proc_def", I18N(u8"系統預設"), false },
				};
				const int cpu_pick = DrawSystemSettingCard(
					I18N(u8"處理器排程"), I18N(u8"前景優先讓使用中程式獲得更多 CPU 時間片；需管理員。"),
					cpu_status, cyan_mid(), cpu_btns, 2,
					TagIf(!snap.processor_foreground, u8"遊戲建議"));
				if (cpu_pick >= 0 && SysTryAdmin(I18N(u8"處理器排程"))) {
					SysReportResult(I18N(u8"處理器排程"),
						OptimizeScan::SetProcessorSchedulingPrograms(cpu_pick == 0));
				}

				char gpu_status[16] = {};
				snprintf(gpu_status, sizeof(gpu_status), "%s", OnOffText(snap.gpu_scheduling_on));
				static const SystemActionBtn gpu_btns[] = {
					{ "##gpu_on", I18N(u8"啟用"), true },
					{ "##gpu_off", I18N(u8"關閉"), false },
				};
				const int gpu_pick = DrawSystemSettingCard(
					I18N(u8"硬體 GPU 排程"), I18N(u8"Win10 1903+ 支援；變更後需重新開機；需管理員。"),
					gpu_status, OnOffColor(snap.gpu_scheduling_on), gpu_btns, 2,
					TagIf(!snap.gpu_scheduling_on, u8"建議開啟"));
				if (gpu_pick >= 0 && SysTryAdmin(I18N(u8"硬體 GPU 排程"))) {
					SysReportResult(I18N(u8"硬體 GPU 排程"),
						OptimizeScan::SetHardwareGpuScheduling(gpu_pick == 0));
				}

				char throttle_status[16] = {};
				strncpy_s(throttle_status, snap.power_throttling_on ? I18N(u8"節流中") : I18N(u8"已關閉"), _TRUNCATE);
				static const SystemActionBtn throttle_btns[] = {
					{ "##thr_off", I18N(u8"關閉節流"), true },
					{ "##thr_on", I18N(u8"啟用節流"), false },
				};
				const int throttle_pick = DrawSystemSettingCard(
					I18N(u8"電源節流"), I18N(u8"關閉後背景程式較不易被降頻；筆電續航可能略降；需管理員。"),
					throttle_status,
					snap.power_throttling_on
						? ImVec4(0.65f, 0.68f, 0.72f, 1.f)
						: ImVec4(0.45f, 0.95f, 0.75f, 1.f),
					throttle_btns, 2,
					TagIf(snap.power_throttling_on, u8"建議關閉"));
				if (throttle_pick >= 0 && SysTryAdmin(I18N(u8"電源節流"))) {
					SysReportResult(I18N(u8"電源節流"),
						OptimizeScan::SetPowerThrottlingEnabled(throttle_pick == 1));
				}

				if (DrawSystemToolbarButton("##sys_rp", I18N(u8"建立還原點"), true, 120.f)) {
					if (SysTryAdmin(I18N(u8"建立系統還原點"))) {
						SysReportResult(I18N(u8"建立還原點"), OptimizeScan::CreateSystemRestorePoint());
					}
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("bg", I18N(u8"⑦ 背景與通知"), &g_sys_open_bg)) {
				char tips_status[16] = {};
				strncpy_s(tips_status, OnOffText(snap.tips_suggestions_on), _TRUNCATE);
				static const SystemActionBtn tips_btns[] = {
					{ "##tips_off", I18N(u8"關閉提示"), true },
					{ "##tips_on", I18N(u8"啟用提示"), false },
				};
				const int tips_pick = DrawSystemSettingCard(
					I18N(u8"Windows 提示與建議"), I18N(u8"關閉可減少開始功能表與系統通知的推薦內容。"),
					tips_status, OnOffColor(snap.tips_suggestions_on), tips_btns, 2,
					TagIf(snap.tips_suggestions_on, u8"建議關閉"));
				if (tips_pick >= 0) {
					SysReportResult(I18N(u8"Windows 提示與建議"),
						OptimizeScan::SetTipsAndSuggestionsEnabled(tips_pick == 1));
				}

				char bg_status[16] = {};
				strncpy_s(bg_status, OnOffText(snap.background_apps_on), _TRUNCATE);
				static const SystemActionBtn bg_btns[] = {
					{ "##bg_off", I18N(u8"限制背景"), true },
					{ "##bg_on", I18N(u8"允許背景"), false },
				};
				const int bg_pick = DrawSystemSettingCard(
					I18N(u8"背景應用程式"), I18N(u8"限制後未使用中的 UWP／商店應用較少佔用 CPU 與網路。"),
					bg_status, OnOffColor(snap.background_apps_on), bg_btns, 2,
					TagIf(snap.background_apps_on, u8"建議關閉"));
				if (bg_pick >= 0) {
					SysReportResult(I18N(u8"背景應用程式"),
						OptimizeScan::SetBackgroundAppsEnabled(bg_pick == 1));
				}

				char gbar_status[16] = {};
				strncpy_s(gbar_status, OnOffText(snap.game_bar_on), _TRUNCATE);
				static const SystemActionBtn gbar_btns[] = {
					{ "##gbar_off", I18N(u8"關閉 Game Bar"), true },
					{ "##gbar_on", I18N(u8"啟用 Game Bar"), false },
				};
				const int gbar_pick = DrawSystemSettingCard(
					"Xbox Game Bar", I18N(u8"關閉疊加層與快捷列，遊戲時減少干擾與資源佔用。"),
					gbar_status, OnOffColor(snap.game_bar_on), gbar_btns, 2,
					TagIf(snap.game_bar_on, u8"建議關閉"));
				if (gbar_pick >= 0) {
					SysReportResult("Xbox Game Bar", OptimizeScan::SetGameBarEnabled(gbar_pick == 1));
				}

				char search_status[16] = {};
				strncpy_s(search_status, OnOffText(snap.search_highlights_on), _TRUNCATE);
				static const SystemActionBtn search_btns[] = {
					{ "##search_off", I18N(u8"關閉亮點"), true },
					{ "##search_on", I18N(u8"啟用亮點"), false },
				};
				const int search_pick = DrawSystemSettingCard(
					I18N(u8"搜尋亮點與動態建議"), I18N(u8"關閉可減少開始搜尋列的推薦與新聞內容。"),
					search_status, OnOffColor(snap.search_highlights_on), search_btns, 2,
					TagIf(snap.search_highlights_on, u8"建議關閉"));
				if (search_pick >= 0) {
					SysReportResult(I18N(u8"搜尋亮點"),
						OptimizeScan::SetSearchHighlightsEnabled(search_pick == 1));
				}

				char widget_status[16] = {};
				strncpy_s(widget_status, OnOffText(snap.widgets_on), _TRUNCATE);
				static const SystemActionBtn widget_btns[] = {
					{ "##widget_off", I18N(u8"關閉小工具"), true },
					{ "##widget_on", I18N(u8"啟用小工具"), false },
				};
				const int widget_pick = DrawSystemSettingCard(
					I18N(u8"工作列小工具"), I18N(u8"關閉 Win11 小工具／新聞摘要，減少背景更新。"),
					widget_status, OnOffColor(snap.widgets_on), widget_btns, 2,
					TagIf(snap.widgets_on, u8"建議關閉"));
				if (widget_pick >= 0) {
					SysReportResult(I18N(u8"工作列小工具"), OptimizeScan::SetWidgetsEnabled(widget_pick == 1));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("tune", I18N(u8"⑧ 底層與回應"), &g_sys_open_tune)) {
				char p2p_status[16] = {};
				strncpy_s(p2p_status, OnOffText(snap.delivery_p2p_on), _TRUNCATE);
				static const SystemActionBtn p2p_btns[] = {
					{ "##p2p_off", I18N(u8"關閉 P2P"), true },
					{ "##p2p_on", I18N(u8"啟用 P2P"), false },
				};
				const int p2p_pick = DrawSystemSettingCard(
					I18N(u8"Windows 更新 P2P"), I18N(u8"關閉區網／網際網路 P2P 分享，減少背景上傳與下載搶佔；需管理員。"),
					p2p_status, OnOffColor(snap.delivery_p2p_on), p2p_btns, 2,
					TagIf(snap.delivery_p2p_on, u8"建議關閉"));
				if (p2p_pick >= 0 && SysTryAdmin(I18N(u8"Windows 更新 P2P"))) {
					SysReportResult(I18N(u8"Windows 更新 P2P"),
						OptimizeScan::SetDeliveryOptimizationP2P(p2p_pick == 1));
				}

				char net_status[16] = {};
				strncpy_s(net_status, OnOffText(snap.network_throttling_on), _TRUNCATE);
				static const SystemActionBtn net_btns[] = {
					{ "##netthr_off", I18N(u8"關閉節流"), true },
					{ "##netthr_on", I18N(u8"啟用節流"), false },
				};
				const int net_pick = DrawSystemSettingCard(
					I18N(u8"網路節流"), I18N(u8"關閉後降低遊戲／語音通話時的網路限速；需管理員。"),
					net_status, OnOffColor(snap.network_throttling_on), net_btns, 2,
					TagIf(snap.network_throttling_on, u8"遊戲建議"));
				if (net_pick >= 0 && SysTryAdmin(I18N(u8"網路節流"))) {
					SysReportResult(I18N(u8"網路節流"),
						OptimizeScan::SetNetworkThrottlingEnabled(net_pick == 1));
				}

				char resp_status[16] = {};
				strncpy_s(resp_status, snap.game_responsiveness_on ? I18N(u8"遊戲優先") : I18N(u8"系統預設"),
					_TRUNCATE);
				static const SystemActionBtn resp_btns[] = {
					{ "##resp_game", I18N(u8"遊戲優先"), true },
					{ "##resp_def", I18N(u8"系統預設"), false },
				};
				const int resp_pick = DrawSystemSettingCard(
					I18N(u8"系統響應度"), I18N(u8"遊戲優先可讓前景程式獲得更多 CPU 時間；需管理員。"),
					resp_status, cyan_mid(), resp_btns, 2,
					TagIf(!snap.game_responsiveness_on, u8"遊戲建議"));
				if (resp_pick >= 0 && SysTryAdmin(I18N(u8"系統響應度"))) {
					SysReportResult(I18N(u8"系統響應度"),
						OptimizeScan::SetGameSystemResponsiveness(resp_pick == 0));
				}

				char menu_status[16] = {};
				strncpy_s(menu_status, snap.fast_menu_delay ? I18N(u8"即時") : I18N(u8"預設"), _TRUNCATE);
				static const SystemActionBtn menu_btns[] = {
					{ "##menu_fast", I18N(u8"即時回應"), true },
					{ "##menu_def", I18N(u8"預設延遲"), false },
				};
				const int menu_pick = DrawSystemSettingCard(
					I18N(u8"選單回應速度"), I18N(u8"縮短右鍵／開始選單展開延遲，操作更跟手。"),
					menu_status,
					snap.fast_menu_delay
						? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
						: ImVec4(0.65f, 0.68f, 0.72f, 1.f),
					menu_btns, 2,
					TagIf(!snap.fast_menu_delay, u8"建議開啟"));
				if (menu_pick >= 0) {
					SysReportResult(I18N(u8"選單回應速度"), OptimizeScan::SetFastMenuDelay(menu_pick == 0));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("tools", I18N(u8"⑨ 系統捷徑"), &g_sys_open_tools)) {
				if (DrawSystemToolbarButton("##sys_open_pwr", I18N(u8"Windows 電源設定"), true, 148.f)) {
					SysReportResult(I18N(u8"開啟電源設定"), OptimizeScan::OpenWindowsPowerSettings());
				}
				ImGui::SameLine(0.f, 8.f);
				if (DrawSystemToolbarButton("##sys_open_game", I18N(u8"Windows 遊戲設定"), false, 148.f)) {
					SysReportResult(I18N(u8"開啟遊戲設定"), OptimizeScan::OpenWindowsGameSettings());
				}
				EndSysCollapsibleSection();
			}
			ImGui::PopStyleVar();
		}

		static void FormatBitrate(float bps, char* buf, size_t buf_sz)
		{
			if (buf == nullptr || buf_sz == 0) {
				return;
			}
			if (bps < 0.f) {
				strncpy_s(buf, buf_sz, "—", _TRUNCATE);
				return;
			}
			if (bps < 1024.f) {
				snprintf(buf, buf_sz, "%.0f B/s", bps);
			}
			else if (bps < 1024.f * 1024.f) {
				snprintf(buf, buf_sz, "%.1f KB/s", bps / 1024.f);
			}
			else {
				snprintf(buf, buf_sz, "%.2f MB/s", bps / (1024.f * 1024.f));
			}
		}

		static void FormatByteCount(uint64_t bytes, char* buf, size_t buf_sz)
		{
			if (buf == nullptr || buf_sz == 0) {
				return;
			}
			if (bytes < 1024ull) {
				snprintf(buf, buf_sz, "%llu B", static_cast<unsigned long long>(bytes));
			}
			else if (bytes < 1024ull * 1024ull) {
				snprintf(buf, buf_sz, "%.1f KB", bytes / 1024.0);
			}
			else if (bytes < 1024ull * 1024ull * 1024ull) {
				snprintf(buf, buf_sz, "%.2f MB", bytes / (1024.0 * 1024.0));
			}
			else {
				snprintf(buf, buf_sz, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
			}
		}

		static void FormatLinkSpeed(uint64_t bps, char* buf, size_t buf_sz)
		{
			if (buf == nullptr || buf_sz == 0) {
				return;
			}
			if (bps == 0) {
				strncpy_s(buf, buf_sz, "—", _TRUNCATE);
				return;
			}
			const double mbps = static_cast<double>(bps) / 1000000.0;
			if (mbps >= 1000.0) {
				snprintf(buf, buf_sz, "%.1f Gbps", mbps / 1000.0);
			}
			else {
				snprintf(buf, buf_sz, "%.0f Mbps", mbps);
			}
		}

		static void NetWidgetAdvance(float w, float h)
		{
			ImGui::Dummy(ImVec2(w, h));
		}

		static bool NetReportResult(const char* action_label, bool ok)
		{
			if (ok) {
				return true;
			}
			const char* msg = OptimizeNetworkScan::GetLastActionMessage();
			char body[640] = {};
			snprintf(body, sizeof(body),
				I18N(u8"「%s」未能完成。\n\n原因：%s"),
				action_label != nullptr ? action_label : I18N(u8"操作"),
				(msg != nullptr && msg[0] != '\0') ? UiTxt(msg) : I18N(u8"系統拒絕變更或目前環境不支援。"));
			OpenSysInfoPopup(I18N(u8"無法完成操作"), body);
			return false;
		}

		enum class NetHealthLevel : int {
			Good = 0,
			Warning = 1,
			Bad = 2,
		};

		static NetHealthLevel ComputeNetHealth(const OptimizeNetworkScan::Snapshot& ns)
		{
			if (!ns.internet_reachable) {
				return NetHealthLevel::Bad;
			}
			if (ns.gateway[0] != '\0' && ns.gateway_ping_ms < 0) {
				return NetHealthLevel::Warning;
			}
			if (ns.internet_ping_ms > 150) {
				return NetHealthLevel::Warning;
			}
			if (ns.proxy_enabled && ns.proxy_server[0] == '\0') {
				return NetHealthLevel::Warning;
			}
			return NetHealthLevel::Good;
		}

		static const char* NetLatencyGrade(int ms)
		{
			if (ms < 0) {
				return I18N(u8"無回應");
			}
			if (ms < 50) {
				return I18N(u8"極快");
			}
			if (ms < 100) {
				return I18N(u8"良好");
			}
			if (ms < 200) {
				return I18N(u8"普通");
			}
			return I18N(u8"偏慢");
		}

		static ImVec4 NetLatencyColor(int ms)
		{
			if (ms < 0) {
				return ImVec4(1.f, 0.45f, 0.35f, 1.f);
			}
			if (ms < 100) {
				return ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			}
			if (ms < 200) {
				return ImVec4(1.f, 0.85f, 0.4f, 1.f);
			}
			return ImVec4(1.f, 0.55f, 0.35f, 1.f);
		}

		static ImVec4 ProcessRankColor(int rank)
		{
			if (rank == 1) {
				return ImVec4(1.f, 0.82f, 0.35f, 1.f);
			}
			if (rank == 2) {
				return ImVec4(0.78f, 0.82f, 0.88f, 1.f);
			}
			if (rank == 3) {
				return ImVec4(0.85f, 0.62f, 0.38f, 1.f);
			}
			return ImVec4(0.55f, 0.58f, 0.62f, 1.f);
		}

		static int NetHealthScore(const OptimizeNetworkScan::Snapshot& ns)
		{
			if (!ns.internet_reachable) {
				return 15;
			}
			int score = 100;
			if (ns.internet_ping_ms > 200) {
				score -= 35;
			}
			else if (ns.internet_ping_ms > 120) {
				score -= 20;
			}
			else if (ns.internet_ping_ms > 80) {
				score -= 10;
			}
			if (ns.gateway[0] != '\0' && ns.gateway_ping_ms < 0) {
				score -= 18;
			}
			if (ns.proxy_enabled) {
				score -= 5;
			}
			return ImClamp(score, 5, 100);
		}

		static void DrawNetSparkline(const ImRect& bb, const float* values, int count,
			const char* title, const ImVec4& line_col, float max_val_override = 0.f)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), title);

			const ImRect plot(ImVec2(bb.Min.x + 10.f, bb.Min.y + 28.f),
				ImVec2(bb.Max.x - 10.f, bb.Max.y - 10.f));
			dl->AddRectFilled(plot.Min, plot.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.08f, 0.10f, 1.f)), 3.f);
			if (count < 2) {
				dl->AddText(ImVec2(plot.Min.x + 8.f, plot.Min.y + 8.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"收集資料中…"));
				return;
			}
			float vmax = max_val_override;
			if (vmax <= 0.f) {
				for (int i = 0; i < count; ++i) {
					vmax = ImMax(vmax, values[i]);
				}
			}
			vmax = ImMax(vmax, 1.f);
			const float pw = plot.GetWidth();
			const float ph = plot.GetHeight();
			for (int i = 0; i < count; ++i) {
				const float x = plot.Min.x + pw * (static_cast<float>(i) / static_cast<float>(count - 1));
				const float y = plot.Max.y - ph * ImClamp(values[i] / vmax, 0.f, 1.f);
				const ImVec2 p(x, y);
				if (i == 0) {
					dl->PathClear();
					dl->PathLineTo(p);
				}
				else {
					dl->PathLineTo(p);
				}
			}
			dl->PathStroke(ImGui::GetColorU32(line_col), 0, 2.f);
		}

		static void DrawNetDualSparkline(const ImRect& bb,
			const OptimizeNetworkScan::BandwidthHistory& hist)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"即時流量曲線"));
			const float leg_y = bb.Min.y + 8.f;
			const float leg_x = bb.Max.x - 10.f;
			const ImVec2 dl_ts = ImGui::CalcTextSize(I18N(u8"下載"));
			const ImVec2 ul_ts = ImGui::CalcTextSize(I18N(u8"上傳"));
			const float leg_block = dl_ts.x + ul_ts.x + 36.f;
			const float leg_start = leg_x - leg_block;
			dl->AddRectFilled(ImVec2(leg_start, leg_y + 4.f),
				ImVec2(leg_start + 10.f, leg_y + 8.f), ImGui::GetColorU32(cyan_mid()), 1.f);
			dl->AddText(ImVec2(leg_start + 14.f, leg_y),
				ImGui::GetColorU32(cyan_mid()), I18N(u8"下載"));
			const float ul_x = leg_start + 14.f + dl_ts.x + 10.f;
			dl->AddRectFilled(ImVec2(ul_x, leg_y + 4.f),
				ImVec2(ul_x + 10.f, leg_y + 8.f),
				ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 1.f, 1.f)), 1.f);
			dl->AddText(ImVec2(ul_x + 14.f, leg_y),
				ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 1.f, 1.f)), I18N(u8"上傳"));

			const ImRect plot(ImVec2(bb.Min.x + 10.f, bb.Min.y + 28.f),
				ImVec2(bb.Max.x - 10.f, bb.Max.y - 10.f));
			dl->AddRectFilled(plot.Min, plot.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.08f, 0.10f, 1.f)), 3.f);
			if (hist.count < 2) {
				dl->AddText(ImVec2(plot.Min.x + 8.f, plot.Min.y + 8.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"收集資料中…"));
				return;
			}
			float vmax = 1.f;
			for (int i = 0; i < hist.count; ++i) {
				vmax = ImMax(vmax, hist.samples[i].download_bps);
				vmax = ImMax(vmax, hist.samples[i].upload_bps);
			}
			const float pw = plot.GetWidth();
			const float ph = plot.GetHeight();
			auto draw_series = [&](bool upload) {
				for (int i = 0; i < hist.count; ++i) {
					const float v = upload ? hist.samples[i].upload_bps : hist.samples[i].download_bps;
					const float x = plot.Min.x + pw * (static_cast<float>(i) / static_cast<float>(hist.count - 1));
					const float y = plot.Max.y - ph * ImClamp(v / vmax, 0.f, 1.f);
					if (i == 0) {
						dl->PathClear();
						dl->PathLineTo(ImVec2(x, y));
					}
					else {
						dl->PathLineTo(ImVec2(x, y));
					}
				}
				const ImVec4 col = upload
					? ImVec4(0.55f, 0.9f, 1.f, 1.f) : cyan_mid();
				dl->PathStroke(ImGui::GetColorU32(col), 0, 2.f);
			};
			draw_series(false);
			draw_series(true);
		}

		static void DrawNetHBar(const char* label, float value, float max_val,
			const ImVec4& col, const char* suffix = nullptr, const char* value_text = nullptr)
		{
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float row_h = 28.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 4.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float val_w = 72.f;
			const float label_w = ImClamp(w * 0.30f, 72.f, 140.f);
			const float bar_w = ImMax(24.f, w - label_w - val_w - 8.f);
			const ImVec2 label_pos(pos.x, pos.y + 4.f);
			const ImVec2 label_max(pos.x + label_w - 4.f, pos.y + row_h);
			ImGui::PushClipRect(label_pos, label_max, true);
			dl->AddText(label_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
			ImGui::PopClipRect();
			const float bar_x = pos.x + label_w;
			const ImRect track(bar_x, pos.y + 12.f, bar_x + bar_w, pos.y + 20.f);
			dl->AddRectFilled(track.Min, track.Max, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 3.f);
			const float ratio = (max_val > 0.f && value >= 0.f)
				? ImClamp(value / max_val, 0.f, 1.f) : 0.f;
			dl->AddRectFilled(track.Min, ImVec2(track.Min.x + bar_w * ratio, track.Max.y),
				ImGui::GetColorU32(col), 3.f);
			char val[32] = {};
			if (value_text != nullptr && value_text[0] != '\0') {
				strncpy_s(val, value_text, _TRUNCATE);
			}
			else if (value < 0.f) {
				strncpy_s(val, "—", _TRUNCATE);
			}
			else if (suffix != nullptr) {
				snprintf(val, sizeof(val), "%.0f%s", value, suffix);
			}
			else {
				snprintf(val, sizeof(val), "%.0f", value);
			}
			const ImVec2 vs = ImGui::CalcTextSize(val);
			dl->AddText(ImVec2(pos.x + w - vs.x, pos.y + 4.f),
				ImGui::GetColorU32(col), val);
		}

		static void DrawNetArcSpeedGauge(const ImRect& bb, float mbps, float max_mbps,
			const char* title)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
			const float pad_top = 30.f;
			const float pad_bottom = 14.f;
			const float inner_h = ImMax(40.f, bb.GetHeight() - pad_top - pad_bottom);
			const ImVec2 center(bb.Min.x + bb.GetWidth() * 0.5f,
				bb.Min.y + pad_top + inner_h * 0.72f);
			const float radius = ImMin(bb.GetWidth() * 0.38f, inner_h * 0.55f);
			const float ratio = (mbps >= 0.f && max_mbps > 0.f)
				? ImClamp(mbps / max_mbps, 0.f, 1.f) : 0.f;
			const float a0 = IM_PI;
			const float a1 = IM_PI * (1.f - ratio);
			dl->PathClear();
			dl->PathArcTo(center, radius, IM_PI, 0.f, 32);
			dl->PathStroke(ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 0, 10.f);
			if (ratio > 0.01f) {
				dl->PathClear();
				dl->PathArcTo(center, radius, a0, a1, 32);
				dl->PathStroke(ImGui::GetColorU32(cyan_neon()), 0, 10.f);
			}
			char val_txt[32] = {};
			if (mbps >= 0.f) {
				snprintf(val_txt, sizeof(val_txt), "%.1f", mbps);
			}
			else {
				strncpy_s(val_txt, "—", _TRUNCATE);
			}
			const ImVec2 ts = ImGui::CalcTextSize(val_txt);
			dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - radius * 0.55f),
				ImGui::GetColorU32(cyan_neon()), val_txt);
			const char* unit = "Mbps";
			const ImVec2 us = ImGui::CalcTextSize(unit);
			dl->AddText(ImVec2(center.x - us.x * 0.5f, center.y - radius * 0.55f + ts.y + 2.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), unit);
		}

		static void DrawNetStatChip(const ImRect& bb, const char* label, const char* value,
			const ImVec4& value_col)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 26.f),
				ImGui::GetColorU32(value_col), value);
		}

		static void DrawNetSectionTitle(const char* title, const char* subtitle = nullptr)
		{
			ImGui::Dummy(ImVec2(0.f, 4.f));
			ImGui::TextColored(cyan_neon(), "%s", title);
			if (subtitle != nullptr && subtitle[0] != '\0') {
				ImGui::SameLine(0.f, 10.f);
				ImGui::TextDisabled("%s", subtitle);
			}
			ImGui::Dummy(ImVec2(0.f, 2.f));
		}

		static void DrawNetKpiTile(const ImRect& bb, const char* label, const char* value,
			const ImVec4& value_color)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
			const ImVec2 val_min(bb.Min.x + 10.f, bb.Min.y + 26.f);
			const ImVec2 val_max(bb.Max.x - 8.f, bb.Max.y - 6.f);
			ImGui::PushClipRect(val_min, val_max, true);
			ImGui::PushStyleColor(ImGuiCol_Text, value_color);
			ImGui::RenderTextClipped(val_min, val_max, value, nullptr, nullptr,
				ImVec2(0.f, 0.f), nullptr);
			ImGui::PopStyleColor();
			ImGui::PopClipRect();
		}

		static void DrawNetRingGauge(const ImRect& bb, float ratio, const char* title,
			const char* center_text, const char* sub_text, const ImVec4& accent)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
			const float pad_top = 28.f;
			const float inner_h = ImMax(48.f, bb.GetHeight() - pad_top - 10.f);
			const ImVec2 center(bb.Min.x + bb.GetWidth() * 0.5f,
				bb.Min.y + pad_top + inner_h * 0.52f);
			const float radius = ImMin(bb.GetWidth() * 0.32f, inner_h * 0.42f);
			const float thick = 9.f;
			const float clamped = ImClamp(ratio, 0.f, 1.f);
			const float a0 = -IM_PI * 0.5f;
			const float a1 = a0 + IM_PI * 2.f * clamped;
			dl->PathClear();
			dl->PathArcTo(center, radius, 0.f, IM_PI * 2.f, 48);
			dl->PathStroke(ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 0, thick);
			if (clamped > 0.01f) {
				dl->PathClear();
				dl->PathArcTo(center, radius, a0, a1, 48);
				dl->PathStroke(ImGui::GetColorU32(accent), 0, thick);
			}
			if (center_text != nullptr && center_text[0] != '\0') {
				const ImVec2 ts = ImGui::CalcTextSize(center_text);
				dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f - 4.f),
					ImGui::GetColorU32(accent), center_text);
			}
			if (sub_text != nullptr && sub_text[0] != '\0') {
				const ImVec2 us = ImGui::CalcTextSize(sub_text);
				dl->AddText(ImVec2(center.x - us.x * 0.5f, center.y + 6.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), sub_text);
			}
		}

		static void DrawDnsSummaryTiles(const std::vector<OptimizeNetworkScan::DnsBenchRow>& rows,
			bool running)
		{
			int fastest_ms = -1;
			char fastest_lbl[40] = {};
			int sum_ms = 0;
			int ok_count = 0;
			int fail_count = 0;
			for (const auto& row : rows) {
				if (row.resolve_ms >= 0) {
					sum_ms += row.resolve_ms;
					++ok_count;
					if (fastest_ms < 0 || row.resolve_ms < fastest_ms) {
						fastest_ms = row.resolve_ms;
						strncpy_s(fastest_lbl, row.label, _TRUNCATE);
					}
				}
				else if (!running) {
					++fail_count;
				}
			}
			char v0[48] = {}, v1[48] = {}, v2[48] = {};
			ImVec4 c0 = cyan_neon();
			ImVec4 c1 = cyan_mid();
			ImVec4 c2 = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			if (running) {
				strncpy_s(v0, I18N(u8"測試中…"), _TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				strncpy_s(v2, "—", _TRUNCATE);
			}
			else if (ok_count == 0 && rows.empty()) {
				strncpy_s(v0, I18N(u8"尚未測速"), _TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				strncpy_s(v2, "—", _TRUNCATE);
			}
			else {
				if (fastest_ms >= 0) {
					snprintf(v0, sizeof(v0), "%d ms · %s", fastest_ms, UiTxt(fastest_lbl));
					c0 = fastest_ms < 80
						? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
						: ImVec4(1.f, 0.85f, 0.4f, 1.f);
				}
				else {
					strncpy_s(v0, I18N(u8"全部失敗"), _TRUNCATE);
					c0 = ImVec4(1.f, 0.45f, 0.35f, 1.f);
				}
				if (ok_count > 0) {
					snprintf(v1, sizeof(v1), I18N(u8"%d ms（%d 組）"),
						sum_ms / ok_count, ok_count);
				}
				else {
					strncpy_s(v1, "—", _TRUNCATE);
				}
				snprintf(v2, sizeof(v2), I18N(u8"%d 組"), fail_count);
				if (fail_count > 0) {
					c2 = ImVec4(1.f, 0.78f, 0.45f, 1.f);
				}
			}
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float gap = 6.f;
			const float tile_w = ImMax(72.f, (w - gap * 2.f) / 3.f);
			const float tile_h = 64.f;
			const char* labels[3] = { I18N(u8"最快解析"), I18N(u8"平均解析"), I18N(u8"解析失敗") };
			const char* vals[3] = { v0, v1, v2 };
			const ImVec4* cols[3] = { &c0, &c1, &c2 };
			for (int i = 0; i < 3; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				const ImVec2 tp = ImGui::GetCursorScreenPos();
				DrawNetKpiTile(ImRect(tp, ImVec2(tp.x + tile_w, tp.y + tile_h)),
					labels[i], vals[i], *cols[i]);
				ImGui::Dummy(ImVec2(tile_w, tile_h));
			}
		}

		static void DrawDnsCompareBars(const std::vector<OptimizeNetworkScan::DnsBenchRow>& rows,
			bool running)
		{
			if (rows.empty() && !running) {
				return;
			}
			int max_ms = 80;
			for (const auto& row : rows) {
				if (row.resolve_ms > max_ms) {
					max_ms = row.resolve_ms;
				}
			}
			max_ms = ImMax(120, ((max_ms + 39) / 40) * 40);
			const float panel_h = ImMin(28.f * static_cast<float>(rows.size()) + 36.f, 220.f);
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, panel_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect panel(pos, ImVec2(pos.x + w, pos.y + panel_h));
			dl->AddRectFilled(panel.Min, panel.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(panel.Min, panel.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(panel.Min.x + 12.f, panel.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"解析延遲對比（橫條越短越快）"));
			float y = panel.Min.y + 30.f;
			const float label_w = ImClamp(w * 0.28f, 64.f, 120.f);
			const float bar_max_w = ImMax(40.f, w - label_w - 88.f);
			for (size_t i = 0; i < rows.size() && y + 26.f <= panel.Max.y; ++i) {
				const auto& row = rows[i];
				char short_lbl[24] = {};
				strncpy_s(short_lbl, row.label, _TRUNCATE);
				dl->AddText(ImVec2(panel.Min.x + 12.f, y),
					ImGui::GetColorU32(cyan_neon()), UiTxt(short_lbl));
				const ImRect track(panel.Min.x + label_w, y + 10.f,
					panel.Min.x + label_w + bar_max_w, y + 18.f);
				dl->AddRectFilled(track.Min, track.Max,
					ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 3.f);
				char val_txt[24] = {};
				ImVec4 col = ImVec4(1.f, 0.45f, 0.35f, 1.f);
				if (running && row.resolve_ms < 0) {
					strncpy_s(val_txt, "…", _TRUNCATE);
				}
				else if (row.resolve_ms >= 0) {
					snprintf(val_txt, sizeof(val_txt), "%d ms", row.resolve_ms);
					col = row.resolve_ms < 80
						? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
						: (row.resolve_ms < 160
							? ImVec4(1.f, 0.85f, 0.4f, 1.f)
							: ImVec4(1.f, 0.55f, 0.38f, 1.f));
					const float ratio = ImClamp(static_cast<float>(row.resolve_ms)
						/ static_cast<float>(max_ms), 0.f, 1.f);
					dl->AddRectFilled(track.Min,
						ImVec2(track.Min.x + bar_max_w * ratio, track.Max.y),
						ImGui::GetColorU32(col), 3.f);
					if (row.is_fastest) {
						const char* tag = "★";
						dl->AddText(ImVec2(track.Max.x + 6.f, y),
							ImGui::GetColorU32(ImVec4(0.45f, 0.95f, 0.75f, 1.f)), tag);
					}
				}
				else {
					strncpy_s(val_txt, I18N(u8"失敗"), _TRUNCATE);
				}
				const ImVec2 vs = ImGui::CalcTextSize(val_txt);
				dl->AddText(ImVec2(panel.Max.x - vs.x - 12.f, y),
					ImGui::GetColorU32(col), val_txt);
				y += 26.f;
			}
		}

		static void DnsResolveLevel(int ms, const char** out_label, ImVec4* out_col)
		{
			if (ms < 0) {
				*out_label = I18N(u8"失敗");
				*out_col = ImVec4(1.f, 0.45f, 0.35f, 1.f);
			}
			else if (ms < 80) {
				*out_label = I18N(u8"極快");
				*out_col = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			}
			else if (ms < 160) {
				*out_label = I18N(u8"普通");
				*out_col = ImVec4(1.f, 0.85f, 0.4f, 1.f);
			}
			else {
				*out_label = I18N(u8"偏慢");
				*out_col = ImVec4(1.f, 0.55f, 0.38f, 1.f);
			}
		}

		static void DrawDnsStatusStrip(const OptimizeNetworkScan::Snapshot& ns,
			const OptimizeNetworkScan::DnsBenchSnapshot& bench,
			bool running, bool full_running)
		{
			char headline_buf[96] = {};
			char action_buf[120] = {};
			const char* headline = I18N(u8"尚未執行 DNS 測速");
			const char* action = I18N(u8"點「開始測速」比較 8 組公共 DNS 與自訂 DNS。");
			ImVec4 accent = ImVec4(0.55f, 0.58f, 0.62f, 1.f);
			if (running || full_running) {
				headline = I18N(u8"DNS 測速進行中…");
				action = full_running
					? I18N(u8"全面診斷：測速、連線測試與網路診斷。")
					: I18N(u8"正在 Ping 與解析測試網域，請稍候。");
				accent = cyan_mid();
			}
			else if (!bench.rows.empty()) {
				const OptimizeNetworkScan::DnsBenchRow* fastest = nullptr;
				const OptimizeNetworkScan::DnsBenchRow* current = nullptr;
				int fail_count = 0;
				for (const auto& row : bench.rows) {
					if (row.resolve_ms < 0) {
						++fail_count;
						continue;
					}
					if (row.is_fastest) {
						fastest = &row;
					}
					if (row.is_current) {
						current = &row;
					}
				}
				if (fastest == nullptr && fail_count == static_cast<int>(bench.rows.size())) {
					headline = I18N(u8"✕ 所有 DNS 解析失敗");
					action = I18N(u8"請檢查網路連線，或嘗試「改回自動 DNS」後重測。");
					accent = ImVec4(1.f, 0.45f, 0.35f, 1.f);
				}
				else if (current != nullptr && current->is_fastest) {
					snprintf(headline_buf, sizeof(headline_buf),
						I18N(u8"✓ 目前 %s 已是最快"), current->label);
					headline = headline_buf;
					snprintf(action_buf, sizeof(action_buf), I18N(u8"解析 %d ms · Ping %d ms"),
						current->resolve_ms, current->ping_ms);
					action = action_buf;
					accent = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				}
				else if (fastest != nullptr) {
					snprintf(headline_buf, sizeof(headline_buf),
						I18N(u8"△ 建議切換至 %s"), fastest->label);
					headline = headline_buf;
					if (current != nullptr) {
						snprintf(action_buf, sizeof(action_buf),
							I18N(u8"目前 %s %d ms → 最快 %d ms（快 %d ms）"),
							current->label, current->resolve_ms, fastest->resolve_ms,
							current->resolve_ms - fastest->resolve_ms);
					}
					else {
						snprintf(action_buf, sizeof(action_buf),
							I18N(u8"最快解析 %d ms · %s"), fastest->resolve_ms, fastest->server_ip);
					}
					action = action_buf;
					accent = ImVec4(1.f, 0.85f, 0.4f, 1.f);
				}
				else if (fail_count > 0) {
					snprintf(headline_buf, sizeof(headline_buf),
						I18N(u8"△ 部分 DNS 解析失敗（%d 組）"), fail_count);
					headline = headline_buf;
					action = I18N(u8"可查看下方紅色標記項目，或更換測試網域。");
					accent = ImVec4(1.f, 0.85f, 0.4f, 1.f);
				}
				else {
					strncpy_s(headline_buf, I18N(u8"✓ DNS 測速完成"), _TRUNCATE);
					headline = headline_buf;
					if (bench.status_text[0] != '\0') {
						strncpy_s(action_buf, UiTxt(bench.status_text), _TRUNCATE);
						action = action_buf;
					}
					accent = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				}
			}
			else if (ns.dns_primary[0] != '\0') {
				snprintf(headline_buf, sizeof(headline_buf), I18N(u8"目前使用手動 DNS"));
				headline = headline_buf;
				snprintf(action_buf, sizeof(action_buf), "%s%s%s",
					ns.dns_primary,
					ns.dns_secondary[0] ? " / " : "",
					ns.dns_secondary[0] ? ns.dns_secondary : "");
				action = action_buf;
				accent = cyan_mid();
			}

			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float status_h = 56.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, status_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect status_bb(pos, ImVec2(pos.x + w, pos.y + status_h));
			dl->AddRectFilled(status_bb.Min, status_bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(status_bb.Min, status_bb.Max, ImGui::GetColorU32(accent), kRounding, 0, 1.5f);
			dl->AddCircleFilled(ImVec2(status_bb.Min.x + 22.f, status_bb.Min.y + status_h * 0.5f),
				10.f, ImGui::GetColorU32(accent));
			dl->AddText(ImVec2(status_bb.Min.x + 42.f, status_bb.Min.y + 10.f),
				ImGui::GetColorU32(cyan_neon()), headline);
			dl->AddText(ImVec2(status_bb.Min.x + 42.f, status_bb.Min.y + 30.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), action);
		}

		static void DrawDnsOverviewTiles(const OptimizeNetworkScan::Snapshot& ns,
			const std::vector<OptimizeNetworkScan::DnsBenchRow>& rows, bool running)
		{
			char v0[48] = {}, v1[48] = {}, v2[48] = {}, v3[32] = {};
			ImVec4 c0 = cyan_mid(), c1 = cyan_neon();
			ImVec4 c2 = ImVec4(0.65f, 0.68f, 0.72f, 1.f), c3 = cyan_neon();
			if (ns.dns_primary[0] != '\0') {
				snprintf(v0, sizeof(v0), "%s%s%s",
					ns.dns_primary,
					ns.dns_secondary[0] ? " / " : "",
					ns.dns_secondary[0] ? ns.dns_secondary : "");
			}
			else {
				strncpy_s(v0, I18N(u8"自動取得"), _TRUNCATE);
				c0 = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			}
			int fastest_ms = -1;
			char fastest_lbl[32] = {};
			int sum_ms = 0;
			int ok_count = 0;
			int fail_count = 0;
			for (const auto& row : rows) {
				if (row.resolve_ms >= 0) {
					sum_ms += row.resolve_ms;
					++ok_count;
					if (fastest_ms < 0 || row.resolve_ms < fastest_ms) {
						fastest_ms = row.resolve_ms;
						strncpy_s(fastest_lbl, row.label, _TRUNCATE);
					}
				}
				else if (!running) {
					++fail_count;
				}
			}
			if (running) {
				strncpy_s(v1, I18N(u8"測試中…"), _TRUNCATE);
				strncpy_s(v2, "—", _TRUNCATE);
				snprintf(v3, sizeof(v3), I18N(u8"%zu 組"), rows.size());
			}
			else if (rows.empty()) {
				strncpy_s(v1, I18N(u8"尚未測速"), _TRUNCATE);
				strncpy_s(v2, "—", _TRUNCATE);
				strncpy_s(v3, I18N(u8"0 組"), _TRUNCATE);
			}
			else {
				if (fastest_ms >= 0) {
					snprintf(v1, sizeof(v1), "%s · %d ms", fastest_lbl, fastest_ms);
					c1 = fastest_ms < 80
						? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
						: ImVec4(1.f, 0.85f, 0.4f, 1.f);
				}
				else {
					strncpy_s(v1, I18N(u8"全部失敗"), _TRUNCATE);
					c1 = ImVec4(1.f, 0.45f, 0.35f, 1.f);
				}
				if (ok_count > 0) {
					snprintf(v2, sizeof(v2), I18N(u8"%d ms（%d 組）"), sum_ms / ok_count, ok_count);
					c2 = NetLatencyColor(sum_ms / ok_count);
				}
				else {
					strncpy_s(v2, "—", _TRUNCATE);
				}
				snprintf(v3, sizeof(v3), I18N(u8"%d 失敗 / %zu"), fail_count, rows.size());
				if (fail_count > 0) {
					c3 = ImVec4(1.f, 0.78f, 0.45f, 1.f);
				}
			}
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float gap = 6.f;
			const float tile_w = ImMax(72.f, (w - gap * 3.f) / 4.f);
			const float tile_h = 64.f;
			const char* labels[4] = { I18N(u8"目前 DNS"), I18N(u8"最快解析"), I18N(u8"平均解析"), I18N(u8"測速結果") };
			const char* vals[4] = { v0, v1, v2, v3 };
			const ImVec4* cols[4] = { &c0, &c1, &c2, &c3 };
			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				const ImVec2 tp = ImGui::GetCursorScreenPos();
				DrawNetKpiTile(ImRect(tp, ImVec2(tp.x + tile_w, tp.y + tile_h)),
					labels[i], vals[i], *cols[i]);
				ImGui::Dummy(ImVec2(tile_w, tile_h));
			}
		}

		static void DrawDnsConfigChips(const OptimizeNetworkScan::Snapshot& ns,
			const char* test_domain)
		{
			const float chip_h = 56.f;
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float chip_w = ImMax(100.f, (w - 12.f) / 3.f);
			auto draw_chip = [&](const char* label, const char* value, const ImVec4& col) {
				const ImVec2 p = ImGui::GetCursorScreenPos();
				DrawNetStatChip(ImRect(p, ImVec2(p.x + chip_w, p.y + chip_h)), label, value, col);
				ImGui::Dummy(ImVec2(chip_w, chip_h));
			};
			char pri[48] = {};
			strncpy_s(pri, ns.dns_primary[0] ? ns.dns_primary : I18N(u8"自動"), _TRUNCATE);
			draw_chip(I18N(u8"主要 DNS"), pri, ns.dns_primary[0] ? cyan_neon() : ImVec4(0.65f, 0.68f, 0.72f, 1.f));
			ImGui::SameLine(0.f, 6.f);
			char sec[48] = {};
			strncpy_s(sec, ns.dns_secondary[0] ? ns.dns_secondary : "—", _TRUNCATE);
			draw_chip(I18N(u8"備用 DNS"), sec, cyan_mid());
			ImGui::SameLine(0.f, 6.f);
			char dom[48] = {};
			strncpy_s(dom, (test_domain != nullptr && test_domain[0] != '\0')
				? test_domain : I18N(u8"（未設定）"), _TRUNCATE);
			draw_chip(I18N(u8"測試網域"), dom, cyan_neon());
		}

		static void DrawDnsTableHeader(float row_w)
		{
			const float row_h = 28.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(row_w, row_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + row_w, pos.y + row_h));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.09f, 0.11f, 1.f)), 4.f);
			const float col_rank = 32.f;
			const float col_ping = 52.f;
			const float col_lvl = 44.f;
			const float col_btn = 72.f;
			const float name_x = bb.Min.x + col_rank + 4.f;
			const float bar_right = bb.Max.x - col_lvl - col_ping - col_btn - 8.f;
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "#");
			dl->AddText(ImVec2(name_x, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"DNS 伺服器"));
			dl->AddText(ImVec2(bar_right, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"解析延遲"));
			dl->AddText(ImVec2(bb.Max.x - col_lvl - col_ping - col_btn, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "Ping");
			dl->AddText(ImVec2(bb.Max.x - col_lvl - col_btn, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"等級"));
		}

		static void DrawDnsRankRow(const OptimizeNetworkScan::DnsBenchRow& row, int rank,
			int max_ms, int index, bool running)
		{
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float row_h = 68.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 4.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + w, pos.y + row_h));
			const bool failed = !running && row.resolve_ms < 0;
			const ImVec4 border_col = row.is_current
				? ImVec4(0.55f, 0.75f, 1.f, 1.f)
				: (row.is_fastest ? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
					: (failed ? ImVec4(0.45f, 0.22f, 0.18f, 1.f) : cyan_dark()));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border_col), kRounding, 0,
				(row.is_current || row.is_fastest) ? 1.4f : 1.f);

			const float col_rank = 32.f;
			const float col_ping = 52.f;
			const float col_lvl = 44.f;
			const float col_btn = 72.f;
			const float name_x = bb.Min.x + col_rank + 4.f;
			const float name_max = bb.Max.x - col_lvl - col_ping - col_btn - 120.f;
			const float bar_left = name_max + 6.f;
			const float bar_right = bb.Max.x - col_lvl - col_ping - col_btn - 6.f;

			char rank_txt[8] = {};
			snprintf(rank_txt, sizeof(rank_txt), "%d", rank);
			const ImVec2 rs = ImGui::CalcTextSize(rank_txt);
			dl->AddText(ImVec2(bb.Min.x + (col_rank - rs.x) * 0.5f, bb.Min.y + 12.f),
				ImGui::GetColorU32(ProcessRankColor(rank)), rank_txt);

			ImGui::PushClipRect(ImVec2(name_x, bb.Min.y), ImVec2(name_max, bb.Max.y), true);
			dl->AddText(ImVec2(name_x, bb.Min.y + 8.f),
				ImGui::GetColorU32(cyan_neon()), UiTxt(row.label));
			char sub[120] = {};
			if (row.secondary_ip[0] != '\0') {
				snprintf(sub, sizeof(sub), "%s / %s", row.server_ip, row.secondary_ip);
			}
			else {
				snprintf(sub, sizeof(sub), "%s", row.server_ip);
			}
			dl->AddText(ImVec2(name_x, bb.Min.y + 26.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
			float tag_x = name_x;
			if (row.is_fastest) {
				const char* tag = I18N(u8"最快");
				const ImVec2 ts = ImGui::CalcTextSize(tag);
				const ImRect pill(ImVec2(tag_x, bb.Min.y + 44.f),
					ImVec2(tag_x + ts.x + 12.f, bb.Min.y + 58.f));
				dl->AddRectFilled(pill.Min, pill.Max,
					ImGui::GetColorU32(ImVec4(0.08f, 0.22f, 0.18f, 1.f)), 3.f);
				dl->AddText(ImVec2(pill.Min.x + 6.f, pill.Min.y + 2.f),
					ImGui::GetColorU32(ImVec4(0.45f, 0.95f, 0.75f, 1.f)), tag);
				tag_x = pill.Max.x + 4.f;
			}
			if (row.is_current) {
				const char* tag = I18N(u8"目前");
				const ImVec2 ts = ImGui::CalcTextSize(tag);
				const ImRect pill(ImVec2(tag_x, bb.Min.y + 44.f),
					ImVec2(tag_x + ts.x + 12.f, bb.Min.y + 58.f));
				dl->AddRectFilled(pill.Min, pill.Max,
					ImGui::GetColorU32(ImVec4(0.10f, 0.14f, 0.22f, 1.f)), 3.f);
				dl->AddText(ImVec2(pill.Min.x + 6.f, pill.Min.y + 2.f),
					ImGui::GetColorU32(ImVec4(0.70f, 0.82f, 1.f, 1.f)), tag);
			}
			ImGui::PopClipRect();

			const float bar_w = ImMax(20.f, bar_right - bar_left);
			const ImRect track(bar_left, bb.Min.y + 28.f, bar_left + bar_w, bb.Min.y + 36.f);
			dl->AddRectFilled(track.Min, track.Max,
				ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 3.f);
			const char* lvl_lbl = nullptr;
			ImVec4 lvl_col = {};
			if (running && row.resolve_ms < 0) {
				DnsResolveLevel(-1, &lvl_lbl, &lvl_col);
			}
			else {
				DnsResolveLevel(row.resolve_ms, &lvl_lbl, &lvl_col);
				if (row.resolve_ms >= 0 && max_ms > 0) {
					const float ratio = ImClamp(static_cast<float>(row.resolve_ms)
						/ static_cast<float>(max_ms), 0.05f, 1.f);
					dl->AddRectFilled(track.Min,
						ImVec2(track.Min.x + bar_w * ratio, track.Max.y),
						ImGui::GetColorU32(lvl_col), 3.f);
				}
			}

			char ping_txt[16] = {};
			if (running && row.ping_ms < 0) {
				strncpy_s(ping_txt, "…", _TRUNCATE);
			}
			else if (row.ping_ms >= 0) {
				snprintf(ping_txt, sizeof(ping_txt), "%d ms", row.ping_ms);
			}
			else {
				strncpy_s(ping_txt, "—", _TRUNCATE);
			}
			const ImVec2 pcs = ImGui::CalcTextSize(ping_txt);
			dl->AddText(
				ImVec2(bb.Max.x - col_lvl - col_ping - col_btn + (col_ping - pcs.x) * 0.5f,
					bb.Min.y + 24.f),
				ImGui::GetColorU32(NetLatencyColor(row.ping_ms)), ping_txt);

			const ImVec2 ls = ImGui::CalcTextSize(lvl_lbl);
			const ImRect lvl_pill(
				ImVec2(bb.Max.x - col_lvl - col_btn + (col_lvl - ls.x - 12.f) * 0.5f,
					bb.Min.y + 20.f),
				ImVec2(bb.Max.x - col_btn - 4.f, bb.Min.y + 20.f + ls.y + 10.f));
			ImVec4 lvl_bg = ImVec4(lvl_col.x * 0.18f, lvl_col.y * 0.18f, lvl_col.z * 0.18f, 1.f);
			dl->AddRectFilled(lvl_pill.Min, lvl_pill.Max, ImGui::GetColorU32(lvl_bg), 4.f);
			dl->AddText(ImVec2(lvl_pill.Min.x + 6.f, lvl_pill.Min.y + 4.f),
				ImGui::GetColorU32(lvl_col), lvl_lbl);

			const bool show_btn = !running && !row.is_current
				&& (row.apply_provider >= 0 || row.apply_as_custom);
			if (show_btn) {
				const ImRect btn_bb(ImVec2(bb.Max.x - col_btn - 4.f, bb.Min.y + 18.f),
					ImVec2(bb.Max.x - 8.f, bb.Min.y + 46.f));
				char btn_id[32] = {};
				snprintf(btn_id, sizeof(btn_id), "##dns_rank_use_%d", index);
				if (CyberTextButton(btn_id, btn_bb, I18N(u8"切換"), row.is_fastest, false)) {
					char act[48] = {};
					snprintf(act, sizeof(act), I18N(u8"改用 %s"), UiTxt(row.label));
					bool ok = false;
					if (row.apply_as_custom) {
						ok = OptimizeNetworkScan::SetAdapterDnsCustom(
							row.server_ip,
							row.secondary_ip[0] != '\0' ? row.secondary_ip : nullptr);
					}
					else {
						ok = OptimizeNetworkScan::SetAdapterDnsPublic(row.apply_provider);
					}
					NetReportResult(act, ok);
				}
			}
			else if (!running && row.is_current) {
				dl->AddText(ImVec2(bb.Max.x - col_btn - 2.f, bb.Min.y + 26.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"使用中"));
			}
		}

		static void DrawDnsAdvStatusChips(const OptimizeNetworkScan::Snapshot& ns)
		{
			const float chip_h = 56.f;
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float chip_w = ImMax(88.f, (w - 18.f) / 4.f);
			static const char* doh_policy_labels[] = { I18N(u8"允許 DNS"), I18N(u8"強制 DoH"), I18N(u8"停用 DoH") };
			const int policy_idx = (ns.doh_policy >= 0 && ns.doh_policy <= 2)
				? ns.doh_policy : 0;
			struct ChipItem {
				const char* label;
				char value[24];
				ImVec4 col;
			};
			ChipItem chips[4] = {};
			strncpy_s(chips[0].value, ns.doh_auto_enabled ? I18N(u8"已開啟") : I18N(u8"已關閉"), _TRUNCATE);
			chips[0].label = I18N(u8"自動 DoH");
			chips[0].col = OnOffColor(ns.doh_auto_enabled);
			strncpy_s(chips[1].value, ns.llmnr_enabled ? I18N(u8"已啟用") : I18N(u8"已停用"), _TRUNCATE);
			chips[1].label = "LLMNR";
			chips[1].col = OnOffColor(ns.llmnr_enabled);
			strncpy_s(chips[2].value, doh_policy_labels[policy_idx], _TRUNCATE);
			chips[2].label = I18N(u8"DoH 策略");
			chips[2].col = OnOffColor(policy_idx == 1);
			strncpy_s(chips[3].value, ns.parallel_dns_queries ? I18N(u8"已啟用") : I18N(u8"已停用"), _TRUNCATE);
			chips[3].label = I18N(u8"平行查詢");
			chips[3].col = OnOffColor(ns.parallel_dns_queries);
			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, 6.f);
				}
				const ImVec2 p = ImGui::GetCursorScreenPos();
				DrawNetStatChip(ImRect(p, ImVec2(p.x + chip_w, p.y + chip_h)),
					chips[i].label, chips[i].value, chips[i].col);
				ImGui::Dummy(ImVec2(chip_w, chip_h));
			}
		}

		static void DrawCustomDnsCompactRow(const OptimizeNetworkScan::CustomDnsEntry& e,
			size_t index)
		{
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float row_h = 48.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 4.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + w, pos.y + row_h));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			char title[48] = {};
			strncpy_s(title, e.label[0] ? UiTxt(e.label) : I18N(u8"自訂 DNS"), _TRUNCATE);
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(cyan_neon()), title);
			char sub[120] = {};
			snprintf(sub, sizeof(sub), "%s%s%s",
				e.primary, e.secondary[0] ? " / " : "", e.secondary[0] ? e.secondary : "");
			dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + 26.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
			const float btn_w = 56.f;
			const ImRect use_bb(ImVec2(bb.Max.x - btn_w * 2.f - 14.f, bb.Min.y + 10.f),
				ImVec2(bb.Max.x - btn_w - 10.f, bb.Min.y + 38.f));
			const ImRect del_bb(ImVec2(bb.Max.x - btn_w - 6.f, bb.Min.y + 10.f),
				ImVec2(bb.Max.x - 6.f, bb.Min.y + 38.f));
			char use_id[32] = {}, del_id[32] = {};
			snprintf(use_id, sizeof(use_id), "##cdn_use_%zu", index);
			snprintf(del_id, sizeof(del_id), "##cdn_del_%zu", index);
			if (CyberTextButton(use_id, use_bb, I18N(u8"套用"), false, false)) {
				NetReportResult(I18N(u8"套用 DNS"), OptimizeNetworkScan::SetAdapterDnsCustom(
					e.primary, e.secondary[0] != '\0' ? e.secondary : nullptr));
			}
			if (CyberTextButton(del_id, del_bb, I18N(u8"刪除"), false, false)) {
				OptimizeNetworkScan::RemoveCustomDnsEntry(static_cast<int>(index));
			}
		}

		static void DrawLinkTestCell(const OptimizeNetworkScan::LinkTestRow& row, float cell_w)
		{
			const float cell_h = 76.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(cell_w, cell_h + 4.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + cell_w, pos.y + cell_h));
			ImVec4 accent = ImVec4(0.35f, 0.38f, 0.42f, 1.f);
			ImVec4 bg_tint = ImVec4(0.06f, 0.08f, 0.10f, 1.f);
			char val[32] = {};
			char sub[32] = {};
			if (row.reachable) {
				snprintf(val, sizeof(val), "%d ms", row.ping_ms);
				accent = NetLatencyColor(row.ping_ms);
				bg_tint = ImVec4(accent.x * 0.12f, accent.y * 0.12f, accent.z * 0.12f, 1.f);
				if (row.jitter_ms >= 0) {
					snprintf(sub, sizeof(sub), I18N(u8"抖動 %d ms"), row.jitter_ms);
				}
				else {
					strncpy_s(sub, NetLatencyGrade(row.ping_ms), _TRUNCATE);
				}
			}
			else {
				strncpy_s(val, I18N(u8"逾時"), _TRUNCATE);
				strncpy_s(sub, I18N(u8"無法連線"), _TRUNCATE);
				accent = ImVec4(1.f, 0.45f, 0.35f, 1.f);
				bg_tint = ImVec4(0.18f, 0.08f, 0.06f, 1.f);
			}
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bg_tint), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(accent), kRounding, 0, 1.2f);
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(cyan_neon()), row.name);
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 28.f),
				ImGui::GetColorU32(accent), val);
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 48.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
			if (row.target[0] != '\0' && ImGui::IsMouseHoveringRect(bb.Min, bb.Max)) {
				ImGui::SetTooltip("%s", row.target);
			}
		}

		static void DrawLinkTestMatrix(const std::vector<OptimizeNetworkScan::LinkTestRow>& rows,
			int columns)
		{
			if (rows.empty()) {
				return;
			}
			const int cols = ImClamp(columns, 2, 4);
			const float full_w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float gap = 6.f;
			const float cell_w = ImMax(88.f, (full_w - gap * static_cast<float>(cols - 1))
				/ static_cast<float>(cols));
			if (ImGui::BeginTable("##link_matrix", cols,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX)) {
				for (size_t i = 0; i < rows.size(); ++i) {
					if (i % static_cast<size_t>(cols) == 0) {
						ImGui::TableNextRow();
					}
					ImGui::TableNextColumn();
					DrawLinkTestCell(rows[i], cell_w);
				}
				ImGui::EndTable();
			}
		}

		static void DrawNetDualSpeedGauges(const ImRect& bb, float dl_mbps, float ul_mbps,
			float max_mbps)
		{
			const float half_w = bb.GetWidth() * 0.5f - 4.f;
			const ImRect dl_bb(bb.Min, ImVec2(bb.Min.x + half_w, bb.Max.y));
			const ImRect ul_bb(ImVec2(bb.Min.x + half_w + 8.f, bb.Min.y), bb.Max);
			DrawNetArcSpeedGauge(dl_bb, dl_mbps, max_mbps, I18N(u8"下載"));
			DrawNetArcSpeedGauge(ul_bb, ul_mbps, max_mbps, I18N(u8"上傳"));
		}

		struct LinkTestStats {
			int ok_count = 0;
			int fail_count = 0;
			int avg_ms = -1;
			int best_ms = -1;
			int worst_ms = -1;
			char best_name[48] = {};
			char worst_name[48] = {};
		};

		static LinkTestStats ComputeLinkTestStats(
			const std::vector<OptimizeNetworkScan::LinkTestRow>& rows)
		{
			LinkTestStats st = {};
			int sum = 0;
			for (const auto& row : rows) {
				if (row.reachable && row.ping_ms >= 0) {
					++st.ok_count;
					sum += row.ping_ms;
					if (st.best_ms < 0 || row.ping_ms < st.best_ms) {
						st.best_ms = row.ping_ms;
						strncpy_s(st.best_name, row.name, _TRUNCATE);
					}
					if (st.worst_ms < 0 || row.ping_ms > st.worst_ms) {
						st.worst_ms = row.ping_ms;
						strncpy_s(st.worst_name, row.name, _TRUNCATE);
					}
				}
				else {
					++st.fail_count;
				}
			}
			if (st.ok_count > 0) {
				st.avg_ms = sum / st.ok_count;
			}
			return st;
		}

		static void DrawNetTestProgressBar(float progress, const char* label)
		{
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float bar_h = 22.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, bar_h + 18.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			if (label != nullptr && label[0] != '\0') {
				dl->AddText(pos, ImGui::GetColorU32(cyan_mid()), label);
			}
			const ImRect track(pos.x, pos.y + 16.f, pos.x + w, pos.y + 16.f + 6.f);
			dl->AddRectFilled(track.Min, track.Max,
				ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 4.f);
			const float ratio = ImClamp(progress, 0.f, 1.f);
			dl->AddRectFilled(track.Min, ImVec2(track.Min.x + w * ratio, track.Max.y),
				ImGui::GetColorU32(cyan_neon()), 4.f);
			char pct[16] = {};
			snprintf(pct, sizeof(pct), "%.0f%%", ratio * 100.f);
			const ImVec2 ps = ImGui::CalcTextSize(pct);
			dl->AddText(ImVec2(pos.x + w - ps.x, pos.y),
				ImGui::GetColorU32(cyan_neon()), pct);
		}

		static void DrawNetTestStatusStrip(
			const OptimizeNetworkScan::LinkTestSnapshot& link,
			const OptimizeNetworkScan::SpeedTestSnapshot& speed,
			const LinkTestStats& link_st,
			bool link_running, bool speed_running, bool full_net_running)
		{
			char headline_buf[80] = {};
			char action_buf[96] = {};
			const char* headline = I18N(u8"尚未執行測試");
			const char* action = I18N(u8"點「測試連線」或「全面測試」開始檢查網路。");
			ImVec4 accent = ImVec4(0.55f, 0.58f, 0.62f, 1.f);
			if (full_net_running || link_running || speed_running) {
				headline = I18N(u8"網路測試進行中…");
				action = full_net_running
					? I18N(u8"正在依序執行連線、DNS、下載、上傳與診斷。")
					: (link_running ? I18N(u8"正在測試各節點連線延遲。") : I18N(u8"正在測量頻寬速度。"));
				accent = cyan_mid();
			}
			else if (!link.rows.empty()) {
				const int total = link_st.ok_count + link_st.fail_count;
				if (link_st.fail_count > 0 && link_st.ok_count == 0) {
					headline = I18N(u8"✕ 所有節點均無法連線");
					action = I18N(u8"請檢查路由器、網路線或 Wi‑Fi，再按「一鍵修復」。");
					accent = ImVec4(1.f, 0.45f, 0.35f, 1.f);
				}
				else if (link_st.fail_count > 0) {
					snprintf(headline_buf, sizeof(headline_buf),
						I18N(u8"△ %d/%d 節點正常，%d 個失敗"),
						link_st.ok_count, total, link_st.fail_count);
					headline = headline_buf;
					action = I18N(u8"部分網站或 DNS 可能異常，可查看下方紅色節點。");
					accent = ImVec4(1.f, 0.85f, 0.4f, 1.f);
				}
				else {
					snprintf(headline_buf, sizeof(headline_buf),
						I18N(u8"✓ 全部 %d 節點連線正常"), link_st.ok_count);
					headline = headline_buf;
					if (link_st.avg_ms >= 0) {
						snprintf(action_buf, sizeof(action_buf), I18N(u8"平均延遲 %d ms · %s"),
							link_st.avg_ms, NetLatencyGrade(link_st.avg_ms));
						action = action_buf;
					}
					else {
						action = I18N(u8"連線狀態良好。");
					}
					accent = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				}
			}
			if (!link_running && !speed_running && !full_net_running
				&& (speed.download_mbps >= 0.f || speed.upload_mbps >= 0.f)) {
				if (speed.download_mbps >= 0.f && speed.upload_mbps >= 0.f) {
					snprintf(action_buf, sizeof(action_buf), I18N(u8"下載 %.1f · 上傳 %.1f Mbps"),
						speed.download_mbps, speed.upload_mbps);
				}
				else if (speed.download_mbps >= 0.f) {
					snprintf(action_buf, sizeof(action_buf), I18N(u8"下載 %.1f Mbps"),
						speed.download_mbps);
				}
				else {
					snprintf(action_buf, sizeof(action_buf), I18N(u8"上傳 %.1f Mbps"),
						speed.upload_mbps);
				}
				action = action_buf;
			}

			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float status_h = 56.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, status_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect status_bb(pos, ImVec2(pos.x + w, pos.y + status_h));
			dl->AddRectFilled(status_bb.Min, status_bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(status_bb.Min, status_bb.Max, ImGui::GetColorU32(accent), kRounding, 0, 1.5f);
			dl->AddCircleFilled(ImVec2(status_bb.Min.x + 22.f, status_bb.Min.y + status_h * 0.5f),
				10.f, ImGui::GetColorU32(accent));
			dl->AddText(ImVec2(status_bb.Min.x + 42.f, status_bb.Min.y + 10.f),
				ImGui::GetColorU32(cyan_neon()), headline);
			dl->AddText(ImVec2(status_bb.Min.x + 42.f, status_bb.Min.y + 30.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), action);
		}

		static void DrawLinkTestSummaryTiles(const LinkTestStats& st, int total_rows,
			bool running)
		{
			char v0[32] = {}, v1[32] = {}, v2[40] = {}, v3[40] = {};
			ImVec4 c0 = cyan_neon(), c1 = cyan_mid(), c2 = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			ImVec4 c3 = ImVec4(1.f, 0.72f, 0.38f, 1.f);
			if (running) {
				strncpy_s(v0, I18N(u8"測試中"), _TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				strncpy_s(v2, "—", _TRUNCATE);
				strncpy_s(v3, "—", _TRUNCATE);
			}
			else if (total_rows == 0) {
				strncpy_s(v0, "—", _TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				strncpy_s(v2, I18N(u8"尚未測試"), _TRUNCATE);
				strncpy_s(v3, "—", _TRUNCATE);
			}
			else {
				snprintf(v0, sizeof(v0), "%d / %d", st.ok_count, total_rows);
				c0 = (st.fail_count == 0)
					? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
					: ImVec4(1.f, 0.78f, 0.45f, 1.f);
				if (st.avg_ms >= 0) {
					snprintf(v1, sizeof(v1), "%d ms", st.avg_ms);
					c1 = NetLatencyColor(st.avg_ms);
				}
				else {
					strncpy_s(v1, "—", _TRUNCATE);
				}
				if (st.best_ms >= 0) {
					snprintf(v2, sizeof(v2), "%s · %d ms", st.best_name, st.best_ms);
					c2 = NetLatencyColor(st.best_ms);
				}
				else {
					strncpy_s(v2, "—", _TRUNCATE);
				}
				if (st.worst_ms >= 0) {
					snprintf(v3, sizeof(v3), "%s · %d ms", st.worst_name, st.worst_ms);
					c3 = NetLatencyColor(st.worst_ms);
				}
				else {
					strncpy_s(v3, "—", _TRUNCATE);
				}
			}
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float gap = 6.f;
			const float tile_w = ImMax(72.f, (w - gap * 3.f) / 4.f);
			const float tile_h = 64.f;
			const char* labels[4] = { I18N(u8"節點通過"), I18N(u8"平均延遲"), I18N(u8"最快節點"), I18N(u8"最慢節點") };
			const char* vals[4] = { v0, v1, v2, v3 };
			const ImVec4* cols[4] = { &c0, &c1, &c2, &c3 };
			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				const ImVec2 tp = ImGui::GetCursorScreenPos();
				DrawNetKpiTile(ImRect(tp, ImVec2(tp.x + tile_w, tp.y + tile_h)),
					labels[i], vals[i], *cols[i]);
				ImGui::Dummy(ImVec2(tile_w, tile_h));
			}
		}

		static void DrawLinkLatencyTopBars(
			const std::vector<OptimizeNetworkScan::LinkTestRow>& rows, int max_rows)
		{
			std::vector<const OptimizeNetworkScan::LinkTestRow*> sorted;
			sorted.reserve(rows.size());
			for (const auto& row : rows) {
				if (row.reachable && row.ping_ms >= 0) {
					sorted.push_back(&row);
				}
			}
			if (sorted.empty()) {
				return;
			}
			std::sort(sorted.begin(), sorted.end(),
				[](const OptimizeNetworkScan::LinkTestRow* a,
					const OptimizeNetworkScan::LinkTestRow* b) {
					return a->ping_ms < b->ping_ms;
				});
			const int show_n = ImMin(max_rows, static_cast<int>(sorted.size()));
			int max_ms = 1;
			for (int i = 0; i < show_n; ++i) {
				max_ms = ImMax(max_ms, sorted[static_cast<size_t>(i)]->ping_ms);
			}
			const float panel_h = 34.f + 26.f * static_cast<float>(show_n);
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, panel_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect panel(pos, ImVec2(pos.x + w, pos.y + panel_h));
			dl->AddRectFilled(panel.Min, panel.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(panel.Min, panel.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(panel.Min.x + 12.f, panel.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"延遲排行（條越短越快）"));
			float y = panel.Min.y + 30.f;
			const float label_w = ImClamp(w * 0.32f, 80.f, 160.f);
			const float bar_max_w = ImMax(48.f, w - label_w - 56.f);
			for (int i = 0; i < show_n; ++i) {
				const auto& row = *sorted[static_cast<size_t>(i)];
				const ImVec4 col = NetLatencyColor(row.ping_ms);
				char rank_txt[8] = {};
				snprintf(rank_txt, sizeof(rank_txt), "#%d", i + 1);
				dl->AddText(ImVec2(panel.Min.x + 12.f, y),
					ImGui::GetColorU32(ProcessRankColor(i + 1)), rank_txt);
				dl->AddText(ImVec2(panel.Min.x + 40.f, y),
					ImGui::GetColorU32(cyan_neon()), row.name);
				const ImRect track(panel.Min.x + label_w, y + 10.f,
					panel.Min.x + label_w + bar_max_w, y + 18.f);
				dl->AddRectFilled(track.Min, track.Max,
					ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 3.f);
				const float ratio = ImClamp(static_cast<float>(row.ping_ms)
					/ static_cast<float>(max_ms), 0.05f, 1.f);
				dl->AddRectFilled(track.Min,
					ImVec2(track.Min.x + bar_max_w * ratio, track.Max.y),
					ImGui::GetColorU32(col), 3.f);
				char ms_txt[16] = {};
				snprintf(ms_txt, sizeof(ms_txt), "%d ms", row.ping_ms);
				const ImVec2 ms = ImGui::CalcTextSize(ms_txt);
				dl->AddText(ImVec2(panel.Max.x - ms.x - 12.f, y),
					ImGui::GetColorU32(col), ms_txt);
				y += 26.f;
			}
		}

		static void DrawLinkDnsHttpChips(const OptimizeNetworkScan::LinkTestSnapshot& link,
			bool running)
		{
			const float chip_h = 56.f;
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float chip_w = ImMax(100.f, (w - 6.f) * 0.5f);
			auto draw_chip = [&](const char* label, const char* value, const ImVec4& col) {
				const ImVec2 p = ImGui::GetCursorScreenPos();
				DrawNetStatChip(ImRect(p, ImVec2(p.x + chip_w, p.y + chip_h)), label, value, col);
				ImGui::Dummy(ImVec2(chip_w, chip_h));
			};
			char dns_val[32] = {};
			ImVec4 dns_col = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			if (running && link.dns_resolve_ms < 0) {
				strncpy_s(dns_val, I18N(u8"測試中…"), _TRUNCATE);
			}
			else if (link.dns_resolve_ms >= 0) {
				snprintf(dns_val, sizeof(dns_val), "%d ms · %s",
					link.dns_resolve_ms, link.dns_ok ? I18N(u8"正常") : I18N(u8"異常"));
				dns_col = link.dns_ok
					? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
					: ImVec4(1.f, 0.45f, 0.35f, 1.f);
			}
			else {
				strncpy_s(dns_val, I18N(u8"尚未測試"), _TRUNCATE);
			}
			draw_chip(I18N(u8"DNS 解析"), dns_val, dns_col);
			ImGui::SameLine(0.f, 6.f);
			char http_val[32] = {};
			ImVec4 http_col = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			if (running && link.http_ms < 0) {
				strncpy_s(http_val, I18N(u8"測試中…"), _TRUNCATE);
			}
			else if (link.http_ms >= 0) {
				snprintf(http_val, sizeof(http_val), "%d ms · %s",
					link.http_ms, link.http_ok ? I18N(u8"正常") : I18N(u8"異常"));
				http_col = link.http_ok
					? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
					: ImVec4(1.f, 0.45f, 0.35f, 1.f);
			}
			else {
				strncpy_s(http_val, I18N(u8"尚未測試"), _TRUNCATE);
			}
			draw_chip(I18N(u8"HTTPS 連線"), http_val, http_col);
		}

		static void SpeedBandwidthGrade(float mbps, const char** out_label, ImVec4* out_col)
		{
			if (mbps < 0.f) {
				*out_label = "—";
				*out_col = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			}
			else if (mbps >= 100.f) {
				*out_label = I18N(u8"極快");
				*out_col = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			}
			else if (mbps >= 50.f) {
				*out_label = I18N(u8"快速");
				*out_col = ImVec4(0.55f, 0.9f, 1.f, 1.f);
			}
			else if (mbps >= 10.f) {
				*out_label = I18N(u8"普通");
				*out_col = ImVec4(1.f, 0.85f, 0.4f, 1.f);
			}
			else {
				*out_label = I18N(u8"較慢");
				*out_col = ImVec4(1.f, 0.55f, 0.38f, 1.f);
			}
		}

		static void DrawSpeedTestSummaryTiles(
			const OptimizeNetworkScan::SpeedTestSnapshot& speed, bool running)
		{
			char v0[32] = {}, v1[32] = {}, v2[32] = {}, v3[24] = {};
			ImVec4 c0 = cyan_mid(), c1 = ImVec4(0.55f, 0.9f, 1.f, 1.f);
			ImVec4 c2 = cyan_neon(), c3 = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			if (running) {
				strncpy_s(v0, speed.upload_mode ? I18N(u8"上傳中") : I18N(u8"下載中"), _TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				snprintf(v2, sizeof(v2), "%.0f%%", speed.progress * 100.f);
				strncpy_s(v3, I18N(u8"測速中"), _TRUNCATE);
			}
			else {
				if (speed.download_mbps >= 0.f) {
					snprintf(v0, sizeof(v0), "%.1f Mbps", speed.download_mbps);
					const char* g = nullptr;
					SpeedBandwidthGrade(speed.download_mbps, &g, &c0);
				}
				else {
					strncpy_s(v0, I18N(u8"尚未測試"), _TRUNCATE);
					c0 = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
				}
				if (speed.upload_mbps >= 0.f) {
					snprintf(v1, sizeof(v1), "%.1f Mbps", speed.upload_mbps);
					const char* g = nullptr;
					SpeedBandwidthGrade(speed.upload_mbps, &g, &c1);
				}
				else {
					strncpy_s(v1, I18N(u8"尚未測試"), _TRUNCATE);
					c1 = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
				}
				if (speed.peak_mbps >= 0.f) {
					snprintf(v2, sizeof(v2), "%.1f Mbps", speed.peak_mbps);
				}
				else {
					strncpy_s(v2, "—", _TRUNCATE);
				}
				const float ref = speed.download_mbps >= 0.f ? speed.download_mbps : speed.upload_mbps;
				const char* grade = nullptr;
				SpeedBandwidthGrade(ref, &grade, &c3);
				strncpy_s(v3, grade, _TRUNCATE);
			}
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float gap = 6.f;
			const float tile_w = ImMax(72.f, (w - gap * 3.f) / 4.f);
			const float tile_h = 64.f;
			const char* labels[4] = { I18N(u8"下載速度"), I18N(u8"上傳速度"), I18N(u8"峰值"), I18N(u8"綜合評級") };
			const char* vals[4] = { v0, v1, v2, v3 };
			const ImVec4* cols[4] = { &c0, &c1, &c2, &c3 };
			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				const ImVec2 tp = ImGui::GetCursorScreenPos();
				DrawNetKpiTile(ImRect(tp, ImVec2(tp.x + tile_w, tp.y + tile_h)),
					labels[i], vals[i], *cols[i]);
				ImGui::Dummy(ImVec2(tile_w, tile_h));
			}
		}

		static void DrawNetDashboard(const OptimizeNetworkScan::Snapshot& ns)
		{
			const NetHealthLevel level = ComputeNetHealth(ns);
			const char* headline = I18N(u8"✓ 網路正常，可以上網");
			const char* action = I18N(u8"目前一切正常，無需操作。");
			ImVec4 accent = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			if (level == NetHealthLevel::Warning) {
				headline = I18N(u8"△ 能上網，但建議優化");
				action = I18N(u8"→ 請點下方「一鍵修復網路」或「DNS 測速」");
				accent = ImVec4(1.f, 0.85f, 0.4f, 1.f);
			}
			else if (level == NetHealthLevel::Bad) {
				headline = I18N(u8"✕ 目前無法上網");
				action = I18N(u8"→ 請先點下方「一鍵修復網路」，仍不行再檢查路由器");
				accent = ImVec4(1.f, 0.45f, 0.35f, 1.f);
			}

			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float status_h = 56.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, status_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect status_bb(pos, ImVec2(pos.x + w, pos.y + status_h));
			dl->AddRectFilled(status_bb.Min, status_bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(status_bb.Min, status_bb.Max, ImGui::GetColorU32(accent), kRounding, 0, 1.5f);
			dl->AddCircleFilled(ImVec2(status_bb.Min.x + 22.f, status_bb.Min.y + status_h * 0.5f),
				10.f, ImGui::GetColorU32(accent));
			dl->AddText(ImVec2(status_bb.Min.x + 42.f, status_bb.Min.y + 10.f),
				ImGui::GetColorU32(cyan_neon()), headline);
			dl->AddText(ImVec2(status_bb.Min.x + 42.f, status_bb.Min.y + 30.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), action);

			ImGui::Dummy(ImVec2(0.f, 8.f));
			const float gap = 6.f;
			const float tile_w = ImMax(72.f, (w - gap * 3.f) / 4.f);
			const float tile_h = 64.f;

			struct KpiItem {
				const char* label;
				char value[40];
				ImVec4 color;
			};
			KpiItem kpis[4] = {};
			if (ns.internet_ping_ms >= 0) {
				snprintf(kpis[0].value, sizeof(kpis[0].value), "%d ms · %s",
					ns.internet_ping_ms, NetLatencyGrade(ns.internet_ping_ms));
			}
			else {
				strncpy_s(kpis[0].value, I18N(u8"無法連線"), _TRUNCATE);
			}
			kpis[0].label = I18N(u8"上網延遲");
			kpis[0].color = NetLatencyColor(ns.internet_ping_ms);

			if (ns.gateway_ping_ms >= 0) {
				snprintf(kpis[1].value, sizeof(kpis[1].value), "%d ms", ns.gateway_ping_ms);
			}
			else {
				strncpy_s(kpis[1].value, ns.gateway[0] ? I18N(u8"無回應") : "—", _TRUNCATE);
			}
			kpis[1].label = I18N(u8"路由器");
			kpis[1].color = NetLatencyColor(ns.gateway_ping_ms);

			FormatBitrate(ns.download_bps, kpis[2].value, sizeof(kpis[2].value));
			if (ns.download_bps < 0.f) {
				strncpy_s(kpis[2].value, I18N(u8"測量中"), _TRUNCATE);
			}
			kpis[2].label = I18N(u8"下載");
			kpis[2].color = cyan_mid();

			FormatBitrate(ns.upload_bps, kpis[3].value, sizeof(kpis[3].value));
			if (ns.upload_bps < 0.f) {
				strncpy_s(kpis[3].value, I18N(u8"測量中"), _TRUNCATE);
			}
			kpis[3].label = I18N(u8"上傳");
			kpis[3].color = ImVec4(0.55f, 0.9f, 1.f, 1.f);

			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				const ImVec2 tp = ImGui::GetCursorScreenPos();
				DrawNetKpiTile(ImRect(tp, ImVec2(tp.x + tile_w, tp.y + tile_h)),
					kpis[i].label, kpis[i].value, kpis[i].color);
				ImGui::Dummy(ImVec2(tile_w, tile_h));
			}
		}

		static void DrawNetDetailsPanel(const OptimizeNetworkScan::Snapshot& ns)
		{
			char dns_line[96] = {};
			if (ns.dns_primary[0] != '\0') {
				snprintf(dns_line, sizeof(dns_line), "%s%s%s",
					ns.dns_primary,
					ns.dns_secondary[0] ? "、" : "",
					ns.dns_secondary[0] ? ns.dns_secondary : "");
			}
			else {
				strncpy_s(dns_line, I18N(u8"自動取得"), _TRUNCATE);
			}

			char proxy_line[120] = {};
			if (ns.proxy_enabled) {
				snprintf(proxy_line, sizeof(proxy_line), "%s",
					ns.proxy_server[0] ? ns.proxy_server : I18N(u8"已開啟（系統代理）"));
			}
			else {
				strncpy_s(proxy_line, I18N(u8"未使用"), _TRUNCATE);
			}

			DrawKeyValueRow(I18N(u8"連線名稱"), ns.adapter_name[0] ? ns.adapter_name : "—");
			DrawKeyValueRow(I18N(u8"本機 IP（IPv4）"), ns.ipv4[0] ? ns.ipv4 : "—");
			DrawKeyValueRow(I18N(u8"本機 IP（IPv6）"), ns.ipv6[0] ? ns.ipv6 : "—");
			DrawKeyValueRow(I18N(u8"網址解析（DNS）"), dns_line);
			DrawKeyValueRow(I18N(u8"路由器位址"), ns.gateway[0] ? ns.gateway : "—");
			for (const auto& ad : ns.adapters) {
				if (ad.connected && ad.link_speed_bps > 0) {
					char link_spd[32] = {};
					FormatLinkSpeed(ad.link_speed_bps, link_spd, sizeof(link_spd));
					DrawKeyValueRow(I18N(u8"介面卡速率"), link_spd);
					break;
				}
			}
			ImVec4 proxy_col = ns.proxy_enabled
				? ImVec4(1.f, 0.78f, 0.45f, 1.f) : ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			DrawKeyValueRow(I18N(u8"上網代理"), proxy_line, &proxy_col);

			if (!ns.adapters.empty()) {
				ImGui::Dummy(ImVec2(0.f, 6.f));
				ImGui::TextDisabled(I18N(u8"所有網路介面（%zu）"), ns.adapters.size());
				for (size_t i = 0; i < ns.adapters.size() && i < 6; ++i) {
					const auto& ad = ns.adapters[i];
					char line[200] = {};
					snprintf(line, sizeof(line), "%s%s · %s",
						ad.friendly_name[0] ? ad.friendly_name : ad.desc_utf8,
						ad.connected ? I18N(u8" · 使用中") : "",
						ad.ipv4[0] ? ad.ipv4 : I18N(u8"無 IP"));
					ImGui::BulletText("%s", line);
				}
			}
		}

		static float DnsSpeedCardHeight(const OptimizeNetworkScan::DnsBenchRow& row, bool running)
		{
			const bool show_button = !running && !row.is_current
				&& (row.apply_provider >= 0 || row.apply_as_custom);
			return show_button ? 124.f : 96.f;
		}

		static void DrawDnsSpeedCard(const OptimizeNetworkScan::DnsBenchRow& row, int index,
			bool running, float card_w)
		{
			const float w = ImMax(100.f, card_w);
			const float row_h = DnsSpeedCardHeight(row, running);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 8.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + w, pos.y + row_h));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);

			const char* row_label = UiTxt(row.label);
			dl->AddText(ImVec2(bb.Min.x + 12.f, bb.Min.y + 8.f),
				ImGui::GetColorU32(cyan_neon()), row_label);
			float tag_x = bb.Min.x + 12.f + ImGui::CalcTextSize(row_label).x + 8.f;
			if (row.is_fastest) {
				const char* tag = I18N(u8"最快");
				const ImVec2 ts = ImGui::CalcTextSize(tag);
				const ImRect pill(ImVec2(tag_x, bb.Min.y + 6.f),
					ImVec2(tag_x + ts.x + 14.f, bb.Min.y + 22.f));
				dl->AddRectFilled(pill.Min, pill.Max, ImGui::GetColorU32(ImVec4(0.08f, 0.22f, 0.18f, 1.f)), 3.f);
				dl->AddText(ImVec2(pill.Min.x + 7.f, pill.Min.y + 3.f),
					ImGui::GetColorU32(ImVec4(0.45f, 0.95f, 0.75f, 1.f)), tag);
				tag_x = pill.Max.x + 6.f;
			}
			if (row.is_current) {
				const char* tag = I18N(u8"目前");
				const ImVec2 ts = ImGui::CalcTextSize(tag);
				const ImRect pill(ImVec2(tag_x, bb.Min.y + 6.f),
					ImVec2(tag_x + ts.x + 14.f, bb.Min.y + 22.f));
				dl->AddRectFilled(pill.Min, pill.Max, ImGui::GetColorU32(ImVec4(0.10f, 0.14f, 0.22f, 1.f)), 3.f);
				dl->AddText(ImVec2(pill.Min.x + 7.f, pill.Min.y + 3.f),
					ImGui::GetColorU32(ImVec4(0.70f, 0.82f, 1.f, 1.f)), tag);
			}

			char sub[160] = {};
			if (row.secondary_ip[0] != '\0') {
				snprintf(sub, sizeof(sub), "%s / %s · %s",
					row.server_ip, row.secondary_ip, UiTxt(row.hint));
			}
			else {
				snprintf(sub, sizeof(sub), "%s · %s", row.server_ip, UiTxt(row.hint));
			}
			dl->AddText(ImVec2(bb.Min.x + 12.f, bb.Min.y + 28.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);

			char metrics[96] = {};
			if (running) {
				strncpy_s(metrics, I18N(u8"測試中…"), _TRUNCATE);
			}
			else {
				const char* ping_txt = row.ping_ms >= 0 ? nullptr : I18N(u8"逾時");
				char ping_buf[24] = {};
				if (row.ping_ms >= 0) {
					snprintf(ping_buf, sizeof(ping_buf), "%d ms", row.ping_ms);
					ping_txt = ping_buf;
				}
				if (row.resolve_ms >= 0) {
					snprintf(metrics, sizeof(metrics), I18N(u8"Ping %s · 解析 %d ms"),
						ping_txt, row.resolve_ms);
				}
				else {
					snprintf(metrics, sizeof(metrics), I18N(u8"Ping %s · 解析失敗"), ping_txt);
				}
			}
			const ImVec4 speed_col = row.resolve_ms >= 0 && row.resolve_ms < 80
				? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
				: (row.resolve_ms < 0 ? ImVec4(1.f, 0.45f, 0.35f, 1.f) : ImVec4(1.f, 0.85f, 0.4f, 1.f));
			dl->AddText(ImVec2(bb.Min.x + 12.f, bb.Min.y + 46.f),
				ImGui::GetColorU32(speed_col), metrics);

			const float bar_w = ImMin(160.f, w - 24.f);
			const ImRect track(ImVec2(bb.Min.x + 12.f, bb.Min.y + 64.f),
				ImVec2(bb.Min.x + 12.f + bar_w, bb.Min.y + 72.f));
			dl->AddRectFilled(track.Min, track.Max, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 4.f);
			if (row.resolve_ms >= 0) {
				const float ratio = 1.f - ImClamp(row.resolve_ms / 300.f, 0.f, 1.f);
				dl->AddRectFilled(track.Min,
					ImVec2(track.Min.x + bar_w * ratio, track.Max.y),
					ImGui::GetColorU32(speed_col), 4.f);
			}

			const bool show_button = !running && !row.is_current
				&& (row.apply_provider >= 0 || row.apply_as_custom);
			if (show_button) {
				const ImRect btn_bb(ImVec2(bb.Min.x + 12.f, bb.Max.y - 30.f),
					ImVec2(bb.Max.x - 12.f, bb.Max.y - 8.f));
				char btn_id[32] = {};
				snprintf(btn_id, sizeof(btn_id), "##dns_use_%d", index);
				if (CyberTextButton(btn_id, btn_bb, I18N(u8"切換到此 DNS"), row.is_fastest, false)) {
					char act[48] = {};
					snprintf(act, sizeof(act), I18N(u8"改用 %s"), UiTxt(row.label));
					bool ok = false;
					if (row.apply_as_custom) {
						ok = OptimizeNetworkScan::SetAdapterDnsCustom(
							row.server_ip,
							row.secondary_ip[0] != '\0' ? row.secondary_ip : nullptr);
					}
					else {
						ok = OptimizeNetworkScan::SetAdapterDnsPublic(row.apply_provider);
					}
					NetReportResult(act, ok);
				}
			}
			else if (!running && row.is_current) {
				dl->AddText(ImVec2(bb.Min.x + 12.f, bb.Max.y - 24.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"正在使用"));
			}
		}

		static void ProcessActivityLevel(int score, const char** out_label, ImVec4* out_col)
		{
			if (score >= 12) {
				*out_label = I18N(u8"高");
				*out_col = ImVec4(1.f, 0.55f, 0.38f, 1.f);
			}
			else if (score >= 5) {
				*out_label = I18N(u8"中");
				*out_col = ImVec4(1.f, 0.85f, 0.4f, 1.f);
			}
			else {
				*out_label = I18N(u8"低");
				*out_col = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
			}
		}

		static void DrawProcessSummaryTiles(const OptimizeNetworkScan::Snapshot& ns)
		{
			char v0[32] = {}, v1[32] = {}, v2[32] = {}, v3[48] = {};
			snprintf(v0, sizeof(v0), "%d", ns.active_process_count);
			snprintf(v1, sizeof(v1), "%d", ns.total_tcp_connections);
			snprintf(v2, sizeof(v2), "%d", ns.total_udp_connections);
			if (!ns.processes.empty()) {
				const auto& top = ns.processes.front();
				snprintf(v3, sizeof(v3), "%s · %d",
					top.name_utf8[0] ? top.name_utf8 : I18N(u8"未知"), top.activity_score);
			}
			else {
				strncpy_s(v3, ns.scanning ? I18N(u8"掃描中…") : "—", _TRUNCATE);
			}
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float gap = 6.f;
			const float tile_w = ImMax(72.f, (w - gap * 3.f) / 4.f);
			const float tile_h = 64.f;
			const char* labels[4] = { I18N(u8"有連線程式"), I18N(u8"TCP 總數"), I18N(u8"UDP 總數"), I18N(u8"占用最高") };
			const char* vals[4] = { v0, v1, v2, v3 };
			const ImVec4 cols[4] = {
				cyan_neon(),
				cyan_mid(),
				ImVec4(0.55f, 0.9f, 1.f, 1.f),
				ns.processes.empty() ? ImVec4(0.65f, 0.68f, 0.72f, 1.f)
					: ImVec4(1.f, 0.72f, 0.38f, 1.f),
			};
			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				const ImVec2 tp = ImGui::GetCursorScreenPos();
				DrawNetKpiTile(ImRect(tp, ImVec2(tp.x + tile_w, tp.y + tile_h)),
					labels[i], vals[i], cols[i]);
				ImGui::Dummy(ImVec2(tile_w, tile_h));
			}
		}

		static void DrawProcessTopBars(const std::vector<OptimizeNetworkScan::ProcessNetRow>& processes,
			int max_rows)
		{
			if (processes.empty()) {
				return;
			}
			const int show_n = ImMin(max_rows, static_cast<int>(processes.size()));
			int max_score = 1;
			for (int i = 0; i < show_n; ++i) {
				max_score = ImMax(max_score, processes[static_cast<size_t>(i)].activity_score);
			}
			const float panel_h = 34.f + 26.f * static_cast<float>(show_n);
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, panel_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect panel(pos, ImVec2(pos.x + w, pos.y + panel_h));
			dl->AddRectFilled(panel.Min, panel.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(panel.Min, panel.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(ImVec2(panel.Min.x + 12.f, panel.Min.y + 8.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"連線占用排行（條越長 = 連線越多）"));
			float y = panel.Min.y + 30.f;
			const float label_w = ImClamp(w * 0.32f, 80.f, 160.f);
			const float bar_max_w = ImMax(48.f, w - label_w - 56.f);
			for (int i = 0; i < show_n; ++i) {
				const auto& row = processes[static_cast<size_t>(i)];
				const ImVec4 rank_col = ProcessRankColor(i + 1);
				char rank_txt[8] = {};
				snprintf(rank_txt, sizeof(rank_txt), "#%d", i + 1);
				dl->AddText(ImVec2(panel.Min.x + 12.f, y),
					ImGui::GetColorU32(rank_col), rank_txt);
				char short_name[20] = {};
				strncpy_s(short_name, row.name_utf8[0] ? row.name_utf8 : "?", _TRUNCATE);
				if (ImGui::CalcTextSize(short_name).x > label_w - 36.f) {
					for (int c = static_cast<int>(strlen(short_name)) - 1; c > 3; --c) {
						short_name[c] = '\0';
						if (ImGui::CalcTextSize(short_name).x <= label_w - 40.f) {
							strcat_s(short_name, "…");
							break;
						}
					}
				}
				dl->AddText(ImVec2(panel.Min.x + 40.f, y),
					ImGui::GetColorU32(cyan_neon()), short_name);
				const ImRect track(panel.Min.x + label_w, y + 10.f,
					panel.Min.x + label_w + bar_max_w, y + 18.f);
				dl->AddRectFilled(track.Min, track.Max,
					ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 3.f);
				const float ratio = ImClamp(static_cast<float>(row.activity_score)
					/ static_cast<float>(max_score), 0.05f, 1.f);
				const char* lvl_lbl = nullptr;
				ImVec4 lvl_col = {};
				ProcessActivityLevel(row.activity_score, &lvl_lbl, &lvl_col);
				dl->AddRectFilled(track.Min,
					ImVec2(track.Min.x + bar_max_w * ratio, track.Max.y),
					ImGui::GetColorU32(lvl_col), 3.f);
				char cnt[16] = {};
				snprintf(cnt, sizeof(cnt), "%d", row.activity_score);
				const ImVec2 cs = ImGui::CalcTextSize(cnt);
				dl->AddText(ImVec2(panel.Max.x - cs.x - 12.f, y),
					ImGui::GetColorU32(lvl_col), cnt);
				y += 26.f;
			}
		}

		static void DrawProcessTableHeader(float row_w)
		{
			const float row_h = 28.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(row_w, row_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + row_w, pos.y + row_h));
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.09f, 0.11f, 1.f)), 4.f);
			const float col_rank = 36.f;
			const float col_tcp = 52.f;
			const float col_udp = 52.f;
			const float col_lvl = 44.f;
			const float name_x = bb.Min.x + col_rank + 6.f;
			const float bar_x = bb.Max.x - col_lvl - col_udp - col_tcp - 8.f;
			dl->AddText(ImVec2(bb.Min.x + 8.f, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "#");
			dl->AddText(ImVec2(name_x, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"程式名稱"));
			dl->AddText(ImVec2(bar_x, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"連線佔比"));
			dl->AddText(ImVec2(bb.Max.x - col_lvl - col_udp - col_tcp, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "TCP");
			dl->AddText(ImVec2(bb.Max.x - col_lvl - col_udp, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "UDP");
			dl->AddText(ImVec2(bb.Max.x - col_lvl, bb.Min.y + 6.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"等級"));
		}

		static void DrawNetProcessRankRow(const OptimizeNetworkScan::ProcessNetRow& row,
			int display_rank, int max_score, int index)
		{
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float row_h = 58.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(w, row_h + 4.f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect bb(pos, ImVec2(pos.x + w, pos.y + row_h));
			const bool hot = row.activity_score >= 12;
			const ImVec4 bg = hot
				? ImVec4(0.10f, 0.08f, 0.06f, 1.f)
				: card_bg();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bg), kRounding);
			dl->AddRect(bb.Min, bb.Max,
				ImGui::GetColorU32(hot ? ImVec4(0.45f, 0.28f, 0.12f, 1.f) : cyan_dark()),
				kRounding, 0, hot ? 1.4f : 1.f);

			const float col_rank = 36.f;
			const float col_tcp = 52.f;
			const float col_udp = 52.f;
			const float col_lvl = 44.f;
			const float name_x = bb.Min.x + col_rank + 6.f;
			const float name_max = bb.Max.x - col_lvl - col_udp - col_tcp - 120.f;
			const float bar_left = name_max + 8.f;
			const float bar_right = bb.Max.x - col_lvl - col_udp - col_tcp - 6.f;

			char rank_txt[8] = {};
			snprintf(rank_txt, sizeof(rank_txt), "%d", display_rank);
			const ImVec2 rs = ImGui::CalcTextSize(rank_txt);
			dl->AddText(ImVec2(bb.Min.x + (col_rank - rs.x) * 0.5f, bb.Min.y + 10.f),
				ImGui::GetColorU32(ProcessRankColor(display_rank)), rank_txt);

			const char* title = row.name_utf8[0] ? row.name_utf8 : I18N(u8"未知程式");
			ImGui::PushClipRect(ImVec2(name_x, bb.Min.y), ImVec2(name_max, bb.Max.y), true);
			dl->AddText(ImVec2(name_x, bb.Min.y + 8.f),
				ImGui::GetColorU32(cyan_neon()), title);
			char pid_line[48] = {};
			snprintf(pid_line, sizeof(pid_line), I18N(u8"PID %u · 共 %d 條連線"),
				static_cast<unsigned>(row.pid), row.activity_score);
			dl->AddText(ImVec2(name_x, bb.Min.y + 28.f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), pid_line);
			ImGui::PopClipRect();

			const float bar_w = ImMax(24.f, bar_right - bar_left);
			const ImRect track(bar_left, bb.Min.y + 22.f, bar_left + bar_w, bb.Min.y + 30.f);
			dl->AddRectFilled(track.Min, track.Max,
				ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.12f, 1.f)), 3.f);
			const float ratio = (max_score > 0)
				? ImClamp(static_cast<float>(row.activity_score) / static_cast<float>(max_score),
					0.05f, 1.f) : 0.f;
			const char* lvl_lbl = nullptr;
			ImVec4 lvl_col = {};
			ProcessActivityLevel(row.activity_score, &lvl_lbl, &lvl_col);
			dl->AddRectFilled(track.Min, ImVec2(track.Min.x + bar_w * ratio, track.Max.y),
				ImGui::GetColorU32(lvl_col), 3.f);

			char tcp_txt[12] = {};
			snprintf(tcp_txt, sizeof(tcp_txt), "%d", row.tcp_count);
			const ImVec2 tcs = ImGui::CalcTextSize(tcp_txt);
			dl->AddText(ImVec2(bb.Max.x - col_lvl - col_udp - col_tcp + (col_tcp - tcs.x) * 0.5f,
				bb.Min.y + 20.f), ImGui::GetColorU32(cyan_mid()), tcp_txt);

			char udp_txt[12] = {};
			snprintf(udp_txt, sizeof(udp_txt), "%d", row.udp_count);
			const ImVec2 ucs = ImGui::CalcTextSize(udp_txt);
			dl->AddText(ImVec2(bb.Max.x - col_lvl - col_udp + (col_udp - ucs.x) * 0.5f,
				bb.Min.y + 20.f), ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 1.f, 1.f)), udp_txt);

			const ImVec2 ls = ImGui::CalcTextSize(lvl_lbl);
			const ImRect lvl_pill(
				ImVec2(bb.Max.x - col_lvl + (col_lvl - ls.x - 14.f) * 0.5f, bb.Min.y + 16.f),
				ImVec2(bb.Max.x - 8.f, bb.Min.y + 16.f + ls.y + 10.f));
			ImVec4 lvl_bg = ImVec4(lvl_col.x * 0.18f, lvl_col.y * 0.18f, lvl_col.z * 0.18f, 1.f);
			dl->AddRectFilled(lvl_pill.Min, lvl_pill.Max, ImGui::GetColorU32(lvl_bg), 4.f);
			dl->AddText(ImVec2(lvl_pill.Min.x + 7.f, lvl_pill.Min.y + 4.f),
				ImGui::GetColorU32(lvl_col), lvl_lbl);

			if (ImGui::IsMouseHoveringRect(bb.Min, bb.Max)) {
				dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_neon()), kRounding, 0, 1.2f);
				if (row.path_utf8[0] != '\0') {
					ImGui::SetTooltip(I18N(u8"路徑：%s\nTCP %d · UDP %d · 總計 %d"),
						row.path_utf8, row.tcp_count, row.udp_count, row.activity_score);
				}
			}
			(void)index;
		}

		static void DrawNetworkSubTabBar()
		{
			const float gap = 6.f;
			const int tab_count = static_cast<int>(NetSubTab::Count);
			const float bar_h = kSubTabH + 4.f;
			const float avail_w = ImGui::GetContentRegionAvail().x;
			const ImVec2 outer_start = ImGui::GetCursorScreenPos();

			if (ImGui::BeginChild("##net_subtab_scroll", ImVec2(avail_w, bar_h), false,
				ImGuiWindowFlags_HorizontalScrollbar)) {
				float x = 0.f;
				for (int i = 0; i < tab_count; ++i) {
					const char* label = NetSubTabLabel(static_cast<NetSubTab>(i));
					const ImVec2 ts = ImGui::CalcTextSize(label);
					const float tw = ts.x + 24.f;
					const ImVec2 row_start = ImGui::GetCursorScreenPos();
					const ImRect bb(ImVec2(row_start.x + x, row_start.y),
						ImVec2(row_start.x + x + tw, row_start.y + kSubTabH));
					char id[48] = {};
					snprintf(id, sizeof(id), "##net_subtab_%d", i);
					if (CyberSubTabButton(id, bb, label, g_net_subtab == i)) {
						g_net_subtab = i;
					}
					x += tw + gap;
				}
				ImGui::Dummy(ImVec2(x, kSubTabH));
			}
			ImGui::EndChild();

			const ImVec2 line_y(outer_start.x, outer_start.y + kSubTabH + 2.f);
			DrawGradientHLine(ImGui::GetWindowDrawList(), line_y,
				ImVec2(outer_start.x + avail_w, line_y.y + 2.f));
			ImGui::Dummy(ImVec2(0.f, 4.f));
		}

		static void DrawLinkTestRow(const OptimizeNetworkScan::LinkTestRow& row)
		{
			char val[64] = {};
			ImVec4 col = ImVec4(1.f, 0.45f, 0.35f, 1.f);
			if (row.reachable) {
				if (row.jitter_ms >= 0) {
					snprintf(val, sizeof(val), I18N(u8"%d ms · 抖動 %d ms"),
						row.ping_ms, row.jitter_ms);
				}
				else {
					snprintf(val, sizeof(val), I18N(u8"%d 毫秒 · 正常"), row.ping_ms);
				}
				col = NetLatencyColor(row.ping_ms);
			}
			else {
				strncpy_s(val, I18N(u8"無法連線"), _TRUNCATE);
			}
			DrawKeyValueRow(row.name, val, &col);
		}

		static void NetTickCommon()
		{
			if (!g_net_did_request) {
				OptimizeNetworkScan::RequestRefresh();
				g_net_did_request = true;
			}
			OptimizeNetworkScan::TickBandwidth();
			g_net_auto_refresh += ImGui::GetIO().DeltaTime;
			if (g_net_auto_refresh >= 8.f) {
				g_net_auto_refresh = 0.f;
				OptimizeNetworkScan::RequestRefresh();
			}
		}

		static void DrawNetSubOverview(const OptimizeNetworkScan::Snapshot& ns)
		{
			if (BeginCyberPanel("##opt_panel_net_dash")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"網路總覽"));
				if (ns.scanning) {
					ImGui::SameLine(0.f, 10.f);
					ImGui::TextColored(cyan_mid(), I18NF(u8"檢查中 %.0f%%"), ns.progress * 100.f);
				}
				const char* msg = OptimizeNetworkScan::GetLastActionMessage();
				if (msg != nullptr && msg[0] != '\0') {
					ImGui::SameLine(0.f, 10.f);
					ImGui::TextDisabled("· %s", UiTxt(msg));
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				DrawNetDashboard(ns);
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_actions")) {
				DrawNetSectionTitle(I18N(u8"我想做什麼？"), I18N(u8"點卡片上的按鈕即可"));
				constexpr float action_h = 100.f;
				struct NetActionDef {
					const char* id;
					const char* title;
					const char* sub;
					const char* btn;
					bool accent;
				};
				NetActionDef actions[4] = {
					{ "##na_fix", I18N(u8"一鍵修復網路"), I18N(u8"網站慢、上不了網時先點這個"), I18N(u8"立即修復"), true },
					{ "##na_test", I18N(u8"網路測試"), I18N(u8"測延遲、連線與下載速度"), I18N(u8"前往測試"), true },
					{ "##na_dns", I18N(u8"DNS 設定"), I18N(u8"測速、手動新增與切換 DNS"), I18N(u8"前往 DNS"), false },
					{ "##na_sys", I18N(u8"系統網路設定"), I18N(u8"打開 Windows 網路設定頁"), I18N(u8"前往設定"), false },
				};
				if (ImGui::BeginTable("##net_action_tbl", 2,
					ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
					for (int i = 0; i < 4; ++i) {
						if (i % 2 == 0) {
							ImGui::TableNextRow(ImGuiTableRowFlags_None, action_h);
						}
						ImGui::TableNextColumn();
						const ImVec2 p0 = ImGui::GetCursorScreenPos();
						const float cw = ImMax(40.f, ImGui::GetContentRegionAvail().x);
						const ImRect bb(p0, ImVec2(p0.x + cw, p0.y + action_h - 4.f));
						if (DrawOverviewActionCard(bb, actions[i].id, actions[i].title,
							actions[i].sub, actions[i].btn, actions[i].accent)) {
							if (strcmp(actions[i].id, "##na_fix") == 0) {
								NetReportResult(I18N(u8"一鍵修復"), OptimizeNetworkScan::RunQuickNetworkRepair());
							}
							else if (strcmp(actions[i].id, "##na_test") == 0) {
								g_net_subtab = static_cast<int>(NetSubTab::SpeedTest);
								OptimizeNetworkScan::RequestLinkTest();
							}
							else if (strcmp(actions[i].id, "##na_dns") == 0) {
								g_net_subtab = static_cast<int>(NetSubTab::Dns);
								OptimizeNetworkScan::RequestDnsBenchmark();
							}
							else if (strcmp(actions[i].id, "##na_sys") == 0) {
								OptimizeNetworkScan::OpenWindowsNetworkSettings();
							}
						}
					}
					ImGui::EndTable();
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_info")) {
				DrawNetSectionTitle(I18N(u8"連線詳情"), I18N(u8"圖表與測試請至「監控」「DNS」「網路測試」分頁"));
				DrawNetDetailsPanel(ns);
				ImGui::Dummy(ImVec2(0.f, 4.f));
				char health_line[64] = {};
				snprintf(health_line, sizeof(health_line), I18N(u8"%d 分 · %s"),
					NetHealthScore(ns),
					ns.internet_reachable ? I18N(u8"可上網") : I18N(u8"無法上網"));
				ImVec4 health_col = ns.internet_reachable
					? NetLatencyColor(ns.internet_ping_ms)
					: ImVec4(1.f, 0.45f, 0.35f, 1.f);
				DrawKeyValueRow(I18N(u8"健康評分"), health_line, &health_col);
				if (ns.hosts_has_extra) {
					char hosts_warn[64] = {};
					snprintf(hosts_warn, sizeof(hosts_warn), I18N(u8"發現 %d 筆非預設項目"), ns.hosts_extra_count);
					ImVec4 warn_col = ImVec4(1.f, 0.78f, 0.45f, 1.f);
					DrawKeyValueRow(I18N(u8"Hosts 檔案"), hosts_warn, &warn_col);
				}
			}
			EndCyberPanel();
		}

		static void DrawNetSubMonitor(const OptimizeNetworkScan::Snapshot& ns)
		{
			const auto bw = OptimizeNetworkScan::GetBandwidthHistory();
			const auto speed_hist = OptimizeNetworkScan::GetSpeedTestHistory();

			if (BeginCyberPanel("##opt_panel_net_mon_top")) {
				const uint64_t cache_ms = OptimizeNetworkScan::GetCacheSavedAtMs();
				if (cache_ms > 0) {
					const uint64_t age_min = (GetTickCount64() - cache_ms) / 60000ull;
					ImGui::TextDisabled(I18N(u8"資料快取：約 %llu 分鐘前寫入（重開程式可還原圖表與 DNS 清單）"),
						static_cast<unsigned long long>(age_min));
					ImGui::Dummy(ImVec2(0.f, 2.f));
				}
				DrawNetSectionTitle(I18N(u8"即時監控"), I18N(u8"圖表、診斷與歷史資料"));
				const float chip_h = 56.f;
				char v0[24] = {}, v1[24] = {}, v2[24] = {}, v3[24] = {};
				snprintf(v0, sizeof(v0), "%d", ns.total_tcp_connections);
				snprintf(v1, sizeof(v1), "%d", ns.total_udp_connections);
				snprintf(v2, sizeof(v2), "%d", ns.active_process_count);
				FormatBitrate(ns.download_bps, v3, sizeof(v3));
				const char* labels[4] = { I18N(u8"TCP 連線"), I18N(u8"UDP 連線"), I18N(u8"活躍程式"), I18N(u8"目前下載") };
				const char* vals[4] = { v0, v1, v2, v3 };
				if (ImGui::BeginTable("##net_mon_chips", 4,
					ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadInnerX)) {
					ImGui::TableNextRow(ImGuiTableRowFlags_None, chip_h);
					for (int i = 0; i < 4; ++i) {
						ImGui::TableNextColumn();
						const ImVec2 p = ImGui::GetCursorScreenPos();
						const float cw = ImGui::GetContentRegionAvail().x;
						DrawNetStatChip(ImRect(p, ImVec2(p.x + cw, p.y + chip_h)),
							labels[i], vals[i], cyan_neon());
						NetWidgetAdvance(cw, chip_h);
					}
					ImGui::EndTable();
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_mon_bw")) {
				const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
				const float h = 168.f;
				const ImVec2 p = ImGui::GetCursorScreenPos();
				DrawNetDualSparkline(ImRect(p, ImVec2(p.x + w, p.y + h)), bw);
				NetWidgetAdvance(w, h);
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_mon_diag")) {
				DrawNetSectionTitle(I18N(u8"網路診斷"), I18N(u8"Wi‑Fi、MTU、封包遺失與路由"));
				{
					const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
					const float gauge_h = 132.f;
					const float gw = ImMax(100.f, (w - 8.f) * 0.5f);
					if (ImGui::BeginTable("##net_mon_gauges", 2,
						ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadInnerX)) {
						ImGui::TableNextRow(ImGuiTableRowFlags_None, gauge_h);
						ImGui::TableNextColumn();
						const ImVec2 p0 = ImGui::GetCursorScreenPos();
						char ping_txt[24] = {};
						char ping_sub[24] = {};
						float ping_ratio = 0.f;
						ImVec4 ping_col = ImVec4(1.f, 0.45f, 0.35f, 1.f);
						if (ns.internet_ping_ms >= 0) {
							snprintf(ping_txt, sizeof(ping_txt), "%d ms", ns.internet_ping_ms);
							strncpy_s(ping_sub, NetLatencyGrade(ns.internet_ping_ms), _TRUNCATE);
							ping_ratio = 1.f - ImClamp(ns.internet_ping_ms / 300.f, 0.f, 1.f);
							ping_col = NetLatencyColor(ns.internet_ping_ms);
						}
						else {
							strncpy_s(ping_txt, "—", _TRUNCATE);
							strncpy_s(ping_sub, I18N(u8"尚未測量"), _TRUNCATE);
						}
						DrawNetRingGauge(ImRect(p0, ImVec2(p0.x + gw, p0.y + gauge_h)),
							ping_ratio, I18N(u8"上網延遲"), ping_txt, ping_sub, ping_col);
						NetWidgetAdvance(gw, gauge_h);
						ImGui::TableNextColumn();
						const ImVec2 p1 = ImGui::GetCursorScreenPos();
						char loss_txt[24] = {};
						char loss_sub[24] = {};
						float loss_ratio = 0.f;
						ImVec4 loss_col = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
						if (ns.packet_loss_percent >= 0) {
							snprintf(loss_txt, sizeof(loss_txt), "%d%%", ns.packet_loss_percent);
							strncpy_s(loss_sub, ns.packet_loss_percent <= 5 ? I18N(u8"良好") : I18N(u8"偏高"), _TRUNCATE);
							loss_ratio = ImClamp(ns.packet_loss_percent / 100.f, 0.f, 1.f);
							loss_col = ns.packet_loss_percent <= 5
								? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
								: ImVec4(1.f, 0.78f, 0.45f, 1.f);
						}
						else if (OptimizeNetworkScan::IsNetworkDiagnosticsRunning()) {
							strncpy_s(loss_txt, "…", _TRUNCATE);
							strncpy_s(loss_sub, I18N(u8"測量中"), _TRUNCATE);
						}
						else {
							strncpy_s(loss_txt, "—", _TRUNCATE);
							strncpy_s(loss_sub, I18N(u8"執行診斷"), _TRUNCATE);
						}
						DrawNetRingGauge(ImRect(p1, ImVec2(p1.x + gw, p1.y + gauge_h)),
							loss_ratio, I18N(u8"封包遺失率"), loss_txt, loss_sub, loss_col);
						NetWidgetAdvance(gw, gauge_h);
						ImGui::EndTable();
					}
					ImGui::Dummy(ImVec2(0.f, 4.f));
				}
				if (ns.wifi.connected) {
					char wifi_val[96] = {};
					snprintf(wifi_val, sizeof(wifi_val), "%s · %d%% · %s",
						ns.wifi.ssid[0] ? ns.wifi.ssid : "Wi‑Fi",
						ns.wifi.signal_percent,
						ns.wifi.band[0] ? ns.wifi.band : "—");
					DrawKeyValueRow(I18N(u8"Wi‑Fi 訊號"), wifi_val);
				}
				else {
					DrawKeyValueRow(I18N(u8"Wi‑Fi 訊號"), I18N(u8"未連線或非無線介面卡"));
				}
				char mtu_val[32] = {};
				snprintf(mtu_val, sizeof(mtu_val), "%d", ns.mtu > 0 ? ns.mtu : 0);
				DrawKeyValueRow("MTU", ns.mtu > 0 ? mtu_val : "—");
				char loss_val[32] = {};
				if (ns.packet_loss_percent >= 0) {
					snprintf(loss_val, sizeof(loss_val), "%d%%", ns.packet_loss_percent);
					ImVec4 loss_col = ns.packet_loss_percent <= 5
						? ImVec4(0.45f, 0.95f, 0.75f, 1.f)
						: ImVec4(1.f, 0.78f, 0.45f, 1.f);
					DrawKeyValueRow(I18N(u8"封包遺失率"), loss_val, &loss_col);
				}
				else if (OptimizeNetworkScan::IsNetworkDiagnosticsRunning()) {
					DrawKeyValueRow(I18N(u8"封包遺失率"), I18N(u8"測量中…"));
				}
				else {
					DrawKeyValueRow(I18N(u8"封包遺失率"), I18N(u8"尚未測量"));
				}
				char fw_val[96] = {};
				snprintf(fw_val, sizeof(fw_val), I18N(u8"網域 %s · 私人 %s · 公用 %s"),
					ns.firewall.domain_enabled ? I18N(u8"開") : I18N(u8"關"),
					ns.firewall.private_enabled ? I18N(u8"開") : I18N(u8"關"),
					ns.firewall.public_enabled ? I18N(u8"開") : I18N(u8"關"));
				DrawKeyValueRow(I18N(u8"防火牆設定檔"), fw_val);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_diag_run", I18N(u8"執行診斷"), true, 96.f)) {
					OptimizeNetworkScan::RequestNetworkDiagnostics();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_trace_run", I18N(u8"路由追蹤"), false, 96.f)) {
					OptimizeNetworkScan::RequestTraceroute();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_trace_copy", I18N(u8"複製路由報告"), false, 120.f)) {
					const std::string report = OptimizeNetworkScan::BuildTracerouteReportText();
					ImGui::SetClipboardText(report.c_str());
					OpenSysInfoPopup(I18N(u8"已複製"), I18N(u8"路由追蹤報告已複製到剪貼簿。"));
				}
				ImGui::PopStyleVar();
				const auto trace = OptimizeNetworkScan::GetTraceroute();
				if (OptimizeNetworkScan::IsTracerouteRunning() || trace.running) {
					ImGui::TextColored(cyan_mid(), "%s", I18N(u8"路由追蹤進行中…"));
				}
				else if (trace.status_text[0] != '\0') {
					ImGui::TextDisabled("%s", UiTxt(trace.status_text));
				}
				if (!trace.hops.empty()) {
					if (ImGui::BeginTable("##net_trace_tbl", 4,
						ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg
						| ImGuiTableFlags_BordersInnerV)) {
						ImGui::TableSetupColumn(I18N(u8"跳"), ImGuiTableColumnFlags_WidthFixed, 36.f);
						ImGui::TableSetupColumn(I18N(u8"位址"), ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn(I18N(u8"延遲"), ImGuiTableColumnFlags_WidthFixed, 72.f);
						ImGui::TableSetupColumn(I18N(u8"狀態"), ImGuiTableColumnFlags_WidthFixed, 64.f);
						ImGui::TableHeadersRow();
						for (const auto& hop : trace.hops) {
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							ImGui::Text("%d", hop.hop);
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(hop.addr[0] ? hop.addr : "*");
							ImGui::TableNextColumn();
							if (hop.rtt_ms >= 0) {
								ImGui::Text("%d ms", hop.rtt_ms);
							}
							else {
								ImGui::TextDisabled("—");
							}
							ImGui::TableNextColumn();
							const char* st = hop.status[0] ? hop.status
								: (hop.is_destination ? I18N(u8"到達") : "—");
							ImGui::TextUnformatted(st);
						}
						ImGui::EndTable();
					}
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_mon_speed_hist")) {
				DrawNetSectionTitle(I18N(u8"測速歷史"), I18N(u8"最近下載／上傳測速紀錄"));
				if (speed_hist.size() < 2) {
					ImGui::TextDisabled(I18N(u8"完成至少兩次下載測速後會顯示趨勢圖。"));
				}
				else {
					std::vector<float> vals;
					vals.reserve(speed_hist.size());
					float vmax = 1.f;
					for (const auto& e : speed_hist) {
						vals.push_back(e.download_mbps);
						vmax = ImMax(vmax, e.download_mbps);
					}
					const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
					const float h = 120.f;
					const ImVec2 p = ImGui::GetCursorScreenPos();
					DrawNetSparkline(ImRect(p, ImVec2(p.x + w, p.y + h)),
						vals.data(), static_cast<int>(vals.size()),
						I18N(u8"下載 Mbps 趨勢"), cyan_neon(), vmax * 1.1f);
					NetWidgetAdvance(w, h);
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_mon_adapters")) {
				DrawNetSectionTitle(I18N(u8"介面卡流量"), I18N(u8"累計收發量（延遲與 DNS 請至對應分頁）"));
				uint64_t max_bytes = 1;
				for (const auto& ad : ns.adapters) {
					max_bytes = std::max(max_bytes, ad.bytes_in + ad.bytes_out);
				}
				for (size_t i = 0; i < ns.adapters.size() && i < 8; ++i) {
					const auto& ad = ns.adapters[i];
					char lbl[96] = {};
					snprintf(lbl, sizeof(lbl), "%s%s",
						ad.friendly_name[0] ? ad.friendly_name : ad.desc_utf8,
						ad.connected ? I18N(u8" · 使用中") : "");
					const uint64_t total_bytes = ad.bytes_in + ad.bytes_out;
					char val_txt[48] = {};
					FormatByteCount(total_bytes, val_txt, sizeof(val_txt));
					const float total = static_cast<float>(total_bytes);
					DrawNetHBar(lbl, total, static_cast<float>(max_bytes),
						cyan_mid(), nullptr, val_txt);
				}
				if (ns.adapters.empty()) {
					ImGui::TextDisabled(I18N(u8"尚無介面卡資料。"));
				}
			}
			EndCyberPanel();
		}

		static void DrawNetSubDns(const OptimizeNetworkScan::Snapshot& ns)
		{
			if (!g_dns_test_domain_inited) {
				const char* saved = OptimizeNetworkScan::GetDnsBenchmarkDomain();
				if (saved != nullptr && saved[0] != '\0') {
					strncpy_s(g_dns_test_domain, saved, _TRUNCATE);
				}
				g_dns_test_domain_inited = true;
			}
			const OptimizeNetworkScan::DnsBenchSnapshot dns_bench =
				OptimizeNetworkScan::GetDnsBenchmark();
			const bool dns_running = OptimizeNetworkScan::IsDnsBenchmarkRunning();
			const bool full_dns_running = OptimizeNetworkScan::IsFullDnsDiagnosticsRunning();
			const bool bench_running = dns_running || full_dns_running;
			const auto custom_list = OptimizeNetworkScan::GetCustomDnsEntries();
			const char* test_domain = dns_bench.test_domain[0] != '\0'
				? dns_bench.test_domain : g_dns_test_domain;

			if (BeginCyberPanel("##opt_panel_net_dns_overview")) {
				DrawNetSectionTitle(I18N(u8"DNS 總覽"), I18N(u8"一眼看懂目前 DNS 與測速建議"));
				DrawDnsStatusStrip(ns, dns_bench, dns_running, full_dns_running);
				ImGui::Dummy(ImVec2(0.f, 8.f));
				DrawDnsOverviewTiles(ns, dns_bench.rows, bench_running);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				DrawDnsConfigChips(ns, test_domain);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				ImGui::SetNextItemWidth(ImMin(300.f, ImGui::GetContentRegionAvail().x));
				if (ImGui::InputTextWithHint("##dns_test_domain", I18N(u8"測試網域（Enter 立即測速）"),
					g_dns_test_domain, sizeof(g_dns_test_domain), ImGuiInputTextFlags_EnterReturnsTrue)) {
					OptimizeNetworkScan::SetDnsBenchmarkDomain(g_dns_test_domain);
					OptimizeNetworkScan::RequestDnsBenchmark();
				}
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_dns_run", I18N(u8"開始測速"), true, 92.f)) {
					OptimizeNetworkScan::SetDnsBenchmarkDomain(g_dns_test_domain);
					OptimizeNetworkScan::RequestDnsBenchmark();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_dns_full", I18N(u8"全面診斷"), true, 92.f)) {
					OptimizeNetworkScan::SetDnsBenchmarkDomain(g_dns_test_domain);
					OptimizeNetworkScan::RequestFullDnsDiagnostics();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_dns_auto", I18N(u8"改回自動"), false, 92.f)) {
					NetReportResult(I18N(u8"改回自動 DNS"), OptimizeNetworkScan::SetAdapterDnsDhcp());
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				if (DrawSystemToolbarButton("##net_dns_cf", "Cloudflare", false, 88.f)) {
					NetReportResult("Cloudflare", OptimizeNetworkScan::SetAdapterDnsPublic(1));
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_dns_gg", "Google", false, 72.f)) {
					NetReportResult("Google", OptimizeNetworkScan::SetAdapterDnsPublic(2));
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_dns_ali", I18N(u8"阿里"), false, 56.f)) {
					NetReportResult(I18N(u8"阿里"), OptimizeNetworkScan::SetAdapterDnsPublic(3));
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_dns_114", "114", false, 52.f)) {
					NetReportResult("114 DNS", OptimizeNetworkScan::SetAdapterDnsPublic(4));
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_dns_tx", I18N(u8"騰訊"), false, 52.f)) {
					NetReportResult(I18N(u8"騰訊 DNS"), OptimizeNetworkScan::SetAdapterDnsPublic(5));
				}
				ImGui::PopStyleVar();
				if (bench_running) {
					ImGui::Dummy(ImVec2(0.f, 4.f));
					ImGui::TextColored(cyan_mid(), full_dns_running
						? I18N(u8"全面 DNS 診斷進行中…") : I18N(u8"DNS 測速進行中…"));
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_dns_bench")) {
				DrawNetSectionTitle(I18N(u8"測速排行"), I18N(u8"條越短越快 · 綠框最快 · 藍框目前使用"));
				DrawDnsCompareBars(dns_bench.rows, bench_running);
				ImGui::Dummy(ImVec2(0.f, 6.f));

				int max_ms = 80;
				for (const auto& row : dns_bench.rows) {
					if (row.resolve_ms > max_ms) {
						max_ms = row.resolve_ms;
					}
				}
				max_ms = ImMax(120, ((max_ms + 39) / 40) * 40);

				if (dns_bench.rows.empty() && !bench_running) {
					ImGui::TextDisabled(I18N(u8"按「開始測速」比較 8 組公共 DNS 與自訂 DNS。"));
				}
				else {
					const float row_w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
					DrawDnsTableHeader(row_w);
					const float list_h = ImMax(280.f, ImGui::GetContentRegionAvail().y - 4.f);
					if (ImGui::BeginChild("##dns_rank_scroll", ImVec2(0.f, list_h), false,
						ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
						for (size_t i = 0; i < dns_bench.rows.size(); ++i) {
							DrawDnsRankRow(dns_bench.rows[i], static_cast<int>(i) + 1,
								max_ms, static_cast<int>(i), bench_running);
						}
						if (dns_bench.rows.empty() && bench_running) {
							ImGui::TextColored(cyan_mid(), "%s", I18N(u8"正在測試各 DNS 伺服器…"));
						}
					}
					ImGui::EndChild();
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_dns_add")) {
				DrawNetSectionTitle(I18N(u8"自訂 DNS"), I18N(u8"手動新增並加入測速清單"));
				ImGui::SetNextItemWidth(ImMin(180.f, ImGui::GetContentRegionAvail().x * 0.3f));
				ImGui::InputTextWithHint("##custom_dns_label", I18N(u8"名稱（可選）"),
					g_custom_dns_label, sizeof(g_custom_dns_label));
				ImGui::SameLine(0.f, 8.f);
				ImGui::SetNextItemWidth(ImMin(150.f, ImGui::GetContentRegionAvail().x * 0.3f));
				ImGui::InputTextWithHint("##custom_dns_pri", I18N(u8"主要 DNS *"),
					g_custom_dns_primary, sizeof(g_custom_dns_primary));
				ImGui::SameLine(0.f, 8.f);
				ImGui::SetNextItemWidth(ImMin(150.f, ImGui::GetContentRegionAvail().x));
				ImGui::InputTextWithHint("##custom_dns_sec", I18N(u8"備用 DNS（可選）"),
					g_custom_dns_secondary, sizeof(g_custom_dns_secondary));
				ImGui::Dummy(ImVec2(0.f, 4.f));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##custom_dns_add_btn", I18N(u8"加入清單"), true, 96.f)) {
					if (OptimizeNetworkScan::AddCustomDnsEntry(
						g_custom_dns_label, g_custom_dns_primary, g_custom_dns_secondary)) {
						g_custom_dns_label[0] = '\0';
						g_custom_dns_primary[0] = '\0';
						g_custom_dns_secondary[0] = '\0';
						OptimizeNetworkScan::RequestDnsBenchmark();
					}
					else {
						NetReportResult(I18N(u8"加入 DNS"), false);
					}
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##custom_dns_apply", I18N(u8"直接套用"), false, 96.f)) {
					NetReportResult(I18N(u8"套用 DNS"), OptimizeNetworkScan::SetAdapterDnsCustom(
						g_custom_dns_primary,
						g_custom_dns_secondary[0] != '\0' ? g_custom_dns_secondary : nullptr));
				}
				ImGui::PopStyleVar();
				if (!custom_list.empty()) {
					ImGui::Dummy(ImVec2(0.f, 6.f));
					ImGui::TextDisabled(I18N(u8"已儲存 %zu 組"), custom_list.size());
					for (size_t i = 0; i < custom_list.size(); ++i) {
						DrawCustomDnsCompactRow(custom_list[i], i);
					}
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_dns_adv")) {
				DrawNetSectionTitle(I18N(u8"進階設定"), I18N(u8"系統開關一覽與修復工具"));
				DrawDnsAdvStatusChips(ns);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				if (ns.dns_suffix[0] != '\0') {
					DrawKeyValueRow(I18N(u8"連線 DNS 後綴"), ns.dns_suffix);
				}
				if (ns.dns_search_list[0] != '\0') {
					DrawKeyValueRow(I18N(u8"搜尋後綴清單"), ns.dns_search_list);
				}
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##dns_flush", I18N(u8"清理 DNS 快取"), true, 112.f)) {
					NetReportResult(I18N(u8"清理 DNS 快取"), OptimizeScan::FlushDnsCache());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##dns_reg", I18N(u8"註冊 DNS"), false, 88.f)) {
					NetReportResult(I18N(u8"註冊 DNS"), OptimizeScan::RegisterDnsCache());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##dns_ping", I18N(u8"連線診斷"), false, 88.f)) {
					NetReportResult(I18N(u8"連線診斷"), OptimizeNetworkScan::PingDiagnostics());
				}
				ImGui::PopStyleVar();
				ImGui::Dummy(ImVec2(0.f, 4.f));
				char doh_auto_st[16] = {};
				strncpy_s(doh_auto_st, ns.doh_auto_enabled ? I18N(u8"已開啟") : I18N(u8"已關閉"), _TRUNCATE);
				static const SystemActionBtn doh_auto_btns[] = {
					{ "##doh_auto_off", I18N(u8"關閉"), true },
					{ "##doh_auto_on", I18N(u8"開啟"), false },
				};
				const int doh_auto_pick = DrawSystemSettingCard(
					I18N(u8"自動 DNS over HTTPS"), I18N(u8"Windows 自動選擇 DoH 伺服器；需管理員。"),
					doh_auto_st, OnOffColor(ns.doh_auto_enabled), doh_auto_btns, 2);
				if (doh_auto_pick >= 0 && SysTryAdmin("DNS over HTTPS")) {
					NetReportResult(I18N(u8"自動 DoH"),
						OptimizeNetworkScan::SetDnsOverHttpsEnabled(doh_auto_pick == 1));
				}
				char llmnr_st[16] = {};
				strncpy_s(llmnr_st, ns.llmnr_enabled ? I18N(u8"已啟用") : I18N(u8"已停用"), _TRUNCATE);
				static const SystemActionBtn llmnr_btns[] = {
					{ "##llmnr_off", I18N(u8"停用"), true },
					{ "##llmnr_on", I18N(u8"啟用"), false },
				};
				const int llmnr_pick = DrawSystemSettingCard(
					I18N(u8"LLMNR（本機名稱解析）"), I18N(u8"區網內自動解析電腦名稱；關閉可減少廣播。"),
					llmnr_st, OnOffColor(ns.llmnr_enabled), llmnr_btns, 2);
				if (llmnr_pick >= 0 && SysTryAdmin("LLMNR")) {
					NetReportResult("LLMNR",
						OptimizeNetworkScan::SetLlmnrEnabled(llmnr_pick == 1));
				}
				char nb_st[16] = {};
				strncpy_s(nb_st, ns.netbios_over_tcp ? I18N(u8"已啟用") : I18N(u8"已停用"), _TRUNCATE);
				static const SystemActionBtn nb_btns[] = {
					{ "##nb_off", I18N(u8"停用"), true },
					{ "##nb_on", I18N(u8"啟用"), false },
				};
				const int nb_pick = DrawSystemSettingCard(
					"NetBIOS over TCP/IP", I18N(u8"舊版區網共用協定；一般家用可關閉。"),
					nb_st, OnOffColor(ns.netbios_over_tcp), nb_btns, 2);
				if (nb_pick >= 0 && SysTryAdmin("NetBIOS")) {
					NetReportResult("NetBIOS",
						OptimizeNetworkScan::SetNetbiosOverTcpEnabled(nb_pick == 1));
				}
				static const char* doh_policy_labels[] = { I18N(u8"允許一般 DNS"), I18N(u8"強制 DoH"), I18N(u8"停用 DoH") };
				const int policy_idx = (ns.doh_policy >= 0 && ns.doh_policy <= 2)
					? ns.doh_policy : 0;
				char doh_pol_st[32] = {};
				strncpy_s(doh_pol_st, doh_policy_labels[policy_idx], _TRUNCATE);
				static const SystemActionBtn doh_pol_btns[] = {
					{ "##dohpol_0", I18N(u8"允許"), true },
					{ "##dohpol_1", I18N(u8"強制"), false },
					{ "##dohpol_2", I18N(u8"停用"), false },
				};
				const int doh_pol_pick = DrawSystemSettingCard(
					I18N(u8"DoH 策略"), I18N(u8"控制系統是否強制使用 DNS over HTTPS。"),
					doh_pol_st, OnOffColor(policy_idx == 1), doh_pol_btns, 3);
				if (doh_pol_pick >= 0 && SysTryAdmin(I18N(u8"DoH 策略"))) {
					NetReportResult(I18N(u8"DoH 策略"),
						OptimizeNetworkScan::SetDohPolicy(doh_pol_pick));
				}
				char par_st[16] = {};
				strncpy_s(par_st, ns.parallel_dns_queries ? I18N(u8"已啟用") : I18N(u8"已停用"), _TRUNCATE);
				static const SystemActionBtn par_btns[] = {
					{ "##par_off", I18N(u8"停用"), true },
					{ "##par_on", I18N(u8"啟用"), false },
				};
				const int par_pick = DrawSystemSettingCard(
					I18N(u8"平行 A/AAAA 查詢"), I18N(u8"同時查詢 IPv4/IPv6 可加快解析；關閉可減少流量。"),
					par_st, OnOffColor(ns.parallel_dns_queries), par_btns, 2);
				if (par_pick >= 0 && SysTryAdmin(I18N(u8"平行 DNS 查詢"))) {
					NetReportResult(I18N(u8"平行 DNS 查詢"),
						OptimizeNetworkScan::SetParallelDnsQueriesEnabled(par_pick == 1));
				}
				if (ns.hosts_has_extra) {
					char hosts_val[48] = {};
					snprintf(hosts_val, sizeof(hosts_val), I18N(u8"發現 %d 筆非預設項目"),
						ns.hosts_extra_count);
					ImVec4 warn_col = ImVec4(1.f, 0.78f, 0.45f, 1.f);
					DrawKeyValueRow(I18N(u8"Hosts 檔案"), hosts_val, &warn_col);
				}
				else {
					DrawKeyValueRow(I18N(u8"Hosts 檔案"), I18N(u8"未發現異常自訂項目"));
				}
			}
			EndCyberPanel();
		}

		static void DrawNetSubSpeedTest()
		{
			const OptimizeNetworkScan::LinkTestSnapshot link =
				OptimizeNetworkScan::GetLinkTest();
			const OptimizeNetworkScan::SpeedTestSnapshot speed =
				OptimizeNetworkScan::GetSpeedTest();
			const bool link_running = OptimizeNetworkScan::IsLinkTestRunning();
			const bool speed_running = OptimizeNetworkScan::IsSpeedTestRunning();
			const bool full_net_running = OptimizeNetworkScan::IsFullNetworkTestRunning();
			const LinkTestStats link_st = ComputeLinkTestStats(link.rows);
			const int link_total = static_cast<int>(link.rows.size());

			if (BeginCyberPanel("##opt_panel_net_test_overview")) {
				DrawNetSectionTitle(I18N(u8"測試總覽"), I18N(u8"一眼看懂連線與頻寬狀態"));
				DrawNetTestStatusStrip(link, speed, link_st,
					link_running, speed_running, full_net_running);
				ImGui::Dummy(ImVec2(0.f, 8.f));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_link_run", I18N(u8"測試連線"), true, 96.f)) {
					OptimizeNetworkScan::RequestLinkTest();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_speed_both", I18N(u8"下載+上傳"), true, 104.f)) {
					OptimizeNetworkScan::RequestSpeedTest();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_full_test", I18N(u8"全面測試"), true, 96.f)) {
					OptimizeNetworkScan::RequestFullNetworkTest();
				}
				ImGui::PopStyleVar();
				if (link_running || full_net_running) {
					ImGui::Dummy(ImVec2(0.f, 6.f));
					char prog_lbl[48] = {};
					snprintf(prog_lbl, sizeof(prog_lbl), I18N(u8"連線測試 %s"),
						full_net_running ? I18N(u8"（全面測試）") : "");
					DrawNetTestProgressBar(link.progress, prog_lbl);
				}
				if (speed_running) {
					ImGui::Dummy(ImVec2(0.f, 4.f));
					char spd_lbl[32] = {};
					snprintf(spd_lbl, sizeof(spd_lbl), I18N(u8"%s測速"),
						speed.upload_mode ? I18N(u8"上傳") : I18N(u8"下載"));
					DrawNetTestProgressBar(speed.progress, spd_lbl);
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_link")) {
				DrawNetSectionTitle(I18N(u8"連線測試"), I18N(u8"12 節點 · 綠色正常 · 紅色逾時"));
				if (!link_running && !full_net_running && link.status_text[0] != '\0') {
					ImGui::TextDisabled("%s", UiTxt(link.status_text));
				}
				DrawLinkTestSummaryTiles(link_st, link_total, link_running || full_net_running);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				DrawLinkDnsHttpChips(link, link_running || full_net_running);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				DrawLinkLatencyTopBars(link.rows, 6);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				if (!link.rows.empty()) {
					ImGui::TextDisabled(I18N(u8"全部節點（%d）· 懸停可看目標位址"), link_total);
					const float full_w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
					const int matrix_cols = full_w >= 720.f ? 4 : (full_w >= 480.f ? 3 : 2);
					DrawLinkTestMatrix(link.rows, matrix_cols);
				}
				else if (!link_running && !full_net_running) {
					ImGui::TextDisabled(I18N(u8"按上方「測試連線」檢查路由器、DNS、GitHub、Steam 等節點。"));
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_speed")) {
				DrawNetSectionTitle(I18N(u8"頻寬測速"), I18N(u8"Cloudflare 下載 10MB／上傳 5MB"));
				if (!speed_running && speed.status_text[0] != '\0') {
					ImGui::TextDisabled("%s", UiTxt(speed.status_text));
				}
				DrawSpeedTestSummaryTiles(speed, speed_running);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_speed_run", I18N(u8"下載測速"), true, 96.f)) {
					OptimizeNetworkScan::RequestSpeedTest();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_upload_run", I18N(u8"上傳測速"), false, 96.f)) {
					OptimizeNetworkScan::RequestUploadSpeedTest();
				}
				ImGui::PopStyleVar();
				ImGui::Dummy(ImVec2(0.f, 6.f));

				const bool has_dl = speed.download_mbps >= 0.f;
				const bool has_ul = speed.upload_mbps >= 0.f;
				const bool show_dual_gauge = has_dl && has_ul && !speed_running;
				const float block_h = 148.f;
				const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
				if (show_dual_gauge) {
					const ImVec2 gp = ImGui::GetCursorScreenPos();
					DrawNetDualSpeedGauges(ImRect(gp, ImVec2(gp.x + w, gp.y + block_h)),
						speed.download_mbps, speed.upload_mbps, 200.f);
					NetWidgetAdvance(w, block_h);
				}
				else if ((has_dl || has_ul || speed_running) && !speed_running) {
					const ImVec2 gp = ImGui::GetCursorScreenPos();
					const float gauge_mbps = has_ul && !has_dl
						? speed.upload_mbps : speed.download_mbps;
					DrawNetArcSpeedGauge(ImRect(gp, ImVec2(gp.x + w, gp.y + block_h)),
						gauge_mbps, 200.f, has_ul && !has_dl ? I18N(u8"上傳速度") : I18N(u8"下載速度"));
					NetWidgetAdvance(w, block_h);
				}
				else if (speed_running) {
					DrawNetTestProgressBar(speed.progress,
						speed.upload_mode ? I18N(u8"上傳測速進度") : I18N(u8"下載測速進度"));
				}
				else {
					ImGui::TextDisabled(I18N(u8"按「下載測速」或「上傳測速」測量頻寬。"));
				}

				const auto speed_hist = OptimizeNetworkScan::GetSpeedTestHistory();
				if (speed_hist.size() >= 2) {
					ImGui::Dummy(ImVec2(0.f, 6.f));
					ImGui::TextDisabled(I18N(u8"下載測速歷史趨勢"));
					std::vector<float> vals;
					vals.reserve(speed_hist.size());
					float vmax = 1.f;
					for (const auto& e : speed_hist) {
						vals.push_back(e.download_mbps);
						vmax = ImMax(vmax, e.download_mbps);
					}
					const float h = 100.f;
					const ImVec2 p = ImGui::GetCursorScreenPos();
					DrawNetSparkline(ImRect(p, ImVec2(p.x + w, p.y + h)),
						vals.data(), static_cast<int>(vals.size()),
						nullptr, cyan_neon(), vmax * 1.1f);
					NetWidgetAdvance(w, h);
				}

				if ((has_dl || has_ul) && !speed_running && speed.duration_ms > 0) {
					char dur[48] = {};
					snprintf(dur, sizeof(dur), I18N(u8"%.1f 秒"), speed.duration_ms / 1000.f);
					DrawKeyValueRow(I18N(u8"最近測速耗時"), dur);
				}
			}
			EndCyberPanel();
		}

		static void DrawNetSubTools(const OptimizeScan::Snapshot& sys,
			const OptimizeNetworkScan::Snapshot& ns)
		{
			if (BeginCyberPanel("##opt_panel_net_tools_hdr")) {
				DrawNetSectionTitle(I18N(u8"網路設定與工具"), I18N(u8"修復、系統開關與匯出"));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_quick_fix", I18N(u8"一鍵修復"), true, 96.f)) {
					NetReportResult(I18N(u8"一鍵修復"), OptimizeNetworkScan::RunQuickNetworkRepair());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_copy_report", I18N(u8"複製報告"), true, 96.f)) {
					const std::string report = OptimizeNetworkScan::BuildNetworkReportText();
					ImGui::SetClipboardText(report.c_str());
					OpenSysInfoPopup(I18N(u8"已複製"), I18N(u8"網路狀態報告已複製到剪貼簿。"));
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_export_refresh", I18N(u8"重新檢查"), false, 96.f)) {
					OptimizeNetworkScan::RequestRefresh();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_open_sys", I18N(u8"Windows 網路"), false, 108.f)) {
					OptimizeNetworkScan::OpenWindowsNetworkSettings();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_save_cache", I18N(u8"儲存快取"), false, 96.f)) {
					OptimizeNetworkScan::SaveNetworkCache();
					OpenSysInfoPopup(I18N(u8"已儲存"), I18N(u8"網路監控資料已寫入本機快取。"));
				}
				ImGui::PopStyleVar();
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_firewall")) {
				DrawNetSectionTitle(I18N(u8"Windows 防火牆"), I18N(u8"各設定檔開關（需管理員）"));
				auto fw_card = [&](const char* title, const char* hint, bool enabled, int profile) {
					char st[16] = {};
					strncpy_s(st, enabled ? I18N(u8"已開啟") : I18N(u8"已關閉"), _TRUNCATE);
					static const SystemActionBtn btns[] = {
						{ "##fw_off", I18N(u8"關閉"), true },
						{ "##fw_on", I18N(u8"開啟"), false },
					};
					char id_buf[64] = {};
					snprintf(id_buf, sizeof(id_buf), "%s_%d", title, profile);
					const int pick = DrawSystemSettingCard(
						title, hint, st, OnOffColor(enabled), btns, 2);
					if (pick >= 0 && SysTryAdmin(I18N(u8"防火牆"))) {
						NetReportResult(title,
							OptimizeNetworkScan::SetFirewallProfileEnabled(profile, pick == 1));
					}
				};
				fw_card(I18N(u8"網域防火牆"), I18N(u8"公司網域環境使用。"), ns.firewall.domain_enabled, 0);
				fw_card(I18N(u8"私人防火牆"), I18N(u8"家中或信任網路。"), ns.firewall.private_enabled, 1);
				fw_card(I18N(u8"公用防火牆"), I18N(u8"咖啡廳等公共 Wi‑Fi。"), ns.firewall.public_enabled, 2);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_fw_reset", I18N(u8"重設防火牆"), false, 108.f)) {
					if (SysTryAdmin(I18N(u8"重設防火牆"))) {
						NetReportResult(I18N(u8"重設防火牆"), OptimizeNetworkScan::ResetFirewallDefaults());
					}
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_fw_cpl", I18N(u8"防火牆設定"), false, 108.f)) {
					OptimizeNetworkScan::OpenFirewallSettings();
				}
				ImGui::PopStyleVar();
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_adv")) {
				DrawNetSectionTitle(I18N(u8"進階網路設定"), I18N(u8"計量連線、DoH、IPv6、網卡省電"));
				char metered_st[16] = {};
				strncpy_s(metered_st, ns.metered_connection ? I18N(u8"計量") : I18N(u8"非計量"), _TRUNCATE);
				static const SystemActionBtn meter_btns[] = {
					{ "##meter_off", I18N(u8"非計量"), true },
					{ "##meter_on", I18N(u8"計量"), false },
				};
				const int meter_pick = DrawSystemSettingCard(
					I18N(u8"計量連線"), I18N(u8"計量連線可減少背景更新占用。"),
					metered_st, OnOffColor(ns.metered_connection), meter_btns, 2);
				if (meter_pick >= 0) {
					NetReportResult(I18N(u8"計量連線"),
						OptimizeNetworkScan::SetMeteredConnection(meter_pick == 1));
				}
				char doh_st[16] = {};
				strncpy_s(doh_st, ns.doh_auto_enabled ? I18N(u8"已開啟") : I18N(u8"已關閉"), _TRUNCATE);
				static const SystemActionBtn doh_btns[] = {
					{ "##doh_off", I18N(u8"關閉"), true },
					{ "##doh_on", I18N(u8"開啟"), false },
				};
				const int doh_pick = DrawSystemSettingCard(
					"DNS over HTTPS", I18N(u8"自動透過 HTTPS 解析 DNS；需管理員。"),
					doh_st, OnOffColor(ns.doh_auto_enabled), doh_btns, 2);
				if (doh_pick >= 0 && SysTryAdmin("DNS over HTTPS")) {
					NetReportResult("DNS over HTTPS",
						OptimizeNetworkScan::SetDnsOverHttpsEnabled(doh_pick == 1));
				}
				char ipv6_st[16] = {};
				strncpy_s(ipv6_st, ns.ipv6_enabled ? I18N(u8"已啟用") : I18N(u8"已停用"), _TRUNCATE);
				static const SystemActionBtn ipv6_btns[] = {
					{ "##ipv6_off", I18N(u8"停用"), true },
					{ "##ipv6_on", I18N(u8"啟用"), false },
				};
				const int ipv6_pick = DrawSystemSettingCard(
					I18N(u8"IPv6 協定"), I18N(u8"停用可排除部分舊軟體相容問題；需管理員。"),
					ipv6_st, OnOffColor(ns.ipv6_enabled), ipv6_btns, 2);
				if (ipv6_pick >= 0 && SysTryAdmin("IPv6")) {
					NetReportResult("IPv6",
						OptimizeNetworkScan::SetPrimaryIpv6Enabled(ipv6_pick == 1));
				}
				char pwr_st[16] = {};
				strncpy_s(pwr_st, ns.nic_power_save_on ? I18N(u8"允許省電") : I18N(u8"已關閉"), _TRUNCATE);
				static const SystemActionBtn pwr_btns[] = {
					{ "##nicpwr_off", I18N(u8"關閉"), true },
					{ "##nicpwr_on", I18N(u8"允許"), false },
				};
				const int pwr_pick = DrawSystemSettingCard(
					I18N(u8"網卡省電"), I18N(u8"關閉可避免 Wi‑Fi 莫名斷線。"),
					pwr_st, OnOffColor(!ns.nic_power_save_on), pwr_btns, 2,
					TagIf(ns.nic_power_save_on, u8"建議關閉"));
				if (pwr_pick >= 0) {
					NetReportResult(I18N(u8"網卡省電"),
						OptimizeNetworkScan::SetNicPowerSaveEnabled(pwr_pick == 1));
				}
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_tools")) {
				DrawNetSectionTitle(I18N(u8"快取與連線修復"), I18N(u8"由淺到深依序嘗試"));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_flush", I18N(u8"清理 DNS 快取"), true, 112.f)) {
					NetReportResult(I18N(u8"清理 DNS 快取"), OptimizeScan::FlushDnsCache());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_arp", I18N(u8"清除 ARP"), false, 96.f)) {
					if (SysTryAdmin(I18N(u8"清除 ARP"))) {
						NetReportResult(I18N(u8"清除 ARP"), OptimizeNetworkScan::FlushArpCache());
					}
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_renew", I18N(u8"更新 IP"), false, 88.f)) {
					NetReportResult(I18N(u8"更新 IP"), OptimizeScan::RenewIpAddresses());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_release", I18N(u8"釋放並更新 IP"), false, 120.f)) {
					NetReportResult(I18N(u8"釋放並更新 IP"), OptimizeNetworkScan::ReleaseAndRenewIp());
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				if (DrawSystemToolbarButton("##net_reg", I18N(u8"註冊 DNS"), false, 88.f)) {
					NetReportResult(I18N(u8"註冊 DNS"), OptimizeScan::RegisterDnsCache());
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_winsock", I18N(u8"重設 Winsock"), false, 108.f)) {
					if (SysTryAdmin(I18N(u8"重設 Winsock"))) {
						NetReportResult(I18N(u8"重設 Winsock"), OptimizeScan::ResetWinsockCatalog());
					}
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_tcpip", I18N(u8"重設 TCP/IP"), false, 108.f)) {
					if (SysTryAdmin(I18N(u8"重設 TCP/IP"))) {
						NetReportResult(I18N(u8"重設 TCP/IP"), OptimizeNetworkScan::ResetTcpIpStack());
					}
				}
				ImGui::PopStyleVar();
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_tools_open")) {
				DrawNetSectionTitle(I18N(u8"系統頁面"), I18N(u8"開啟 Windows 內建工具"));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##net_proxy", I18N(u8"代理設定"), false, 88.f)) {
					OptimizeNetworkScan::OpenProxySettings();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_resmon", I18N(u8"資源監視器"), false, 108.f)) {
					OptimizeNetworkScan::OpenResourceMonitorNetwork();
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_ncpa", I18N(u8"介面卡設定"), false, 108.f)) {
					OptimizeNetworkScan::OpenNetworkAdapterSettings();
				}
				ImGui::PopStyleVar();
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_settings")) {
				DrawNetSectionTitle(I18N(u8"背景網路行為"), I18N(u8"減少遊戲與下載時被限速"));
				char thr_status[16] = {};
				strncpy_s(thr_status, sys.network_throttling_on ? I18N(u8"已開啟") : I18N(u8"已關閉"), _TRUNCATE);
				static const SystemActionBtn thr_btns[] = {
					{ "##nethr_off", I18N(u8"關閉"), true },
					{ "##nethr_on", I18N(u8"開啟"), false },
				};
				const int thr_pick = DrawSystemSettingCard(
					I18N(u8"遊戲網路節流"), I18N(u8"關閉後遊戲連線較不易被系統限速；需管理員。"),
					thr_status, OnOffColor(!sys.network_throttling_on), thr_btns, 2,
					TagIf(sys.network_throttling_on, u8"遊戲建議"));
				if (thr_pick >= 0 && SysTryAdmin(I18N(u8"網路節流"))) {
					NetReportResult(I18N(u8"網路節流"),
						OptimizeScan::SetNetworkThrottlingEnabled(thr_pick == 1));
				}

				char p2p_st[16] = {};
				strncpy_s(p2p_st, OnOffText(sys.delivery_p2p_on), _TRUNCATE);
				static const SystemActionBtn p2p_btns[] = {
					{ "##netp2p_off", I18N(u8"關閉"), true },
					{ "##netp2p_on", I18N(u8"開啟"), false },
				};
				const int p2p_pick = DrawSystemSettingCard(
					I18N(u8"Windows 更新分享"), I18N(u8"關閉可減少背景上傳佔頻寬；需管理員。"),
					p2p_st, OnOffColor(sys.delivery_p2p_on), p2p_btns, 2,
					TagIf(sys.delivery_p2p_on, u8"建議關閉"));
				if (p2p_pick >= 0 && SysTryAdmin(I18N(u8"Windows 更新 P2P"))) {
					NetReportResult(I18N(u8"Windows 更新分享"),
						OptimizeScan::SetDeliveryOptimizationP2P(p2p_pick == 1));
				}
			}
			EndCyberPanel();
		}

		static void DrawNetSubProcesses(const OptimizeNetworkScan::Snapshot& ns)
		{
			if (BeginCyberPanel("##opt_panel_net_proc_sum")) {
				DrawNetSectionTitle(I18N(u8"程式網路占用"), I18N(u8"一眼看出誰佔用最多連線"));
				if (ns.scanning) {
					ImGui::SameLine(0.f, 10.f);
					ImGui::TextColored(cyan_mid(), "%s", I18N(u8"更新中…"));
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				DrawProcessSummaryTiles(ns);
				ImGui::Dummy(ImVec2(0.f, 6.f));
				DrawProcessTopBars(ns.processes, 6);
				ImGui::Dummy(ImVec2(0.f, 4.f));
				ImGui::TextDisabled(I18N(u8"依 TCP+UDP 連線數排序，最多顯示前 24 個程式。"));
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginCyberPanel("##opt_panel_net_proc_list")) {
				DrawNetSectionTitle(I18N(u8"詳細清單"), I18N(u8"可搜尋名稱或路徑、篩選高占用"));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				ImGui::SetNextItemWidth(ImMin(260.f, ImGui::GetContentRegionAvail().x * 0.5f));
				ImGui::InputTextWithHint("##net_proc_filter", I18N(u8"搜尋程式名稱或路徑…"),
					g_net_proc_filter, sizeof(g_net_proc_filter));
				ImGui::SameLine(0.f, 8.f);
				if (DrawSystemToolbarButton("##net_proc_hot", I18N(u8"僅高占用"), g_net_proc_hot_only, 88.f)) {
					g_net_proc_hot_only = !g_net_proc_hot_only;
				}
				ImGui::SameLine(0.f, 6.f);
				if (DrawSystemToolbarButton("##net_proc_refresh", I18N(u8"重新整理"), true, 88.f)) {
					OptimizeNetworkScan::RequestRefresh();
				}
				ImGui::PopStyleVar();
				ImGui::Dummy(ImVec2(0.f, 6.f));

				int max_score = 1;
				for (const auto& row : ns.processes) {
					max_score = ImMax(max_score, row.activity_score);
				}

				const float row_w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
				DrawProcessTableHeader(row_w);
				const float list_h = ImMax(300.f, ImGui::GetContentRegionAvail().y - 4.f);
				if (ImGui::BeginChild("##net_proc_scroll", ImVec2(0.f, list_h), false,
					ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
					int shown = 0;
					for (size_t i = 0; i < ns.processes.size(); ++i) {
						const auto& row = ns.processes[i];
						if (g_net_proc_hot_only && row.activity_score < 12) {
							continue;
						}
						if (g_net_proc_filter[0] != '\0'
							&& strstr(row.name_utf8, g_net_proc_filter) == nullptr
							&& strstr(row.path_utf8, g_net_proc_filter) == nullptr) {
							continue;
						}
						DrawNetProcessRankRow(row, shown + 1, max_score, static_cast<int>(i));
						++shown;
					}
					if (shown == 0) {
						ImGui::Dummy(ImVec2(0.f, 12.f));
						if (ns.scanning) {
							ImGui::TextColored(cyan_mid(), "%s", I18N(u8"正在掃描程式連線…"));
						}
						else if (g_net_proc_hot_only) {
							ImGui::TextDisabled(I18N(u8"目前沒有「高占用」程式（連線數 ≥ 12）。"));
							ImGui::TextDisabled(I18N(u8"可關閉「僅高占用」查看全部，或按「重新整理」。"));
						}
						else {
							ImGui::TextDisabled(I18N(u8"沒有符合搜尋條件的程式。"));
						}
					}
					else {
						ImGui::Dummy(ImVec2(0.f, 6.f));
						ImGui::TextDisabled(I18N(u8"共 %d 個程式（全部 %zu）"), shown, ns.processes.size());
					}
				}
				ImGui::EndChild();
			}
			EndCyberPanel();
		}

		static void DrawTabNetwork()
		{
			NetTickCommon();
			const OptimizeNetworkScan::Snapshot ns = OptimizeNetworkScan::GetSnapshot();
			if (!g_net_did_dns_bench && ns.valid && !ns.scanning
				&& g_net_subtab == static_cast<int>(NetSubTab::Dns)) {
				OptimizeNetworkScan::RequestDnsBenchmark();
				g_net_did_dns_bench = true;
			}

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 4.f));
			DrawNetworkSubTabBar();

			switch (static_cast<NetSubTab>(g_net_subtab)) {
			case NetSubTab::Overview:
				DrawNetSubOverview(ns);
				break;
			case NetSubTab::Monitor:
				DrawNetSubMonitor(ns);
				break;
			case NetSubTab::Dns:
				if (!g_net_did_dns_bench && ns.valid && !ns.scanning) {
					OptimizeNetworkScan::RequestDnsBenchmark();
					g_net_did_dns_bench = true;
				}
				DrawNetSubDns(ns);
				break;
			case NetSubTab::SpeedTest:
				DrawNetSubSpeedTest();
				break;
			case NetSubTab::Tools:
				DrawNetSubTools(OptimizeScan::GetSnapshot(), ns);
				break;
			case NetSubTab::Processes:
				DrawNetSubProcesses(ns);
				break;
			default:
				DrawNetSubOverview(ns);
				break;
			}
			ImGui::PopStyleVar();
		}

		static void DrawStorageWorkPanel(const OptimizeScan::StorageWorkSnapshot& work)
		{
			if (BeginCyberPanel("##opt_panel_store_work")) {
				DrawNetSectionTitle(I18N(u8"工作進度"), work.running ? I18N(u8"作業進行中") : I18N(u8"最近作業紀錄"));
				const char* prog_label = work.status_text[0] != '\0'
					? UiTxt(work.status_text)
					: (work.running ? I18N(u8"處理中…") : I18N(u8"就緒"));
				DrawNetTestProgressBar(work.running ? work.progress : 1.f, prog_label);
				if (work.job_name[0] != '\0') {
					ImGui::TextDisabled(I18N(u8"工作：%s"), work.job_name);
				}
				if (work.last_result_bytes > 0 && !work.running) {
					char sz[32] = {};
					FormatCleanSize(work.last_result_bytes, sz, sizeof(sz));
					ImGui::TextColored(cyan_mid(), I18NF(u8"上次釋放：%s"), sz);
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				constexpr float kLogPanelH = 128.f;
				ImGui::PushStyleColor(ImGuiCol_ChildBg, card_bg());
				if (ImGui::BeginChild("##store_work_log", ImVec2(0.f, kLogPanelH), true)) {
					if (work.log_count <= 0) {
						ImGui::TextDisabled(I18N(u8"執行掃描、清理或磁碟維護後，日誌會顯示於此。"));
					}
					else {
						for (int i = 0; i < work.log_count; ++i) {
							ImGui::TextColored(
								i == work.log_count - 1 ? cyan_mid() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
								"%s", work.log_lines[i]);
						}
					}
					if (work.running) {
						ImGui::TextColored(cyan_neon(), "▶ %s", prog_label);
					}
					if (work.log_count > 0 || work.running) {
						ImGui::SetScrollY(ImGui::GetScrollMaxY());
					}
					if (work.log_count > 0 && !work.running) {
						ImGui::Dummy(ImVec2(0.f, 4.f));
						if (DrawSystemToolbarButton("##store_clear_log", I18N(u8"清除日誌"), false, 88.f)) {
							HLOG_INFO("StoragePage: 使用者清除工作日誌");
							OptimizeScan::ClearStorageWorkLog();
						}
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
			EndCyberPanel();
			ImGui::Spacing();
		}

		static void DrawStorageDriveSelector()
		{
			const int drive_count = OptimizeScan::GetStorageDriveCount();
			const char selected = OptimizeScan::GetStorageMaintenanceDrive();
			ImGui::TextDisabled(I18N(u8"目標磁碟"));
			ImGui::Dummy(ImVec2(0.f, 2.f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
			for (int i = 0; i < drive_count; ++i) {
				OptimizeScan::StorageDriveInfo drive = {};
				if (!OptimizeScan::GetStorageDrive(i, drive)) {
					continue;
				}
				if (i > 0) {
					ImGui::SameLine(0.f, 6.f);
				}
				char btn_id[16] = {};
				snprintf(btn_id, sizeof(btn_id), "##store_drv_%c", drive.letter);
				char btn_label[40] = {};
				if (drive.free_bytes > 0) {
					char free_txt[20] = {};
					FormatCleanSize(drive.free_bytes, free_txt, sizeof(free_txt));
					snprintf(btn_label, sizeof(btn_label), I18N(u8"%c: 剩%s"),
						drive.letter, free_txt);
				}
				else {
					snprintf(btn_label, sizeof(btn_label), "%c:", drive.letter);
				}
				const bool is_sel = (drive.letter == selected);
				if (DrawSystemToolbarButton(btn_id, btn_label, is_sel, 88.f)) {
					HLOG_INFO("StoragePage: 使用者選擇維護磁碟 {}", drive.letter);
					OptimizeScan::SetStorageMaintenanceDrive(drive.letter);
				}
			}
			ImGui::PopStyleVar();
			if (drive_count > 0) {
				OptimizeScan::StorageDriveInfo sel_drive = {};
				for (int i = 0; i < drive_count; ++i) {
					OptimizeScan::StorageDriveInfo drive = {};
					if (OptimizeScan::GetStorageDrive(i, drive) && drive.letter == selected) {
						sel_drive = drive;
						break;
					}
				}
				char hint[120] = {};
				if (sel_drive.label[0] != '\0') {
					snprintf(hint, sizeof(hint), I18N(u8"已選 %c:（%s）"), selected, UiTxt(sel_drive.label));
				}
				else {
					snprintf(hint, sizeof(hint), I18N(u8"已選 %c: 磁碟"), selected);
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				ImGui::TextDisabled("%s", hint);
			}
		}

		static void DrawDiskOptStatusChips(const OptimizeScan::DiskOptimizationSnapshot& disk_opt)
		{
			const float chip_h = 56.f;
			const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
			const float chip_w = ImMax(88.f, (w - 12.f) / 3.f);
			char v0[32] = {}, v1[32] = {}, v2[48] = {};
			ImVec4 c0 = ImVec4(0.65f, 0.68f, 0.72f, 1.f);
			ImVec4 c1 = c0;
			ImVec4 c2 = c0;
			if (disk_opt.running) {
				strncpy_s(v0, I18N(u8"分析/最佳化中"), _TRUNCATE);
				strncpy_s(v1, "…", _TRUNCATE);
				strncpy_s(v2, UiTxt(disk_opt.status_text), _TRUNCATE);
				c0 = cyan_mid();
			}
			else if (disk_opt.status_text[0] != '\0'
				&& strstr(disk_opt.status_text, u8"失敗") != nullptr) {
				strncpy_s(v0, strstr(disk_opt.status_text, u8"最佳化") ? I18N(u8"最佳化失敗") : I18N(u8"分析失敗"),
					_TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				if (disk_opt.detail_text[0] != '\0') {
					strncpy_s(v2, UiTxt(disk_opt.detail_text), _TRUNCATE);
				}
				else {
					strncpy_s(v2, UiTxt(disk_opt.status_text), _TRUNCATE);
				}
				c2 = ImVec4(1.f, 0.55f, 0.45f, 1.f);
			}
			else if (disk_opt.valid
				&& disk_opt.drive_letter != OptimizeScan::GetStorageMaintenanceDrive()) {
				snprintf(v0, sizeof(v0), I18N(u8"%c: 已分析"), disk_opt.drive_letter);
				strncpy_s(v1, "—", _TRUNCATE);
				strncpy_s(v2, I18N(u8"請分析此磁碟"), _TRUNCATE);
				c0 = cyan_mid();
			}
			else if (disk_opt.valid) {
				strncpy_s(v0, disk_opt.media_label[0] ? disk_opt.media_label : I18N(u8"未知"), _TRUNCATE);
				if (disk_opt.last_run_was_optimize) {
					if (disk_opt.fragmentation_percent >= 0) {
						snprintf(v1, sizeof(v1), I18N(u8"%d%% 良好"), disk_opt.fragmentation_percent);
					}
					else {
						strncpy_s(v1, disk_opt.is_ssd ? "TRIM" : I18N(u8"重組"), _TRUNCATE);
					}
					c1 = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
					if (disk_opt.last_optimize_elapsed_sec > 0) {
						snprintf(v2, sizeof(v2), I18N(u8"已最佳化（%u 秒）"),
							disk_opt.last_optimize_elapsed_sec);
					}
					else {
						strncpy_s(v2, I18N(u8"已最佳化"), _TRUNCATE);
					}
					c2 = ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				}
				else if (disk_opt.fragmentation_percent >= 0) {
					snprintf(v1, sizeof(v1), "%d%%", disk_opt.fragmentation_percent);
					c1 = disk_opt.fragmentation_percent >= 10
						? ImVec4(1.f, 0.78f, 0.45f, 1.f) : ImVec4(0.45f, 0.95f, 0.75f, 1.f);
					snprintf(v2, sizeof(v2), "%s",
						disk_opt.needs_optimization ? I18N(u8"需要最佳化") : I18N(u8"狀況良好"));
					c2 = disk_opt.needs_optimization
						? ImVec4(1.f, 0.78f, 0.45f, 1.f) : ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				}
				else {
					strncpy_s(v1, disk_opt.is_ssd ? "TRIM" : "—", _TRUNCATE);
					strncpy_s(v2, disk_opt.needs_optimization ? I18N(u8"需要最佳化") : I18N(u8"狀態良好"), _TRUNCATE);
					c2 = disk_opt.needs_optimization
						? ImVec4(1.f, 0.78f, 0.45f, 1.f) : ImVec4(0.45f, 0.95f, 0.75f, 1.f);
				}
			}
			else {
				strncpy_s(v0, I18N(u8"尚未分析"), _TRUNCATE);
				strncpy_s(v1, "—", _TRUNCATE);
				strncpy_s(v2, I18N(u8"按「分析磁碟」"), _TRUNCATE);
			}
			const char* labels[3] = { I18N(u8"磁碟類型"), I18N(u8"分散/模式"), I18N(u8"最佳化狀態") };
			const char* vals[3] = { v0, v1, v2 };
			const ImVec4* cols[3] = { &c0, &c1, &c2 };
			for (int i = 0; i < 3; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, 6.f);
				}
				const ImVec2 p = ImGui::GetCursorScreenPos();
				DrawNetStatChip(ImRect(p, ImVec2(p.x + chip_w, p.y + chip_h)),
					labels[i], vals[i], *cols[i]);
				ImGui::Dummy(ImVec2(chip_w, chip_h));
			}
			if (!disk_opt.running && disk_opt.detail_text[0] != '\0'
				&& (disk_opt.valid
					|| strstr(disk_opt.status_text, u8"失敗") != nullptr)) {
				ImGui::Dummy(ImVec2(0.f, 4.f));
				ImGui::TextDisabled("%s", UiTxt(disk_opt.detail_text));
			}
		}

		static void DrawTabStorage(OptimizeScan::Snapshot& snap)
		{
			const OptimizeScan::DiskOptimizationSnapshot disk_opt =
				OptimizeScan::GetDiskOptimization();
			const OptimizeScan::StorageWorkSnapshot work =
				OptimizeScan::GetStorageWorkSnapshot();
			const OptimizeScan::StorageLocalSettings local =
				OptimizeScan::GetStorageLocalSettings();
			const bool disk_maint_busy = OptimizeScan::IsDiskOptimizationRunning()
				|| disk_opt.running;
			const bool storage_job_busy = disk_maint_busy || work.running;

			if (BeginCyberPanel("##opt_panel_store_hdr")) {
				ImGui::TextColored(cyan_neon(), "%s", I18N(u8"儲存設定"));
				const char* msg = OptimizeScan::GetLastActionMessage();
				if (msg != nullptr && msg[0] != '\0') {
					ImGui::SameLine(0.f, 12.f);
					ImGui::TextColored(cyan_mid(), I18NF(u8"最近：%s"), UiTxt(msg));
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
				if (DrawSystemToolbarButton("##store_refresh", I18N(u8"重新讀取"), false, 88.f)) {
					HLOG_INFO("StoragePage: 使用者點擊重新讀取設定");
					OptimizeScan::RefreshSystemSettings();
				}
				ImGui::PopStyleVar();
				ImGui::Dummy(ImVec2(0.f, 2.f));
				char line[160] = {};
				if (snap.system_drive_free_bytes > 0 && snap.system_drive_used_percent >= 0.f) {
					char free_txt[32] = {};
					FormatCleanSize(snap.system_drive_free_bytes, free_txt, sizeof(free_txt));
					snprintf(line, sizeof(line), I18N(u8"C: 剩餘 %s · 使用率 %.0f%% · 本頁直接調整設定"),
						free_txt, snap.system_drive_used_percent);
				}
				else {
					strncpy_s(line, I18N(u8"在本頁直接調整儲存策略與維護，無需跳轉外部設定"), _TRUNCATE);
				}
				ImGui::TextDisabled("%s", line);
			}
			EndCyberPanel();
			ImGui::Spacing();

			if (BeginSysCollapsibleSection("store_work", I18N(u8"工作進度與日誌"), &g_store_open_work)) {
				DrawStorageWorkPanel(work);
				EndSysCollapsibleSection();
			}

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 2.f));

			if (BeginSysCollapsibleSection("store_local", I18N(u8"① 本機儲存策略"), &g_store_open_local)) {
				char sense_status[16] = {};
				snprintf(sense_status, sizeof(sense_status), "%s",
					OnOffText(local.storage_sense_on));
				static const SystemActionBtn sense_btns[] = {
					{ "##store_sense_on", I18N(u8"啟用"), true },
					{ "##store_sense_off", I18N(u8"關閉"), false },
				};
				const int sense_pick = DrawSystemSettingCard(
					I18N(u8"儲存空間感知"), I18N(u8"直接寫入本機儲存策略登錄，自動管理暫存與回收空間。"),
					sense_status, OnOffColor(local.storage_sense_on), sense_btns, 2);
				if (sense_pick >= 0) {
					HLOG_INFO("StoragePage: 使用者設定儲存感知 enabled={}", sense_pick == 0);
					SysReportResult(I18N(u8"儲存感知"), OptimizeScan::SetStorageSenseEnabled(sense_pick == 0));
				}

				char temp_status[16] = {};
				snprintf(temp_status, sizeof(temp_status), "%s",
					OnOffText(local.auto_temp_cleanup));
				static const SystemActionBtn temp_btns[] = {
					{ "##store_temp_on", I18N(u8"啟用"), true },
					{ "##store_temp_off", I18N(u8"關閉"), false },
				};
				const int temp_pick = DrawSystemSettingCard(
					I18N(u8"自動清理暫存"), I18N(u8"啟用後由儲存感知在適當時機清理暫存檔。"),
					temp_status, OnOffColor(local.auto_temp_cleanup), temp_btns, 2);
				if (temp_pick >= 0) {
					HLOG_INFO("StoragePage: 使用者設定自動清理暫存 enabled={}", temp_pick == 0);
					SysReportResult(I18N(u8"暫存清理"), OptimizeScan::SetStorageAutoTempCleanup(temp_pick == 0));
				}

				char low_status[16] = {};
				snprintf(low_status, sizeof(low_status), "%s",
					OnOffText(local.low_disk_auto_run));
				static const SystemActionBtn low_btns[] = {
					{ "##store_low_on", I18N(u8"啟用"), true },
					{ "##store_low_off", I18N(u8"關閉"), false },
				};
				const int low_pick = DrawSystemSettingCard(
					I18N(u8"低空間自動清理"), I18N(u8"磁碟空間不足時自動觸發儲存感知清理。"),
					low_status, OnOffColor(local.low_disk_auto_run), low_btns, 2);
				if (low_pick >= 0) {
					HLOG_INFO("StoragePage: 使用者設定低空間自動清理 enabled={}", low_pick == 0);
					SysReportResult(I18N(u8"低空間清理"), OptimizeScan::SetStorageLowDiskAutoRun(low_pick == 0));
				}

				char recycle_status[24] = {};
				if (local.recycle_bin_days <= 0) {
					strncpy_s(recycle_status, I18N(u8"永不"), _TRUNCATE);
				}
				else {
					snprintf(recycle_status, sizeof(recycle_status), I18N(u8"%d 天"), local.recycle_bin_days);
				}
				static const SystemActionBtn recycle_btns[] = {
					{ "##store_rb_never", I18N(u8"永不"), false },
					{ "##store_rb_1", I18N(u8"1 天"), false },
					{ "##store_rb_7", I18N(u8"7 天"), false },
					{ "##store_rb_30", I18N(u8"30 天"), true },
				};
				const int recycle_pick = DrawSystemSettingCard(
					I18N(u8"回收筒保留"), I18N(u8"設定資源回收筒檔案自動刪除天數（本機登錄）。"),
					recycle_status, cyan_mid(), recycle_btns, 4);
				if (recycle_pick == 0) {
					HLOG_INFO("StoragePage: 使用者設定回收筒保留 days=0");
					SysReportResult(I18N(u8"回收筒保留"), OptimizeScan::SetStorageRecycleBinDays(0));
				}
				else if (recycle_pick == 1) {
					HLOG_INFO("StoragePage: 使用者設定回收筒保留 days=1");
					SysReportResult(I18N(u8"回收筒保留"), OptimizeScan::SetStorageRecycleBinDays(1));
				}
				else if (recycle_pick == 2) {
					HLOG_INFO("StoragePage: 使用者設定回收筒保留 days=7");
					SysReportResult(I18N(u8"回收筒保留"), OptimizeScan::SetStorageRecycleBinDays(7));
				}
				else if (recycle_pick == 3) {
					HLOG_INFO("StoragePage: 使用者設定回收筒保留 days=30");
					SysReportResult(I18N(u8"回收筒保留"), OptimizeScan::SetStorageRecycleBinDays(30));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("store_space", I18N(u8"② 釋放磁碟空間"), &g_store_open_space)) {
				char hib_status[16] = {};
				snprintf(hib_status, sizeof(hib_status), "%s", OnOffText(snap.hibernate_on));
				static const SystemActionBtn hib_btns[] = {
					{ "##store_hib_on", I18N(u8"啟用"), false },
					{ "##store_hib_off", I18N(u8"關閉"), true },
				};
				const int hib_pick = DrawSystemSettingCard(
					I18N(u8"休眠檔案"), I18N(u8"關閉休眠可刪除 hiberfil.sys，釋放約等於實體記憶體大小的空間。"),
					hib_status, OnOffColor(snap.hibernate_on), hib_btns, 2,
					TagIf(snap.hibernate_on, u8"建議關閉"));
				if (hib_pick >= 0) {
					const bool enabling = (hib_pick == 0);
					HLOG_INFO("StoragePage: 使用者設定休眠 enabled={}", enabling);
					if (!enabling || SysTryAdmin(I18N(u8"啟用休眠"))) {
						SysReportResult(I18N(u8"休眠設定"), OptimizeScan::SetHibernateEnabled(enabling));
					}
				}

				char p2p_status[16] = {};
				snprintf(p2p_status, sizeof(p2p_status), "%s", OnOffText(snap.delivery_p2p_on));
				static const SystemActionBtn p2p_btns[] = {
					{ "##store_p2p_on", I18N(u8"啟用"), false },
					{ "##store_p2p_off", I18N(u8"關閉"), true },
				};
				const int p2p_pick = DrawSystemSettingCard(
					I18N(u8"傳遞優化"), I18N(u8"關閉可減少 Windows 更新在本機的快取占用（可能略增下載時間）。"),
					p2p_status, OnOffColor(snap.delivery_p2p_on), p2p_btns, 2,
					TagIf(snap.delivery_p2p_on, u8"可關閉"));
				if (p2p_pick >= 0 && SysTryAdmin(I18N(u8"傳遞優化"))) {
					HLOG_INFO("StoragePage: 使用者設定傳遞優化 P2P enabled={}", p2p_pick == 0);
					SysReportResult(I18N(u8"傳遞優化"),
						OptimizeScan::SetDeliveryOptimizationP2P(p2p_pick == 0));
				}

				char scan_hint[64] = {};
				if (snap.suggested_clean_bytes > 0) {
					char sz[24] = {};
					FormatCleanSize(snap.suggested_clean_bytes, sz, sizeof(sz));
					snprintf(scan_hint, sizeof(scan_hint), I18N(u8"約 %s"), sz);
				}
				else {
					strncpy_s(scan_hint, I18N(u8"尚未掃描"), _TRUNCATE);
				}
				static const SystemActionBtn scan_btns[] = {
					{ "##store_scan_clean", I18N(u8"開始掃描"), true },
				};
				if (!storage_job_busy
					&& DrawSystemSettingCard(
					I18N(u8"掃描可清理項目"), I18N(u8"在本頁背景掃描所有清理任務大小，進度顯示於工作日誌。"),
					scan_hint, cyan_neon(), scan_btns, 1) == 0) {
					HLOG_INFO("StoragePage: 使用者點擊掃描可清理項目");
					OptimizeScan::RequestStorageCleanScan();
				}

				static const SystemActionBtn quick_btns[] = {
					{ "##store_quick_clean", I18N(u8"執行清理"), true },
				};
				if (!storage_job_busy && DrawSystemSettingCard(
					I18N(u8"本機快速清理"), I18N(u8"清理暫存、回收筒與傳遞優化快取（不開啟 cleanmgr）。"),
					I18N(u8"本機執行"), cyan_mid(), quick_btns, 1) == 0) {
					HLOG_INFO("StoragePage: 使用者點擊本機快速清理");
					OptimizeScan::RequestStorageQuickClean(
						static_cast<uint32_t>(OptimizeScan::StorageQuickCleanFlags::TempFiles)
						| static_cast<uint32_t>(OptimizeScan::StorageQuickCleanFlags::RecycleBin)
						| static_cast<uint32_t>(OptimizeScan::StorageQuickCleanFlags::DeliveryCache));
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("store_maintain", I18N(u8"③ 硬碟維護"), &g_store_open_maintain)) {
				DrawStorageDriveSelector();
				ImGui::Dummy(ImVec2(0.f, 6.f));
				DrawDiskOptStatusChips(disk_opt);
				if (disk_maint_busy) {
					const float prog = disk_opt.progress > 0.f ? disk_opt.progress : work.progress;
					const char* prog_txt = disk_opt.status_text[0] != '\0'
						? UiTxt(disk_opt.status_text) : UiTxt(work.status_text);
					DrawNetTestProgressBar(prog, prog_txt);
				}
				ImGui::Dummy(ImVec2(0.f, 4.f));

				char opt_status[48] = {};
				if (disk_maint_busy) {
					strncpy_s(opt_status, I18N(u8"進行中"), _TRUNCATE);
				}
				else if (disk_opt.last_run_was_optimize
					&& disk_opt.drive_letter == OptimizeScan::GetStorageMaintenanceDrive()) {
					if (disk_opt.last_optimize_elapsed_sec > 0) {
						snprintf(opt_status, sizeof(opt_status), I18N(u8"已最佳化（%u 秒）"),
							disk_opt.last_optimize_elapsed_sec);
					}
					else if (disk_opt.fragmentation_percent >= 0) {
						snprintf(opt_status, sizeof(opt_status), I18N(u8"%d%% · 已最佳化"),
							disk_opt.fragmentation_percent);
					}
					else {
						strncpy_s(opt_status, I18N(u8"已最佳化"), _TRUNCATE);
					}
				}
				else if (disk_opt.valid
					&& disk_opt.drive_letter == OptimizeScan::GetStorageMaintenanceDrive()) {
					if (disk_opt.fragmentation_percent >= 0) {
						snprintf(opt_status, sizeof(opt_status), I18N(u8"%d%% 分散"),
							disk_opt.fragmentation_percent);
					}
					else {
						strncpy_s(opt_status,
							disk_opt.needs_optimization ? I18N(u8"需要最佳化") : I18N(u8"狀態良好"), _TRUNCATE);
					}
				}
				else if (disk_opt.valid) {
					snprintf(opt_status, sizeof(opt_status), I18N(u8"%c: 結果"), disk_opt.drive_letter);
				}
				else if (disk_opt.status_text[0] != '\0'
					&& strstr(disk_opt.status_text, u8"失敗") != nullptr) {
					strncpy_s(opt_status,
						strstr(disk_opt.status_text, u8"最佳化") ? I18N(u8"最佳化失敗") : I18N(u8"分析失敗"),
						_TRUNCATE);
				}
				else {
					strncpy_s(opt_status, I18N(u8"尚未分析"), _TRUNCATE);
				}
				const char sel_drive = OptimizeScan::GetStorageMaintenanceDrive();
				char analyze_hint[96] = {};
				snprintf(analyze_hint, sizeof(analyze_hint),
					I18N(u8"對 %c: 執行 defrag /A /V（需管理員），讀取分散率。"), sel_drive);
				static const SystemActionBtn analyze_btns[] = {
					{ "##disk_analyze", I18N(u8"分析磁碟"), true },
				};
				const int analyze_pick = DrawSystemSettingCard(
					I18N(u8"磁碟分析"), analyze_hint,
					opt_status, disk_opt.needs_optimization
						? ImVec4(1.f, 0.78f, 0.45f, 1.f) : cyan_neon(),
					analyze_btns, 1);
				if (analyze_pick == 0 && !disk_maint_busy) {
					HLOG_INFO("StoragePage: 使用者點擊磁碟分析 drive={}",
						OptimizeScan::GetStorageMaintenanceDrive());
					if (HCleanHasElevatedAccess()) {
						g_pending_storage_op = PendingStorageOp::None;
						OptimizeScan::RequestDiskOptimizationAnalyze();
					}
					else {
						g_pending_storage_op = PendingStorageOp::Analyze;
						SysTryAdmin(I18N(u8"磁碟分析"));
					}
				}

				char optimize_hint[96] = {};
				snprintf(optimize_hint, sizeof(optimize_hint),
					I18N(u8"對 %c: 執行 TRIM 或重組。0%% 表示已良好；成功執行後狀態會顯示「已最佳化」。"), sel_drive);
				static const SystemActionBtn optimize_btns[] = {
					{ "##disk_optimize", I18N(u8"執行最佳化"), true },
				};
				const int optimize_pick = DrawSystemSettingCard(
					I18N(u8"硬碟最佳化"), optimize_hint,
					opt_status, cyan_mid(), optimize_btns, 1);
				if (optimize_pick == 0 && !disk_maint_busy) {
					HLOG_INFO("StoragePage: 使用者點擊硬碟最佳化 drive={}",
						OptimizeScan::GetStorageMaintenanceDrive());
					if (HCleanHasElevatedAccess()) {
						g_pending_storage_op = PendingStorageOp::None;
						OptimizeScan::RequestDiskOptimizationRun();
					}
					else {
						g_pending_storage_op = PendingStorageOp::Optimize;
						SysTryAdmin(I18N(u8"硬碟最佳化"));
					}
				}
				EndSysCollapsibleSection();
			}

			if (BeginSysCollapsibleSection("store_tools", I18N(u8"④ 安全還原"), &g_store_open_tools)) {
				static const SystemActionBtn restore_btns[] = {
					{ "##store_restore", I18N(u8"建立還原點"), true },
				};
				if (!storage_job_busy && DrawSystemSettingCard(
					I18N(u8"系統還原點"), I18N(u8"變更儲存相關設定前建議先建立還原點；進度寫入工作日誌。"),
					I18N(u8"安全"), ImVec4(0.45f, 0.95f, 0.75f, 1.f), restore_btns, 1) == 0) {
					HLOG_INFO("StoragePage: 使用者點擊建立還原點");
					if (HCleanHasElevatedAccess()) {
						OptimizeScan::RequestStorageRestorePoint();
					}
					else {
						SysTryAdmin(I18N(u8"建立系統還原點"));
					}
				}
				EndSysCollapsibleSection();
			}

			ImGui::PopStyleVar();
		}

		static void DrawActiveSubTab(OptimizeScan::Snapshot& snap)
		{
			const float body_h = ImMax(100.f, ImGui::GetContentRegionAvail().y);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 6.f));
			if (ImGui::BeginChild("##opt_subtab_body", ImVec2(0, body_h), false,
				ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
				switch (static_cast<SubTab>(g_active_subtab)) {
				case SubTab::Overview:
					DrawTabOverview(snap);
					break;
				case SubTab::Startup:
					DrawTabStartup(snap);
					break;
				case SubTab::Services:
					DrawTabServices(snap);
					break;
				case SubTab::System:
					DrawTabSystem(snap);
					break;
				case SubTab::Network:
					DrawTabNetwork();
					break;
				case SubTab::Storage:
					DrawTabStorage(snap);
					break;
				default:
					break;
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
		}
	}

	void RenderContent()
	{
		if (!g_did_initial_scan) {
			OptimizeScan::RequestScan();
			g_did_initial_scan = true;
		}

		OptimizeScan::TickStorageWork();
		TryRunPendingStorageOp();

		OptimizeScan::Snapshot snap = OptimizeScan::GetSnapshot();
		DrawTopStatusBar(snap);
		DrawSubTabBar();
		DrawApplyConfirmModal();
		DrawSysInfoPopupModal();
		DrawActiveSubTab(snap);
	}
}
