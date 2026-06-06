#define IMGUI_DEFINE_MATH_OPERATORS
#include "DiskHealthUI.h"
#include "DiskHealthScan.h"
#include "DiskHealthTest.h"
#include "HCleanTask.h"
#include "HAdminPrompt.h"
#include "HPage.h"
#include "Hi18n.h"
#include <imgui_internal.h>
#include <implot.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <vector>
#include <string>

namespace DiskHealthUI {
	namespace Theme {
		const ImVec4 cyan_neon(0.00f, 0.90f, 0.90f, 1.0f);
		const ImVec4 cyan_dim(0.00f, 0.55f, 0.55f, 1.0f);
		const ImVec4 panel_bg(0.04f, 0.07f, 0.07f, 1.0f);
		const ImVec4 card_bg(0.06f, 0.10f, 0.10f, 1.0f);
		const ImVec4 card_border(0.0f, 0.32f, 0.32f, 0.55f);
		const ImVec4 text_muted(0.45f, 0.55f, 0.55f, 1.0f);
	}

	namespace {
		static int g_selected_drive = 0;
		static int g_speed_sample_mb = 1024;
		static int g_speed_mode = 0;
		static bool g_pending_write_confirm = false;
		static int g_bad_sector_mode = 0;
		static bool g_implot_theme = false;
		static bool g_admin_prompt_done = false;
		static bool g_live_refresh_armed = false;
		static bool g_deferred_scan_requested = false;
		static bool g_persist_loaded = false;
		static size_t g_last_snap_drive_count = 0;

		static void SnprintfI18n(char* buf, size_t buf_size, const char* zh_fmt_key, ...)
		{
			const std::string fmt = Hi18n::I18NStr(zh_fmt_key);
			va_list ap;
			va_start(ap, zh_fmt_key);
			vsnprintf(buf, buf_size, fmt.c_str(), ap);
			va_end(ap);
		}

		static const char* StatusDisplayText(const char* status_text)
		{
			if (status_text == nullptr || status_text[0] == '\0') {
				return "";
			}
			if (strcmp(status_text, u8"未找到實體硬碟") == 0
				|| strcmp(status_text, u8"掃描硬碟中…") == 0) {
				return I18N(status_text);
			}
			return status_text;
		}

		static const char* BusDisplayText(const char* bus)
		{
			if (bus == nullptr || bus[0] == '\0' || strcmp(bus, "—") == 0) {
				return "—";
			}
			return I18N(bus);
		}

		static void FormatSmartAttrTitle(const DiskHealthScan::SmartAttribute& attr,
			char* title, size_t title_size)
		{
			if (attr.id >= 240 && attr.id <= 247) {
				SnprintfI18n(title, title_size, u8"溫度感測器 %d", attr.id - 239);
				return;
			}
			snprintf(title, title_size, "%s", I18N(attr.name_utf8));
		}

		struct HealthCounts {
			int good = 0;
			int caution = 0;
			int bad = 0;
			int other = 0;
		};

		static ImVec4 HealthColor(DiskHealthScan::HealthLevel level)
		{
			switch (level) {
			case DiskHealthScan::HealthLevel::Good:
				return ImVec4(0.25f, 0.95f, 0.45f, 1.f);
			case DiskHealthScan::HealthLevel::Caution:
				return ImVec4(1.f, 0.78f, 0.2f, 1.f);
			case DiskHealthScan::HealthLevel::Bad:
				return ImVec4(1.f, 0.35f, 0.3f, 1.f);
			default:
				return ImVec4(0.55f, 0.6f, 0.6f, 1.f);
			}
		}

		static float HealthScore01(DiskHealthScan::HealthLevel level)
		{
			switch (level) {
			case DiskHealthScan::HealthLevel::Good: return 0.92f;
			case DiskHealthScan::HealthLevel::Caution: return 0.55f;
			case DiskHealthScan::HealthLevel::Bad: return 0.22f;
			case DiskHealthScan::HealthLevel::Unavailable: return 0.08f;
			default: return 0.4f;
			}
		}

		// 依 SMART 關鍵欄位計算 0–100 分（等級仍由 DiskHealthScan::ComputeHealth 決定）
		static void QuerySectorCounters(const DiskHealthScan::DriveInfo& drive,
			int& reallocated, int& pending, int& uncorrectable)
		{
			DiskHealthScan::GetSectorCounters(drive, reallocated, pending, uncorrectable);
		}

		static void FormatSectorCounterText(int value, char* buf, size_t buf_size)
		{
			if (value < 0) {
				strncpy_s(buf, buf_size, "—", _TRUNCATE);
			}
			else {
				snprintf(buf, buf_size, "%d", value);
			}
		}

		static int ComputeHealthScorePercent(const DiskHealthScan::DriveInfo& drive)
		{
			if (drive.health == DiskHealthScan::HealthLevel::Unavailable) {
				return 8;
			}
			if (!drive.smart_available && drive.temperature_c < 0) {
				return 40;
			}

			int realloc_c = -1;
			int pending_c = -1;
			int uncorr_c = -1;
			QuerySectorCounters(drive, realloc_c, pending_c, uncorr_c);

			int score = 100;
			if (realloc_c > 0) {
				score -= (std::min)(realloc_c * 2, 25);
			}
			if (pending_c > 0) {
				score -= (std::min)(pending_c * 4, 30);
			}
			if (uncorr_c > 0) {
				score -= (std::min)(uncorr_c * 10, 40);
			}
			if (drive.temperature_c >= 58) {
				score -= 8;
			}
			if (drive.temperature_c >= 65) {
				score -= 12;
			}
			for (const auto& attr : drive.smart_attributes) {
				if (attr.prefailure && attr.threshold > 0 && attr.current <= attr.threshold) {
					score -= 12;
				}
				if (attr.id == 231 && attr.raw >= 90) {
					score -= 15;
				}
				if (attr.id == 231 && attr.raw >= 100) {
					score -= 25;
				}
				if (attr.id == 1 && attr.current > 0) {
					score -= (std::min)(static_cast<int>(attr.current), 10);
				}
			}
			if (drive.status_note[0] != '\0' && strstr(drive.status_note, u8"關鍵警告") != nullptr) {
				score -= 30;
			}
			if (drive.health == DiskHealthScan::HealthLevel::Bad) {
				score = (std::min)(score, 25);
			}
			else if (drive.health == DiskHealthScan::HealthLevel::Caution) {
				score = (std::min)(score, 65);
			}
			return (std::max)(5, (std::min)(100, score));
		}

		static float DriveHealthScore01(const DiskHealthScan::DriveInfo& drive)
		{
			return static_cast<float>(ComputeHealthScorePercent(drive)) / 100.f;
		}

		static HealthCounts CountHealth(const DiskHealthScan::Snapshot& snap)
		{
			HealthCounts c = {};
			for (const auto& d : snap.drives) {
				switch (d.health) {
				case DiskHealthScan::HealthLevel::Good: ++c.good; break;
				case DiskHealthScan::HealthLevel::Caution: ++c.caution; break;
				case DiskHealthScan::HealthLevel::Bad: ++c.bad; break;
				default: ++c.other; break;
				}
			}
			return c;
		}

		static void FormatSize(uint64_t bytes, char* buf, size_t buf_size)
		{
			FormatCleanSize(static_cast<int64_t>(bytes), buf, buf_size);
		}

		static void ApplyImPlotThemeOnce()
		{
			if (g_implot_theme) {
				return;
			}
			g_implot_theme = true;
			ImPlotStyle& style = ImPlot::GetStyle();
			style.PlotPadding = ImVec2(8.f, 8.f);
			style.PlotBorderSize = 1.f;
			ImVec4* colors = style.Colors;
			colors[ImPlotCol_FrameBg] = ImVec4(0.03f, 0.06f, 0.06f, 1.f);
			colors[ImPlotCol_PlotBg] = ImVec4(0.02f, 0.05f, 0.05f, 1.f);
			colors[ImPlotCol_PlotBorder] = ImVec4(0.f, 0.35f, 0.35f, 0.5f);
			colors[ImPlotCol_AxisGrid] = ImVec4(0.f, 0.35f, 0.35f, 0.15f);
			colors[ImPlotCol_AxisText] = Theme::text_muted;
			colors[ImPlotCol_InlayText] = Theme::cyan_neon;
		}

		static ImU32 Vec4ToU32(const ImVec4& c)
		{
			return ImGui::GetColorU32(c);
		}

		static void DrawCardBackground(const ImVec2& pos, const ImVec2& size, float rounding = 8.f)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p1(pos.x + size.x, pos.y + size.y);
			dl->AddRectFilled(pos, p1, Vec4ToU32(Theme::card_bg), rounding);
			dl->AddRect(pos, p1, Vec4ToU32(Theme::card_border), rounding, 0, 1.2f);
		}

		static bool DiskCyberButton(const char* id, const ImVec2& size, const char* label,
			bool enabled = true)
		{
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window->SkipItems) {
				return false;
			}
			const ImVec2 pos = window->DC.CursorPos;
			const ImRect bb(pos, pos + size);
			const ImGuiID btn_id = window->GetID(id);
			ImGui::ItemSize(size);
			if (!ImGui::ItemAdd(bb, btn_id)) {
				return false;
			}

			bool hovered = false;
			bool held = false;
			bool pressed = false;
			if (enabled) {
				pressed = ImGui::ButtonBehavior(bb, btn_id, &hovered, &held);
			}

			const ImU32 border = ImGui::GetColorU32((enabled && (hovered || held))
				? Theme::cyan_neon : Theme::card_border);
			const ImU32 bg = ImGui::GetColorU32((enabled && held) ? ImVec4(0.f, 0.25f, 0.25f, 1.f)
				: (enabled && hovered) ? ImVec4(0.06f, 0.14f, 0.14f, 1.f)
				: Theme::card_bg);
			window->DrawList->AddRectFilled(bb.Min, bb.Max, bg, 6.f);
			window->DrawList->AddRect(bb.Min, bb.Max, border, 6.f, 0, 1.2f);

			const ImVec2 ts = ImGui::CalcTextSize(label);
			const ImVec2 tp(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
				bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f);
			const ImU32 text_col = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
			window->DrawList->AddText(tp, text_col, label);
			return pressed && enabled;
		}

		static bool DiskSegmentedControl(const char* id, const char* const* labels,
			const int* values, int count, int* selected_value)
		{
			if (labels == nullptr || values == nullptr || count <= 0 || selected_value == nullptr) {
				return false;
			}
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			bool changed = false;
			const float seg_h = 28.f;
			float total_w = 0.f;
			for (int i = 0; i < count; ++i) {
				total_w += ImGui::CalcTextSize(labels[i]).x + 20.f;
			}
			const ImVec2 row_pos = window->DC.CursorPos;
			float x = row_pos.x;
			for (int i = 0; i < count; ++i) {
				const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
				const float seg_w = ts.x + 20.f;
				const ImRect bb(ImVec2(x, row_pos.y), ImVec2(x + seg_w, row_pos.y + seg_h));
				ImGui::PushID(id);
				ImGui::PushID(i);
				const ImGuiID seg_id = window->GetID("##seg");
				ImGui::ItemAdd(bb, seg_id);
				bool hovered = false;
				bool held = false;
				const bool pressed = ImGui::ButtonBehavior(bb, seg_id, &hovered, &held);
				const bool active = (*selected_value == values[i]);
				const ImU32 bg = ImGui::GetColorU32(active ? ImVec4(0.f, 0.28f, 0.28f, 1.f)
					: (hovered ? ImVec4(0.07f, 0.12f, 0.12f, 1.f) : Theme::card_bg));
				window->DrawList->AddRectFilled(bb.Min, bb.Max, bg, 4.f);
				window->DrawList->AddRect(bb.Min, bb.Max,
					ImGui::GetColorU32(active || hovered ? Theme::cyan_neon : Theme::card_border),
					4.f, 0, 1.f);
				const ImU32 seg_text = active
					? Vec4ToU32(Theme::cyan_neon)
					: ImGui::GetColorU32(ImGuiCol_Text);
				window->DrawList->AddText(
					ImVec2(bb.Min.x + (seg_w - ts.x) * 0.5f, bb.Min.y + (seg_h - ts.y) * 0.5f),
					seg_text, labels[i]);
				if (pressed) {
					*selected_value = values[i];
					changed = true;
				}
				ImGui::PopID();
				ImGui::PopID();
				x += seg_w + 4.f;
			}
			ImGui::Dummy(ImVec2(total_w + (count - 1) * 4.f, seg_h));
			return changed;
		}

		static bool DiskToggleChip(const char* id, const char* label, bool* value)
		{
			if (value == nullptr) {
				return false;
			}
			const ImVec2 ts = ImGui::CalcTextSize(label);
			const ImVec2 size(ts.x + 24.f, 26.f);
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImVec2 pos = window->DC.CursorPos;
			const ImRect bb(pos, pos + size);
			const ImGuiID chip_id = window->GetID(id);
			ImGui::ItemSize(size);
			if (!ImGui::ItemAdd(bb, chip_id)) {
				return false;
			}
			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, chip_id, &hovered, &held);
			if (pressed) {
				*value = !*value;
			}
			const bool on = *value;
			const ImU32 bg = ImGui::GetColorU32(on ? ImVec4(0.f, 0.3f, 0.3f, 1.f)
				: (hovered ? ImVec4(0.07f, 0.12f, 0.12f, 1.f) : Theme::card_bg));
			window->DrawList->AddRectFilled(bb.Min, bb.Max, bg, 5.f);
			window->DrawList->AddRect(bb.Min, bb.Max,
				ImGui::GetColorU32(on || hovered ? Theme::cyan_neon : Theme::card_border), 5.f, 0, 1.f);
			const ImU32 chip_text = on ? Vec4ToU32(Theme::cyan_neon) : ImGui::GetColorU32(ImGuiCol_Text);
			window->DrawList->AddText(
				ImVec2(bb.Min.x + (size.x - ts.x) * 0.5f, bb.Min.y + (size.y - ts.y) * 0.5f),
				chip_text, label);
			return pressed;
		}

		static void DrawSectionTitle(const char* title)
		{
			ImGui::Spacing();
			ImGui::TextColored(Theme::cyan_neon, "%s", title);
			ImGui::Separator();
			ImGui::Spacing();
		}

		static void DrawStatCard(const char* title, const char* value, const ImVec4& accent,
			const ImVec2& size)
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawCardBackground(pos, size);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float pad = 10.f;
			dl->AddRectFilled(
				ImVec2(pos.x, pos.y),
				ImVec2(pos.x + 4.f, pos.y + size.y),
				Vec4ToU32(accent), 4.f);
			const ImU32 muted = ImGui::GetColorU32(Theme::text_muted);
			dl->AddText(ImVec2(pos.x + pad + 4.f, pos.y + 8.f), muted, title);
			dl->AddText(ImVec2(pos.x + pad + 4.f, pos.y + 26.f), Vec4ToU32(accent), value);
			ImGui::Dummy(size);
		}

		struct EffectiveTestData {
			DiskHealthTest::SpeedTestResult speed = {};
			DiskHealthTest::BadSectorResult bad = {};
			bool has_speed = false;
			bool has_bad = false;
			bool speed_live = false;
			bool bad_live = false;
			bool speed_saved = false;
			bool bad_saved = false;
			int64_t speed_saved_ms = 0;
			int64_t bad_saved_ms = 0;
		};

		static EffectiveTestData GetEffectiveTestData(const DiskHealthScan::DriveInfo& drive)
		{
			EffectiveTestData e = {};
			const DiskHealthTest::JobState job = DiskHealthTest::GetState();
			const DiskHealthTest::DriveTestHistory hist =
				DiskHealthTest::GetDriveHistory(drive.physical_index);
			const bool match = (job.physical_index == drive.physical_index);

			if (job.running && match && job.kind == DiskHealthTest::JobKind::SpeedTest) {
				e.speed = job.speed;
				e.has_speed = true;
				e.speed_live = true;
			}
			else if (match && job.kind == DiskHealthTest::JobKind::SpeedTest && job.speed.ok) {
				e.speed = job.speed;
				e.has_speed = true;
			}
			else if (hist.has_speed) {
				e.speed = hist.speed;
				e.has_speed = true;
				e.speed_saved = true;
				e.speed_saved_ms = hist.speed_finished_unix_ms;
			}

			if (job.running && match && job.kind == DiskHealthTest::JobKind::BadSectorScan) {
				e.bad = job.bad_sector;
				e.has_bad = job.bad_sector.matrix_rows > 0 || job.bad_sector.bytes_planned > 0;
				e.bad_live = true;
			}
			else if (match && job.kind == DiskHealthTest::JobKind::BadSectorScan
				&& (job.bad_sector.ok || job.bad_sector.bytes_planned > 0
					|| !job.bad_sector.matrix.empty())) {
				e.bad = job.bad_sector;
				e.has_bad = true;
			}
			else if (hist.has_bad_sector) {
				e.bad = hist.bad_sector;
				e.has_bad = true;
				e.bad_saved = true;
				e.bad_saved_ms = hist.bad_finished_unix_ms;
			}
			return e;
		}

		static void DrawToolsHelpText()
		{
			ImGui::TextWrapped("%s",
				I18N_JOIN(
					u8"速度測試：在磁碟區建立暫存檔，依樣本大小讀寫並計算 MB/s。",
					u8"「僅讀取」不寫入資料，讀取階段會關閉系統快取（FILE_FLAG_NO_BUFFERING），",
					u8"較能反映真實順序讀速；樣本愈大（建議 1～8 GB）愈不易被 RAM 快取影響。",
					u8"「讀+寫」會先寫入再讀取，需足夠可用空間。"));
			ImGui::Spacing();
			ImGui::TextWrapped("%s",
				I18N_JOIN(
					u8"壞軌檢測：以矩陣抽樣讀取/驗證實體碟區塊。「快速」掃描碟首+碟尾；",
					u8"「完整」覆蓋更多區域（耗時較長）。紅格表示該抽樣點讀取失敗，",
					u8"建議備份資料並以廠商工具複檢。結果會自動儲存，下次開啟本頁仍可查看上次報告。"));
			ImGui::Spacing();
		}

		static void DrawVisualizationRow(const DiskHealthScan::DriveInfo& drive,
			const HealthCounts& hc, const EffectiveTestData& eff);
		static void DrawBadSectorMatrix(const DiskHealthTest::BadSectorResult& result, bool running);
		static void DrawBadSectorProgress(const DiskHealthTest::BadSectorResult& r,
			const ImVec2& plot_size);
		static void DrawDiagnosticLiveReport(const DiskHealthScan::DriveInfo& drive,
			const DiskHealthTest::JobState& job, const EffectiveTestData& eff);
		static void DrawToolsPanel(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff);
		static void DrawSmartAttributesPanel(const DiskHealthScan::DriveInfo& drive, float height);
		static void DrawSpeedCurvePlot(const DiskHealthTest::SpeedTestResult& speed,
			const ImVec2& plot_sz, bool mini);
		static void DrawReportPanel(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff);

		static void DrawHealthPie(const HealthCounts& hc, const ImVec2& plot_size);
		static void DrawSpeedBars(double read_mbps, double write_mbps, bool has_write,
			const ImVec2& plot_size);
		static void DrawSectionTitle(const char* title);
		static void DrawAdminBanner();
		static void DrawDiagnosticsTab(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff);
		static void DrawReportPanel(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff);

		static bool IsUsbWithoutSmart(const DiskHealthScan::DriveInfo& drive)
		{
			if (drive.smart_available) {
				return false;
			}
			return strcmp(drive.bus_type_utf8, "USB") == 0;
		}

		static void DrawUsbLimitedHero(const DiskHealthScan::DriveInfo& drive)
		{
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float hero_h = 128.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawCardBackground(pos, ImVec2(full_w, hero_h), 10.f);
			ImGui::Dummy(ImVec2(full_w, hero_h));

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(pos, ImVec2(pos.x + 5.f, pos.y + hero_h),
				ImGui::GetColorU32(ImVec4(1.f, 0.75f, 0.25f, 0.85f)), 10.f,
				ImDrawFlags_RoundCornersLeft);

			char cap[48] = {};
			if (drive.size_bytes > 0) {
				FormatSize(drive.size_bytes, cap, sizeof(cap));
			}
			else {
				strncpy_s(cap, I18N(u8"容量未知"), _TRUNCATE);
			}

			dl->AddText(ImVec2(pos.x + 16.f, pos.y + 12.f),
				ImGui::GetColorU32(Theme::cyan_neon),
				I18N(u8"USB 儲存裝置"));
			char line1[160] = {};
			snprintf(line1, sizeof(line1), "PhysicalDrive%d  ·  [%s]",
				drive.physical_index,
				drive.volume_letters[0] ? drive.volume_letters : "—");
			dl->AddText(ImVec2(pos.x + 16.f, pos.y + 32.f),
				ImGui::GetColorU32(Theme::text_muted), line1);

			const char* model = drive.model_utf8[0] ? drive.model_utf8 : I18N(u8"未知型號");
			dl->AddText(ImVec2(pos.x + 16.f, pos.y + 50.f), Vec4ToU32(Theme::cyan_neon), model);

			char line3[96] = {};
			const char* bus_disp = drive.bus_type_utf8[0] ? BusDisplayText(drive.bus_type_utf8) : "USB";
			SnprintfI18n(line3, sizeof(line3), u8"容量 %s  |  %s", cap, bus_disp);
			dl->AddText(ImVec2(pos.x + 16.f, pos.y + 72.f),
				ImGui::GetColorU32(Theme::text_muted), line3);

			const ImVec2 pill_pos(pos.x + full_w - 118.f, pos.y + 14.f);
			dl->AddRectFilled(pill_pos, ImVec2(pill_pos.x + 102.f, pill_pos.y + 22.f),
				ImGui::GetColorU32(ImVec4(1.f, 0.75f, 0.25f, 0.2f)), 6.f);
			dl->AddText(ImVec2(pill_pos.x + 10.f, pill_pos.y + 4.f),
				ImGui::GetColorU32(ImVec4(1.f, 0.78f, 0.35f, 1.f)), I18N(u8"無 SMART 資料"));
		}

		static void DrawUsbLimitedInfoBox(const DiskHealthScan::DriveInfo& drive)
		{
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			float box_h = 72.f;
			if (drive.status_note[0] != '\0') {
				const ImVec2 ts = ImGui::CalcTextSize(drive.status_note, nullptr, false, full_w - 24.f);
				box_h = (std::max)(box_h, ts.y + 52.f);
			}
			DrawCardBackground(pos, ImVec2(full_w, box_h), 8.f);
			ImGui::Dummy(ImVec2(full_w, box_h));

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 10.f),
				ImGui::GetColorU32(ImVec4(1.f, 0.78f, 0.35f, 1.f)), I18N(u8"為何沒有健康屬性？"));
			ImGui::SetCursorScreenPos(ImVec2(pos.x + 12.f, pos.y + 30.f));
			ImGui::PushTextWrapPos(pos.x + full_w - 12.f);
			if (drive.status_note[0] != '\0') {
				ImGui::TextWrapped("%s", drive.status_note);
			}
			else {
				ImGui::TextWrapped("%s",
					I18N_JOIN(
						u8"此 USB 裝置未向系統回報 SMART。常見於隨身碟、簡易讀卡機或部分轉接晶片。",
						u8"您仍可使用本頁的速度測試與壞軌抽樣評估讀寫是否正常。"));
			}
			ImGui::PopTextWrapPos();
		}

		static void DrawUsbLimitedFacts(const DiskHealthScan::DriveInfo& drive)
		{
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float card_w = (full_w - 12.f) * 0.5f;
			const ImVec2 card_sz(card_w, 64.f);

			char cap_v[48] = {};
			if (drive.size_bytes > 0) {
				FormatSize(drive.size_bytes, cap_v, sizeof(cap_v));
			}
			else {
				strncpy_s(cap_v, I18N(u8"未知"), _TRUNCATE);
			}

			char vol_v[32] = {};
			snprintf(vol_v, sizeof(vol_v), "[%s]",
				drive.volume_letters[0] ? drive.volume_letters : "—");

			DrawStatCard(I18N(u8"容量"), cap_v, Theme::cyan_neon, card_sz);
			ImGui::SameLine(0.f, 8.f);
			DrawStatCard(I18N(u8"磁碟區"), vol_v, Theme::cyan_dim, card_sz);
			ImGui::Spacing();

			char bus_v[24] = {};
			strncpy_s(bus_v, drive.bus_type_utf8[0] ? drive.bus_type_utf8 : "USB", _TRUNCATE);
			DrawStatCard(I18N(u8"介面"), bus_v, Theme::cyan_dim, card_sz);
			ImGui::SameLine(0.f, 8.f);
			DrawStatCard(I18N(u8"資料監測"), I18N(u8"未提供 SMART"), ImVec4(1.f, 0.78f, 0.35f, 1.f), card_sz);
			ImGui::Spacing();
		}

		static void DrawUsbLimitedCharts(const DiskHealthScan::DriveInfo& drive,
			const HealthCounts& hc, const EffectiveTestData& eff)
		{
			const float plot_h = 128.f;
			const ImVec2 plot_sz(-1.f, plot_h);

			if (ImGui::BeginTable("##usb_viz", 2,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX,
				ImVec2(0.f, plot_h + 56.f))) {
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", I18N(u8"本電腦硬碟概況"));
				DrawHealthPie(hc, plot_sz);
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", I18N(u8"本碟測速／壞軌"));
				const bool has_curve = eff.has_speed
					&& (!eff.speed.read_curve_mbps.empty()
						|| !eff.speed.write_curve_mbps.empty());
				if (eff.speed_live) {
					ImGui::TextColored(Theme::cyan_neon, "%s", I18N(u8"測速進行中…"));
				}
				if (has_curve) {
					DrawSpeedCurvePlot(eff.speed, plot_sz, true);
				}
				else if (eff.has_speed && eff.speed.ok) {
					DrawSpeedBars(eff.speed.read_mbps, eff.speed.write_mbps,
						eff.speed.write_tested, plot_sz);
					ImGui::TextColored(Theme::cyan_neon,
						I18NF(u8"讀 %.0f  寫 %.0f MB/s"),
						eff.speed.read_mbps,
						eff.speed.write_tested ? eff.speed.write_mbps : 0.0);
				}
				else if (eff.has_bad && eff.bad.ok) {
					if (!eff.bad.matrix.empty() && eff.bad.matrix_rows > 0) {
						DrawBadSectorMatrix(eff.bad, false);
					}
					else {
						DrawBadSectorProgress(eff.bad, plot_sz);
					}
					const ImVec4 col = (eff.bad.error_count > 0)
						? ImVec4(1.f, 0.55f, 0.35f, 1.f)
						: Theme::cyan_dim;
					ImGui::TextColored(col, I18NF(u8"壞軌抽樣：問題格 %u"), eff.bad.error_count);
				}
				else {
					ImGui::TextWrapped("%s",
						I18N_JOIN(
							u8"尚無測速或壞軌紀錄。請在下方「診斷工具」執行檢測，",
							u8"結果會顯示於此並寫入報告。"));
				}
				ImGui::EndTable();
			}
			(void)drive;
		}

		static void DrawMainDetailUsbLimited(const DiskHealthScan::Snapshot& snap,
			const DiskHealthScan::DriveInfo& drive)
		{
			const DiskHealthTest::JobState job = DiskHealthTest::GetState();
			const HealthCounts hc = CountHealth(snap);
			const EffectiveTestData eff = GetEffectiveTestData(drive);

			DrawAdminBanner();
			DrawUsbLimitedHero(drive);
			ImGui::Spacing();
			DrawUsbLimitedInfoBox(drive);
			ImGui::Spacing();
			DrawUsbLimitedFacts(drive);

			DrawSectionTitle(I18N(u8"概況與檢測結果"));
			DrawUsbLimitedCharts(drive, hc, eff);

			DrawSectionTitle(I18N(u8"診斷工具（速度／壞軌）"));
			DrawDiagnosticsTab(drive, eff);

			DrawSectionTitle(I18N(u8"文字報告"));
			DrawReportPanel(drive, eff);
		}

		static void DrawArcGaugeDl(ImDrawList* dl, const ImVec2& center, float fraction,
			const ImVec4& color, float radius, const char* center_label, const char* sub_label)
		{
			const float t = ImGui::GetTime();
			const float pulse = 0.85f + 0.15f * sinf(t * 2.f);
			const ImU32 track = ImGui::GetColorU32(ImVec4(0.1f, 0.16f, 0.16f, 1.f));
			const ImU32 fg = Vec4ToU32(ImVec4(color.x * pulse, color.y * pulse, color.z * pulse, 1.f));

			const float a0 = -IM_PI * 0.75f;
			const float a1 = IM_PI * 0.75f;
			dl->PathArcTo(center, radius, a0, a1, 48);
			dl->PathStroke(track, 0, 10.f);

			const float filled = a0 + (a1 - a0) * ImClamp(fraction, 0.f, 1.f);
			if (fraction > 0.001f) {
				dl->PathArcTo(center, radius, a0, filled, 48);
				dl->PathStroke(fg, 0, 10.f);
			}

			if (center_label != nullptr && center_label[0] != '\0') {
				const ImVec2 ts = ImGui::CalcTextSize(center_label);
				dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f - 6.f),
					Vec4ToU32(color), center_label);
			}
			if (sub_label != nullptr && sub_label[0] != '\0') {
				const ImVec2 ts2 = ImGui::CalcTextSize(sub_label);
				dl->AddText(ImVec2(center.x - ts2.x * 0.5f, center.y + 8.f),
					ImGui::GetColorU32(Theme::text_muted), sub_label);
			}
		}

		static void DrawTempBarDl(ImDrawList* dl, const ImVec2& pos, int temp_c, float width,
			float height)
		{
			const ImVec2 p1(pos.x + width, pos.y + height);
			dl->AddRectFilled(pos, p1, ImGui::GetColorU32(ImVec4(0.08f, 0.12f, 0.12f, 1.f)), 4.f);

			float frac = 0.f;
			ImVec4 col = Theme::text_muted;
			if (temp_c >= 0) {
				frac = ImClamp(static_cast<float>(temp_c) / 85.f, 0.f, 1.f);
				if (temp_c < 45) {
					col = ImVec4(0.25f, 0.95f, 0.45f, 1.f);
				}
				else if (temp_c < 58) {
					col = ImVec4(1.f, 0.78f, 0.2f, 1.f);
				}
				else {
					col = ImVec4(1.f, 0.35f, 0.3f, 1.f);
				}
			}
			if (frac > 0.f) {
				const ImVec2 fill_end(pos.x + width * frac, p1.y);
				dl->AddRectFilled(pos, fill_end, Vec4ToU32(col), 4.f);
			}
			char buf[32] = {};
			if (temp_c >= 0) {
				snprintf(buf, sizeof(buf), "%d C", temp_c);
			}
			else {
				strncpy_s(buf, "N/A", _TRUNCATE);
			}
			const ImVec2 ts = ImGui::CalcTextSize(buf);
			dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f),
				Vec4ToU32(col), buf);
		}

		static void DrawHealthStackedBar(const HealthCounts& c, float width, float height)
		{
			const int total = c.good + c.caution + c.bad + c.other;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(width, height + 18.f));
			if (total <= 0) {
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 p1(pos.x + width, pos.y + height);
				dl->AddRectFilled(pos, p1, ImGui::GetColorU32(ImVec4(0.08f, 0.12f, 0.12f, 1.f)), 4.f);
				return;
			}

			ImDrawList* dl = ImGui::GetWindowDrawList();
			float x = pos.x;
			const float bar_h = height;
			auto segment = [&](int count, const ImVec4& col) {
				if (count <= 0) {
					return;
				}
				const float w = width * (static_cast<float>(count) / static_cast<float>(total));
				const ImVec2 p0(x, pos.y);
				const ImVec2 p1(x + w, pos.y + bar_h);
				dl->AddRectFilled(p0, p1, Vec4ToU32(col), 3.f);
				x += w;
			};
			segment(c.good, HealthColor(DiskHealthScan::HealthLevel::Good));
			segment(c.caution, HealthColor(DiskHealthScan::HealthLevel::Caution));
			segment(c.bad, HealthColor(DiskHealthScan::HealthLevel::Bad));
			segment(c.other, HealthColor(DiskHealthScan::HealthLevel::Unknown));

			const float legend_y = pos.y + bar_h + 4.f;
			auto legend = [&](const char* name, int n, const ImVec4& col) {
				if (n <= 0) {
					return;
				}
				dl->AddRectFilled(ImVec2(x, legend_y + 3.f), ImVec2(x + 8.f, legend_y + 11.f),
					Vec4ToU32(col), 2.f);
				x += 12.f;
				char txt[48] = {};
				snprintf(txt, sizeof(txt), "%s %d", name, n);
				dl->AddText(ImVec2(x, legend_y), ImGui::GetColorU32(Theme::text_muted), txt);
				x += ImGui::CalcTextSize(txt).x + 12.f;
			};
			x = pos.x;
			legend(I18N(u8"良好"), c.good, HealthColor(DiskHealthScan::HealthLevel::Good));
			legend(I18N(u8"注意"), c.caution, HealthColor(DiskHealthScan::HealthLevel::Caution));
			legend(I18N(u8"不良"), c.bad, HealthColor(DiskHealthScan::HealthLevel::Bad));
			legend(I18N(u8"其他"), c.other, HealthColor(DiskHealthScan::HealthLevel::Unknown));
		}

		static void DrawTopDashboard(const DiskHealthScan::Snapshot& snap)
		{
			using namespace Theme;
			const HealthCounts hc = CountHealth(snap);
			const int total = static_cast<int>(snap.drives.size());

			char total_buf[16] = {};
			char good_buf[16] = {};
			char caution_buf[16] = {};
			char bad_buf[16] = {};
			snprintf(total_buf, sizeof(total_buf), "%d", total);
			snprintf(good_buf, sizeof(good_buf), "%d", hc.good);
			snprintf(caution_buf, sizeof(caution_buf), "%d", hc.caution);
			snprintf(bad_buf, sizeof(bad_buf), "%d", hc.bad);

			const float card_w = 118.f;
			const float card_h = 56.f;
			const float gap = 8.f;

			if (ImGui::Button(I18N(u8"重新掃描"), ImVec2(96, 32))) {
				DiskHealthScan::RequestRescan();
			}
			ImGui::SameLine(0.f, 12.f);

			const float bar_w = ImGui::GetContentRegionAvail().x - (card_w + gap) * 4.f - 16.f;
			if (bar_w > 120.f) {
				ImGui::BeginGroup();
				ImGui::TextDisabled("%s", I18N(u8"健康分佈"));
				DrawHealthStackedBar(hc, bar_w, 14.f);
				ImGui::EndGroup();
				ImGui::SameLine(0.f, 12.f);
			}

			DrawStatCard(I18N(u8"硬碟總數"), total_buf, cyan_neon, ImVec2(card_w, card_h));
			ImGui::SameLine(0.f, gap);
			DrawStatCard(I18N(u8"良好"), good_buf, HealthColor(DiskHealthScan::HealthLevel::Good),
				ImVec2(card_w, card_h));
			ImGui::SameLine(0.f, gap);
			DrawStatCard(I18N(u8"注意"), caution_buf, HealthColor(DiskHealthScan::HealthLevel::Caution),
				ImVec2(card_w, card_h));
			ImGui::SameLine(0.f, gap);
			DrawStatCard(I18N(u8"不良"), bad_buf, HealthColor(DiskHealthScan::HealthLevel::Bad),
				ImVec2(card_w, card_h));

			if (snap.scanning) {
				ImGui::ProgressBar(snap.progress, ImVec2(-1, 18),
					StatusDisplayText(snap.status_text));
			}
			else {
				ImGui::TextDisabled("%s%s%s", StatusDisplayText(snap.status_text),
					snap.last_scan_time[0] != '\0' ? I18N(u8"  |  完整掃描：") : "",
					snap.last_scan_time[0] != '\0' ? snap.last_scan_time : "");
				if (DiskHealthScan::IsLiveRefreshEnabled()) {
					const char* live_t = snap.last_live_update_time[0]
						? snap.last_live_update_time
						: I18N(u8"尚未");
					char live_line[320] = {};
					SnprintfI18n(live_line, sizeof(live_line),
						u8"即時 SMART：每 %d 秒更新%s%s%s",
						DiskHealthScan::GetLiveRefreshIntervalSec(),
						snap.live_refreshing ? I18N(u8"（更新中）") : "",
						I18N(u8"  |  上次："),
						live_t);
					ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.85f, 1.f), "%s", live_line);
				}
			}
			if (snap.needs_admin_hint) {
				ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f), "%s", I18N(u8"部分 SMART 資料建議以系統管理員執行"));
			}
		}

		static void DrawDriveCard(const DiskHealthScan::DriveInfo& d, int index, bool selected,
			float card_w)
		{
			const ImVec4 hcol = HealthColor(d.health);
			const float card_h = 86.f;
			const float gap = 6.f;

			ImGui::PushID(index);
			const ImVec2 pos = ImGui::GetCursorScreenPos();

			if (ImGui::InvisibleButton("##card", ImVec2(card_w, card_h))) {
				g_selected_drive = index;
			}

			ImDrawList* dl = ImGui::GetWindowDrawList();
			DrawCardBackground(pos, ImVec2(card_w, card_h), 6.f);
			if (selected) {
				dl->AddRect(pos, ImVec2(pos.x + card_w, pos.y + card_h),
					Vec4ToU32(Theme::cyan_neon), 6.f, 0, 2.f);
			}
			dl->AddRectFilled(pos, ImVec2(pos.x + 5.f, pos.y + card_h), Vec4ToU32(hcol), 4.f);

			char line0[96] = {};
			snprintf(line0, sizeof(line0), "PD%d  %s", d.physical_index,
				DiskHealthScan::HealthLevelLabel(d.health));
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 8.f), Vec4ToU32(Theme::cyan_neon), line0);

			const int score_pct = ComputeHealthScorePercent(d);
			char score_txt[16] = {};
			snprintf(score_txt, sizeof(score_txt), "%d%%", score_pct);
			const ImVec2 score_ts = ImGui::CalcTextSize(score_txt);
			dl->AddText(ImVec2(pos.x + card_w - score_ts.x - 10.f, pos.y + 6.f),
				Vec4ToU32(hcol), score_txt);

			const char* model = d.model_utf8[0] ? d.model_utf8 : I18N(u8"未知型號");
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 24.f), IM_COL32_WHITE, model);

			char line2[96] = {};
			if (d.volume_letters[0] != '\0') {
				snprintf(line2, sizeof(line2), "[%s]  %s", d.volume_letters,
					BusDisplayText(d.bus_type_utf8[0] ? d.bus_type_utf8 : "—"));
			}
			else {
				snprintf(line2, sizeof(line2), "%s",
					BusDisplayText(d.bus_type_utf8[0] ? d.bus_type_utf8 : "—"));
			}
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 40.f), ImGui::GetColorU32(Theme::text_muted),
				line2);

			char line3[96] = {};
			char sz[32] = {};
			FormatSize(d.size_bytes, sz, sizeof(sz));
			snprintf(line3, sizeof(line3), "%s  |  SMART: %s", sz,
				d.smart_available ? "OK" : "N/A");
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 56.f), ImGui::GetColorU32(Theme::text_muted),
				line3);

			if (d.temperature_c >= 0) {
				DrawTempBarDl(dl, ImVec2(pos.x + 12.f, pos.y + card_h - 18.f),
					d.temperature_c, card_w - 24.f, 10.f);
			}

			ImGui::Dummy(ImVec2(0, gap));
			ImGui::PopID();
		}

		static void DrawDriveChipRow(const DiskHealthScan::Snapshot& snap)
		{
			ImGui::TextColored(Theme::cyan_neon, "%s", I18N(u8"硬碟一覽"));
			ImGui::SameLine();
			ImGui::TextDisabled(I18NF(u8"(%zu)  點選切換下方詳情"), snap.drives.size());
			ImGui::Spacing();

			if (snap.drives.empty() && !snap.scanning) {
				ImGui::TextDisabled("%s", I18N(u8"未偵測到實體硬碟"));
				return;
			}

			const float chip_h = 54.f;
			const float gap = 6.f;
			float row_y = ImGui::GetCursorPosY();
			float x = ImGui::GetCursorPosX();
			const float max_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

			for (size_t i = 0; i < snap.drives.size(); ++i) {
				const auto& d = snap.drives[i];
				const bool selected = g_selected_drive == static_cast<int>(i);
				char chip_title[48] = {};
				snprintf(chip_title, sizeof(chip_title), "PD%d", d.physical_index);
				char chip_sub[96] = {};
				const int score = ComputeHealthScorePercent(d);
				if (d.volume_letters[0] != '\0') {
					snprintf(chip_sub, sizeof(chip_sub), "[%s] %d%% %s",
						d.volume_letters, score,
						DiskHealthScan::HealthLevelLabel(d.health));
				}
				else {
					snprintf(chip_sub, sizeof(chip_sub), "%d%% %s", score,
						DiskHealthScan::HealthLevelLabel(d.health));
				}

				const ImVec2 t0 = ImGui::CalcTextSize(chip_title);
				const ImVec2 t1 = ImGui::CalcTextSize(chip_sub);
				const float chip_w = ImMax(t0.x, t1.x) + 24.f;
				if (x + chip_w > max_x && x > ImGui::GetCursorPosX()) {
					x = ImGui::GetCursorPosX();
					row_y += chip_h + gap;
				}

				ImGui::SetCursorPos(ImVec2(x, row_y));
				ImGui::PushID(static_cast<int>(i));
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##chip", ImVec2(chip_w, chip_h))) {
					g_selected_drive = static_cast<int>(i);
				}
				ImDrawList* dl = ImGui::GetWindowDrawList();
				DrawCardBackground(pos, ImVec2(chip_w, chip_h), 6.f);
				if (selected) {
					dl->AddRect(pos, ImVec2(pos.x + chip_w, pos.y + chip_h),
						Vec4ToU32(Theme::cyan_neon), 6.f, 0, 2.f);
				}
				dl->AddRectFilled(pos, ImVec2(pos.x + 4.f, pos.y + chip_h),
					Vec4ToU32(HealthColor(d.health)), 4.f);
				dl->AddText(ImVec2(pos.x + 10.f, pos.y + 8.f), Vec4ToU32(Theme::cyan_neon), chip_title);
				dl->AddText(ImVec2(pos.x + 10.f, pos.y + 26.f), IM_COL32(200, 210, 210, 255), chip_sub);
				ImGui::PopID();

				x += chip_w + gap;
			}
			ImGui::SetCursorPosY(row_y + chip_h + 8.f);
		}

		static void DrawHealthPie(const HealthCounts& hc, const ImVec2& plot_size)
		{
			ApplyImPlotThemeOnce();
			const double vals[4] = {
				static_cast<double>(hc.good),
				static_cast<double>(hc.caution),
				static_cast<double>(hc.bad),
				static_cast<double>(hc.other),
			};
			const char* labels[4] = { I18N(u8"良好"), I18N(u8"注意"), I18N(u8"不良"), I18N(u8"其他") };
			int count = 0;
			for (double v : vals) {
				if (v > 0.0) {
					++count;
				}
			}
			if (count == 0) {
				ImGui::TextDisabled("%s", I18N(u8"尚無資料"));
				return;
			}
			if (ImPlot::BeginPlot("##health_pie", plot_size,
				ImPlotFlags_NoInputs | ImPlotFlags_NoLegend | ImPlotFlags_Equal)) {
				ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations,
					ImPlotAxisFlags_NoDecorations);
				ImPlot::SetupAxesLimits(0.0, 1.0, 0.0, 1.0, ImPlotCond_Always);
				ImPlotSpec spec;
				spec.Flags = ImPlotPieChartFlags_Normalize;
				ImPlot::PlotPieChart(labels, vals, 4, 0.5, 0.5, 0.38, "%.0f", 90, spec);
				ImPlot::EndPlot();
			}
		}

		static void DrawSpeedBars(double read_mbps, double write_mbps, bool has_write,
			const ImVec2& plot_size)
		{
			ApplyImPlotThemeOnce();
			const char* labels[2] = { I18N(u8"讀取"), I18N(u8"寫入") };
			const double vals[2] = { read_mbps, has_write ? write_mbps : 0.0 };
			const int n = has_write ? 2 : 1;
			const double ymax = (std::max)((std::max)(read_mbps, write_mbps), 100.0) * 1.15;
			double xs[2] = { 0.0, 1.0 };

			if (ImPlot::BeginPlot("##speed_bars", plot_size,
				ImPlotFlags_NoInputs | ImPlotFlags_NoLegend)) {
				ImPlot::SetupAxes("MB/s", nullptr, ImPlotAxisFlags_AutoFit,
					ImPlotAxisFlags_NoDecorations);
				ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, ymax, ImPlotCond_Always);
				ImPlot::SetupAxisTicks(ImAxis_X1, xs, n, labels);
				ImPlot::PlotBars("##spd", xs, vals, n, 0.55);
				ImPlot::EndPlot();
			}
		}

		static void DrawSmartRiskBars(const DiskHealthScan::DriveInfo& drive, const ImVec2& plot_size)
		{
			ApplyImPlotThemeOnce();
			const char* labels[4] = { I18N(u8"重配置"), I18N(u8"待處理"), I18N(u8"不可修正"), I18N(u8"溫度") };
			double vals[4] = {};
			bool any = false;

			int realloc_c = -1;
			int pending_c = -1;
			int uncorr_c = -1;
			QuerySectorCounters(drive, realloc_c, pending_c, uncorr_c);
			if (realloc_c >= 0) {
				vals[0] = static_cast<double>(realloc_c);
				any = true;
			}
			if (pending_c >= 0) {
				vals[1] = static_cast<double>(pending_c);
				any = true;
			}
			if (uncorr_c >= 0) {
				vals[2] = static_cast<double>(uncorr_c);
				any = true;
			}
			if (drive.temperature_c >= 0) {
				vals[3] = static_cast<double>(drive.temperature_c);
				any = true;
			}
			if (!any) {
				ImGui::TextDisabled("%s", I18N(u8"無 SMART 指標"));
				return;
			}

			double ymax = 10.0;
			for (int i = 0; i < 4; ++i) {
				ymax = (std::max)(ymax, vals[i]);
			}
			ymax *= 1.2;

			double xs[4] = { 0.0, 1.0, 2.0, 3.0 };
			if (ImPlot::BeginPlot("##smart_risk", plot_size,
				ImPlotFlags_NoInputs | ImPlotFlags_NoLegend)) {
				ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit,
					ImPlotAxisFlags_NoDecorations);
				ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, ymax, ImPlotCond_Always);
				ImPlot::SetupAxisTicks(ImAxis_X1, xs, 4, labels);
				ImPlot::PlotBars("##risk", xs, vals, 4, 0.55);
				ImPlot::EndPlot();
			}
		}

		static void DrawBadSectorProgress(const DiskHealthTest::BadSectorResult& r,
			const ImVec2& plot_size)
		{
			const float frac = (r.bytes_planned > 0)
				? static_cast<float>(r.bytes_scanned) / static_cast<float>(r.bytes_planned)
				: 0.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(plot_size);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 center(pos.x + plot_size.x * 0.5f, pos.y + plot_size.y * 0.5f);
			const float radius = (std::min)(plot_size.x, plot_size.y) * 0.38f;
			dl->PathArcTo(center, radius, 0.f, IM_PI * 2.f, 64);
			dl->PathStroke(ImGui::GetColorU32(ImVec4(0.1f, 0.16f, 0.16f, 1.f)), 0, 8.f);
			if (frac > 0.f) {
				dl->PathArcTo(center, radius, -IM_PI * 0.5f,
					-IM_PI * 0.5f + IM_PI * 2.f * frac, 64);
				const ImVec4 col = (r.error_count == 0)
					? ImVec4(0.25f, 0.95f, 0.45f, 1.f)
					: ImVec4(1.f, 0.55f, 0.35f, 1.f);
				dl->PathStroke(Vec4ToU32(col), 0, 8.f);
			}
			char pct[32] = {};
			snprintf(pct, sizeof(pct), "%.0f%%", static_cast<double>(frac) * 100.0);
			const ImVec2 ts = ImGui::CalcTextSize(pct);
			dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f - 8.f),
				Vec4ToU32(Theme::cyan_neon), pct);
			char sub[64] = {};
			SnprintfI18n(sub, sizeof(sub), u8"問題區 %u", r.error_count);
			const ImVec2 ts2 = ImGui::CalcTextSize(sub);
			dl->AddText(ImVec2(center.x - ts2.x * 0.5f, center.y + 6.f),
				ImGui::GetColorU32(Theme::text_muted), sub);
		}

		static const char* BuildHealthVerdict(const DiskHealthScan::DriveInfo& drive,
			char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return "";
			}
			if (!drive.smart_available) {
				SnprintfI18n(out, out_size, u8"無完整 SMART，僅顯示容量與介面資訊");
				return out;
			}
			if (drive.health == DiskHealthScan::HealthLevel::Bad) {
				SnprintfI18n(out, out_size, u8"建議立即備份並規劃更換");
				return out;
			}
			if (drive.health == DiskHealthScan::HealthLevel::Caution) {
				SnprintfI18n(out, out_size, u8"存在風險指標，請留意並定期檢查");
				return out;
			}
			int realloc_c = -1;
			int pending_c = -1;
			int uncorr_c = -1;
			QuerySectorCounters(drive, realloc_c, pending_c, uncorr_c);
			if (realloc_c > 0 || pending_c > 0 || uncorr_c > 0) {
				SnprintfI18n(out, out_size, u8"整體良好，但 SMART 有少量異常計數");
				return out;
			}
			SnprintfI18n(out, out_size, u8"目前指標正常，可持續監控");
			return out;
		}

		static void DrawAtAGlancePanel(const DiskHealthScan::DriveInfo& drive)
		{
			using namespace Theme;
			const ImVec4 hcol = HealthColor(drive.health);
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float panel_h = 200.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawCardBackground(pos, ImVec2(full_w, panel_h), 10.f);
			ImGui::Dummy(ImVec2(full_w, panel_h));

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 gauge_center(pos.x + 78.f, pos.y + panel_h * 0.52f);
			const int score_pct = ComputeHealthScorePercent(drive);
			char gauge_main[16] = {};
			snprintf(gauge_main, sizeof(gauge_main), "%d%%", score_pct);
			char gauge_sub[32] = {};
			snprintf(gauge_sub, sizeof(gauge_sub), "%s",
				DiskHealthScan::HealthLevelLabel(drive.health));
			DrawArcGaugeDl(dl, gauge_center, DriveHealthScore01(drive), hcol, 48.f,
				gauge_main, gauge_sub);

			const float kpi_x = pos.x + 168.f;
			float kpi_y = pos.y + 14.f;
			const float kpi_w = (full_w - 176.f) * 0.5f - 6.f;
			const float kpi_h = 52.f;

			auto draw_kpi = [&](float x, float y, const char* title, const char* value,
				const ImVec4& accent) {
				const ImVec2 kpos(x, y);
				DrawCardBackground(kpos, ImVec2(kpi_w, kpi_h), 6.f);
				dl->AddText(ImVec2(x + 10.f, y + 8.f), ImGui::GetColorU32(text_muted), title);
				dl->AddText(ImVec2(x + 10.f, y + 24.f), Vec4ToU32(accent), value);
			};

			char temp_v[24] = {};
			if (drive.temperature_c >= 0) {
				snprintf(temp_v, sizeof(temp_v), "%d °C", drive.temperature_c);
			}
			else {
				strncpy_s(temp_v, "—", _TRUNCATE);
			}

			char hours_v[32] = {};
			if (drive.power_on_hours >= 0) {
				SnprintfI18n(hours_v, sizeof(hours_v), u8"%d 小時", drive.power_on_hours);
			}
			else {
				strncpy_s(hours_v, "—", _TRUNCATE);
			}

			char realloc_v[24] = {};
			char pending_v[24] = {};
			char uncorr_v[24] = {};
			int realloc_c = -1;
			int pending_c = -1;
			int uncorr_c = -1;
			QuerySectorCounters(drive, realloc_c, pending_c, uncorr_c);
			FormatSectorCounterText(realloc_c, realloc_v, sizeof(realloc_v));
			FormatSectorCounterText(pending_c, pending_v, sizeof(pending_v));
			FormatSectorCounterText(uncorr_c, uncorr_v, sizeof(uncorr_v));

			draw_kpi(kpi_x, kpi_y, I18N(u8"溫度"), temp_v,
				drive.temperature_c >= 58 ? ImVec4(1.f, 0.4f, 0.35f, 1.f)
				: (drive.temperature_c >= 45 ? ImVec4(1.f, 0.78f, 0.2f, 1.f)
					: ImVec4(0.25f, 0.95f, 0.45f, 1.f)));
			draw_kpi(kpi_x + kpi_w + 12.f, kpi_y, I18N(u8"通電時數"), hours_v, cyan_dim);
			kpi_y += kpi_h + 8.f;
			draw_kpi(kpi_x, kpi_y, I18N(u8"重配置扇區"), realloc_v,
				realloc_c > 0 ? ImVec4(1.f, 0.75f, 0.25f, 1.f) : cyan_dim);
			draw_kpi(kpi_x + kpi_w + 12.f, kpi_y, I18N(u8"待處理扇區"), pending_v,
				pending_c > 0 ? ImVec4(1.f, 0.4f, 0.35f, 1.f) : cyan_dim);
			kpi_y += kpi_h + 8.f;
			draw_kpi(kpi_x, kpi_y, I18N(u8"不可修正"), uncorr_v,
				uncorr_c > 0 ? ImVec4(1.f, 0.4f, 0.35f, 1.f) : cyan_dim);

			char verdict[128] = {};
			BuildHealthVerdict(drive, verdict, sizeof(verdict));
			dl->AddText(ImVec2(kpi_x, pos.y + panel_h - 28.f),
				ImGui::GetColorU32(text_muted), I18N(u8"結論"));
			dl->AddText(ImVec2(kpi_x + 36.f, pos.y + panel_h - 28.f), Vec4ToU32(hcol), verdict);
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + panel_h - 14.f),
				ImGui::GetColorU32(text_muted),
				I18N(u8"健康%：依 SMART 重配置/待處理/不可修正/溫度/SSD 壽命% 等加權估算"));

			ImGui::Dummy(ImVec2(0.f, 8.f));
		}

		static void DrawHeroPanel(const DiskHealthScan::DriveInfo& drive,
			const DiskHealthTest::JobState& job)
		{
			(void)job;
			using namespace Theme;
			const ImVec4 hcol = HealthColor(drive.health);
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);

			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawCardBackground(pos, ImVec2(full_w, 128.f), 10.f);

			if (ImGui::BeginTable("##hero_main", 2,
				ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX,
				ImVec2(full_w, 0.f))) {
				ImGui::TableNextColumn();
				ImGui::TextColored(cyan_neon, "PhysicalDrive%d  %s", drive.physical_index,
					DiskHealthScan::HealthLevelLabel(drive.health));
				ImGui::Text("%s", drive.model_utf8[0] ? drive.model_utf8 : "—");
				char size_buf[48] = {};
				FormatSize(drive.size_bytes, size_buf, sizeof(size_buf));
				ImGui::TextDisabled("%s  |  %s  |  %s", size_buf,
					BusDisplayText(drive.bus_type_utf8[0] ? drive.bus_type_utf8 : "—"),
					drive.volume_letters[0] ? drive.volume_letters : "—");

				ImGui::TableNextColumn();
				const int score = ComputeHealthScorePercent(drive);
				ImGui::TextColored(hcol, "%d%%", score);
				ImGui::TextDisabled("%s", I18N(u8"健康指數 (SMART 估算)"));
				ImGui::EndTable();
			}

			if (ImGui::BeginTable("##hero_metrics", 3,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg,
				ImVec2(full_w, 0.f))) {
				ImGui::TableSetupColumn(I18N(u8"溫度"));
				ImGui::TableSetupColumn(I18N(u8"通電時數"));
				ImGui::TableSetupColumn("SMART");
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				if (drive.temperature_c >= 0) {
					ImGui::TextColored(hcol, "%d °C", drive.temperature_c);
				}
				else {
					ImGui::TextDisabled("—");
				}

				ImGui::TableNextColumn();
				if (drive.power_on_hours >= 0) {
					ImGui::TextColored(cyan_dim, I18NF(u8"%d 小時"), drive.power_on_hours);
				}
				else {
					ImGui::TextDisabled("—");
				}

				ImGui::TableNextColumn();
				if (drive.smart_available) {
					ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.45f, 1.f), I18NF(u8"可讀 (%zu 項)"),
						drive.smart_attributes.size());
				}
				else {
					ImGui::TextDisabled("%s", I18N(u8"不可用"));
				}
				ImGui::EndTable();
			}

			ImGui::Dummy(ImVec2(0, 6.f));
		}

		static void DrawDriveQuickFacts(const DiskHealthScan::DriveInfo& drive)
		{
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float card_w = (full_w - 12.f) * 0.25f;
			const ImVec2 card_sz(card_w, 56.f);

			char cap_v[48] = {};
			if (drive.size_bytes > 0) {
				FormatSize(drive.size_bytes, cap_v, sizeof(cap_v));
			}
			else if (drive.volume_letters[0] != '\0') {
				SnprintfI18n(cap_v, sizeof(cap_v), u8"未知（槽位 %s）", drive.volume_letters);
			}
			else {
				strncpy_s(cap_v, I18N(u8"未知"), _TRUNCATE);
			}

			char temp_v[24] = {};
			if (drive.temperature_c >= 0) {
				snprintf(temp_v, sizeof(temp_v), "%d °C", drive.temperature_c);
			}
			else {
				strncpy_s(temp_v, "—", _TRUNCATE);
			}

			char poh_v[32] = {};
			if (drive.power_on_hours >= 0) {
				SnprintfI18n(poh_v, sizeof(poh_v), u8"%d 小時", drive.power_on_hours);
			}
			else {
				strncpy_s(poh_v, "—", _TRUNCATE);
			}

			char smart_v[32] = {};
			if (drive.smart_available) {
				SnprintfI18n(smart_v, sizeof(smart_v), u8"%zu 項", drive.smart_attributes.size());
			}
			else {
				strncpy_s(smart_v, I18N(u8"不可用"), _TRUNCATE);
			}

			const ImVec4 temp_col = drive.temperature_c >= 58 ? ImVec4(1.f, 0.4f, 0.35f, 1.f)
				: (drive.temperature_c >= 45 ? ImVec4(1.f, 0.78f, 0.2f, 1.f)
					: ImVec4(0.25f, 0.95f, 0.45f, 1.f));

			DrawStatCard(I18N(u8"容量"), cap_v, Theme::cyan_neon, card_sz);
			ImGui::SameLine(0.f, 4.f);
			DrawStatCard(I18N(u8"溫度"), temp_v, temp_col, card_sz);
			ImGui::SameLine(0.f, 4.f);
			DrawStatCard(I18N(u8"通電時數"), poh_v, Theme::cyan_dim, card_sz);
			ImGui::SameLine(0.f, 4.f);
			DrawStatCard("SMART", smart_v,
				drive.smart_available ? ImVec4(0.25f, 0.95f, 0.45f, 1.f) : Theme::text_muted,
				card_sz);
			ImGui::Dummy(ImVec2(0.f, 6.f));
		}

		static void DrawChartsTab(const DiskHealthScan::DriveInfo& drive,
			const DiskHealthTest::JobState& job, const HealthCounts& hc)
		{
			DrawDriveQuickFacts(drive);
			char verdict[128] = {};
			BuildHealthVerdict(drive, verdict, sizeof(verdict));
			ImGui::TextColored(HealthColor(drive.health), I18NF(u8"結論：%s"), verdict);
			ImGui::Spacing();
			DrawVisualizationRow(drive, hc, GetEffectiveTestData(drive));
		}

		static void DrawDiagnosticsTab(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff)
		{
			const DiskHealthTest::JobState job = DiskHealthTest::GetState();
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const ImVec2 card_sz((full_w - 8.f) * 0.5f, 72.f);

			char speed_v[80] = {};
			if (eff.speed_live) {
				strncpy_s(speed_v, I18N(u8"測速進行中…"), _TRUNCATE);
			}
			else if (eff.has_speed && eff.speed.ok) {
				if (eff.speed.write_tested) {
					SnprintfI18n(speed_v, sizeof(speed_v), u8"讀 %.0f  寫 %.0f MB/s%s",
						eff.speed.read_mbps, eff.speed.write_mbps,
						eff.speed_saved ? I18N(u8" ·已儲存") : "");
				}
				else {
					SnprintfI18n(speed_v, sizeof(speed_v), u8"僅讀 %.0f MB/s%s",
						eff.speed.read_mbps, eff.speed_saved ? I18N(u8" ·已儲存") : "");
				}
			}
			else {
				strncpy_s(speed_v, I18N(u8"尚未測速"), _TRUNCATE);
			}

			char bad_v[80] = {};
			if (eff.bad_live) {
				strncpy_s(bad_v, I18N(u8"掃描進行中…"), _TRUNCATE);
			}
			else if (eff.has_bad && eff.bad.ok) {
				SnprintfI18n(bad_v, sizeof(bad_v), u8"問題格 %u  %.0f%%%s",
					eff.bad.error_count,
					eff.bad.bytes_planned > 0
						? 100.0 * static_cast<double>(eff.bad.bytes_scanned)
							/ static_cast<double>(eff.bad.bytes_planned)
						: 0.0,
					eff.bad_saved ? I18N(u8" ·已儲存") : "");
			}
			else {
				strncpy_s(bad_v, I18N(u8"尚未掃描"), _TRUNCATE);
			}

			DrawStatCard(I18N(u8"速度測試"), speed_v, Theme::cyan_neon, card_sz);
			ImGui::SameLine(0.f, 8.f);
			DrawStatCard(I18N(u8"壞軌掃描"), bad_v,
				(eff.has_bad && eff.bad.error_count > 0)
					? ImVec4(1.f, 0.55f, 0.35f, 1.f)
					: Theme::cyan_dim,
				card_sz);
			ImGui::Spacing();
			DrawToolsHelpText();
			DrawDiagnosticLiveReport(drive, job, eff);
			ImGui::Spacing();
			ImGui::Separator();
			DrawToolsPanel(drive, eff);
		}

		static void DrawSmartTab(const DiskHealthScan::DriveInfo& drive)
		{
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float card_w = (full_w - 12.f) * 0.25f;
			const ImVec2 card_sz(card_w, 56.f);

			char r_v[16] = {};
			char p_v[16] = {};
			char u_v[16] = {};
			char t_v[16] = {};
			char poh_v[24] = {};
			int realloc_c = -1;
			int pending_c = -1;
			int uncorr_c = -1;
			QuerySectorCounters(drive, realloc_c, pending_c, uncorr_c);
			FormatSectorCounterText(realloc_c, r_v, sizeof(r_v));
			FormatSectorCounterText(pending_c, p_v, sizeof(p_v));
			FormatSectorCounterText(uncorr_c, u_v, sizeof(u_v));
			if (drive.temperature_c < 0) {
				strncpy_s(t_v, "—", _TRUNCATE);
			}
			else {
				snprintf(t_v, sizeof(t_v), "%d", drive.temperature_c);
			}
			if (drive.power_on_hours >= 0) {
				SnprintfI18n(poh_v, sizeof(poh_v), u8"%d 小時", drive.power_on_hours);
			}
			else {
				strncpy_s(poh_v, "—", _TRUNCATE);
			}

			DrawStatCard(I18N(u8"重配置扇區"), r_v,
				realloc_c > 0 ? ImVec4(1.f, 0.75f, 0.25f, 1.f) : Theme::cyan_dim,
				card_sz);
			ImGui::SameLine(0.f, 4.f);
			DrawStatCard(I18N(u8"待處理扇區"), p_v,
				pending_c > 0 ? ImVec4(1.f, 0.4f, 0.35f, 1.f) : Theme::cyan_dim,
				card_sz);
			ImGui::SameLine(0.f, 4.f);
			DrawStatCard(I18N(u8"不可修正"), u_v,
				uncorr_c > 0 ? ImVec4(1.f, 0.4f, 0.35f, 1.f) : Theme::cyan_dim,
				card_sz);
			ImGui::SameLine(0.f, 4.f);
			const ImVec4 temp_card_col = (drive.temperature_c >= 58)
				? ImVec4(1.f, 0.4f, 0.35f, 1.f)
				: Theme::cyan_neon;
			DrawStatCard(I18N(u8"溫度"), t_v, temp_card_col, card_sz);
			ImGui::Spacing();

			const float meter_w = (std::max)(200.f, full_w - 8.f);
			if (drive.temperature_c >= 0) {
				ImGui::TextColored(Theme::cyan_neon, I18NF(u8"目前溫度 %d °C"), drive.temperature_c);
				DrawTempBarDl(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
					drive.temperature_c, meter_w, 24.f);
				ImGui::Dummy(ImVec2(meter_w, 24.f));
			}
			else {
				ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f), "%s", I18N(u8"頂部溫度未解析：請看下方 ID 194/190 屬性（部分 USB 橋接把溫度放在「目前值」而非原始值）"));
			}
			ImGui::Spacing();

			const float half_w = (full_w - 8.f) * 0.5f;
			DrawStatCard(I18N(u8"通電時數"), poh_v, Theme::cyan_dim, ImVec2(half_w, 48.f));
			ImGui::SameLine(0.f, 8.f);
			char attr_v[32] = {};
			SnprintfI18n(attr_v, sizeof(attr_v), u8"%zu 項", drive.smart_attributes.size());
			DrawStatCard(I18N(u8"SMART 屬性"), attr_v,
				drive.smart_available ? ImVec4(0.25f, 0.95f, 0.45f, 1.f) : Theme::text_muted,
				ImVec2(half_w, 48.f));
			if (drive.status_note[0] != '\0') {
				ImGui::Spacing();
				ImGui::TextWrapped(I18NF(u8"備註：%s"), drive.status_note);
			}
			ImGui::Spacing();
			ImGui::TextWrapped("%s",
				I18N_JOIN(
					u8"預故障（Pre-failure）：廠商標記為「可能預示故障」的屬性。",
					u8"若目前值 ≤ 門檻（最差欄位）需留意；紅色=已低於門檻，黃色=預故障屬性。"));
			ImGui::TextDisabled("%s", I18N(u8"排序：關鍵扇區 → 溫度/壽命 → 其餘；NVMe 另含健康日誌欄位。"));

			const float avail_y = ImGui::GetContentRegionAvail().y;
			const float min_child_h = 340.f;
			const float prefer_h = ImGui::GetIO().DisplaySize.y * 0.36f;
			const float smart_h = (std::max)((std::max)(avail_y, min_child_h), prefer_h);
			DrawSmartAttributesPanel(drive, smart_h);
		}

		static void DrawReportTab(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff)
		{
			DrawDriveQuickFacts(drive);
			if (drive.status_note[0] != '\0') {
				ImGui::TextWrapped(I18NF(u8"狀態備註：%s"), drive.status_note);
			}
			ImGui::Spacing();
			DrawReportPanel(drive, eff);
		}

		static void DrawVisualizationRow(const DiskHealthScan::DriveInfo& drive,
			const HealthCounts& hc, const EffectiveTestData& eff)
		{
			const float plot_h = 88.f;
			const ImVec2 plot_sz(-1.f, plot_h);
			const float row_h = plot_h + 72.f;

			if (ImGui::BeginTable("##viz_row", 4,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX,
				ImVec2(0, row_h))) {
				auto cell = [&](const char* id, const char* title, auto&& body) {
					ImGui::TableNextColumn();
					ImGui::PushID(id);
					ImGui::TextDisabled("%s", title);
					body();
					ImGui::PopID();
				};
				cell("pie", I18N(u8"全機健康"), [&] { DrawHealthPie(hc, plot_sz); });
				cell("smart", I18N(u8"風險指標"), [&] { DrawSmartRiskBars(drive, plot_sz); });
				cell("speed", I18N(u8"速度 MB/s"), [&] {
					const bool has_curve = eff.has_speed
						&& (!eff.speed.read_curve_mbps.empty()
							|| !eff.speed.write_curve_mbps.empty());
					if (eff.speed_live) {
						ImGui::TextColored(Theme::cyan_neon, "%s", I18N(u8"測速進行中…"));
					}
					if (has_curve) {
						DrawSpeedCurvePlot(eff.speed, plot_sz, true);
					}
					else if (eff.has_speed && eff.speed.ok) {
						DrawSpeedBars(eff.speed.read_mbps, eff.speed.write_mbps,
							eff.speed.write_tested, plot_sz);
					}
					if (eff.has_speed && eff.speed.ok) {
						char line[192] = {};
						if (eff.speed.write_tested) {
							SnprintfI18n(line, sizeof(line),
								u8"讀 %.0f  寫 %.0f MB/s · %u MB 樣本",
								eff.speed.read_mbps, eff.speed.write_mbps, eff.speed.sample_mb);
						}
						else {
							SnprintfI18n(line, sizeof(line),
								u8"僅讀 %.0f MB/s · %u MB 樣本",
								eff.speed.read_mbps, eff.speed.sample_mb);
						}
						ImGui::TextColored(Theme::cyan_neon, "%s", line);
						if (eff.speed.read_uncached) {
							ImGui::TextDisabled("%s", I18N(u8"無快取順序讀取"));
						}
						if (eff.speed_saved) {
							ImGui::TextDisabled("%s", I18N(u8"已載入上次測速結果"));
						}
					}
					else if (!eff.speed_live && eff.has_speed && eff.speed.error_utf8[0] != '\0') {
						ImGui::TextColored(ImVec4(1.f, 0.45f, 0.35f, 1.f), "%s",
							eff.speed.error_utf8);
						ImGui::TextWrapped("%s", I18N(u8"僅讀模式仍需在磁碟區建立暫存檔；請確認可寫入且可用空間大於樣本大小。"));
					}
					else {
						ImGui::TextDisabled("%s", I18N(u8"尚未測速"));
					}
				});
				cell("bad", I18N(u8"壞軌掃描"), [&] {
					const bool has_matrix = eff.has_bad && !eff.bad.matrix.empty()
						&& eff.bad.matrix_rows > 0;
					if (eff.bad_live) {
						if (has_matrix) {
							DrawBadSectorMatrix(eff.bad, true);
						}
						else {
							DrawBadSectorProgress(eff.bad, plot_sz);
						}
						const float pct = (eff.bad.bytes_planned > 0)
							? 100.f * static_cast<float>(eff.bad.bytes_scanned)
								/ static_cast<float>(eff.bad.bytes_planned)
							: 0.f;
						ImGui::TextColored(Theme::cyan_neon,
							I18NF(u8"掃描中 %.0f%% · 問題格 %u"), static_cast<double>(pct),
							eff.bad.error_count);
					}
					else if (eff.has_bad) {
						if (has_matrix) {
							DrawBadSectorMatrix(eff.bad, false);
						}
						else {
							DrawBadSectorProgress(eff.bad, plot_sz);
						}
						const float pct = (eff.bad.bytes_planned > 0)
							? 100.f * static_cast<float>(eff.bad.bytes_scanned)
								/ static_cast<float>(eff.bad.bytes_planned)
							: 0.f;
						const ImVec4 col = (eff.bad.error_count > 0)
							? ImVec4(1.f, 0.55f, 0.35f, 1.f)
							: ImVec4(0.25f, 0.95f, 0.45f, 1.f);
						ImGui::TextColored(col, I18NF(u8"完成 %.0f%% · 問題格 %u · %s"),
							static_cast<double>(pct), eff.bad.error_count,
							eff.bad.mode == DiskHealthTest::BadSectorMode::Full
								? I18N(u8"完整掃描") : I18N(u8"快速掃描"));
						if (eff.bad_saved) {
							ImGui::TextDisabled("%s", I18N(u8"已載入上次掃描結果"));
						}
					}
					else {
						ImGui::TextDisabled("%s", I18N(u8"尚未掃描"));
					}
				});
				ImGui::EndTable();
			}
			ImGui::Spacing();
		}

		static void DrawAdminBanner()
		{
			if (DiskHealthScan::IsRunningAsAdmin()) {
				ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.45f, 1.f), "%s", I18N(u8"已以系統管理員身分執行"));
				return;
			}
			ImGui::TextColored(ImVec4(1.f, 0.75f, 0.25f, 1.f), "%s", I18N(u8"目前非管理員：SMART / 實體碟掃描可能不完整"));
			ImGui::SameLine();
			if (ImGui::Button(I18N(u8"以管理員開新視窗"))) {
				HAdminPrompt::Queue(HAdminPrompt::Scene::Manual);
			}
		}

		static void CurveMinMax(const std::vector<float>& curve, float& out_min, float& out_max)
		{
			if (curve.empty()) {
				out_min = 0.f;
				out_max = 0.f;
				return;
			}
			out_min = curve[0];
			out_max = curve[0];
			for (float v : curve) {
				out_min = (std::min)(out_min, v);
				out_max = (std::max)(out_max, v);
			}
		}

		static void SetupSpeedCurveYAxis(const DiskHealthTest::SpeedTestResult& speed)
		{
			float r_min = 0.f, r_max = 0.f, w_min = 0.f, w_max = 0.f;
			bool have = false;
			if (!speed.read_curve_mbps.empty()) {
				CurveMinMax(speed.read_curve_mbps, r_min, r_max);
				have = true;
			}
			if (!speed.write_curve_mbps.empty()) {
				CurveMinMax(speed.write_curve_mbps, w_min, w_max);
				have = true;
			}
			if (!have) {
				return;
			}
			double y_lo = have ? static_cast<double>((std::min)(r_min, w_min)) : 0.0;
			double y_hi = static_cast<double>((std::max)(r_max, w_max));
			if (!speed.read_curve_mbps.empty() && !speed.write_curve_mbps.empty()) {
				y_lo = static_cast<double>((std::min)(r_min, w_min));
				y_hi = static_cast<double>((std::max)(r_max, w_max));
			}
			const double span = y_hi - y_lo;
			const double pad = (span > 1.0) ? span * 0.12 : 8.0;
			y_lo = (std::max)(0.0, y_lo - pad);
			y_hi = y_hi + pad;
			if (y_hi - y_lo < 12.0) {
				const double mid = (y_hi + y_lo) * 0.5;
				y_lo = mid - 6.0;
				y_hi = mid + 6.0;
			}
			ImPlot::SetupAxisLimits(ImAxis_Y1, y_lo, y_hi, ImPlotCond_Always);
		}

		static void PlotSpeedCurvesInPlot(const DiskHealthTest::SpeedTestResult& speed)
		{
			const auto& read = speed.read_curve_mbps;
			if (!read.empty()) {
				std::vector<double> xs(read.size());
				std::vector<double> ys(read.size());
				for (size_t i = 0; i < read.size(); ++i) {
					xs[i] = static_cast<double>(i);
					ys[i] = static_cast<double>(read[i]);
				}
				ImPlot::PlotLine(I18N(u8"讀取"), xs.data(), ys.data(), static_cast<int>(read.size()));
			}
			const auto& write = speed.write_curve_mbps;
			if (!write.empty()) {
				std::vector<double> xs(write.size());
				std::vector<double> ys(write.size());
				for (size_t i = 0; i < write.size(); ++i) {
					xs[i] = static_cast<double>(i);
					ys[i] = static_cast<double>(write[i]);
				}
				ImPlot::PlotLine(I18N(u8"寫入"), xs.data(), ys.data(), static_cast<int>(write.size()));
			}
		}

		static const ImPlotFlags kSpeedPlotNoInteract = ImPlotFlags_NoInputs
			| ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle;

		static void DrawSpeedCurvePlot(const DiskHealthTest::SpeedTestResult& speed,
			const ImVec2& plot_sz, bool mini)
		{
			if (speed.read_curve_mbps.empty() && speed.write_curve_mbps.empty()) {
				return;
			}
			ApplyImPlotThemeOnce();
			const ImPlotFlags flags = kSpeedPlotNoInteract
				| (mini ? ImPlotFlags_NoLegend : ImPlotFlags_None);
			if (ImPlot::BeginPlot(mini ? "##speed_mini" : "##speed_curve", plot_sz, flags)) {
				ImPlot::SetupAxes(mini ? nullptr : I18N(u8"採樣點"), mini ? nullptr : "MB/s",
					mini ? ImPlotAxisFlags_NoDecorations : ImPlotAxisFlags_None,
					mini ? ImPlotAxisFlags_NoDecorations : ImPlotAxisFlags_None);
				SetupSpeedCurveYAxis(speed);
				PlotSpeedCurvesInPlot(speed);
				ImPlot::EndPlot();
			}
		}

		static void DrawSpeedCurve(const DiskHealthTest::SpeedTestResult& speed)
		{
			if (speed.read_curve_mbps.empty() && speed.write_curve_mbps.empty()) {
				ImGui::TextDisabled("%s", I18N(u8"執行測速後將顯示即時速度曲線"));
				return;
			}
			ImGui::TextDisabled("%s", I18N(u8"Y 軸依本次曲線最小～最大自動留白，便於觀察波動（非從 0 起算）"));
			DrawSpeedCurvePlot(speed, ImVec2(-1.f, 150.f), false);
			if (speed.ok) {
				ImGui::TextColored(Theme::cyan_neon,
					I18NF(u8"平均  讀 %.1f MB/s  寫 %.1f MB/s"),
					speed.read_mbps, speed.write_tested ? speed.write_mbps : 0.0);
			}
			float r_min = 0.f, r_max = 0.f, w_min = 0.f, w_max = 0.f;
			CurveMinMax(speed.read_curve_mbps, r_min, r_max);
			if (!speed.read_curve_mbps.empty()) {
				ImGui::Text(I18NF(u8"讀取  最小 %.1f  最大 %.1f MB/s"), r_min, r_max);
			}
			if (speed.write_tested && !speed.write_curve_mbps.empty()) {
				CurveMinMax(speed.write_curve_mbps, w_min, w_max);
				ImGui::Text(I18NF(u8"寫入  最小 %.1f  最大 %.1f MB/s"), w_min, w_max);
			}
		}

		static int SmartAttrSortPriority(const DiskHealthScan::SmartAttribute& attr)
		{
			switch (attr.id) {
			case 5:
			case 187:
			case 197:
			case 198:
				return 0;
			case 194:
			case 190:
				return 1;
			case 9:
			case 231:
			case 233:
			case 232:
				return 2;
			default:
				return attr.prefailure ? 3 : 4;
			}
		}

		static bool SmartAttrBelowThreshold(const DiskHealthScan::SmartAttribute& attr)
		{
			return attr.prefailure && attr.threshold > 0 && attr.current <= attr.threshold;
		}

		static ImVec4 SmartAttrAccent(const DiskHealthScan::SmartAttribute& attr)
		{
			if (SmartAttrBelowThreshold(attr)) {
				return ImVec4(1.f, 0.35f, 0.3f, 1.f);
			}
			if (attr.prefailure) {
				return ImVec4(1.f, 0.75f, 0.25f, 1.f);
			}
			if (attr.id == 194 || attr.id == 190) {
				return Theme::cyan_neon;
			}
			return Theme::cyan_dim;
		}

		static float SmartAttrFill(const DiskHealthScan::SmartAttribute& attr)
		{
			if (attr.id == 231 || attr.id == 232 || attr.id == 233) {
				return ImClamp(static_cast<float>(attr.raw) / 100.f, 0.f, 1.f);
			}
			if (attr.id == 194 || attr.id == 190) {
				const int t = (attr.raw > 0 && attr.raw <= 120)
					? static_cast<int>(attr.raw)
					: static_cast<int>(attr.current);
				return ImClamp(static_cast<float>(t) / 85.f, 0.f, 1.f);
			}
			if (attr.threshold > 0 && attr.current > 0) {
				return ImClamp(static_cast<float>(attr.current) / static_cast<float>(attr.threshold),
					0.f, 1.f);
			}
			if (attr.current > 0) {
				return ImClamp(static_cast<float>(attr.current) / 253.f, 0.f, 1.f);
			}
			if (attr.raw > 0) {
				return 0.35f;
			}
			return 0.f;
		}

		static void FormatSmartAttrPrimary(const DiskHealthScan::SmartAttribute& attr,
			char* out, size_t out_size)
		{
			if (attr.id == 194 || attr.id == 190) {
				const int t = (attr.raw > 0 && attr.raw <= 120)
					? static_cast<int>(attr.raw)
					: ((attr.current > 0 && attr.current <= 120)
						? static_cast<int>(attr.current) : -1);
				if (t >= 0) {
					snprintf(out, out_size, "%d °C", t);
					return;
				}
			}
			if (attr.id == 231 || attr.id == 232 || attr.id == 233) {
				snprintf(out, out_size, "%llu %%",
					static_cast<unsigned long long>(attr.raw <= 100 ? attr.raw : attr.current));
				return;
			}
			if (attr.id == 9) {
				SnprintfI18n(out, out_size, u8"%llu 小時",
					static_cast<unsigned long long>(attr.raw));
				return;
			}
			if (attr.id == 5 || attr.id == 196 || attr.id == 197 || attr.id == 187
				|| attr.id == 198) {
				const uint64_t low16 = attr.raw & 0xFFFF;
				const uint64_t shown = (low16 > 0 && (attr.raw >> 16) == 0) ? low16 : attr.raw;
				snprintf(out, out_size, "%llu",
					static_cast<unsigned long long>(shown));
				return;
			}
			if (attr.raw > 0) {
				snprintf(out, out_size, "%llu",
					static_cast<unsigned long long>(attr.raw));
				return;
			}
			if (attr.current > 0 || attr.worst > 0) {
				SnprintfI18n(out, out_size, u8"目前 %u  最差 %u",
					static_cast<unsigned>(attr.current), static_cast<unsigned>(attr.worst));
				return;
			}
			strncpy_s(out, out_size, I18N(u8"無資料"), _TRUNCATE);
		}

		static float DrawSmartAttributeCard(const DiskHealthScan::SmartAttribute& attr, float width)
		{
			const bool below = SmartAttrBelowThreshold(attr);
			const float row_h = below ? 108.f : 98.f;
			width = (std::min)(width, ImGui::GetContentRegionAvail().x);
			if (width < 120.f) {
				width = 120.f;
			}

			ImGui::PushID(static_cast<int>(attr.id));
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(width, row_h));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p1(pos.x + width, pos.y + row_h);
			DrawCardBackground(pos, ImVec2(width, row_h), 6.f);

			const ImVec4 accent = SmartAttrAccent(attr);
			dl->AddRectFilled(pos, ImVec2(pos.x + 3.f, p1.y), Vec4ToU32(accent), 6.f,
				ImDrawFlags_RoundCornersLeft);

			char id_pill[16] = {};
			snprintf(id_pill, sizeof(id_pill), "ID %u", static_cast<unsigned>(attr.id));
			const ImVec2 pill_pos(pos.x + 10.f, pos.y + 6.f);
			const ImVec2 pill_sz(46.f, 15.f);
			dl->AddRectFilled(pill_pos, ImVec2(pill_pos.x + pill_sz.x, pill_pos.y + pill_sz.y),
				ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.22f)), 3.f);
			dl->AddText(ImVec2(pill_pos.x + 6.f, pill_pos.y + 1.f), Vec4ToU32(accent), id_pill);

			const float title_x = pill_pos.x + pill_sz.x + 6.f;
			const float title_w = width - (title_x - pos.x) - 52.f;
			char title[96] = {};
			FormatSmartAttrTitle(attr, title, sizeof(title));
			ImGui::PushClipRect(ImVec2(title_x, pos.y + 4.f),
				ImVec2(title_x + title_w, pos.y + 22.f), true);
			dl->AddText(ImVec2(title_x, pos.y + 6.f), ImGui::GetColorU32(Theme::text_muted), title);
			ImGui::PopClipRect();

			if (attr.prefailure) {
				const ImVec2 warn_pos(p1.x - 50.f, pos.y + 6.f);
				dl->AddRectFilled(warn_pos, ImVec2(warn_pos.x + 42.f, warn_pos.y + 14.f),
					ImGui::GetColorU32(ImVec4(1.f, 0.4f, 0.3f, 0.28f)), 3.f);
				dl->AddText(ImVec2(warn_pos.x + 4.f, warn_pos.y),
					ImGui::GetColorU32(ImVec4(1.f, 0.55f, 0.35f, 1.f)), I18N(u8"預故障"));
			}

			char primary[64] = {};
			FormatSmartAttrPrimary(attr, primary, sizeof(primary));
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 28.f), Vec4ToU32(accent), primary);

			const float bar_y = pos.y + 46.f;
			const float bar_w = width - 20.f;
			const float bar_h = 7.f;
			const ImVec2 bar0(pos.x + 10.f, bar_y);
			const ImVec2 bar1(bar0.x + bar_w, bar0.y + bar_h);
			dl->AddRectFilled(bar0, bar1, ImGui::GetColorU32(ImVec4(0.08f, 0.12f, 0.12f, 1.f)), 4.f);
			const float fill = SmartAttrFill(attr);
			if (fill > 0.001f) {
				dl->AddRectFilled(bar0, ImVec2(bar0.x + bar_w * fill, bar1.y), Vec4ToU32(accent), 4.f);
			}
			if (below) {
				dl->AddRect(bar0, bar1, ImGui::GetColorU32(ImVec4(1.f, 0.35f, 0.3f, 0.9f)), 4.f, 0, 1.2f);
			}

			char detail[220] = {};
			if (attr.current > 0 || attr.worst > 0 || attr.threshold > 0) {
				SnprintfI18n(detail, sizeof(detail),
					u8"目前 %u  最差 %u  門檻 %u  原始 %llu",
					static_cast<unsigned>(attr.current), static_cast<unsigned>(attr.worst),
					attr.threshold, static_cast<unsigned long long>(attr.raw));
			}
			else {
				SnprintfI18n(detail, sizeof(detail), u8"原始 %llu",
					static_cast<unsigned long long>(attr.raw));
			}
			dl->AddText(ImVec2(pos.x + 12.f, bar1.y + 5.f),
				ImGui::GetColorU32(Theme::text_muted), detail);

			if (below) {
				dl->AddText(ImVec2(pos.x + 12.f, bar1.y + 22.f),
					ImGui::GetColorU32(ImVec4(1.f, 0.45f, 0.35f, 1.f)), I18N(u8"低於門檻"));
			}

			ImGui::PopID();
			return row_h;
		}

		static void DrawSmartAttributesPanel(const DiskHealthScan::DriveInfo& drive, float height)
		{
			if (drive.smart_attributes.empty()) {
				ImGui::TextDisabled("%s",
					drive.status_note[0] != '\0' ? drive.status_note : I18N(u8"無 SMART 屬性"));
				return;
			}

			std::vector<DiskHealthScan::SmartAttribute> attrs = drive.smart_attributes;
			std::sort(attrs.begin(), attrs.end(),
				[](const DiskHealthScan::SmartAttribute& a, const DiskHealthScan::SmartAttribute& b) {
					const int pa = SmartAttrSortPriority(a);
					const int pb = SmartAttrSortPriority(b);
					if (pa != pb) {
						return pa < pb;
					}
					return a.id < b.id;
				});

			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float row_w = (std::max)(120.f, full_w - 12.f);

			ImGui::BeginChild("##smart_attr_scroll", ImVec2(0.f, height), ImGuiChildFlags_Borders);
			for (const auto& attr : attrs) {
				DrawSmartAttributeCard(attr, row_w);
				ImGui::Dummy(ImVec2(0.f, 3.f));
			}
			ImGui::EndChild();
		}

		static ImU32 SectorMatrixColor(uint8_t state, bool is_scan_head)
		{
			if (is_scan_head) {
				return ImGui::GetColorU32(ImVec4(0.15f, 0.95f, 1.f, 1.f));
			}
			switch (static_cast<DiskHealthTest::SectorCellState>(state)) {
			case DiskHealthTest::SectorCellState::Ok:
				return ImGui::GetColorU32(ImVec4(0.18f, 0.82f, 0.42f, 1.f));
			case DiskHealthTest::SectorCellState::Bad:
				return ImGui::GetColorU32(ImVec4(0.98f, 0.32f, 0.22f, 1.f));
			default:
				return ImGui::GetColorU32(ImVec4(0.12f, 0.17f, 0.19f, 1.f));
			}
		}

		static void DrawMatrixLegendItem(const char* label, const ImVec4& color)
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float box = 11.f;
			ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + box, pos.y + box),
				ImGui::GetColorU32(color), 2.f);
			ImGui::Dummy(ImVec2(box, box));
			ImGui::SameLine();
			ImGui::TextDisabled("%s", label);
			ImGui::SameLine(0.f, 14.f);
		}

		static void DrawBadSectorMatrix(const DiskHealthTest::BadSectorResult& result, bool running)
		{
			if (result.matrix.empty() || result.matrix_rows <= 0) {
				ImGui::TextDisabled("%s", I18N(u8"執行壞軌掃描後將顯示掃描矩陣"));
				DrawMatrixLegendItem(I18N(u8"未掃描"), ImVec4(0.12f, 0.17f, 0.19f, 1.f));
				DrawMatrixLegendItem(I18N(u8"正常"), ImVec4(0.18f, 0.82f, 0.42f, 1.f));
				DrawMatrixLegendItem(I18N(u8"異常"), ImVec4(0.98f, 0.32f, 0.22f, 1.f));
				DrawMatrixLegendItem(I18N(u8"掃描中"), ImVec4(0.15f, 0.95f, 1.f, 1.f));
				return;
			}
			const int cols = result.matrix_cols;
			const int rows = result.matrix_rows;
			const float avail_w = ImGui::GetContentRegionAvail().x;
			const float cell = (std::min)(12.f,
				(std::max)(5.f, avail_w / static_cast<float>(cols)));
			const ImVec2 grid_sz(cols * cell, rows * cell);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(grid_sz);
			ImDrawList* dl = ImGui::GetWindowDrawList();

			int scan_head = -1;
			if (running) {
				for (size_t i = 0; i < result.matrix.size(); ++i) {
					if (result.matrix[i] == static_cast<uint8_t>(DiskHealthTest::SectorCellState::Pending)) {
						scan_head = static_cast<int>(i);
						break;
					}
				}
			}

			for (int y = 0; y < rows; ++y) {
				for (int x = 0; x < cols; ++x) {
					const size_t idx = static_cast<size_t>(y * cols + x);
					const uint8_t state = result.matrix[idx];
					const bool head = (static_cast<int>(idx) == scan_head);
					const ImU32 col = SectorMatrixColor(state, head);
					const ImVec2 p0(pos.x + x * cell, pos.y + y * cell);
					const ImVec2 p1(p0.x + cell - 1.f, p0.y + cell - 1.f);
					dl->AddRectFilled(p0, p1, col);
					if (head) {
						dl->AddRect(p0, p1, ImGui::GetColorU32(Theme::cyan_neon), 0.f, 0, 1.5f);
					}
				}
			}
			ImGui::TextDisabled(I18NF(u8"矩陣 %d×%d：左→右為掃描進度，上→下為區域分塊"), cols, rows);
			DrawMatrixLegendItem(I18N(u8"未掃描"), ImVec4(0.12f, 0.17f, 0.19f, 1.f));
			DrawMatrixLegendItem(I18N(u8"正常"), ImVec4(0.18f, 0.82f, 0.42f, 1.f));
			DrawMatrixLegendItem(I18N(u8"異常"), ImVec4(0.98f, 0.32f, 0.22f, 1.f));
			DrawMatrixLegendItem(I18N(u8"掃描中"), ImVec4(0.15f, 0.95f, 1.f, 1.f));
		}

		static void DrawReportPanel(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff)
		{
			char report[6144] = {};
			DiskHealthScan::BuildDriveReport(drive, report, sizeof(report));

			char extra[4096] = {};
			if (eff.has_speed && eff.speed.ok) {
				DiskHealthTest::BuildSpeedReport(eff.speed, extra, sizeof(extra));
			}
			if (eff.has_bad && eff.bad.ok) {
				char bad_part[2048] = {};
				DiskHealthTest::BuildBadSectorReport(eff.bad, bad_part, sizeof(bad_part));
				if (extra[0] != '\0') {
					strncat_s(extra, "\n\n", _TRUNCATE);
				}
				strncat_s(extra, bad_part, _TRUNCATE);
			}

			const float avail_h = ImGui::GetContentRegionAvail().y;
			const float h = (std::max)(160.f, (std::min)(320.f, avail_h));
			ImGui::BeginChild("##disk_report_box", ImVec2(0.f, h), ImGuiChildFlags_Borders);
			ImGui::TextWrapped("%s", report);
			if (extra[0] != '\0') {
				ImGui::Separator();
				ImGui::TextWrapped("%s", extra);
			}
			ImGui::EndChild();
		}

		static const char* JobKindLabel(DiskHealthTest::JobKind kind)
		{
			switch (kind) {
			case DiskHealthTest::JobKind::SpeedTest: return I18N(u8"速度測試");
			case DiskHealthTest::JobKind::BadSectorScan: return I18N(u8"壞軌掃描");
			default: return "—";
			}
		}

		static void DrawStatusPill(const char* label, const ImVec4& color)
		{
			const ImVec2 ts = ImGui::CalcTextSize(label);
			const ImVec2 pad(10.f, 4.f);
			const ImVec2 size(ts.x + pad.x * 2.f, ts.y + pad.y * 2.f);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
				ImGui::GetColorU32(ImVec4(color.x * 0.22f, color.y * 0.22f, color.z * 0.22f, 0.95f)), 6.f);
			dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Vec4ToU32(color), 6.f, 0, 1.f);
			dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), Vec4ToU32(color), label);
			ImGui::Dummy(size);
			ImGui::SameLine(0.f, 8.f);
		}

		static void DrawDiagnosticLiveReport(const DiskHealthScan::DriveInfo& drive,
			const DiskHealthTest::JobState& job, const EffectiveTestData& eff)
		{
			using namespace Theme;
			const float full_w = (std::max)(200.f, ImGui::GetContentRegionAvail().x);
			const float panel_h = job.running ? 196.f : 168.f;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawCardBackground(pos, ImVec2(full_w, panel_h), 10.f);
			ImGui::Dummy(ImVec2(full_w, panel_h));

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddText(ImVec2(pos.x + 12.f, pos.y + 10.f), Vec4ToU32(cyan_neon), I18N(u8"檢測即時報告"));

			const bool matches_drive = (job.physical_index < 0)
				|| (job.physical_index == drive.physical_index);
			const bool active = job.running && matches_drive;
			const bool has_result = matches_drive && !job.running && job.kind != DiskHealthTest::JobKind::None;

			ImVec4 phase_col = ImVec4(0.55f, 0.6f, 0.6f, 1.f);
			const char* phase = I18N(u8"閒置");
			if (active) {
				phase = I18N(u8"進行中");
				phase_col = cyan_neon;
			}
			else if (has_result) {
				const bool failed = (job.kind == DiskHealthTest::JobKind::SpeedTest && !job.speed.ok
						&& job.speed.error_utf8[0] != '\0')
					|| (job.kind == DiskHealthTest::JobKind::BadSectorScan && !job.bad_sector.ok
						&& job.bad_sector.error_utf8[0] != '\0');
				if (failed) {
					phase = I18N(u8"失敗");
					phase_col = ImVec4(1.f, 0.35f, 0.3f, 1.f);
				}
				else if (job.kind == DiskHealthTest::JobKind::BadSectorScan
					&& job.bad_sector.error_count > 0) {
					phase = I18N(u8"完成（有異常格）");
					phase_col = ImVec4(1.f, 0.65f, 0.25f, 1.f);
				}
				else {
					phase = I18N(u8"已完成");
					phase_col = ImVec4(0.25f, 0.95f, 0.45f, 1.f);
				}
			}

			ImGui::SetCursorScreenPos(ImVec2(pos.x + 12.f, pos.y + 34.f));
			DrawStatusPill(JobKindLabel(job.kind), cyan_dim);
			DrawStatusPill(phase, phase_col);
			char pd_buf[32] = {};
			snprintf(pd_buf, sizeof(pd_buf), "PD%d", drive.physical_index);
			DrawStatusPill(pd_buf, ImVec4(0.7f, 0.78f, 0.78f, 1.f));
			ImGui::NewLine();

			float y = pos.y + 66.f;
			char buf[256] = {};
			const auto line = [&](const char* text, const ImVec4& col) {
				dl->AddText(ImVec2(pos.x + 12.f, y), Vec4ToU32(col), text);
				y += 18.f;
			};

			if (active || has_result) {
				const float pct = job.progress * 100.f;
				ImGui::SetCursorScreenPos(ImVec2(pos.x + 12.f, pos.y + 62.f));
				ImGui::PushItemWidth(full_w - 24.f);
				ImGui::ProgressBar(job.progress, ImVec2(full_w - 24.f, 18.f),
					job.status_utf8[0] != '\0' ? job.status_utf8 : I18N(u8"處理中…"));
				ImGui::PopItemWidth();
				y = pos.y + 88.f;

				SnprintfI18n(buf, sizeof(buf), u8"進度：%.0f%%", static_cast<double>(pct));
				line(buf, cyan_dim);

				if (job.kind == DiskHealthTest::JobKind::SpeedTest) {
					if (job.speed.ok) {
						SnprintfI18n(buf, sizeof(buf), u8"讀取 %.1f MB/s  |  寫入 %.1f MB/s",
							job.speed.read_mbps,
							job.speed.write_tested ? job.speed.write_mbps : 0.0);
						line(buf, ImVec4(0.25f, 0.95f, 0.45f, 1.f));
					}
					else if (job.speed.error_utf8[0] != '\0') {
						SnprintfI18n(buf, sizeof(buf), u8"速度測試：%s", job.speed.error_utf8);
						line(buf, ImVec4(1.f, 0.45f, 0.35f, 1.f));
					}
					else if (active) {
						line(I18N(u8"速度測試：正在讀寫取樣檔並記錄曲線…"), cyan_neon);
					}
				}
				else if (job.kind == DiskHealthTest::JobKind::BadSectorScan) {
					const auto& bs = job.bad_sector;
					const double scan_pct = (bs.bytes_planned > 0)
						? 100.0 * static_cast<double>(bs.bytes_scanned)
							/ static_cast<double>(bs.bytes_planned)
						: 0.0;
					SnprintfI18n(buf, sizeof(buf),
						u8"已掃描 %.1f%%  |  %llu / %llu MB",
						scan_pct,
						static_cast<unsigned long long>(bs.bytes_scanned / (1024ull * 1024ull)),
						static_cast<unsigned long long>(bs.bytes_planned / (1024ull * 1024ull)));
					line(buf, cyan_neon);

					SnprintfI18n(buf, sizeof(buf), u8"矩陣問題格：%u  |  模式：%s",
						bs.error_count,
						bs.mode == DiskHealthTest::BadSectorMode::Quick ? I18N(u8"快速（頭尾）") : I18N(u8"完整"));
					const ImVec4 err_col = (bs.error_count == 0)
						? ImVec4(0.25f, 0.95f, 0.45f, 1.f)
						: (bs.error_count <= 3
							? ImVec4(1.f, 0.78f, 0.2f, 1.f)
							: ImVec4(1.f, 0.35f, 0.3f, 1.f));
					line(buf, err_col);

					if (bs.error_utf8[0] != '\0') {
						SnprintfI18n(buf, sizeof(buf), u8"錯誤：%s", bs.error_utf8);
						line(buf, ImVec4(1.f, 0.45f, 0.35f, 1.f));
					}
				}
			}
			else {
				if (eff.has_speed && eff.speed.ok) {
					SnprintfI18n(buf, sizeof(buf), u8"上次速度：%s %.0f MB/s（%u MB 樣本）%s",
						eff.speed.write_tested ? I18N(u8"讀+寫") : I18N(u8"僅讀"),
						eff.speed.read_mbps, eff.speed.sample_mb,
						eff.speed_saved ? I18N(u8" [已儲存]") : "");
					line(buf, ImVec4(0.25f, 0.95f, 0.45f, 1.f));
				}
				if (eff.has_bad && eff.bad.ok) {
					SnprintfI18n(buf, sizeof(buf), u8"上次壞軌：問題格 %u（%s）%s",
						eff.bad.error_count,
						eff.bad.mode == DiskHealthTest::BadSectorMode::Quick ? I18N(u8"快速") : I18N(u8"完整"),
						eff.bad_saved ? I18N(u8" [已儲存]") : "");
					const ImVec4 c = eff.bad.error_count > 0
						? ImVec4(1.f, 0.65f, 0.25f, 1.f)
						: ImVec4(0.25f, 0.95f, 0.45f, 1.f);
					line(buf, c);
				}
				if (!eff.has_speed && !eff.has_bad) {
					line(I18N(u8"尚未開始檢測。可於下方啟動速度或壞軌掃描。"), text_muted);
				}
				char smart_line[160] = {};
				if (drive.smart_available) {
					SnprintfI18n(smart_line, sizeof(smart_line),
						u8"SMART：可讀（%zu 項）", drive.smart_attributes.size());
					line(smart_line, ImVec4(0.25f, 0.95f, 0.45f, 1.f));
				}
				else if (drive.status_note[0] != '\0') {
					SnprintfI18n(smart_line, sizeof(smart_line), u8"SMART：%s", drive.status_note);
					line(smart_line, ImVec4(1.f, 0.75f, 0.35f, 1.f));
				}
			}

			if (active) {
				ImGui::SetCursorScreenPos(ImVec2(pos.x + full_w - 88.f, pos.y + panel_h - 34.f));
				if (ImGui::Button(I18N(u8"取消檢測"), ImVec2(76.f, 26.f))) {
					DiskHealthTest::Cancel();
				}
			}
			ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + panel_h + 6.f));
		}

		static void DrawToolsPanel(const DiskHealthScan::DriveInfo& drive,
			const EffectiveTestData& eff)
		{
			using namespace Theme;
			const DiskHealthTest::JobState job = DiskHealthTest::GetState();
			const bool busy = job.running;
			const bool can_speed = drive.volume_letters[0] != '\0' && !busy;

			if (ImGui::BeginTable("##tools_split", 2, ImGuiTableFlags_SizingStretchSame)) {
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", I18N(u8"速度測試"));
				const char* sample_labels[] = { "256MB", "512MB", "1GB", "2GB", "4GB", "8GB" };
				const int sample_values[] = { 256, 512, 1024, 2048, 4096, 8192 };
				DiskSegmentedControl("##speed_mb", sample_labels, sample_values, 6, &g_speed_sample_mb);
				ImGui::Spacing();
				const char* mode_labels[] = { I18N(u8"僅讀取"), I18N(u8"讀+寫") };
				const int mode_values[] = { 0, 1 };
				DiskSegmentedControl("##speed_mode", mode_labels, mode_values, 2, &g_speed_mode);
				ImGui::Spacing();
				if (DiskCyberButton("##speed_start", ImVec2(112.f, 32.f), I18N(u8"開始測速"), can_speed)) {
					const bool include_write = (g_speed_mode != 0);
					if (include_write) {
						g_pending_write_confirm = true;
						ImGui::OpenPopup("##disk_speed_write_confirm");
					}
					else {
						DiskHealthTest::RequestSpeedTest(drive.physical_index, drive.volume_letters,
							static_cast<uint32_t>(g_speed_sample_mb), false);
					}
				}
				if (eff.has_speed && eff.speed.ok && !eff.speed_live) {
					if (eff.speed.write_tested) {
						ImGui::TextColored(cyan_neon, I18NF(u8"讀 %.0f  寫 %.0f MB/s"),
							eff.speed.read_mbps, eff.speed.write_mbps);
					}
					else {
						ImGui::TextColored(cyan_neon, I18NF(u8"僅讀 %.0f MB/s（無快取）"),
							eff.speed.read_mbps);
					}
				}

				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", I18N(u8"壞軌檢測"));
				const char* bad_mode_labels[] = { I18N(u8"快速"), I18N(u8"完整") };
				const int bad_mode_values[] = { 0, 1 };
				DiskSegmentedControl("##bad_mode", bad_mode_labels, bad_mode_values, 2, &g_bad_sector_mode);
				ImGui::Spacing();
				if (DiskCyberButton("##bad_start", ImVec2(112.f, 32.f), I18N(u8"開始掃描"), !busy)) {
					const auto mode = (g_bad_sector_mode == 0)
						? DiskHealthTest::BadSectorMode::Quick
						: DiskHealthTest::BadSectorMode::Full;
					DiskHealthTest::RequestBadSectorScan(drive.physical_index,
						drive.size_bytes, mode);
				}
				if (job.kind == DiskHealthTest::JobKind::BadSectorScan && !job.running
					&& job.bad_sector.ok) {
					ImGui::TextColored(cyan_neon, I18NF(u8"問題區 %u"), job.bad_sector.error_count);
				}
				ImGui::EndTable();
			}

			if (ImGui::BeginPopupModal("##disk_speed_write_confirm", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextWrapped(I18NF(u8"於 %s 寫入約 %d MB 測試檔？"), drive.volume_letters,
					g_speed_sample_mb);
				if (DiskCyberButton("##write_ok", ImVec2(88.f, 30.f), I18N(u8"確定"))) {
					DiskHealthTest::RequestSpeedTest(drive.physical_index, drive.volume_letters,
						static_cast<uint32_t>(g_speed_sample_mb), true);
					g_pending_write_confirm = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (DiskCyberButton("##write_cancel", ImVec2(88.f, 30.f), I18N(u8"取消"))) {
					g_pending_write_confirm = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (g_pending_write_confirm && !ImGui::IsPopupOpen("##disk_speed_write_confirm")) {
				ImGui::OpenPopup("##disk_speed_write_confirm");
			}

			ImGui::Separator();
			ImGui::TextColored(Theme::cyan_neon, "%s", I18N(u8"速度曲線"));
			if (eff.has_speed) {
				DrawSpeedCurve(eff.speed);
			}
			else {
				ImGui::TextDisabled("%s", I18N(u8"尚無速度測試紀錄"));
			}

			ImGui::Separator();
			ImGui::TextColored(Theme::cyan_neon, "%s", I18N(u8"壞軌掃描矩陣"));
			if (eff.has_bad) {
				DrawBadSectorMatrix(eff.bad, eff.bad_live);
			}
			else {
				ImGui::TextDisabled("%s", I18N(u8"尚無壞軌掃描紀錄"));
			}
		}

		static void DrawMainDetail(const DiskHealthScan::Snapshot& snap,
			const DiskHealthScan::DriveInfo& drive)
		{
			if (IsUsbWithoutSmart(drive)) {
				DrawMainDetailUsbLimited(snap, drive);
				return;
			}

			const DiskHealthTest::JobState job = DiskHealthTest::GetState();
			const HealthCounts hc = CountHealth(snap);
			const EffectiveTestData eff = GetEffectiveTestData(drive);

			DrawAdminBanner();

			if (!drive.smart_available) {
				ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f), "%s", I18N(u8"此碟未提供完整 SMART，以下僅顯示可讀取的容量與介面資訊。"));
			}
			if (drive.size_bytes == 0 && drive.volume_letters[0] != '\0') {
				ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f),
					I18NF(u8"未能讀取實體容量；請確認磁碟機代號 [%s] 可正常開啟。"),
					drive.volume_letters);
			}

			if (drive.status_note[0] != '\0') {
				ImGui::TextWrapped(I18NF(u8"狀態：%s"), drive.status_note);
			}

			DrawAtAGlancePanel(drive);
			char model_line[256] = {};
			snprintf(model_line, sizeof(model_line), "%s",
				drive.model_utf8[0] ? drive.model_utf8 : "—");
			ImGui::TextDisabled(I18NF(u8"型號：%s"), model_line);
			if (drive.volume_letters[0] != '\0') {
				ImGui::SameLine();
				ImGui::TextDisabled(I18NF(u8"  槽位 [%s]  |  %s"), drive.volume_letters,
					BusDisplayText(drive.bus_type_utf8[0] ? drive.bus_type_utf8 : "—"));
			}

			DrawSectionTitle(I18N(u8"圖表與風險"));
			DrawChartsTab(drive, job, hc);

			DrawSectionTitle(I18N(u8"診斷工具（速度 / 壞軌）"));
			DrawDiagnosticsTab(drive, eff);

			DrawSectionTitle(I18N(u8"SMART 屬性"));
			DrawSmartTab(drive);

			DrawSectionTitle(I18N(u8"詳細報告"));
			DrawReportTab(drive, eff);
		}

		static void DrawEmptyDetail(bool scanning)
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const ImVec2 sz = ImGui::GetContentRegionAvail();
			DrawCardBackground(pos, sz, 12.f);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const char* line1 = scanning ? I18N(u8"正在掃描硬碟…") : I18N(u8"請從上方選擇硬碟");
			const char* line2 = scanning ? "" : I18N(u8"下方將顯示完整健康、圖表與診斷區塊");
			const ImVec2 t1 = ImGui::CalcTextSize(line1);
			const ImU32 c1 = scanning
				? ImGui::GetColorU32(Theme::cyan_neon)
				: ImGui::GetColorU32(Theme::text_muted);
			dl->AddText(ImVec2(pos.x + (sz.x - t1.x) * 0.5f, pos.y + sz.y * 0.38f), c1, line1);
			if (line2[0] != '\0') {
				const ImVec2 t2 = ImGui::CalcTextSize(line2);
				dl->AddText(ImVec2(pos.x + (sz.x - t2.x) * 0.5f, pos.y + sz.y * 0.38f + 22.f),
					ImGui::GetColorU32(Theme::text_muted), line2);
			}
			ImGui::Dummy(sz);
		}
	}

	void RenderContent()
	{
		if (!g_admin_prompt_done) {
			g_admin_prompt_done = true;
			if (!DiskHealthScan::IsRunningAsAdmin()) {
				HAdminPrompt::Queue(HAdminPrompt::Scene::DiskHealth);
			}
		}
		if (!g_live_refresh_armed) {
			g_live_refresh_armed = true;
			DiskHealthScan::SetLiveRefreshEnabled(true);
		}
		if (!g_persist_loaded) {
			g_persist_loaded = true;
			DiskHealthTest::LoadPersistedResults();
		}
		if (!g_deferred_scan_requested) {
			g_deferred_scan_requested = true;
			DiskHealthScan::RequestRescan();
		}

		DiskHealthScan::Snapshot snap = DiskHealthScan::GetSnapshot();

		if (snap.drives.size() != g_last_snap_drive_count) {
			g_last_snap_drive_count = snap.drives.size();
			int best = 0;
			for (size_t i = 0; i < snap.drives.size(); ++i) {
				if (snap.drives[i].smart_available) {
					best = static_cast<int>(i);
					break;
				}
				if (snap.drives[i].physical_index == 0) {
					best = static_cast<int>(i);
				}
			}
			g_selected_drive = best;
		}

		if (g_selected_drive >= static_cast<int>(snap.drives.size())) {
			g_selected_drive = snap.drives.empty() ? 0 : static_cast<int>(snap.drives.size()) - 1;
		}
		if (g_selected_drive < 0) {
			g_selected_drive = 0;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f));

		DrawTopDashboard(snap);
		ImGui::Spacing();
		DrawDriveChipRow(snap);
		ImGui::Separator();

		if (!snap.drives.empty() && g_selected_drive < static_cast<int>(snap.drives.size())) {
			DrawMainDetail(snap, snap.drives[static_cast<size_t>(g_selected_drive)]);
		}
		else {
			DrawEmptyDetail(snap.scanning);
		}

		ImGui::PopStyleVar();
	}
}
