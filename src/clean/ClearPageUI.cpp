#define IMGUI_DEFINE_MATH_OPERATORS
#include "ClearPageUI.h"
#include "HCleanTask.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HUserConfig.h"
#include <imgui_internal.h>
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
	char g_clean_suggest_preset[32] = {};
	int64_t g_clean_suggest_bytes = 0;
}

namespace ClearPageUI {
	namespace Theme {
		const ImVec4 cyan_neon(0.00f, 0.90f, 0.90f, 1.0f);
		const ImVec4 cyan_dark(0.00f, 0.40f, 0.40f, 1.0f);
		const ImVec4 bg_pure_black(0.03f, 0.03f, 0.03f, 1.0f);
		const ImVec4 card_bg(0.06f, 0.08f, 0.08f, 1.0f);
		const ImVec4 card_bg_hover(0.08f, 0.12f, 0.12f, 1.0f);
		const ImVec4 active_bg(0.00f, 0.25f, 0.25f, 1.0f);
		const ImVec4 hover_bg(0.00f, 0.35f, 0.35f, 1.0f);
		const ImVec4 header_bg(0.04f, 0.06f, 0.06f, 1.0f);
		const ImVec4 header_bg_hover(0.06f, 0.10f, 0.10f, 1.0f);
	}

	namespace {
		constexpr float kHeaderRowH = 34.0f;
		constexpr float kCatScanBlockW = 140.0f;
		constexpr float kCheckSz = 20.0f;
		constexpr float kRounding = 4.0f;
		constexpr float kItemProgressBlockH = 32.0f;
		constexpr float kItemBottomGap = 4.0f;
		constexpr float kDetailModalW = 520.0f;
		constexpr float kDetailModalH = 420.0f;
		constexpr float kDetailRowH = 56.0f;
		constexpr float kDetailRowGap = 6.0f;
		constexpr float kDetailRowStride = kDetailRowH + kDetailRowGap;
		constexpr float kDetailOpenBtnW = 100.0f;

		static const char* DestructiveBadgeLabel()
		{
			return I18N(u8"破壞性");
		}

		static float DrawDestructiveBadge(ImDrawList* dl, ImVec2 pos, float max_x)
		{
			const ImVec2 text_sz = ImGui::CalcTextSize(DestructiveBadgeLabel());
			const float pad_x = 5.0f;
			const float pad_y = 2.0f;
			const ImVec2 badge_sz(text_sz.x + pad_x * 2.0f, text_sz.y + pad_y * 2.0f);
			if (pos.x + badge_sz.x > max_x) {
				return 0.0f;
			}
			const ImRect bb(pos, pos + badge_sz);
			dl->AddRectFilled(bb.Min, bb.Max,
				ImGui::GetColorU32(ImVec4(0.45f, 0.08f, 0.06f, 0.92f)), 3.0f);
			dl->AddRect(bb.Min, bb.Max,
				ImGui::GetColorU32(ImVec4(1.0f, 0.35f, 0.22f, 0.95f)), 3.0f, 0, 1.0f);
			dl->AddText(
				ImVec2(pos.x + pad_x, pos.y + pad_y),
				ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.55f, 1.0f)),
				DestructiveBadgeLabel());
			return badge_sz.x + 6.0f;
		}

		static void DrawGradientHLine(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col_left, ImU32 col_mid, ImU32 col_right)
		{
			const float mid_x = (p0.x + p1.x) * 0.5f;
			dl->AddRectFilledMultiColor(p0, ImVec2(mid_x, p1.y), col_left, col_mid, col_mid, col_left);
			dl->AddRectFilledMultiColor(ImVec2(mid_x, p0.y), p1, col_mid, col_right, col_right, col_mid);
		}

		static bool CyberToggleBox(const char* id_suffix, const ImRect& bb, bool checked, bool partial,
			bool* out_pressed)
		{
			using namespace Theme;
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImGuiID id = window->GetID(id_suffix);
			ImGui::ItemAdd(bb, id);

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
			if (out_pressed != nullptr) {
				*out_pressed = pressed;
			}

			const ImU32 border_col = ImGui::GetColorU32(hovered || held ? cyan_neon : cyan_dark);
			const ImU32 fill_col = ImGui::GetColorU32(
				held ? active_bg : (hovered ? hover_bg : header_bg));
			window->DrawList->AddRectFilled(bb.Min, bb.Max, fill_col, 3.0f);
			window->DrawList->AddRect(bb.Min, bb.Max, border_col, 3.0f, 0, 1.2f);

			if (checked && !partial) {
				const ImVec2 inner_min = bb.Min + ImVec2(4.0f, 4.0f);
				const ImVec2 inner_max = bb.Max - ImVec2(4.0f, 4.0f);
				window->DrawList->AddRectFilled(inner_min, inner_max, ImGui::GetColorU32(cyan_neon), 2.0f);
			}
			else if (partial) {
				const float bar_h = 2.0f;
				const ImVec2 bar_min(bb.Min.x + 5.0f, bb.GetCenter().y - bar_h * 0.5f);
				const ImVec2 bar_max(bb.Max.x - 5.0f, bar_min.y + bar_h);
				window->DrawList->AddRectFilled(bar_min, bar_max, ImGui::GetColorU32(cyan_neon), 1.0f);
			}

			return pressed;
		}

		static bool CyberTextButton(const char* id_suffix, const ImRect& bb, const char* label,
			bool accent_text = false)
		{
			using namespace Theme;
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImGuiID id = window->GetID(id_suffix);
			ImGui::ItemAdd(bb, id);

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

			const ImU32 border_col = ImGui::GetColorU32(hovered || held ? cyan_neon : cyan_dark);
			const ImU32 bg_col = ImGui::GetColorU32(
				held ? active_bg : (hovered ? hover_bg : header_bg));
			window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, kRounding);
			window->DrawList->AddRect(bb.Min, bb.Max, border_col, kRounding, 0, 1.0f);

			const ImVec2 text_size = ImGui::CalcTextSize(label);
			const ImVec2 text_pos(
				bb.Min.x + (bb.GetWidth() - text_size.x) * 0.5f,
				bb.Min.y + (bb.GetHeight() - text_size.y) * 0.5f);
			const ImU32 text_col = (accent_text && hovered)
				? ImGui::GetColorU32(cyan_neon)
				: ImGui::GetColorU32(ImGuiCol_Text);
			window->DrawList->AddText(text_pos, text_col, label);
			return pressed;
		}

		static bool CyberChevronButton(const char* id_suffix, const ImRect& bb, bool expanded)
		{
			using namespace Theme;
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImGuiID id = window->GetID(id_suffix);
			ImGui::ItemAdd(bb, id);

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

			const ImU32 border_col = ImGui::GetColorU32(hovered || held ? cyan_neon : cyan_dark);
			const ImU32 bg_col = ImGui::GetColorU32(
				held ? active_bg : (hovered ? hover_bg : header_bg));
			window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, kRounding);
			window->DrawList->AddRect(bb.Min, bb.Max, border_col, kRounding, 0, 1.0f);

			const ImVec2 c = bb.GetCenter();
			const float half = 4.0f;
			const ImU32 arrow_col = ImGui::GetColorU32(cyan_neon);
			if (expanded) {
				window->DrawList->AddTriangleFilled(
					ImVec2(c.x - half, c.y + 2.0f),
					ImVec2(c.x + half, c.y + 2.0f),
					ImVec2(c.x, c.y - half + 2.0f),
					arrow_col);
			}
			else {
				window->DrawList->AddTriangleFilled(
					ImVec2(c.x - half, c.y - 2.0f),
					ImVec2(c.x + half, c.y - 2.0f),
					ImVec2(c.x, c.y + half - 2.0f),
					arrow_col);
			}
			return pressed;
		}

		static bool CyberFooterButton(const char* label, const ImVec2& size, bool enabled = true)
		{
			using namespace Theme;
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window->SkipItems) {
				return false;
			}

			const ImVec2 pos = window->DC.CursorPos;
			const ImRect bb(pos, pos + size);
			const ImGuiID id = window->GetID(label);
			ImGui::ItemSize(size);
			if (!ImGui::ItemAdd(bb, id)) {
				return false;
			}

			bool hovered = false;
			bool held = false;
			const bool pressed = enabled && ImGui::ButtonBehavior(bb, id, &hovered, &held);

			const ImU32 border_col = ImGui::GetColorU32(
				enabled && (hovered || held) ? cyan_neon : cyan_dark);
			const ImU32 bg_col = ImGui::GetColorU32(
				!enabled ? ImVec4(0.15f, 0.35f, 0.35f, 0.55f)
				: held ? active_bg : (hovered ? hover_bg : ImVec4(0.00f, 0.55f, 0.55f, 1.0f)));
			window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, kRounding);
			window->DrawList->AddRect(bb.Min, bb.Max, border_col, kRounding, 0, 1.0f);

			const ImVec2 text_size = ImGui::CalcTextSize(label);
			const ImVec2 text_pos(
				bb.Min.x + (bb.GetWidth() - text_size.x) * 0.5f,
				bb.Min.y + (bb.GetHeight() - text_size.y) * 0.5f);
			window->DrawList->AddText(text_pos,
				ImGui::GetColorU32(enabled ? bg_pure_black : ImVec4(0.0f, 0.0f, 0.0f, 0.45f)),
				label);

			return pressed;
		}

		static void RenderScrollableMessageChild(const char* child_id, const ImRect& region,
			const char* message)
		{
			const float msg_h = ImMax(1.0f, region.GetHeight());
			const float msg_w = ImMax(1.0f, region.GetWidth());
			ImGui::SetCursorScreenPos(region.Min);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 2.0f));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			if (ImGui::BeginChild(child_id, ImVec2(msg_w, msg_h), false,
				ImGuiWindowFlags_NoScrollbar)) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				if (message != nullptr && message[0] != '\0') {
					ImGui::TextWrapped("%s", message);
				}
				ImGui::PopStyleColor();
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}

		static void TruncateTextWithEllipsis(const char* text, float max_w, char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}
			out[0] = '\0';
			if (text == nullptr || text[0] == '\0') {
				return;
			}
			if (ImGui::CalcTextSize(text).x <= max_w) {
				strncpy_s(out, out_size, text, _TRUNCATE);
				return;
			}
			const char* ellipsis = "...";
			const float ellipsis_w = ImGui::CalcTextSize(ellipsis).x;
			if (max_w <= ellipsis_w) {
				strncpy_s(out, out_size, ellipsis, _TRUNCATE);
				return;
			}

			size_t len = strlen(text);
			while (len > 0) {
				char buf[1024] = {};
				strncpy_s(buf, sizeof(buf), text, len);
				strncat_s(buf, sizeof(buf), ellipsis, _TRUNCATE);
				if (ImGui::CalcTextSize(buf).x <= max_w) {
					strncpy_s(out, out_size, buf, _TRUNCATE);
					return;
				}
				--len;
			}
			strncpy_s(out, out_size, ellipsis, _TRUNCATE);
		}

		static void DrawMarqueePathText(ImGuiWindow* window, const ImRect& clip_rect, const ImVec2& pos,
			const char* text, bool hovered)
		{
			if (window == nullptr || text == nullptr || text[0] == '\0') {
				return;
			}
			const float full_w = ImGui::CalcTextSize(text).x;
			const float max_w = clip_rect.GetWidth();
			const ImU32 path_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			if (!hovered || full_w <= max_w) {
				char truncated[1024] = {};
				TruncateTextWithEllipsis(text, max_w, truncated, sizeof(truncated));
				window->DrawList->PushClipRect(clip_rect.Min, clip_rect.Max, true);
				window->DrawList->AddText(pos, path_col, truncated);
				window->DrawList->PopClipRect();
				return;
			}

			// Hover 後做平滑左右往返滾動，顯示完整長路徑。
			const float overflow = full_w - max_w;
			const float speed = 45.0f;
			const float pause = 0.5f;
			const float travel = overflow / speed;
			const float cycle = pause + travel + pause + travel;
			const float t = fmodf(static_cast<float>(ImGui::GetTime()), cycle);
			float offset = 0.0f;
			if (t < pause) {
				offset = 0.0f;
			}
			else if (t < pause + travel) {
				offset = (t - pause) * speed;
			}
			else if (t < pause + travel + pause) {
				offset = overflow;
			}
			else {
				offset = overflow - (t - (pause + travel + pause)) * speed;
			}

			window->DrawList->PushClipRect(clip_rect.Min, clip_rect.Max, true);
			window->DrawList->AddText(ImVec2(pos.x - offset, pos.y), path_col, text);
			window->DrawList->PopClipRect();
		}

		static bool RenderDetailEntryRow(const char* id_suffix, HCleanDetailEntry* entry, float row_w)
		{
			using namespace Theme;
			if (entry == nullptr) {
				return false;
			}

			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window->SkipItems) {
				return false;
			}

			ImGui::PushID(id_suffix);
			const ImVec2 row_pos = window->DC.CursorPos;
			const ImRect row_bb(row_pos, row_pos + ImVec2(row_w, kDetailRowH));
			ImGui::ItemSize(ImVec2(row_w, kDetailRowStride));
			if (!ImGui::ItemAdd(row_bb, window->GetID("##detail_row"))) {
				ImGui::PopID();
				return false;
			}

			const bool row_hovered = ImGui::IsMouseHoveringRect(row_bb.Min, row_bb.Max, false);
			const ImU32 row_bg = ImGui::GetColorU32(row_hovered ? header_bg_hover : header_bg);
			window->DrawList->AddRectFilled(row_bb.Min, row_bb.Max, row_bg, kRounding);
			window->DrawList->AddRect(row_bb.Min, row_bb.Max,
				ImGui::GetColorU32(row_hovered ? cyan_neon : cyan_dark), kRounding, 0, 1.0f);

			const float pad_x = 10.0f;
			const float y_mid = row_bb.GetCenter().y;
			const ImRect check_bb(
				ImVec2(row_bb.Min.x + pad_x, y_mid - kCheckSz * 0.5f),
				ImVec2(row_bb.Min.x + pad_x + kCheckSz, y_mid + kCheckSz * 0.5f));
			if (CyberToggleBox("##row_sel", check_bb, entry->selected, false, nullptr)) {
				entry->selected = !entry->selected;
			}

			const float label_x = check_bb.Max.x + 10.0f;
			const float right_pad = 10.0f;
			const float open_btn_h = 26.0f;
			const ImRect open_bb(
				ImVec2(row_bb.Max.x - right_pad - kDetailOpenBtnW, y_mid - open_btn_h * 0.5f),
				ImVec2(row_bb.Max.x - right_pad, y_mid + open_btn_h * 0.5f));

			float badge_reserve = 0.0f;
			if (entry->destructive) {
				const ImVec2 badge_sz = ImGui::CalcTextSize(DestructiveBadgeLabel());
				badge_reserve = badge_sz.x + 22.0f;
			}

			char size_buf[32];
			if (entry->bytes < 0) {
				snprintf(size_buf, sizeof(size_buf), I18N(u8"需管理員"));
			}
			else {
				FormatCleanSize(entry->bytes, size_buf, sizeof(size_buf));
			}
			const ImVec2 size_sz = ImGui::CalcTextSize(size_buf);
			const ImVec2 size_pos(
				open_bb.Min.x - 12.0f - size_sz.x,
				y_mid - size_sz.y * 0.5f);
			window->DrawList->AddText(size_pos, ImGui::GetColorU32(cyan_neon), size_buf);

			const float label_max_w = size_pos.x - label_x - 8.0f - badge_reserve;
			const char* label = (entry->label != nullptr && entry->label[0] != '\0')
				? I18N(entry->label)
				: entry->path;
			if (label == nullptr || label[0] == '\0') {
				label = I18N(u8"(未命名)");
			}

			const bool has_path = (entry->path != nullptr && entry->path[0] != '\0');
			const bool has_label = (entry->label != nullptr && entry->label[0] != '\0');
			const ImVec2 label_sz = ImGui::CalcTextSize(label, nullptr, false, label_max_w);
			const float label_y = (has_path && has_label) ? y_mid - label_sz.y * 0.5f - 7.0f : y_mid - label_sz.y * 0.5f;
			window->DrawList->AddText(
				ImGui::GetFont(), ImGui::GetFontSize(),
				ImVec2(label_x, label_y),
				ImGui::GetColorU32(ImGuiCol_Text),
				label, nullptr, label_max_w);

			if (entry->destructive) {
				const ImVec2 badge_text_sz = ImGui::CalcTextSize(DestructiveBadgeLabel());
				const float badge_x = label_x + ImMin(label_sz.x, label_max_w) + 6.0f;
				const ImVec2 badge_pos(badge_x, y_mid - (badge_text_sz.y + 4.0f) * 0.5f);
				DrawDestructiveBadge(window->DrawList, badge_pos, size_pos.x - 4.0f);
			}

			if (has_path && has_label) {
				const ImVec2 path_pos(label_x, label_y + label_sz.y + 2.0f);
				const ImRect path_clip(path_pos, ImVec2(path_pos.x + label_max_w, path_pos.y + ImGui::GetFontSize() + 2.0f));
				DrawMarqueePathText(window, path_clip, path_pos, entry->path, row_hovered);
			}

			if (CyberTextButton("##open_folder", open_bb, I18N(u8"開啟資料夾"), true)) {
				if (entry->path != nullptr && entry->path[0] != '\0') {
					ClearPageUI::OpenFolderInExplorer(entry->path);
				}
			}

			if (row_hovered) {
				char size_tip[32];
				FormatCleanSize(entry->bytes, size_tip, sizeof(size_tip));
				const char* path = (entry->path != nullptr && entry->path[0] != '\0')
					? entry->path
					: I18N(u8"(無路徑)");
				const char* usage = (entry->usage != nullptr && entry->usage[0] != '\0')
					? entry->usage
					: I18N(u8"沿用任務用途說明");
				const char* impact = (entry->impact != nullptr && entry->impact[0] != '\0')
					? entry->impact
					: I18N(u8"可能影響依任務說明而定");
				ImGui::SetTooltip(I18N(u8"路徑：%s\n用途：%s\n可能影響：%s\n大小：%s\n%s%s"),
					path,
					usage,
					impact,
					size_tip,
					entry->selected ? I18N(u8"狀態：已勾選") : I18N(u8"狀態：未勾選"),
					entry->destructive ? I18N(u8"\n標籤：破壞性（可能永久刪除或中斷進行中工作）") : "");
			}

			ImGui::PopID();
			return true;
		}

		static void DrawProgressBar(ImDrawList* dl, const ImRect& bb, float percent, bool hovered)
		{
			using namespace Theme;
			const float t = ImClamp(percent / 100.0f, 0.0f, 1.0f);
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.02f, 0.05f, 0.05f, 1.0f)), 3.0f);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(hovered ? cyan_neon : cyan_dark), 3.0f, 0, 1.0f);
			if (t > 0.0f) {
				const ImVec2 fill_max(bb.Min.x + bb.GetWidth() * t, bb.Max.y);
				dl->AddRectFilled(bb.Min, fill_max, ImGui::GetColorU32(ImVec4(0.0f, 0.65f, 0.65f, 1.0f)), 3.0f);
				dl->AddRectFilled(bb.Min, fill_max, ImGui::GetColorU32(ImVec4(0.0f, 0.9f, 0.9f, 0.35f)), 3.0f);
			}
		}

		static void GetSystemDriveLabel(char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}
			wchar_t sys_dir[MAX_PATH] = {};
			if (GetWindowsDirectoryW(sys_dir, MAX_PATH) > 0 && sys_dir[0] != L'\0' && sys_dir[1] == L':') {
				snprintf(out, out_size, "%c:", static_cast<char>(sys_dir[0]));
				return;
			}
			strncpy_s(out, out_size, "C:", _TRUNCATE);
		}

		static int64_t ProjectedDiskFreeAfterClean(const HCleanSessionInfo& session)
		{
			if (session.disk_free_bytes < 0) {
				return -1;
			}
			if (session.phase == HCleanSessionPhase::Done) {
				return session.disk_free_bytes;
			}
			if (session.phase == HCleanSessionPhase::Cleaning) {
				const int64_t remain = session.selected_bytes_at_clean_start - session.freed_bytes_session;
				return session.disk_free_bytes + (remain > 0 ? remain : 0);
			}
			if (session.selected_bytes > 0) {
				return session.disk_free_bytes + session.selected_bytes;
			}
			return session.disk_free_bytes;
		}

		static void FormatSelectedCleanBytes(const HCleanSessionInfo& session, char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}

			const bool cleaning = session.phase == HCleanSessionPhase::Cleaning;
			const bool done = session.phase == HCleanSessionPhase::Done;

			HCleanSizeSummary summary{};
			GetCleanTasksSizeSummary(&summary);

			int64_t bytes = session.selected_bytes;
			if (done || cleaning) {
				bytes = session.selected_bytes_at_clean_start;
			}

			if (!cleaning && !done && summary.selected_has_unscanned && bytes <= 0) {
				snprintf(out, out_size, I18N(u8"掃描後顯示"));
				return;
			}
			if (!cleaning && !done && summary.selected_has_unscanned && bytes > 0) {
				char measured[32];
				FormatCleanSize(bytes, measured, sizeof(measured));
				snprintf(out, out_size, I18N(u8"%s（部分待掃描）"), measured);
				return;
			}
			FormatCleanSize(bytes, out, out_size);
		}

		static void RenderCleanSessionStatusRow(const HCleanSessionInfo& session)
		{
			using namespace Theme;

			char drive_label[8] = "C:";
			GetSystemDriveLabel(drive_label, sizeof(drive_label));

			char disk_buf[32];
			char projected_buf[32];
			char selected_buf[48];
			char freed_buf[32];
			char visible_buf[32];

			FormatCleanSize(session.disk_free_bytes, disk_buf, sizeof(disk_buf));

			HCleanSizeSummary summary{};
			GetCleanTasksSizeSummary(&summary);

			const bool cleaning = session.phase == HCleanSessionPhase::Cleaning;
			const bool done = session.phase == HCleanSessionPhase::Done;

			const int64_t projected = ProjectedDiskFreeAfterClean(session);
			if (!cleaning && !done && summary.selected_has_unscanned && summary.selected_bytes <= 0) {
				snprintf(projected_buf, sizeof(projected_buf), "—");
			}
			else {
				FormatCleanSize(projected, projected_buf, sizeof(projected_buf));
			}

			FormatSelectedCleanBytes(session, selected_buf, sizeof(selected_buf));
			FormatCleanSize(session.visible_total_bytes, visible_buf, sizeof(visible_buf));

			char free_label[48];
			snprintf(free_label, sizeof(free_label), I18N(u8"%s 目前可用"), drive_label);
			ImGui::TextDisabled("%s", free_label);
			ImGui::SameLine(0.f, 0.f);
			ImGui::TextColored(cyan_neon, "%s", disk_buf);
			ImGui::SameLine(0.f, 12.f);
			ImGui::TextDisabled("|");
			ImGui::SameLine(0.f, 8.f);
			ImGui::TextDisabled(I18N(u8"預計清理"));
			ImGui::SameLine(0.f, 0.f);
			ImGui::TextColored(cyan_neon, "%s", selected_buf);
			ImGui::SameLine(0.f, 12.f);
			ImGui::TextDisabled("|");
			ImGui::SameLine(0.f, 8.f);
			ImGui::TextDisabled(I18N(u8"清理後預估可用"));
			ImGui::SameLine(0.f, 0.f);
			ImGui::TextColored(cyan_neon, "%s", projected_buf);

			ImGui::TextDisabled(I18N(u8"可見已掃描"));
			ImGui::SameLine(0.f, 0.f);
			if (summary.visible_scanned_count < summary.visible_count && summary.visible_total_bytes <= 0) {
				ImGui::TextColored(cyan_neon, "%s", I18N(u8"掃描後顯示"));
			}
			else {
				ImGui::TextColored(cyan_neon, "%s", visible_buf);
			}

			char ratio_buf[48];
			snprintf(ratio_buf, sizeof(ratio_buf), I18N(u8"已選 %d / 可見 %d"), summary.selected_count, summary.visible_count);
			ImGui::SameLine(0.f, 12.f);
			ImGui::TextDisabled("|");
			ImGui::SameLine(0.f, 8.f);
			ImGui::TextDisabled("%s", ratio_buf);

			if (done || cleaning) {
				FormatCleanSize(session.freed_bytes_session, freed_buf, sizeof(freed_buf));
				ImGui::SameLine(0.f, 12.f);
				ImGui::TextDisabled("|");
				ImGui::SameLine(0.f, 8.f);
				ImGui::TextDisabled(cleaning ? I18N(u8"本次已釋放") : I18N(u8"本次釋放"));
				ImGui::SameLine(0.f, 0.f);
				ImGui::TextColored(cyan_neon, "%s", freed_buf);
			}

			if (done) {
				char remain_buf[32];
				FormatCleanSize(session.selected_bytes, remain_buf, sizeof(remain_buf));
				ImGui::SameLine(0.f, 12.f);
				ImGui::TextDisabled("|");
				ImGui::SameLine(0.f, 8.f);
				ImGui::TextDisabled(I18N(u8"剩餘可清理"));
				ImGui::SameLine(0.f, 0.f);
				ImGui::TextColored(cyan_neon, "%s", remain_buf);
			}

			if (cleaning || done) {
				const int skip_total = session.session_skip_locked + session.session_skip_access_denied
					+ session.session_skip_timeout;
				if (skip_total > 0) {
					ImGui::TextDisabled(I18N(u8"略過統計"));
					ImGui::SameLine(0.f, 0.f);
					ImGui::TextColored(cyan_neon, I18NF(u8"鎖定 %d | 拒絕 %d | 逾時 %d"),
						session.session_skip_locked, session.session_skip_access_denied, session.session_skip_timeout);
				}
			}
		}

		static void RenderCategoryTaskGrid(const char* category_id)
		{
			const size_t task_count = GetCleanTasksInCategory(category_id, nullptr, 0);
			std::vector<HCleanTask*> tasks(task_count);
			if (task_count > 0) {
				GetCleanTasksInCategory(category_id, tasks.data(), tasks.size());
			}

			if (tasks.empty()) {
				ImGui::TextDisabled(I18N(u8"此分類尚無清理項目。"));
				return;
			}

			const float gap = 12.0f;
			const float start_x = ImGui::GetCursorPosX();
			const float avail_w = ImGui::GetContentRegionAvail().x;
			float x = start_x;
			float y = ImGui::GetCursorPosY();
			float row_max_h = 0.0f;

			for (size_t i = 0; i < tasks.size(); ++i) {
				HCleanTask* task = tasks[i];
				if (task == nullptr) {
					continue;
				}

				const HCleanScanProgress scan = task->GetScanProgress();
				if (!task->ShouldShowInUI()) {
					continue;
				}

				if (x > start_x && (x + kItemCardW - start_x) > avail_w + 0.5f) {
					x = start_x;
					y += row_max_h + gap;
					row_max_h = 0.0f;
				}

				ImGui::SetCursorPos(ImVec2(x, y));

				char widget_id[96];
				snprintf(widget_id, sizeof(widget_id), "##clean_%s", task->GetId());

				char size_buf[32];
				if (scan.state == HCleanScanState::Scanning) {
					snprintf(size_buf, sizeof(size_buf), "%.0f%%", scan.percent);
				}
				else if (scan.state == HCleanScanState::Done) {
					const HCleanSizeInfo size = task->GetSize();
					if (!size.valid) {
						snprintf(size_buf, sizeof(size_buf), "—");
					}
					else {
						FormatCleanSize(size.bytes, size_buf, sizeof(size_buf));
					}
				}
				else {
					snprintf(size_buf, sizeof(size_buf), "—");
				}

				bool selected = task->IsSelected();
				CleanItemData card{
					task->GetName(),
					task->GetPurpose(),
					task->GetTooltip(),
					size_buf,
					&selected,
					task,
				};
				CleanItemWidget(widget_id, card, ImVec2(kItemCardW, kItemCardH));
				task->SetSelected(selected);

				x += kItemCardW + gap;
				row_max_h = ImMax(row_max_h, kItemCardH);
			}

			ImGui::SetCursorPos(ImVec2(start_x, y + row_max_h + gap));
		}
	}
}

void ClearPageUI::OpenFolderInExplorer(const char* utf8_path)
{
	if (utf8_path == nullptr || utf8_path[0] == '\0') {
		return;
	}
	wchar_t wide_path[MAX_PATH * 2] = {};
	const int converted = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wide_path,
		static_cast<int>(sizeof(wide_path) / sizeof(wide_path[0])));
	if (converted <= 0) {
		return;
	}
	ShellExecuteW(nullptr, L"explore", wide_path, nullptr, nullptr, SW_SHOWNORMAL);
}

void ClearPageUI::RenderTaskDetailModal(HCleanTask* task)
{
	using namespace Theme;
	if (task == nullptr) {
		return;
	}

	const HCleanScanProgress scan = task->GetScanProgress();
	const bool detail_scanning = scan.state == HCleanScanState::Scanning;
	const size_t entry_count = detail_scanning ? 0 : task->GetDetailEntryCount();

	ImGui::SetNextWindowSize(ImVec2(kDetailModalW, kDetailModalH), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.04f, 0.07f, 0.07f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, cyan_dark);
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.0f, 0.45f, 0.45f, 0.6f));

	char popup_id[96];
	snprintf(popup_id, sizeof(popup_id), "clean_detail_%s", task->GetId());

	const bool open = ImGui::BeginPopupModal(popup_id, nullptr,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (!open) {
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(3);
		return;
	}

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const ImVec2 win_pos = window->Pos;
	const float inner_w = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;

	const ImRect accent_bb(
		ImVec2(win_pos.x + 10.0f, win_pos.y + 8.0f),
		ImVec2(win_pos.x + ImGui::GetWindowWidth() - 10.0f, win_pos.y + 11.0f));
	window->DrawList->AddRectFilled(accent_bb.Min, accent_bb.Max, ImGui::GetColorU32(cyan_neon), 2.0f);

	ImGui::Spacing();
	ImGui::TextColored(cyan_neon, "%s", I18N(task->GetName()));
	if (task->GetPurpose() != nullptr && task->GetPurpose()[0] != '\0') {
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner_w);
		ImGui::TextDisabled("%s", I18N(task->GetPurpose()));
		ImGui::PopTextWrapPos();
	}
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	const float footer_reserve = 44.0f;
	const float list_h = ImMax(80.0f, ImGui::GetContentRegionAvail().y - footer_reserve);

	if (entry_count == 0) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.05f, 0.05f, 1.0f));
		if (ImGui::BeginChild("##detail_custom", ImVec2(0.0f, list_h), true,
			ImGuiWindowFlags_NoScrollbar)) {
			if (detail_scanning) {
				ImGui::TextDisabled(I18N(u8"明細掃描中，請稍候…（%.0f%%）"), scan.percent);
			}
			else {
				ImGui::TextDisabled(I18N(u8"此項目尚無子資料夾明細。"));
			}
			task->RenderDetailGui();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}
	else {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.05f, 0.05f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, cyan_dark);
		if (ImGui::BeginChild("##detail_list", ImVec2(0.0f, list_h), true,
			ImGuiWindowFlags_None)) {
			const float row_w = ImGui::GetContentRegionAvail().x;
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(entry_count), kDetailRowStride);
			while (clipper.Step()) {
				const size_t live_count = task->GetDetailEntryCount();
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
					if (static_cast<size_t>(row) >= live_count) {
						continue;
					}
					HCleanDetailEntry* entry = task->GetDetailEntry(static_cast<size_t>(row));
					if (entry == nullptr) {
						continue;
					}
					char row_id[24];
					snprintf(row_id, sizeof(row_id), "##row_%d", row);
					RenderDetailEntryRow(row_id, entry, row_w);
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	const float btn_w = 96.0f;
	const float btn_h = 28.0f;
	const float btn_gap = 10.0f;
	const float btn_x0 = ImGui::GetWindowContentRegionMax().x - btn_w * 2.0f - btn_gap;
	ImGui::SetCursorPosX(btn_x0);
	const ImVec2 apply_pos = ImGui::GetCursorScreenPos();
	const ImRect apply_bb(apply_pos, apply_pos + ImVec2(btn_w, btn_h));
	const ImRect close_bb(
		apply_bb.Min + ImVec2(btn_w + btn_gap, 0.0f),
		apply_bb.Max + ImVec2(btn_w + btn_gap, 0.0f));

	if (CyberTextButton("##detail_apply", apply_bb, I18N(u8"套用"), true)) {
		HLOG_DEBUG("Detail modal: apply clicked for task '{}'", task->GetId());
		task->ApplyDetailSelection();
		SaveTaskDetailConfig(task->GetId());
	}
	if (CyberTextButton("##detail_close", close_bb, I18N(u8"關閉"), true)) {
		task->ApplyDetailSelection();
		SaveTaskDetailConfig(task->GetId());
		ImGui::CloseCurrentPopup();
	}
	ImGui::Dummy(ImVec2(0.0f, btn_h + 4.0f));

	ImGui::EndPopup();
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(3);
}

bool ClearPageUI::CleanItemWidget(const char* id, CleanItemData& item, const ImVec2& size)
{
	using namespace Theme;

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) {
		return false;
	}

	ImGui::PushID(id);
	const ImGuiID widget_id = window->GetID("##card");
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);

	ImGui::ItemSize(size);
	if (!ImGui::ItemAdd(bb, widget_id)) {
		ImGui::PopID();
		return false;
	}

	const bool card_hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max, false);

	HCleanScanProgress scan{};
	if (item.task != nullptr) {
		scan = item.task->GetScanProgress();
	}
	const bool show_progress = scan.state == HCleanScanState::Scanning;
	const bool show_detail = item.task != nullptr
		&& (scan.state == HCleanScanState::Done || scan.state == HCleanScanState::Idle);

	const ImU32 bg_col = ImGui::GetColorU32(card_hovered ? card_bg_hover : card_bg);
	const ImU32 border_col = ImGui::GetColorU32(card_hovered ? cyan_neon : cyan_dark);
	const float rounding = ImGui::GetStyle().FrameRounding;

	window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, rounding);
	window->DrawList->AddRect(bb.Min, bb.Max, border_col, rounding, 0, 1.0f);

	const float pad = 10.0f;
	const float inner_left = bb.Min.x + pad;
	const float inner_right = bb.Max.x - pad;
	const float check_sz = 18.0f;

	const char* detail_label = I18N(u8"詳細");
	const ImVec2 detail_text = ImGui::CalcTextSize(detail_label);
	const float detail_btn_w = detail_text.x + 12.0f;
	const float detail_btn_h = detail_text.y + 8.0f;

	const ImRect check_bb(
		ImVec2(inner_left, bb.Min.y + pad),
		ImVec2(inner_left + check_sz, bb.Min.y + pad + check_sz));
	const ImGuiID check_id = window->GetID("##check");
	ImGui::ItemAdd(check_bb, check_id);

	bool check_hovered = false;
	bool check_held = false;
	const bool check_pressed = ImGui::ButtonBehavior(check_bb, check_id, &check_hovered, &check_held);
	if (check_pressed && item.selected != nullptr) {
		*item.selected = !*item.selected;
	}

	const ImU32 check_border = ImGui::GetColorU32(check_hovered ? cyan_neon : cyan_dark);
	window->DrawList->AddRect(check_bb.Min, check_bb.Max, check_border, 2.0f, 0, 1.0f);
	if (item.selected != nullptr && *item.selected) {
		const ImVec2 inner_min = check_bb.Min + ImVec2(4.0f, 4.0f);
		const ImVec2 inner_max = check_bb.Max - ImVec2(4.0f, 4.0f);
		window->DrawList->AddRectFilled(inner_min, inner_max, ImGui::GetColorU32(cyan_neon), 2.0f);
	}

	const ImVec2 title_pos(check_bb.Max.x + 8.0f, bb.Min.y + pad);
	const char* title_txt = (item.title != nullptr && item.title[0] != '\0')
		? I18N(item.title) : "";
	window->DrawList->AddText(title_pos, ImGui::GetColorU32(ImGuiCol_Text), title_txt);

	const ImVec2 title_sz = ImGui::CalcTextSize(title_txt);
	if (item.task != nullptr && item.task->IsDestructiveTask()) {
		const ImVec2 badge_pos(title_pos.x + title_sz.x + 6.0f, title_pos.y - 1.0f);
		DrawDestructiveBadge(window->DrawList, badge_pos, inner_right);
	}

	const float sep_y = bb.Min.y + pad + check_sz + 8.0f;
	window->DrawList->AddLine(
		ImVec2(inner_left, sep_y),
		ImVec2(inner_right, sep_y),
		ImGui::GetColorU32(cyan_dark), 1.0f);

	const float bottom_row_h = ImMax(detail_btn_h, ImGui::GetFontSize());
	const float footer_y = bb.Max.y - pad - bottom_row_h;
	const float progress_top = footer_y - kItemProgressBlockH;
	const float msg_y = sep_y + 8.0f;
	const float msg_bottom = progress_top - kItemBottomGap;
	const float msg_h = ImMax(1.0f, msg_bottom - msg_y);
	const ImRect msg_region(
		ImVec2(inner_left, msg_y),
		ImVec2(inner_right, msg_y + msg_h));

	const char* msg_txt = (item.message != nullptr && item.message[0] != '\0')
		? I18N(item.message) : "";
	RenderScrollableMessageChild("##msg", msg_region, msg_txt);

	if (show_progress) {
		const ImRect prog_bb(
			ImVec2(inner_left, progress_top + 4.0f),
			ImVec2(inner_right, progress_top + 16.0f));
		DrawProgressBar(window->DrawList, prog_bb, scan.percent, card_hovered);

		if (scan.status_text != nullptr && scan.status_text[0] != '\0') {
			const ImVec2 status_pos(inner_left, progress_top + 18.0f);
			window->DrawList->AddText(
				status_pos,
				ImGui::GetColorU32(ImGuiCol_TextDisabled),
				I18N(scan.status_text));
		}
	}
	else {
		const ImRect prog_track_bb(
			ImVec2(inner_left, progress_top + 4.0f),
			ImVec2(inner_right, progress_top + 16.0f));
		window->DrawList->AddRectFilled(
			prog_track_bb.Min, prog_track_bb.Max,
			ImGui::GetColorU32(ImVec4(0.02f, 0.05f, 0.05f, 0.35f)), 3.0f);
	}

	const ImRect detail_bb(
		ImVec2(inner_left, footer_y),
		ImVec2(inner_left + detail_btn_w, footer_y + detail_btn_h));
	if (show_detail && item.task != nullptr) {
		const bool detail_pressed = CyberTextButton("##detail", detail_bb, detail_label, true);
		char popup_id[96];
		snprintf(popup_id, sizeof(popup_id), "clean_detail_%s", item.task->GetId());
		if (detail_pressed) {
			HLOG_DEBUG("Opened detail popup for task '{}'", item.task->GetId());
			ImGui::OpenPopup(popup_id);
		}
		RenderTaskDetailModal(item.task);
	}

	if (item.size_text != nullptr && item.size_text[0] != '\0') {
		const ImVec2 size_label = ImGui::CalcTextSize(item.size_text);
		const ImVec2 size_pos(inner_right - size_label.x, footer_y + (bottom_row_h - size_label.y) * 0.5f);
		window->DrawList->AddText(size_pos, ImGui::GetColorU32(cyan_neon), item.size_text);
	}

	if (card_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		if (item.selected != nullptr) {
			*item.selected = !*item.selected;
		}
	}

	if (card_hovered && !check_hovered) {
		char detail_summary[256] = {};
		if (item.task != nullptr) {
			item.task->GetDetailSelectionSummary(detail_summary, sizeof(detail_summary));
		}
		if (item.tooltip != nullptr && item.tooltip[0] != '\0') {
			if (detail_summary[0] != '\0') {
				ImGui::SetTooltip("%s\n%s%s", item.tooltip, detail_summary,
					(item.task != nullptr && item.task->IsDestructiveTask())
					? I18N(u8"\n此任務含破壞性明細，請在「詳細」中確認子項。") : "");
			}
			else {
				ImGui::SetTooltip("%s", item.tooltip);
			}
		}
		else if (detail_summary[0] != '\0') {
			ImGui::SetTooltip("%s", detail_summary);
		}
	}

	ImGui::PopID();
	return check_pressed;
}

bool ClearPageUI::CategorySectionHeader(const char* id, const char* category_id, const char* display_name,
	bool* expanded, bool* request_rescan)
{
	using namespace Theme;

	if (category_id == nullptr || expanded == nullptr) {
		return false;
	}

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) {
		return false;
	}

	ImGui::PushID(id);
	bool interacted = false;

	HCleanCategoryScanInfo cat_scan{};
	GetCleanCategoryScanInfo(category_id, &cat_scan);
	const float row_h = kHeaderRowH;

	const ImVec2 row_pos = window->DC.CursorPos;
	const float content_w = ImGui::GetContentRegionAvail().x;
	const ImRect row_bb(row_pos, row_pos + ImVec2(content_w, row_h));
	ImGui::ItemSize(row_bb.GetSize());
	if (!ImGui::ItemAdd(row_bb, window->GetID("##cat_header_row"))) {
		ImGui::PopID();
		return false;
	}

	const bool row_hovered = ImGui::IsMouseHoveringRect(row_bb.Min, row_bb.Max, false);
	const ImU32 row_bg = ImGui::GetColorU32(row_hovered ? header_bg_hover : header_bg);
	window->DrawList->AddRectFilled(row_bb.Min, row_bb.Max, row_bg, kRounding);

	if (row_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		*expanded = !*expanded;
		HLOG_DEBUG("Category '{}' {} by double click", category_id, *expanded ? "expanded" : "collapsed");
		interacted = true;
	}

	const float pad_x = 8.0f;
	float x = row_bb.Min.x + pad_x;
	const float y_mid = row_bb.GetCenter().y;

	const ImRect check_bb(
		ImVec2(x, y_mid - kCheckSz * 0.5f),
		ImVec2(x + kCheckSz, y_mid + kCheckSz * 0.5f));
	x = check_bb.Max.x + 10.0f;

	const bool category_all = IsCleanCategoryAllSelected(category_id);
	const bool category_partial = IsCleanCategoryPartiallySelected(category_id);
	const bool master_checked = category_all;

	bool check_pressed = false;
	if (CyberToggleBox("##cat_master", check_bb, master_checked, category_partial, &check_pressed)) {
		SetCleanCategorySelected(category_id, !category_all);
		interacted = true;
	}

	const char* title = (display_name != nullptr && display_name[0] != '\0')
		? I18N(display_name) : category_id;
	const ImVec2 title_size = ImGui::CalcTextSize(title);
	const ImVec2 title_pos(x, y_mid - title_size.y * 0.5f);
	window->DrawList->AddText(title_pos, ImGui::GetColorU32(cyan_neon), title);
	x += title_size.x + 12.0f;

	const char* scan_label = I18N(u8"掃描");
	const ImVec2 scan_text = ImGui::CalcTextSize(scan_label);
	const float scan_w = scan_text.x + 18.0f;
	const float chevron_w = row_h - 8.0f;
	const float right_pad = pad_x;
	const float x_right = row_bb.Max.x - right_pad;

	const ImRect chevron_bb(
		ImVec2(x_right - chevron_w, row_bb.Min.y + 4.0f),
		ImVec2(x_right, row_bb.Max.y - 4.0f));
	const ImRect scan_bb(
		ImVec2(chevron_bb.Min.x - 8.0f - scan_w, row_bb.Min.y + 4.0f),
		ImVec2(chevron_bb.Min.x - 8.0f, row_bb.Max.y - 4.0f));

	const float block_x = scan_bb.Min.x - 12.0f - kCatScanBlockW;
	const float line_end = block_x - 10.0f;

	if (cat_scan.any_scanning) {
		const ImRect scan_block_bb(
			ImVec2(block_x, row_bb.Min.y),
			ImVec2(block_x + kCatScanBlockW, row_bb.Max.y));

		char scan_status[48];
		snprintf(scan_status, sizeof(scan_status), I18N(u8"掃描中 %d/%d"),
			cat_scan.completed_count, cat_scan.task_count);
		const ImVec2 status_size = ImGui::CalcTextSize(scan_status);

		ImGui::PushClipRect(scan_block_bb.Min, scan_block_bb.Max, true);

		const ImVec2 status_pos(block_x, y_mid - status_size.y * 0.5f);
		window->DrawList->AddText(status_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), scan_status);

		const float bar_y0 = y_mid - 3.0f;
		const float bar_y1 = y_mid + 3.0f;
		const float bar_min_x = block_x + status_size.x + 6.0f;
		const float bar_max_x = scan_block_bb.Max.x;
		if (bar_max_x > bar_min_x + 4.0f) {
			const ImRect mini_bar_bb(ImVec2(bar_min_x, bar_y0), ImVec2(bar_max_x, bar_y1));
			DrawProgressBar(window->DrawList, mini_bar_bb, cat_scan.aggregate_percent, row_hovered);
		}

		ImGui::PopClipRect();
	}

	const float line_y = y_mid;
	if (line_end > x + 8.0f) {
		const ImU32 line_dark = ImGui::GetColorU32(cyan_dark);
		const ImU32 line_bright = ImGui::GetColorU32(ImVec4(0.0f, 0.75f, 0.75f, 0.85f));
		const ImU32 line_fade = ImGui::GetColorU32(ImVec4(0.0f, 0.25f, 0.25f, 0.4f));
		DrawGradientHLine(window->DrawList,
			ImVec2(x, line_y - 0.5f),
			ImVec2(line_end, line_y + 0.5f),
			line_fade, line_bright, line_dark);
	}

	if (CyberTextButton("##cat_scan", scan_bb, scan_label, !IsAnyCleanTaskScanning())) {
		if (request_rescan) {
			*request_rescan = true;
		}
		HLOG_INFO("ClearPage: category '{}' scan button clicked", category_id);
		interacted = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(IsAnyCleanTaskScanning() ? I18N(u8"掃描進行中") : I18N(u8"重新掃描此分類"));
	}

	if (CyberChevronButton("##cat_expand", chevron_bb, *expanded)) {
		*expanded = !*expanded;
		HLOG_DEBUG("Category '{}' {}", category_id, *expanded ? "expanded" : "collapsed");
		interacted = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(*expanded ? I18N(u8"收合（可雙擊標題列）") : I18N(u8"展開（可雙擊標題列）"));
	}

	window->DrawList->AddRect(row_bb.Min, row_bb.Max,
		ImGui::GetColorU32(row_hovered ? cyan_neon : cyan_dark), kRounding, 0, 1.0f);

	ImGui::PopID();
	return interacted;
}

void ClearPageUI::RenderFooter()
{
	using namespace Theme;

	TickScanWorker();
	TickDeferredScanAllCleanTasks(1);
	TickCleanWorker();

	HCleanSessionInfo session{};
	GetCleanSessionInfo(&session);

	const bool cleaning = session.phase == HCleanSessionPhase::Cleaning;
	const bool scanning = IsAnyCleanTaskScanning() || IsDeferredScanAllCleanTasksActive();
	const bool done = session.phase == HCleanSessionPhase::Done;

	ImGui::Separator();
	ImGui::Spacing();

	const float window_width = ImGui::GetWindowWidth();
	const float pad_x = 8.0f;
	const float btn_size_w = 100.0f;
	const float btn_h = 32.0f;
	const float btn_spacing = 10.0f;
	const float buttons_block_w = btn_size_w * 2.0f + btn_spacing;
	const float progress_left = Logo::HPS_Logo.texture != 0 ? (kFooterLogoW + 16.0f) : pad_x;
	const float progress_right = window_width - buttons_block_w - 24.0f;
	const float progress_w = ImMax(120.0f, progress_right - progress_left);

	const float footer_top_y = ImGui::GetCursorPosY();
	const float logo_y = footer_top_y + 2.0f;
	if (Logo::HPS_Logo.texture != 0) {
		ImGui::SetCursorPos(ImVec2(pad_x, logo_y));
		ImGui::Image((ImTextureID)(intptr_t)Logo::HPS_Logo.texture, ImVec2(kFooterLogoW, kFooterLogoH));
	}

	constexpr float kProgressBarH = 14.0f;
	const float progress_y = footer_top_y + 6.0f;
	const ImVec2 progress_pos(progress_left, progress_y);
	ImGui::SetCursorPos(progress_pos);
	ImGui::Dummy(ImVec2(progress_w, kProgressBarH));
	const ImRect prog_bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	HCleanGlobalScanInfo global_scan = {};
	const bool has_global_scan = GetGlobalCleanScanInfo(&global_scan);
	float bar_percent = 0.f;
	if (cleaning || done) {
		bar_percent = session.progress_percent;
	}
	else if (scanning && has_global_scan) {
		bar_percent = global_scan.aggregate_percent;
	}
	DrawProgressBar(ImGui::GetWindowDrawList(), prog_bb, bar_percent,
		ImGui::IsMouseHoveringRect(prog_bb.Min, prog_bb.Max));

	char status_line[256];
	if (cleaning && session.tasks_total > 0) {
		if (session.details_total > 0 && session.current_task_name != nullptr
			&& session.current_task_name[0] != '\0') {
			snprintf(status_line, sizeof(status_line),
				I18N(u8"清理中  %s  任務 %d/%d  明細 %d/%d  %.0f%%"),
				I18N(session.current_task_name),
				session.tasks_completed + 1, session.tasks_total,
				session.details_completed, session.details_total,
				session.progress_percent);
		}
		else {
			snprintf(status_line, sizeof(status_line), I18N(u8"清理中  %s  %d/%d  %.0f%%"),
				session.status_text != nullptr ? I18N(session.status_text) : I18N(u8"處理項目"),
				session.tasks_completed + 1, session.tasks_total, session.progress_percent);
		}
	}
	else if (done) {
		snprintf(status_line, sizeof(status_line), I18N(u8"清理完成"));
	}
	else if (scanning && has_global_scan) {
		if (global_scan.current_task_name[0] != '\0') {
			snprintf(status_line, sizeof(status_line),
				I18N(u8"掃描中  %s  %d/%d 任務  %.0f%%  %s"),
				I18N(global_scan.current_task_name),
				global_scan.completed_count + global_scan.scanning_count,
				global_scan.task_count,
				static_cast<double>(global_scan.aggregate_percent),
				global_scan.status_text[0] != '\0' ? I18N(global_scan.status_text) : "");
		}
		else {
			snprintf(status_line, sizeof(status_line),
				I18N(u8"掃描中  %d/%d 任務  %.0f%%"),
				global_scan.completed_count + global_scan.scanning_count,
				global_scan.task_count,
				static_cast<double>(global_scan.aggregate_percent));
		}
	}
	else if (scanning) {
		snprintf(status_line, sizeof(status_line), I18N(u8"掃描中…"));
	}
	else {
		snprintf(status_line, sizeof(status_line), I18N(u8"就緒"));
	}

	const float status_row_y = progress_y + kProgressBarH + 4.0f;
	const bool show_log = (cleaning || done) && session.last_log_line != nullptr && session.last_log_line[0] != '\0';
	const float log_row_y = status_row_y + ImGui::GetTextLineHeight() + 2.0f;
	const float metrics_row_y = (show_log ? log_row_y : status_row_y) + ImGui::GetTextLineHeight() + 4.0f;

	ImGui::SetCursorPos(ImVec2(progress_left, status_row_y));
	ImGui::TextColored(cyan_neon, "%s", status_line);

	if (show_log) {
		ImGui::SetCursorPos(ImVec2(progress_left, log_row_y));
		ImGui::TextDisabled("%s", session.last_log_line);
	}

	ImGui::SetCursorPos(ImVec2(window_width - buttons_block_w - 12.0f, progress_y));
	const ImVec2 btn_size(btn_size_w, btn_h);
	if (CyberFooterButton(I18N(u8"掃描"), btn_size, !cleaning && !scanning)) {
		if (!cleaning && !scanning) {
			HLOG_INFO("ClearPage: user requested scan all");
			ScanAllCleanTasks();
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(cleaning ? I18N(u8"清理進行中")
			: scanning ? I18N(u8"掃描進行中") : I18N(u8"重新掃描全部清理項目"));
	}

	ImGui::SameLine(0.0f, btn_spacing);
	if (CyberFooterButton(I18N(u8"開始清理"), btn_size)) {
		if (!cleaning) {
			HLOG_INFO("ClearPage: user requested clean selected tasks");
			RequestCleanSelectedTasks();
		}
	}
	if (ImGui::IsItemHovered() && cleaning) {
		ImGui::SetTooltip(I18N(u8"清理進行中"));
	}

	ImGui::SetCursorPos(ImVec2(pad_x, metrics_row_y));
	RenderCleanSessionStatusRow(session);
}

void ClearPageUI::SetCleanSuggestionHint(const char* preset_id, int64_t estimated_bytes)
{
	g_clean_suggest_preset[0] = '\0';
	if (preset_id != nullptr && preset_id[0] != '\0') {
		strncpy_s(g_clean_suggest_preset, preset_id, _TRUNCATE);
	}
	g_clean_suggest_bytes = estimated_bytes;
}

void ClearPageUI::RenderContent()
{
	if (g_clean_suggest_preset[0] != '\0') {
		char size_buf[48] = {};
		if (g_clean_suggest_bytes > 0) {
			FormatCleanSize(g_clean_suggest_bytes, size_buf, sizeof(size_buf));
		}
		else {
			strncpy_s(size_buf, I18N(u8"（需先掃描）"), _TRUNCATE);
		}
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.14f, 0.14f, 1.f));
		if (ImGui::BeginChild("##clean_suggest_banner", ImVec2(0, 52), true)) {
			ImGui::TextWrapped(
				I18N(u8"系統優化建議：可至下方「快速預設」手動套用「%s」相關清理（估計約 %s）。本程式不會自動勾選。"),
				g_clean_suggest_preset, size_buf);
			ImGui::SameLine();
			if (ImGui::SmallButton(I18N(u8"關閉提示##clean_suggest"))) {
				g_clean_suggest_preset[0] = '\0';
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	const size_t category_count = GetCleanCategoryCount();
	if (category_count == 0) {
		ImGui::TextDisabled(I18N(u8"尚未註冊任何清理分類。請在 .cpp 中使用 REG_CLEAN_CATEGORY / REG_CLEAN_TASK。"));
		return;
	}

	{
		const float preset_h = 28.0f;
		const float preset_w = 108.0f;
		const float preset_gap = 8.0f;
		const float label_w = ImGui::CalcTextSize(I18N(u8"快速預設")).x;
		const ImVec2 row_start = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(label_w + (preset_w + preset_gap) * 4.0f, preset_h + 4.0f));

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(row_start, ImGui::GetColorU32(ImGuiCol_TextDisabled), I18N(u8"快速預設"));

		const float buttons_x = row_start.x + label_w + 10.0f;
		auto draw_preset = [&](int index, const char* id_suffix, const char* label, const char* preset_id) {
			const ImVec2 pos(buttons_x + static_cast<float>(index) * (preset_w + preset_gap), row_start.y);
			const ImRect bb(pos, ImVec2(pos.x + preset_w, pos.y + preset_h));
			if (CyberTextButton(id_suffix, bb, label, true)) {
				ApplyCleanPreset(preset_id);
			}
		};
		draw_preset(0, "##preset_gamer", I18N(u8"玩家預設"), "gamer");
		draw_preset(1, "##preset_general", I18N(u8"一般預設"), "general");
		draw_preset(2, "##preset_developer", I18N(u8"開發者預設"), "developer");
		draw_preset(3, "##preset_advanced", I18N(u8"進階"), "advanced");
	}

	static bool initial_scan_done = false;
	static std::unordered_map<std::string, bool> category_expanded;

	if (!initial_scan_done) {
		HLOG_INFO("ClearPage: scheduling deferred initial scan");
		BeginDeferredScanAllCleanTasks();
		initial_scan_done = true;
	}

	for (size_t ci = 0; ci < category_count; ++ci) {
		const HCleanCategoryInfo* category = GetCleanCategory(ci);
		if (category == nullptr || category->id == nullptr) {
			continue;
		}

		if (!CategoryHasVisibleCleanTasks(category->id)) {
			continue;
		}

		const auto inserted = category_expanded.try_emplace(category->id, true);
		bool& expanded = inserted.first->second;

		char section_id[80];
		snprintf(section_id, sizeof(section_id), "##cat_section_%s", category->id);

		bool request_rescan = false;
		CategorySectionHeader(section_id, category->id, category->display_name, &expanded, &request_rescan);
		if (request_rescan) {
			ScanCategory(category->id);
		}

		if (!expanded) {
			ImGui::Spacing();
			continue;
		}

		ImGui::Indent(4.0f);
		RenderCategoryTaskGrid(category->id);
		ImGui::Unindent(4.0f);
		ImGui::Spacing();
	}
}
