#define IMGUI_DEFINE_MATH_OPERATORS
#include "HUiHistoryList.h"
#include "HCleanTask.h"
#include "HUiTheme.h"
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "Hi18n.h"

namespace {
	using namespace HUiTheme;

	void FormatLocalTime(int64_t unix_ms, char* out, size_t out_size)
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

	void DrawStatusPill(ImDrawList* dl, const ImVec2& pos, const CleanHistoryEntry& e)
	{
		char pill[48] = {};
		ImVec4 col(0.25f, 0.95f, 0.45f, 1.f);
		if (e.tasks_failed > 0 && e.tasks_succeeded == 0) {
			snprintf(pill, sizeof(pill), I18N(u8"失敗 %d"), e.tasks_failed);
			col = ImVec4(1.f, 0.4f, 0.35f, 1.f);
		}
		else if (e.tasks_failed > 0) {
			snprintf(pill, sizeof(pill), I18N(u8"部分成功 %d/%d"), e.tasks_succeeded, e.tasks_total);
			col = ImVec4(1.f, 0.75f, 0.25f, 1.f);
		}
		else {
			snprintf(pill, sizeof(pill), I18N(u8"成功 %d/%d"), e.tasks_succeeded, e.tasks_total);
		}

		const ImVec2 ts = ImGui::CalcTextSize(pill);
		const ImVec2 pad(8.f, 3.f);
		const ImRect bb(pos, pos + ts + pad * 2.f);
		dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.18f)), 4.f);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(col), 4.f, 0, 1.f);
		dl->AddText(bb.Min + pad, ImGui::GetColorU32(col), pill);
	}

	void DrawProgressBar(ImDrawList* dl, const ImRect& bb, float ratio, int failed)
	{
		const ImU32 track = ImGui::GetColorU32(track_bg());
		const ImU32 fill = ImGui::GetColorU32(failed > 0
			? ImVec4(1.f, 0.65f, 0.25f, 1.f) : ImVec4(0.f, 0.75f, 0.75f, 1.f));
		dl->AddRectFilled(bb.Min, bb.Max, track, 3.f);
		if (ratio > 0.001f) {
			const float w = ImMax(4.f, bb.GetWidth() * ImClamp(ratio, 0.f, 1.f));
			dl->AddRectFilled(bb.Min, ImVec2(bb.Min.x + w, bb.Max.y), fill, 3.f);
		}
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), 3.f, 0, 1.f);
	}

	static void DrawTagPill(ImDrawList* dl, const ImRect& bb, const char* text)
	{
		const ImVec2 pad(6.f, 2.f);
		dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.10f, 0.10f, 1.f)), 3.f);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), 3.f, 0, 1.f);
		dl->AddText(bb.Min + pad, ImGui::GetColorU32(ImVec4(0.7f, 0.85f, 0.85f, 1.f)), text);
	}

	void DrawTaskTags(ImDrawList* dl, const ImRect& clip_bb, const CleanHistoryEntry& e)
	{
		if (e.task_ids.empty()) {
			return;
		}

		const float tag_h = ImGui::GetTextLineHeight() + 6.f;
		const float gap = 6.f;
		dl->PushClipRect(clip_bb.Min, clip_bb.Max, true);

		float cx = clip_bb.Min.x;
		float cy = clip_bb.Min.y;
		const float max_x = clip_bb.Max.x;
		int shown = 0;

		for (size_t i = 0; i < e.task_ids.size(); ++i) {
			const char* tid = e.task_ids[i].c_str();
			ImVec2 ts = ImGui::CalcTextSize(tid);
			const ImVec2 pad(6.f, 2.f);
			float tw = ts.x + pad.x * 2.f;

			if (cx + tw > max_x && shown > 0) {
				cx = clip_bb.Min.x;
				cy += tag_h + 4.f;
				if (cy + tag_h > clip_bb.Max.y) {
					break;
				}
			}

			if (cx + tw > max_x) {
				const float avail = max_x - cx - pad.x * 2.f - 20.f;
				if (avail > 24.f) {
					char short_buf[64] = {};
					strncpy_s(short_buf, tid, _TRUNCATE);
					while (short_buf[0] != '\0' && ImGui::CalcTextSize(short_buf).x > avail) {
						short_buf[strlen(short_buf) - 1] = '\0';
					}
					ts = ImGui::CalcTextSize(short_buf);
					tw = ts.x + pad.x * 2.f;
					const ImRect tag_bb(ImVec2(cx, cy), ImVec2(cx + tw, cy + tag_h));
					DrawTagPill(dl, tag_bb, short_buf);
					cx = tag_bb.Max.x + gap;
					++shown;
				}
				break;
			}

			const ImRect tag_bb(ImVec2(cx, cy), ImVec2(cx + tw, cy + tag_h));
			DrawTagPill(dl, tag_bb, tid);
			cx = tag_bb.Max.x + gap;
			++shown;
		}

		if (e.task_ids.size() > static_cast<size_t>(shown)) {
			char more[24] = {};
			snprintf(more, sizeof(more), "+%zu", e.task_ids.size() - static_cast<size_t>(shown));
			const ImVec2 more_pad(6.f, 2.f);
			const ImVec2 mts = ImGui::CalcTextSize(more);
			const float mw = mts.x + more_pad.x * 2.f;
			if (cx + mw <= max_x && cy + tag_h <= clip_bb.Max.y) {
				const ImRect more_bb(ImVec2(cx, cy), ImVec2(cx + mw, cy + tag_h));
				DrawTagPill(dl, more_bb, more);
			}
		}

		dl->PopClipRect();
	}

	bool DrawHistoryCard(const CleanHistoryEntry& e, int row_index, float width,
		float card_h, const HUiHistoryList::Style& style, bool is_latest)
	{
		const ImVec2 size(width, card_h);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const ImRect bb(pos, pos + size);

		ImGui::PushID(row_index);
		ImGui::ItemSize(size);
		if (!ImGui::ItemAdd(bb, ImGui::GetID("##hist_card"))) {
			ImGui::PopID();
			ImGui::Dummy(size);
			return false;
		}

		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const ImU32 card_col = ImGui::GetColorU32(hovered ? card_bg_hover() : card_bg());
		dl->AddRectFilled(bb.Min, bb.Max, card_col, style.card_rounding);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(is_latest ? cyan_neon() : cyan_dark()),
			style.card_rounding, 0, is_latest ? 1.4f : 1.f);

		const float accent_x = bb.Min.x + 2.f;
		const ImU32 accent_col = ImGui::GetColorU32(is_latest ? cyan_neon() : cyan_mid());
		dl->AddRectFilled(
			ImVec2(accent_x, bb.Min.y + 6.f),
			ImVec2(accent_x + style.accent_width, bb.Max.y - 6.f),
			accent_col, 2.f);

		const float content_x = bb.Min.x + style.pad + style.accent_width;
		const float content_w = bb.Max.x - content_x - style.pad;
		float y = bb.Min.y + 10.f;

		char time_buf[32] = {};
		FormatLocalTime(e.unix_ms, time_buf, sizeof(time_buf));
		dl->AddText(ImVec2(content_x, y), ImGui::GetColorU32(ImGuiCol_TextDisabled), time_buf);
		if (is_latest) {
			const char* badge = I18N(u8"最近");
			const ImVec2 bts = ImGui::CalcTextSize(badge);
			dl->AddText(ImVec2(content_x + ImGui::CalcTextSize(time_buf).x + 10.f, y),
				ImGui::GetColorU32(cyan_neon()), badge);
		}

		char pill_probe[48] = {};
		if (e.tasks_failed > 0 && e.tasks_succeeded == 0) {
			snprintf(pill_probe, sizeof(pill_probe), I18N(u8"失敗 %d"), e.tasks_failed);
		}
		else if (e.tasks_failed > 0) {
			snprintf(pill_probe, sizeof(pill_probe), I18N(u8"部分成功 %d/%d"), e.tasks_succeeded, e.tasks_total);
		}
		else {
			snprintf(pill_probe, sizeof(pill_probe), I18N(u8"成功 %d/%d"), e.tasks_succeeded, e.tasks_total);
		}
		const float pill_w = ImGui::CalcTextSize(pill_probe).x + 16.f;
		DrawStatusPill(dl, ImVec2(bb.Max.x - style.pad - pill_w, y - 2.f), e);
		y += ImGui::GetTextLineHeight() + 6.f;

		const bool has_disk = (e.disk_free_before >= 0 && e.disk_free_after >= 0);
		if (has_disk) {
			char disk_buf[80] = {};
			char before[24] = {};
			char after[24] = {};
			FormatCleanSize(e.disk_free_before, before, sizeof(before));
			FormatCleanSize(e.disk_free_after, after, sizeof(after));
			snprintf(disk_buf, sizeof(disk_buf), I18N(u8"系統碟可用 %s → %s"), before, after);
			dl->AddText(ImVec2(content_x, y), ImGui::GetColorU32(ImGuiCol_TextDisabled), disk_buf);
			y += ImGui::GetTextLineHeight() + 6.f;
		}

		char freed_buf[32] = {};
		FormatCleanSize(e.freed_bytes, freed_buf, sizeof(freed_buf));
		const ImVec2 freed_ts = ImGui::CalcTextSize(freed_buf);
		dl->AddText(ImVec2(content_x, y), ImGui::GetColorU32(cyan_neon()), I18N(u8"釋放"));
		dl->AddText(ImVec2(content_x + ImGui::CalcTextSize(I18N(u8"釋放")).x + 8.f, y - 2.f),
			ImGui::GetColorU32(cyan_neon()), freed_buf);

		char sel_buf[32] = {};
		FormatCleanSize(e.selected_bytes_at_start, sel_buf, sizeof(sel_buf));
		char sel_line[64] = {};
		snprintf(sel_line, sizeof(sel_line), I18N(u8"選取 %s"), sel_buf);
		dl->AddText(ImVec2(content_x + freed_ts.x + 48.f, y + 2.f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), sel_line);

		y += ImGui::GetTextLineHeight() + 10.f;

		const float bar_h = 6.f;
		const ImRect bar_bb(ImVec2(content_x, y), ImVec2(content_x + content_w * 0.42f, y + bar_h));
		const float ratio = (e.tasks_total > 0)
			? static_cast<float>(e.tasks_succeeded) / static_cast<float>(e.tasks_total) : 0.f;
		DrawProgressBar(dl, bar_bb, ratio, e.tasks_failed);
		y += bar_h + 8.f;

		const int skips = e.skip_locked + e.skip_access_denied + e.skip_timeout + e.skip_reparse;
		if (skips > 0) {
			char skip_buf[96] = {};
			snprintf(skip_buf, sizeof(skip_buf), I18N(u8"略過：鎖 %d · 拒 %d · 逾 %d · 重解析 %d"),
				e.skip_locked, e.skip_access_denied, e.skip_timeout, e.skip_reparse);
			dl->AddText(ImVec2(content_x, y), ImGui::GetColorU32(ImVec4(0.85f, 0.65f, 0.25f, 1.f)), skip_buf);
			y += ImGui::GetTextLineHeight() + 4.f;
		}

		if (!e.task_ids.empty()) {
			const ImRect tag_clip(
				ImVec2(content_x, y),
				ImVec2(bb.Max.x - style.pad, y + style.tag_row_h));
			DrawTaskTags(dl, tag_clip, e);
			y += style.tag_row_h + 6.f;
		}

		ImGui::PopID();
		ImGui::Dummy(size);
		return hovered;
	}
}

namespace HUiHistoryList {
	void Draw(const char* id, const std::vector<CleanHistoryEntry>& entries,
		float width, float height, const Style* style_override)
	{
		Style style = {};
		if (style_override != nullptr) {
			style = *style_override;
		}

		if (entries.empty()) {
			ImGui::PushID(id);
			ImGui::TextDisabled(I18N(u8"尚無清理紀錄。完成一次系統清理後會自動記錄。"));
			ImGui::PopID();
			return;
		}

		ImGui::PushID(id);
		if (!ImGui::BeginChild("##hist_list_scroll", ImVec2(width, height), false)) {
			ImGui::PopID();
			return;
		}

		ImGuiListClipper clipper;
		const float row_h = style.card_height + style.card_gap;
		clipper.Begin(static_cast<int>(entries.size()), row_h);
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				const bool is_latest = (i == 0);
				DrawHistoryCard(entries[static_cast<size_t>(i)], i, width, style.card_height, style, is_latest);
				if (i + 1 < clipper.ItemsCount) {
					ImGui::Dummy(ImVec2(0.f, style.card_gap));
				}
			}
		}

		ImGui::EndChild();
		ImGui::PopID();
	}
}