#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include "HUiHistoryList.h"
#include "CleanHistory.h"
#include "HCleanTask.h"
#include <imgui_internal.h>
#include <cstdio>
#include <ctime>

namespace {
	using namespace HUiTheme;

	constexpr float kRounding = 6.f;

	static void FormatLocalTime(int64_t unix_ms, char* out, size_t out_size)
	{
		if (out == nullptr || out_size == 0) {
			return;
		}
		const time_t sec = static_cast<time_t>(unix_ms / 1000);
		struct tm local_tm = {};
#if defined(_WIN32)
		localtime_s(&local_tm, &sec);
#else
		localtime_r(&sec, &local_tm);
#endif
		strftime(out, out_size, "%Y-%m-%d %H:%M", &local_tm);
	}

	static bool CyberButton(const char* id, const ImVec2& size, const char* label)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const ImRect bb(pos, pos + size);
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiID btn_id = window->GetID(id);
		ImGui::ItemSize(size);
		if (!ImGui::ItemAdd(bb, btn_id)) {
			ImGui::Dummy(size);
			return false;
		}
		bool hovered = false;
		bool held = false;
		const bool pressed = ImGui::ButtonBehavior(bb, btn_id, &hovered, &held);
		const ImU32 border = ImGui::GetColorU32(hovered || held ? cyan_neon() : cyan_dark());
		const ImU32 bg = ImGui::GetColorU32(held ? active_bg() : (hovered ? hover_bg() : header_bg()));
		window->DrawList->AddRectFilled(bb.Min, bb.Max, bg, kRounding);
		window->DrawList->AddRect(bb.Min, bb.Max, border, kRounding, 0, 1.f);
		const ImVec2 ts = ImGui::CalcTextSize(label);
		window->DrawList->AddText(
			ImVec2(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f, bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f),
			ImGui::GetColorU32(hovered ? cyan_neon() : ImGui::GetStyleColorVec4(ImGuiCol_Text)), label);
		return pressed;
	}

	static void DrawSummaryCards(const CleanHistorySummary& summary)
	{
		const float w = ImGui::GetContentRegionAvail().x;
		const float gap = 10.f;
		const float card_w = (w - gap * 2.f) / 3.f;
		const float card_h = 72.f;

		auto draw_card = [&](const char* id, const char* title, const char* value, const char* hint) {
			ImGui::BeginChild(id, ImVec2(card_w, card_h), false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImRect bb(p0, p0 + inner);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
			dl->AddText(bb.Min + ImVec2(12.f, 10.f), ImGui::GetColorU32(cyan_neon()), title);
			dl->AddText(bb.Min + ImVec2(12.f, 32.f), ImGui::GetColorU32(ImGuiCol_Text), value);
			if (hint != nullptr && hint[0] != '\0') {
				dl->AddText(bb.Min + ImVec2(12.f, 52.f), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
			}
			ImGui::EndChild();
		};

		char total_buf[32];
		FormatCleanSize(summary.total_freed_bytes, total_buf, sizeof(total_buf));

		char last_buf[32] = "—";
		char last_hint[48] = {};
		strncpy_s(last_hint, I18N(u8"尚未執行清理"), _TRUNCATE);
		if (summary.has_last) {
			FormatCleanSize(summary.last.freed_bytes, last_buf, sizeof(last_buf));
			char time_buf[32];
			FormatLocalTime(summary.last.unix_ms, time_buf, sizeof(time_buf));
			snprintf(last_hint, sizeof(last_hint), "%s", time_buf);
		}

		char count_buf[24];
		snprintf(count_buf, sizeof(count_buf), I18N(u8"%d 次"), summary.session_count);

		draw_card("##hist_total", I18N(u8"累計釋放"), total_buf, I18N(u8"所有紀錄加總"));
		ImGui::SameLine(0.f, gap);
		draw_card("##hist_last", I18N(u8"上次清理"), last_buf, last_hint);
		ImGui::SameLine(0.f, gap);
		draw_card("##hist_count", I18N(u8"清理次數"), count_buf, nullptr);
	}
}

class HistoryPage_ : public HPage
{
public:
	void Render() override
	{
		RenderPageHeader();
		ImGui::Spacing();

		CleanHistorySummary summary{};
		CleanHistory::GetSummary(&summary);
		DrawSummaryCards(summary);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const float btn_w = 96.f;
		if (CyberButton("##hist_clear", ImVec2(btn_w, 28.f), I18N(u8"清空紀錄"))) {
			ImGui::OpenPopup("confirm_clear_history");
		}
		ImGui::SameLine();
		if (CyberButton("##hist_goto_clear", ImVec2(120.f, 28.f), I18N(u8"前往清理"))) {
			open_page("ClearPage");
		}

		if (ImGui::BeginPopupModal("confirm_clear_history", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(I18N(u8"確定要刪除全部清理歷史？"));
			ImGui::Spacing();
			if (ImGui::Button(I18N(u8"確定"), ImVec2(80.f, 0.f))) {
				CleanHistory::ClearAll();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(I18N(u8"取消"), ImVec2(80.f, 0.f))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Spacing();
		ImGui::TextColored(cyan_neon(), "%s", I18N(u8"清理紀錄"));
		ImGui::SameLine();
		ImGui::TextDisabled(I18N(u8"（最新一筆在上方，卡片可捲動瀏覽）"));
		ImGui::Spacing();

		const std::vector<CleanHistoryEntry>& entries = CleanHistory::GetEntries();
		const ImVec2 list_avail = ImGui::GetContentRegionAvail();
		HUiHistoryList::Draw("##hist_cards", entries, list_avail.x, ImMax(120.f, list_avail.y));
	}

	void init(nlohmann::json& context) override
	{
		(void)context;
		CleanHistory::Reload();
		HLOG_INFO("HistoryPage initialized");
	}

	void release() override { HLOG_INFO("HistoryPage released"); }
};

REG_PAGEN(HistoryPage_, "HistoryPage")

static bool s_nav_history = []() {
	RegistrationNavItem_internal(u8"清理歷史", "HistoryPage", 30);
	return true;
}();
