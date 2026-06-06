#define IMGUI_DEFINE_MATH_OPERATORS
#include "MainPageUI.h"
#include "ClearPageUI.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include "MainPageDiskScan.h"
#include "CleanHistory.h"
#include "HCleanTask.h"
#include "MainPageMemory.h"
#include <imgui_internal.h>
#include <implot.h>
#include <windows.h>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace MainPageUI {
	namespace {
		using namespace HUiTheme;

		constexpr float kRounding = 6.0f;
		constexpr float kBorderPad = 8.0f;
		constexpr float kSectionGap = 12.0f;
		constexpr float kCardGap = 10.0f;
		constexpr int kHistoryLen = 96;
		constexpr int kMaxDrives = 26;
		constexpr float kSampleIntervalSec = 0.5f;
		constexpr float kCompactCardW = 168.f;
		constexpr float kNormalCardW = 228.f;

		struct DonutSegment {
			const char* label;
			float fraction;
			ImVec4 color;
			double gb;
		};

		struct DriveInfo {
			char root[8];
			char title[96];
			double used_gb;
			double total_gb;
			float usage_ratio;
			bool valid;
		};

		struct CpuTimes {
			unsigned long long idle = 0;
			unsigned long long kernel = 0;
			unsigned long long user = 0;
		};

		static float g_cpu_load = 0.f;
		static float g_ram_load = 0.f;
		static float g_cpu_history[kHistoryLen] = {};
		static float g_ram_history[kHistoryLen] = {};
		static std::vector<std::array<float, kHistoryLen>> g_drive_usage_hist;
		static std::vector<float> g_prev_drive_usage;
		static std::vector<DriveInfo> g_drives;
		static DonutSegment g_storage_segments[6] = {};
		static int g_storage_segment_count = 0;
		static char g_storage_subtitle[128] = {};
		static bool g_history_filled = false;
		static bool g_disk_scan_init = false;
		static bool g_cpu_times_init = false;
		static CpuTimes g_cpu_last = {};
		static float g_sample_accum = 0.f;
		static bool g_implot_theme_init = false;

		static unsigned long long FileTimeToUll(const FILETIME& ft)
		{
			ULARGE_INTEGER u;
			u.LowPart = ft.dwLowDateTime;
			u.HighPart = ft.dwHighDateTime;
			return u.QuadPart;
		}

		static void PushHistorySample(float* hist, int len, float sample)
		{
			for (int i = 0; i < len - 1; ++i) {
				hist[i] = hist[i + 1];
			}
			hist[len - 1] = ImClamp(sample, 0.f, 1.f);
		}

		static void FillHistory(float* hist, int len, float value)
		{
			for (int i = 0; i < len; ++i) {
				hist[i] = value;
			}
		}

		static float SampleCpuUsage()
		{
			FILETIME idle_ft{}, kernel_ft{}, user_ft{};
			if (!GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
				return g_cpu_load;
			}

			const CpuTimes now{
				FileTimeToUll(idle_ft),
				FileTimeToUll(kernel_ft),
				FileTimeToUll(user_ft),
			};

			if (!g_cpu_times_init) {
				g_cpu_last = now;
				g_cpu_times_init = true;
				return g_cpu_load;
			}

			const unsigned long long idle_delta = now.idle - g_cpu_last.idle;
			const unsigned long long kernel_delta = now.kernel - g_cpu_last.kernel;
			const unsigned long long user_delta = now.user - g_cpu_last.user;
			g_cpu_last = now;

			const unsigned long long sys_delta = kernel_delta + user_delta;
			if (sys_delta == 0) {
				return g_cpu_load;
			}

			const unsigned long long busy = sys_delta - idle_delta;
			g_cpu_load = static_cast<float>(busy) / static_cast<float>(sys_delta);
			return g_cpu_load;
		}

		static float SampleRamUsage()
		{
			MEMORYSTATUSEX ms = {};
			ms.dwLength = sizeof(ms);
			if (!GlobalMemoryStatusEx(&ms) || ms.ullTotalPhys == 0) {
				return g_ram_load;
			}
			const unsigned long long used = ms.ullTotalPhys - ms.ullAvailPhys;
			g_ram_load = static_cast<float>(used) / static_cast<float>(ms.ullTotalPhys);
			return g_ram_load;
		}

		static ImVec4 StorageCategoryColor(int index)
		{
			static const ImVec4 palette[] = {
				ImVec4(0.00f, 0.90f, 0.90f, 1.0f),
				ImVec4(0.20f, 0.75f, 1.00f, 1.0f),
				ImVec4(0.55f, 0.85f, 0.35f, 1.0f),
				ImVec4(1.00f, 0.75f, 0.20f, 1.0f),
				ImVec4(0.65f, 0.45f, 0.95f, 1.0f),
				ImVec4(0.08f, 0.18f, 0.18f, 1.0f),
			};
			const int count = IM_ARRAYSIZE(palette);
			return palette[index >= 0 && index < count ? index : 0];
		}

		static bool QueryDrive(const wchar_t* root, DriveInfo& out)
		{
			out = {};
			if (root == nullptr || root[0] == L'\0') {
				return false;
			}

			const UINT drive_type = GetDriveTypeW(root);
			if (drive_type != DRIVE_FIXED && drive_type != DRIVE_REMOVABLE) {
				return false;
			}

			ULARGE_INTEGER free_bytes{}, total_bytes{}, total_free{};
			if (!GetDiskFreeSpaceExW(root, &free_bytes, &total_bytes, &total_free)) {
				return false;
			}

			if (total_bytes.QuadPart == 0) {
				return false;
			}

			const unsigned long long used_bytes = total_bytes.QuadPart - free_bytes.QuadPart;
			const double gb = 1024.0 * 1024.0 * 1024.0;
			out.used_gb = static_cast<double>(used_bytes) / gb;
			out.total_gb = static_cast<double>(total_bytes.QuadPart) / gb;
			out.usage_ratio = static_cast<float>(used_bytes) / static_cast<float>(total_bytes.QuadPart);
			out.valid = true;

			char narrow_root[8] = {};
			WideCharToMultiByte(CP_UTF8, 0, root, -1, narrow_root, sizeof(narrow_root), nullptr, nullptr);
			snprintf(out.root, sizeof(out.root), "%s", narrow_root);

			wchar_t vol_name[MAX_PATH + 1] = {};
			GetVolumeInformationW(root, vol_name, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
			char vol_utf8[64] = {};
			if (vol_name[0] != L'\0') {
				WideCharToMultiByte(CP_UTF8, 0, vol_name, -1, vol_utf8, sizeof(vol_utf8), nullptr, nullptr);
				snprintf(out.title, sizeof(out.title), "%s  %s", narrow_root, vol_utf8);
			}
			else {
				snprintf(out.title, sizeof(out.title), "%s", narrow_root);
			}
			return true;
		}

		static void RefreshDriveList()
		{
			std::vector<DriveInfo> next;
			next.reserve(8);

			wchar_t drive_strings[512] = {};
			const DWORD len = GetLogicalDriveStringsW(
				static_cast<DWORD>(IM_ARRAYSIZE(drive_strings)), drive_strings);
			if (len > 0 && len < IM_ARRAYSIZE(drive_strings)) {
				for (wchar_t* p = drive_strings; *p != L'\0'; p += wcslen(p) + 1) {
					DriveInfo info = {};
					if (QueryDrive(p, info)) {
						next.push_back(info);
					}
				}
			}
			else {
				for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
					const std::wstring root = std::wstring(1, letter) + L":\\";
					const UINT type = GetDriveTypeW(root.c_str());
					if (type == DRIVE_NO_ROOT_DIR) {
						continue;
					}
					if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
						continue;
					}
					DriveInfo info = {};
					if (QueryDrive(root.c_str(), info)) {
						next.push_back(info);
					}
				}
			}

			if (static_cast<int>(next.size()) > kMaxDrives) {
				next.resize(static_cast<size_t>(kMaxDrives));
			}

			if (next.size() != g_drives.size()) {
				g_drives = std::move(next);
				g_drive_usage_hist.assign(g_drives.size(), {});
				g_prev_drive_usage.assign(g_drives.size(), 0.f);
				for (auto& hist : g_drive_usage_hist) {
					hist.fill(0.f);
				}
				g_history_filled = false;
			}
			else {
				for (size_t i = 0; i < next.size(); ++i) {
					const float prev_usage = g_drives[i].valid ? g_drives[i].usage_ratio : 0.f;
					g_drives[i] = next[i];
					if (g_prev_drive_usage.size() > i) {
						g_prev_drive_usage[i] = prev_usage;
					}
				}
			}
		}

		static void UpdateStorageDonut()
		{
			const MainPageDiskScan::Snapshot snap = MainPageDiskScan::GetSnapshot();

			if (snap.scanning) {
				g_storage_segment_count = 0;
				if (snap.status_text[0] != '\0') {
					snprintf(g_storage_subtitle, sizeof(g_storage_subtitle), "%s  %.0f%%",
						I18N(snap.status_text), snap.progress * 100.f);
				}
				else {
					snprintf(g_storage_subtitle, sizeof(g_storage_subtitle),
						HTR(ScanningPctFmt), snap.progress * 100.f);
				}
				return;
			}

			char drive_utf8[16] = "C:";
			if (snap.drive_root[0] != L'\0') {
				WideCharToMultiByte(CP_UTF8, 0, snap.drive_root, -1,
					drive_utf8, sizeof(drive_utf8), nullptr, nullptr);
			}

			if (!snap.valid || snap.segment_count <= 0) {
				g_storage_segment_count = 2;
				g_storage_segments[0] = { HTR(Used), 0.5f, StorageCategoryColor(0), 0.0 };
				g_storage_segments[1] = { HTR(Available), 0.5f, StorageCategoryColor(5), 0.0 };
				snprintf(g_storage_subtitle, sizeof(g_storage_subtitle),
					HTR(StorageOverviewFmt), drive_utf8);
				return;
			}

			if (snap.volume_label[0] != '\0') {
				snprintf(g_storage_subtitle, sizeof(g_storage_subtitle), "%s  %s",
					drive_utf8, snap.volume_label);
			}
			else {
				snprintf(g_storage_subtitle, sizeof(g_storage_subtitle),
					HTR(StorageCategoryFmt), drive_utf8);
			}

			const double gb = 1024.0 * 1024.0 * 1024.0;
			g_storage_segment_count = snap.segment_count;
			for (int i = 0; i < snap.segment_count && i < IM_ARRAYSIZE(g_storage_segments); ++i) {
				g_storage_segments[i].label = snap.segments[i].label;
				g_storage_segments[i].fraction = snap.segments[i].fraction;
				g_storage_segments[i].gb = static_cast<double>(snap.segments[i].bytes) / gb;
				g_storage_segments[i].color = StorageCategoryColor(i);
			}
		}

		static void ApplyImPlotThemeOnce()
		{
			if (g_implot_theme_init) {
				return;
			}
			g_implot_theme_init = true;

			ImPlotStyle& style = ImPlot::GetStyle();
			style.PlotPadding = ImVec2(0.f, 0.f);
			style.PlotBorderSize = 0.f;
			style.MinorAlpha = 0.12f;
			style.PlotDefaultSize = ImVec2(120.f, 36.f);

			ImVec4* colors = style.Colors;
			colors[ImPlotCol_FrameBg] = ImVec4(0.02f, 0.06f, 0.06f, 1.0f);
			colors[ImPlotCol_PlotBg] = ImVec4(0.02f, 0.05f, 0.05f, 1.0f);
			colors[ImPlotCol_PlotBorder] = ImVec4(0.0f, 0.35f, 0.35f, 0.6f);
			colors[ImPlotCol_AxisGrid] = ImVec4(0.0f, 0.35f, 0.35f, 0.18f);
			colors[ImPlotCol_AxisText] = ImVec4(0.45f, 0.55f, 0.55f, 1.0f);
			colors[ImPlotCol_InlayText] = cyan_mid();
		}

		static void UpdateSystemMetrics()
		{
			ApplyImPlotThemeOnce();

			if (!g_disk_scan_init) {
				g_disk_scan_init = true;
				MainPageDiskScan::Init();
			}

			const float dt = ImGui::GetIO().DeltaTime;
			g_sample_accum += dt;
			if (g_sample_accum < kSampleIntervalSec) {
				UpdateStorageDonut();
				return;
			}
			g_sample_accum = 0.f;

			const float cpu = SampleCpuUsage();
			const float ram = SampleRamUsage();
			RefreshDriveList();
			UpdateStorageDonut();

			const size_t drive_count = g_drives.size();
			if (!g_history_filled) {
				FillHistory(g_cpu_history, kHistoryLen, cpu);
				FillHistory(g_ram_history, kHistoryLen, ram);
				for (size_t d = 0; d < drive_count && d < g_drive_usage_hist.size(); ++d) {
					const float usage = g_drives[d].valid ? g_drives[d].usage_ratio : 0.f;
					FillHistory(g_drive_usage_hist[d].data(), kHistoryLen, usage);
					g_prev_drive_usage[d] = usage;
				}
				g_history_filled = true;
			}
			else {
				PushHistorySample(g_cpu_history, kHistoryLen, cpu);
				PushHistorySample(g_ram_history, kHistoryLen, ram);
				for (size_t d = 0; d < drive_count && d < g_drive_usage_hist.size(); ++d) {
					const float usage = g_drives[d].valid ? g_drives[d].usage_ratio : 0.f;
					const float activity = ImClamp(fabsf(usage - g_prev_drive_usage[d]) * 8.f, 0.f, 1.f);
					g_prev_drive_usage[d] = usage;
					PushHistorySample(g_drive_usage_hist[d].data(), kHistoryLen, activity);
				}
			}
		}

		static void DrawHistoryPlot(const char* plot_id, const float* values, int count,
			const ImVec2& plot_size, bool shaded_fill)
		{
			if (count < 2) {
				return;
			}

			ImVec2 sz(
				ImMax(plot_size.x, 8.f),
				ImMax(plot_size.y, 8.f));
			if (sz.x < 2.f || sz.y < 2.f) {
				return;
			}

			ImGui::PushID(plot_id);
			ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0.f, 0.f));
			ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.f);

			const ImPlotFlags plot_flags = ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs
				| ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect
				| ImPlotFlags_NoMouseText | ImPlotFlags_NoFrame;
			if (ImPlot::BeginPlot("##hist", sz, plot_flags)) {
				ImPlot::SetupAxes(nullptr, nullptr,
					ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock
						| ImPlotAxisFlags_NoInitialFit,
					ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock
						| ImPlotAxisFlags_NoInitialFit);
				ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, static_cast<double>(count - 1), ImPlotCond_Always);
				ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Always);

				ImPlotSpec spec;
				spec.LineColor = cyan_mid();
				spec.LineWeight = 1.25f;
				spec.FillColor = ImVec4(0.f, 0.65f, 0.65f, 0.22f);
				spec.FillAlpha = 1.0f;
				spec.Flags = ImPlotItemFlags_NoLegend;

				if (shaded_fill) {
					ImPlot::PlotShaded("##fill", values, count, 0.0, 1.0, 0.0, spec);
				}
				ImPlot::PlotLine("##line", values, count, 1.0, 0.0, spec);
				ImPlot::EndPlot();
			}
			ImPlot::PopStyleVar(2);
			ImGui::PopID();
		}

		static void DrawGradientHLine(ImDrawList* dl, ImVec2 p0, ImVec2 p1)
		{
			const ImU32 col_left = ImGui::GetColorU32(ImVec4(0.f, 0.9f, 0.9f, 0.f));
			const ImU32 col_mid = ImGui::GetColorU32(cyan_neon());
			const ImU32 col_right = ImGui::GetColorU32(ImVec4(0.f, 0.9f, 0.9f, 0.f));
			const float mid_x = (p0.x + p1.x) * 0.5f;
			const float h = p1.y - p0.y;
			dl->AddRectFilledMultiColor(p0, ImVec2(mid_x, p0.y + h), col_left, col_mid, col_mid, col_left);
			dl->AddRectFilledMultiColor(ImVec2(mid_x, p0.y), p1, col_mid, col_right, col_right, col_mid);
		}

		static void DrawNeonRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, float thickness)
		{
			dl->AddRect(p0 - ImVec2(2.f, 2.f), p1 + ImVec2(2.f, 2.f), NeonGlow(0.06f), rounding + 1.f, 0, 2.0f);
			dl->AddRect(p0 - ImVec2(1.f, 1.f), p1 + ImVec2(1.f, 1.f), NeonGlow(0.14f), rounding, 0, 1.0f);
			dl->AddRect(p0, p1, ImGui::GetColorU32(cyan_dark()), rounding, 0, thickness);
		}

		static void DrawAccentBar(ImDrawList* dl, ImVec2 p0, ImVec2 p1, bool bright = false)
		{
			const float h = 2.0f;
			const ImU32 left = ImGui::GetColorU32(ImVec4(0.f, bright ? 0.95f : 0.7f, 0.9f, bright ? 0.5f : 0.25f));
			const ImU32 right = ImGui::GetColorU32(cyan_neon());
			dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + h), left, right, right, left);
		}

		static void DrawSectionHeader(const char* title, const char* subtitle = nullptr)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float line_w = ImGui::GetContentRegionAvail().x;
			ImGui::TextColored(cyan_neon(), "%s", title);
			if (subtitle != nullptr && subtitle[0] != '\0') {
				ImGui::SameLine();
				ImGui::TextDisabled("  %s", subtitle);
			}
			const float y = ImGui::GetCursorScreenPos().y + 4.f;
			DrawGradientHLine(ImGui::GetWindowDrawList(),
				ImVec2(start.x, y),
				ImVec2(start.x + line_w, y + 1.f));
			ImGui::Dummy(ImVec2(0.f, 8.f));
		}

		static void DrawPanelChrome(ImDrawList* dl, const ImRect& bb, bool hovered, float rounding = kRounding)
		{
			const ImU32 bg = ImGui::GetColorU32(hovered ? card_bg_hover() : panel_bg());
			dl->AddRectFilled(bb.Min, bb.Max, bg, rounding);
			DrawAccentBar(dl, bb.Min + ImVec2(1.f, 1.f), bb.Max - ImVec2(1.f, 0.f), hovered);
			dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(hovered ? cyan_neon() : cyan_dark()), rounding, 0, 1.0f);
		}

		static bool CyberTextButton(const char* id_suffix, const ImVec2& size, const char* label)
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const ImRect bb(pos, pos + size);
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImGuiID id = window->GetID(id_suffix);
			ImGui::ItemSize(size);
			if (!ImGui::ItemAdd(bb, id)) {
				ImGui::Dummy(size);
				return false;
			}

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

			const ImU32 border_col = ImGui::GetColorU32(hovered || held ? cyan_neon() : cyan_dark());
			const ImU32 bg_col = ImGui::GetColorU32(held ? active_bg() : (hovered ? hover_bg() : header_bg()));
			window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, kRounding);
			window->DrawList->AddRect(bb.Min, bb.Max, border_col, kRounding, 0, 1.0f);

			const ImVec2 text_size = ImGui::CalcTextSize(label);
			const ImVec2 text_pos(
				bb.Min.x + (bb.GetWidth() - text_size.x) * 0.5f,
				bb.Min.y + (bb.GetHeight() - text_size.y) * 0.5f);
			const ImU32 text_col = hovered
				? ImGui::GetColorU32(cyan_neon())
				: ImGui::GetColorU32(ImGuiCol_Text);
			window->DrawList->AddText(text_pos, text_col, label);
			return pressed;
		}

		static void DrawDonutChart(ImDrawList* dl, ImVec2 center, float outer_r, float inner_r,
			const DonutSegment* segments, int segment_count, const char* center_title)
		{
			float angle = -IM_PI * 0.5f;
			for (int i = 0; i < segment_count; ++i) {
				const float sweep = segments[i].fraction * IM_PI * 2.f;
				if (sweep < 0.001f) {
					continue;
				}
				const ImU32 col = ImGui::GetColorU32(segments[i].color);
				dl->PathClear();
				dl->PathArcTo(center, outer_r, angle, angle + sweep, 40);
				dl->PathArcTo(center, inner_r, angle + sweep, angle, 40);
				dl->PathFillConvex(col);
				angle += sweep;
			}

			dl->AddCircle(center, outer_r, ImGui::GetColorU32(cyan_dark()), 64, 1.5f);
			dl->AddCircle(center, inner_r, ImGui::GetColorU32(cyan_dark()), 64, 1.0f);

			const ImVec2 title_sz = ImGui::CalcTextSize(center_title);
			dl->AddText(
				ImVec2(center.x - title_sz.x * 0.5f, center.y - title_sz.y * 0.5f - 2.f),
				ImGui::GetColorU32(cyan_neon()), center_title);
		}

		static void DrawHorizontalBar(ImDrawList* dl, const ImRect& bar_rect, float fraction, bool dimmed)
		{
			fraction = ImClamp(fraction, 0.f, 1.f);
			dl->AddRectFilled(bar_rect.Min, bar_rect.Max, ImGui::GetColorU32(track_bg()), 3.0f);
			if (fraction > 0.001f && !dimmed) {
				const ImVec2 fill_max(bar_rect.Min.x + bar_rect.GetWidth() * fraction, bar_rect.Max.y);
				dl->AddRectFilled(bar_rect.Min, fill_max, ImGui::GetColorU32(cyan_mid()), 3.0f);
				dl->AddRectFilled(bar_rect.Min, fill_max, NeonGlow(0.15f), 3.0f);
			}
			dl->AddRect(bar_rect.Min, bar_rect.Max,
				ImGui::GetColorU32(dimmed ? ImVec4(0.15f, 0.15f, 0.15f, 1.f) : cyan_dark()), 3.0f, 0, 1.0f);
		}

		static void MetricBlock(const char* id_suffix, const char* label, float fraction,
			const float* history, const ImVec2& size)
		{
			ImGui::BeginChild(id_suffix, size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 panel_pos = ImGui::GetCursorScreenPos();
			const ImRect bb(panel_pos, panel_pos + inner);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
			DrawPanelChrome(dl, bb, hovered);

			fraction = ImClamp(fraction, 0.f, 1.f);
			char pct_buf[16];
			snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", fraction * 100.f);

			dl->AddText(bb.Min + ImVec2(12.f, 10.f), ImGui::GetColorU32(cyan_neon()), label);
			const ImVec2 pct_sz = ImGui::CalcTextSize(pct_buf);
			dl->AddText(
				ImVec2(bb.Max.x - 12.f - pct_sz.x, bb.Min.y + 10.f),
				ImGui::GetColorU32(hovered ? cyan_neon() : cyan_mid()), pct_buf);

			const float bar_h = 8.f;
			const ImVec2 bar_min(bb.Min.x + 12.f, bb.Min.y + 34.f);
			const ImVec2 bar_max(bb.Max.x - 12.f, bar_min.y + bar_h);
			DrawHorizontalBar(dl, ImRect(bar_min, bar_max), fraction, false);

			const ImVec2 spark_size(bb.GetWidth() - 20.f, bb.GetHeight() - (bar_max.y - bb.Min.y) - 18.f);
			ImGui::SetCursorPos(ImVec2(10.f, bar_max.y - bb.Min.y + 8.f));
			DrawHistoryPlot("##spark", history, kHistoryLen, spark_size, true);
			ImGui::EndChild();
		}

		static void RenderSystemPanel(const ImVec2& size)
		{
			ImGui::BeginChild("##MainSysPanel", size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 panel_pos = ImGui::GetCursorScreenPos();
			const ImRect panel_bb(panel_pos, panel_pos + inner);
			const bool panel_hovered = ImGui::IsMouseHoveringRect(panel_bb.Min, panel_bb.Max);
			DrawPanelChrome(ImGui::GetWindowDrawList(), panel_bb, panel_hovered);

			ImGui::SetCursorPos(ImVec2(14.f, 12.f));
			DrawSectionHeader(HTR(SectionSystem), HTR(SectionRealtime));

			const float mem_btn_h = 28.f;
			const float mem_footer_h = mem_btn_h + 8.f;
			const float blocks_area = inner.y - 52.f - mem_footer_h - kCardGap;
			const float block_h = ImMax(48.f, (blocks_area - kCardGap) * 0.5f);
			const ImVec2 block_size(inner.x - 28.f, block_h);

			ImGui::SetCursorPosX(14.f);
			MetricBlock("cpu_metric", "CPU", g_cpu_load, g_cpu_history, block_size);
			ImGui::Dummy(ImVec2(0.f, kCardGap));
			ImGui::SetCursorPosX(14.f);
			MetricBlock("ram_metric", "RAM", g_ram_load, g_ram_history, block_size);

			const MainPageMemoryStatus mem_st = MainPageMemory::GetStatus();
			const bool mem_busy = MainPageMemory::IsRunning() || mem_st.running;
			const float mem_btn_w = ImGui::CalcTextSize(HTR(FreeMemory)).x + 28.f;

			ImGui::SetCursorPos(ImVec2(14.f, inner.y - mem_btn_h - 6.f));
			ImGui::BeginGroup();
			if (CyberTextButton("##free_mem", ImVec2(mem_btn_w, mem_btn_h),
				mem_busy ? HTR(Processing) : HTR(FreeMemory))
				&& !mem_busy) {
				MainPageMemory::RequestRelease();
				HLOG_INFO("MainPage: release memory requested");
			}
			if (mem_st.has_result && !mem_busy && mem_st.message[0] != '\0') {
				ImGui::SameLine(0.f, 10.f);
				ImGui::TextDisabled("%s", mem_st.message);
				if (!HCleanIsRunningAsAdmin() && !mem_st.system_purge_ok) {
					ImGui::TextDisabled("%s", HTR(AdminCacheHint));
				}
			}
			else if (!HCleanIsRunningAsAdmin() && !mem_busy) {
				ImGui::SameLine(0.f, 10.f);
				ImGui::TextDisabled("%s", HTR(MemoryHint));
			}
			ImGui::EndGroup();

			ImGui::EndChild();
		}

		static void RenderFilesPanel(const ImVec2& size)
		{
			ImGui::BeginChild("##MainFilesPanel", size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 panel_pos = ImGui::GetCursorScreenPos();
			const ImRect panel_bb(panel_pos, panel_pos + inner);
			const bool panel_hovered = ImGui::IsMouseHoveringRect(panel_bb.Min, panel_bb.Max);
			DrawPanelChrome(ImGui::GetWindowDrawList(), panel_bb, panel_hovered);

			ImGui::SetCursorPos(ImVec2(14.f, 12.f));
			const char* files_sub = g_storage_subtitle[0] != '\0' ? g_storage_subtitle : nullptr;
			DrawSectionHeader(HTR(SectionSystemFiles), files_sub);

			const float btn_h = 30.f;
			const float rescan_w = 88.f;
			const char* btn_label = HTR(GotoClearTool);
			const float btn_w = ImGui::CalcTextSize(btn_label).x + 28.f;

			ImGui::SetCursorPosX(14.f);
			ImGui::BeginGroup();
			if (CyberTextButton("##rescan_storage", ImVec2(rescan_w, btn_h), HTR(Rescan))) {
				HLOG_DEBUG("MainPage: storage rescan clicked");
				MainPageDiskScan::RequestRescan();
			}
			ImGui::SameLine();
			ImGui::SetCursorPosX(inner.x - 14.f - btn_w);
			if (CyberTextButton("##goto_clear", ImVec2(btn_w, btn_h), btn_label)) {
				HLOG_DEBUG("MainPage: navigate to ClearPage clicked");
				open_page("ClearPage");
			}
			ImGui::EndGroup();
			ImGui::Dummy(ImVec2(0.f, 8.f));

			const MainPageDiskScan::Snapshot snap = MainPageDiskScan::GetSnapshot();
			const float progress_block_h = snap.scanning ? 36.f : 0.f;
			const float chart_area_h = ImMax(72.f, inner.y - ImGui::GetCursorPosY() - 8.f);
			const float content_w = inner.x - 28.f;

			ImGui::SetCursorPosX(14.f);
			ImGui::Dummy(ImVec2(content_w, chart_area_h));
			const ImRect chart_bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			char center_title[32] = "C:";
			if (snap.drive_root[0] != L'\0') {
				WideCharToMultiByte(CP_UTF8, 0, snap.drive_root, 2, center_title,
					sizeof(center_title), nullptr, nullptr);
			}

			const ImVec2 chart_center(
				chart_bb.Min.x + chart_bb.GetWidth() * 0.38f,
				chart_bb.Min.y + progress_block_h + (chart_bb.GetHeight() - progress_block_h) * 0.5f);
			const float outer_r = ImMin(chart_bb.GetWidth() * 0.34f, chart_bb.GetHeight() * 0.38f);
			const float inner_r = outer_r * 0.58f;

			if (snap.scanning) {
				const char* status = snap.status_text[0] != '\0' ? I18N(snap.status_text) : HTR(Scanning);
				const ImVec2 status_sz = ImGui::CalcTextSize(status);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(chart_center.x - status_sz.x * 0.5f, chart_center.y - status_sz.y - 14.f),
					ImGui::GetColorU32(cyan_neon()), status);

				const ImRect prog_bb(
					ImVec2(chart_center.x - outer_r, chart_center.y + 4.f),
					ImVec2(chart_center.x + outer_r, chart_center.y + 16.f));
				DrawHorizontalBar(ImGui::GetWindowDrawList(), prog_bb,
					ImClamp(snap.progress, 0.f, 1.f), false);

				char pct_buf[16];
				snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", snap.progress * 100.f);
				const ImVec2 pct_sz = ImGui::CalcTextSize(pct_buf);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(chart_center.x - pct_sz.x * 0.5f, chart_center.y + 20.f),
					ImGui::GetColorU32(cyan_mid()), pct_buf);
			}
			else if (g_storage_segment_count <= 0) {
				const char* wait = "—";
				const ImVec2 wait_sz = ImGui::CalcTextSize(wait);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(chart_center.x - wait_sz.x * 0.5f, chart_center.y - wait_sz.y * 0.5f),
					ImGui::GetColorU32(cyan_neon()), wait);
			}
			else {
				DrawDonutChart(ImGui::GetWindowDrawList(), chart_center, outer_r, inner_r,
					g_storage_segments, g_storage_segment_count, center_title);
			}

			const float legend_x = chart_bb.Min.x + chart_bb.GetWidth() * 0.52f;
			float legend_y = chart_bb.Min.y + 8.f + progress_block_h;
			const int legend_count = snap.scanning ? 6 : g_storage_segment_count;
			for (int i = 0; i < legend_count; ++i) {
				if (!snap.scanning && g_storage_segments[i].fraction < 0.001f) {
					continue;
				}
				const ImVec4 legend_col = snap.scanning
					? StorageCategoryColor(i)
					: g_storage_segments[i].color;
				const ImVec2 dot_min(legend_x, legend_y + 4.f);
				const ImVec2 dot_max(legend_x + 10.f, legend_y + 14.f);
				ImGui::GetWindowDrawList()->AddRectFilled(dot_min, dot_max,
					ImGui::GetColorU32(legend_col), 2.f);
				char row[96];
				if (snap.scanning) {
					static const Hi18n::Key kLegendKeys[6] = {
						Hi18n::Key::LegendSystem,
						Hi18n::Key::LegendApps,
						Hi18n::Key::LegendUser,
						Hi18n::Key::LegendTemp,
						Hi18n::Key::LegendOther,
						Hi18n::Key::LegendFree,
					};
					snprintf(row, sizeof(row), "%s", Hi18n::Tr(kLegendKeys[i]));
				}
				else {
					snprintf(row, sizeof(row), "%s  %.1f GB  %.0f%%",
						I18N(g_storage_segments[i].label),
						g_storage_segments[i].gb,
						g_storage_segments[i].fraction * 100.f);
				}
				const ImU32 row_col = snap.scanning
					? ImGui::GetColorU32(ImGuiCol_TextDisabled)
					: ImGui::GetColorU32(ImGuiCol_Text);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(legend_x + 16.f, legend_y), row_col, row);
				legend_y += ImGui::GetTextLineHeight() + 6.f;
			}

			ImGui::EndChild();
		}

		static void DriveCard(const char* id_suffix, const DriveInfo& drive,
			const float* activity_hist, const ImVec2& size, bool compact)
		{
			ImGui::BeginChild(id_suffix, size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 panel_pos = ImGui::GetCursorScreenPos();
			const ImRect bb(panel_pos, panel_pos + inner);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
			DrawPanelChrome(dl, bb, hovered);

			const float pad = compact ? 10.f : 14.f;
			dl->AddText(bb.Min + ImVec2(pad, 10.f),
				ImGui::GetColorU32(cyan_neon()), drive.title);

			char usage[48];
			snprintf(usage, sizeof(usage), "%.1f/%.1f GB", drive.used_gb, drive.total_gb);
			dl->AddText(
				ImVec2(bb.Min.x + pad, bb.Min.y + 10.f + ImGui::GetTextLineHeight()),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), usage);

			const float activity = activity_hist != nullptr
				? activity_hist[kHistoryLen - 1] : 0.f;
			char pct_buf[24];
			snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", drive.usage_ratio * 100.f);
			const ImVec2 pct_sz = ImGui::CalcTextSize(pct_buf);
			dl->AddText(
				ImVec2(bb.Max.x - pad - pct_sz.x, bb.Min.y + 10.f),
				ImGui::GetColorU32(cyan_neon()), pct_buf);

			if (!compact) {
				const char* pct_suffix = HTR(UsedSuffix);
				const ImVec2 suffix_sz = ImGui::CalcTextSize(pct_suffix);
				dl->AddText(
					ImVec2(bb.Max.x - pad - suffix_sz.x, bb.Min.y + 10.f + ImGui::GetTextLineHeight()),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), pct_suffix);

				char act_buf[24];
				snprintf(act_buf, sizeof(act_buf), HTR(ActivityPctFmt), activity * 100.f);
				const ImVec2 act_sz = ImGui::CalcTextSize(act_buf);
				dl->AddText(
					ImVec2(bb.Max.x - pad - act_sz.x, bb.Min.y + 10.f + ImGui::GetTextLineHeight() * 2.f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), act_buf);

				const float spark_y = inner.y * 0.52f;
				const float spark_h = inner.y * 0.24f;
				ImGui::SetCursorPos(ImVec2(pad, spark_y));
				DrawHistoryPlot("##spark", activity_hist, kHistoryLen,
					ImVec2(inner.x - pad * 2.f, spark_h), false);
			}

			const float bar_h = compact ? 8.f : 10.f;
			const ImVec2 bar_min(bb.Min.x + pad, bb.Max.y - bar_h - 12.f);
			const ImVec2 bar_max(bb.Max.x - pad, bb.Max.y - 12.f);
			DrawHorizontalBar(dl, ImRect(bar_min, bar_max), drive.usage_ratio, false);
			ImGui::EndChild();
		}

		static void FormatLocalTimeShort(int64_t unix_ms, char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}
			const time_t sec = static_cast<time_t>(unix_ms / 1000);
			struct tm local_tm = {};
			localtime_s(&local_tm, &sec);
			strftime(out, out_size, "%m/%d %H:%M", &local_tm);
		}

		static float HeroBarHeight(float content_w)
		{
			return content_w < 520.f ? 104.f : 76.f;
		}

		static void RenderHeroBar(const ImVec2& size)
		{
			ImGui::BeginChild("##MainHero", size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImRect bb(p0, p0 + inner);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
			DrawPanelChrome(dl, bb, hovered);

			const float btn_h = 30.f;
			const float btn_gap = 8.f;
			const char* labels[] = { HTR(HeroClear), HTR(HeroHistory), HTR(HeroDisk) };
			const char* btn_ids[] = { "##hero_clear", "##hero_history", "##hero_disk" };
			const char* pages[] = { "ClearPage", "HistoryPage", "DiskHealthPage" };

			float buttons_w = 0.f;
			for (int i = 0; i < 3; ++i) {
				buttons_w += ImGui::CalcTextSize(labels[i]).x + 24.f;
				if (i > 0) {
					buttons_w += btn_gap;
				}
			}
			const bool compact = inner.x < 520.f
				|| inner.x < buttons_w + 260.f;

			if (compact) {
				dl->AddText(bb.Min + ImVec2(16.f, 10.f), ImGui::GetColorU32(cyan_neon()), "HP CLEANER++");
				dl->AddText(bb.Min + ImVec2(16.f, 10.f + ImGui::GetTextLineHeight()),
					ImGui::GetColorU32(ImGuiCol_TextDisabled),
					HTR(HeroSubtitle));
				float btn_x = bb.Min.x + 16.f;
				const float btn_y = bb.Min.y + 10.f + ImGui::GetTextLineHeight() * 2.f + 8.f;
				for (int i = 0; i < 3; ++i) {
					const float btn_w = ImGui::CalcTextSize(labels[i]).x + 24.f;
					ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
					if (CyberTextButton(btn_ids[i], ImVec2(btn_w, btn_h), labels[i])) {
						open_page(pages[i]);
					}
					btn_x += btn_w + btn_gap;
				}
			}
			else {
				dl->AddText(bb.Min + ImVec2(16.f, 12.f), ImGui::GetColorU32(cyan_neon()), "HP CLEANER++");
				dl->AddText(bb.Min + ImVec2(16.f, 12.f + ImGui::GetTextLineHeight()),
					ImGui::GetColorU32(ImGuiCol_TextDisabled),
					HTR(HeroSubtitle));

				float btn_x = bb.Max.x - 16.f;
				const float btn_y = bb.Min.y + 14.f;
				for (int i = 2; i >= 0; --i) {
					const float btn_w = ImGui::CalcTextSize(labels[i]).x + 24.f;
					btn_x -= btn_w;
					ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
					if (CyberTextButton(btn_ids[i], ImVec2(btn_w, btn_h), labels[i])) {
						open_page(pages[i]);
					}
					btn_x -= btn_gap;
				}
			}

			ImGui::EndChild();
		}

		static float QuickStatsRowHeight(float content_w)
		{
			if (content_w < 500.f) {
				return 64.f * 3.f + kCardGap * 2.f;
			}
			return 72.f;
		}

		static void RenderQuickStatsRow(const ImVec2& size)
		{
			ImGui::BeginChild("##MainQuickStats", size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const float gap = kCardGap;
			const bool vertical = inner.x < 500.f;
			const float card_w = vertical ? inner.x : (inner.x - gap * 2.f) / 3.f;
			const float card_h = vertical ? 64.f : inner.y;

			CleanHistorySummary hist{};
			CleanHistory::GetSummary(&hist);

			HCleanSizeSummary clean_sum{};
			GetCleanTasksSizeSummary(&clean_sum);

			auto stat_card = [&](const char* id, const char* title, const char* main_val, const char* sub) {
				ImGui::BeginChild(id, ImVec2(card_w, card_h), false, ImGuiWindowFlags_NoScrollbar);
				const ImVec2 cp0 = ImGui::GetCursorScreenPos();
				const ImRect cbb(cp0, cp0 + ImGui::GetContentRegionAvail());
				ImDrawList* cdl = ImGui::GetWindowDrawList();
				const bool hov = ImGui::IsMouseHoveringRect(cbb.Min, cbb.Max);
				DrawPanelChrome(cdl, cbb, hov);
				cdl->AddText(cbb.Min + ImVec2(12.f, 10.f), ImGui::GetColorU32(cyan_neon()), title);
				cdl->AddText(cbb.Min + ImVec2(12.f, 28.f), ImGui::GetColorU32(ImGuiCol_Text), main_val);
				if (sub != nullptr && sub[0] != '\0') {
					const float sub_y = vertical ? 46.f : 48.f;
					cdl->AddText(cbb.Min + ImVec2(12.f, sub_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
				}
				ImGui::EndChild();
			};

			char reclaim_buf[32];
			if (clean_sum.selected_has_unscanned && clean_sum.selected_bytes <= 0) {
				strncpy_s(reclaim_buf, HTR(StatShowAfterScan), _TRUNCATE);
			}
			else {
				FormatCleanSize(clean_sum.selected_bytes, reclaim_buf, sizeof(reclaim_buf));
			}

			char total_hist[32];
			FormatCleanSize(hist.total_freed_bytes, total_hist, sizeof(total_hist));

			char last_main[32] = "—";
			char last_sub[40] = {};
			strncpy_s(last_sub, HTR(StatNotCleanedYet), _TRUNCATE);
			if (hist.has_last) {
				FormatCleanSize(hist.last.freed_bytes, last_main, sizeof(last_main));
				FormatLocalTimeShort(hist.last.unix_ms, last_sub, sizeof(last_sub));
			}

			char visible_buf[32];
			FormatCleanSize(clean_sum.visible_total_bytes, visible_buf, sizeof(visible_buf));

			stat_card("##qs_reclaim", HTR(StatReclaimable), reclaim_buf, HTR(StatReclaimHint));
			if (!vertical) {
				ImGui::SameLine(0.f, gap);
			}
			stat_card("##qs_last", HTR(StatLastFreed), last_main, last_sub);
			if (!vertical) {
				ImGui::SameLine(0.f, gap);
			}
			stat_card("##qs_total", HTR(StatTotalFreed), total_hist, visible_buf);

			ImGui::EndChild();
		}

		static void ComputeDashboardHeights(float avail_y, float content_w,
			float* hero_h, float* stats_h, float* mid_h, float* disk_h)
		{
			*hero_h = HeroBarHeight(content_w);
			*stats_h = QuickStatsRowHeight(content_w);
			const float gaps = kSectionGap * 3.f;
			float remain = avail_y - *hero_h - *stats_h - gaps;
			if (remain < 48.f) {
				remain = 48.f;
			}
			*disk_h = ImMax(72.f, remain * 0.38f);
			*mid_h = remain - *disk_h;
			if (*mid_h < 72.f) {
				*mid_h = 72.f;
				*disk_h = ImMax(72.f, remain - *mid_h);
			}

			const float total = *hero_h + *stats_h + *mid_h + *disk_h + gaps;
			if (total > avail_y && total > 1.f) {
				const float scale = (avail_y - gaps) / (*hero_h + *stats_h + *mid_h + *disk_h);
				*hero_h *= scale;
				*stats_h *= scale;
				*mid_h *= scale;
				*disk_h *= scale;
			}
		}

		static void RenderDiskRow(const ImVec2& size)
		{
			ImGui::BeginChild("##MainDiskRow", size, false, ImGuiWindowFlags_NoScrollbar);
			const ImVec2 inner = ImGui::GetContentRegionAvail();
			const ImVec2 panel_pos = ImGui::GetCursorScreenPos();
			const ImRect panel_bb(panel_pos, panel_pos + inner);
			const bool panel_hovered = ImGui::IsMouseHoveringRect(panel_bb.Min, panel_bb.Max);
			DrawPanelChrome(ImGui::GetWindowDrawList(), panel_bb, panel_hovered);

			ImGui::SetCursorPos(ImVec2(14.f, 12.f));
			char disk_sub[48];
			snprintf(disk_sub, sizeof(disk_sub), HTR(DisksRealtimeFmt), g_drives.size());
			DrawSectionHeader(HTR(SectionDisks), disk_sub);

			const size_t drive_count = g_drives.size();
			const bool compact = drive_count > 4;
			constexpr float kMinCardW = 140.f;
			const float card_w = ImMax(kMinCardW, compact ? kCompactCardW : kNormalCardW);
			const float scroll_w = ImMax(inner.x - 28.f, 80.f);
			constexpr float kHeaderBottom = 44.f;
			const float card_h = ImMax(72.f, inner.y - kHeaderBottom - 8.f);
			const float gap = kCardGap;
			const float row_content_w = drive_count > 0
				? static_cast<float>(drive_count) * card_w
					+ static_cast<float>(drive_count > 1 ? drive_count - 1 : 0) * gap
				: scroll_w;
			const float row_offset_x = (drive_count > 0 && row_content_w < scroll_w)
				? (scroll_w - row_content_w) * 0.5f
				: 0.f;

			const float scroll_content_w = row_offset_x + row_content_w + 8.f;

			ImGui::SetCursorPos(ImVec2(14.f, kHeaderBottom));
			const bool disk_scroll_needed = scroll_content_w > scroll_w + 2.f;
			ImGuiWindowFlags disk_scroll_flags = ImGuiWindowFlags_NoScrollbar;
			if (disk_scroll_needed) {
				disk_scroll_flags |= ImGuiWindowFlags_HorizontalScrollbar;
			}
			ImGui::BeginChild("##disk_card_scroll", ImVec2(scroll_w, card_h), false, disk_scroll_flags);
			if (disk_scroll_needed
				&& ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
				ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 48.f);
			}

			if (row_offset_x > 0.5f) {
				ImGui::Dummy(ImVec2(row_offset_x, card_h));
				ImGui::SameLine(0.f, 0.f);
			}

			for (size_t i = 0; i < drive_count; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.f, gap);
				}
				char id[32];
				snprintf(id, sizeof(id), "##drive_%zu", i);
				const float* hist = (i < g_drive_usage_hist.size())
					? g_drive_usage_hist[i].data() : nullptr;
				DriveCard(id, g_drives[i], hist, ImVec2(card_w, card_h), compact);
			}

			if (drive_count == 0) {
				ImGui::Dummy(ImVec2(scroll_w, card_h));
				const ImRect empty_bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				const char* msg = HTR(NoDrivesDetected);
				const ImVec2 msg_sz = ImGui::CalcTextSize(msg);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(empty_bb.Min.x + 12.f, empty_bb.GetCenter().y - msg_sz.y * 0.5f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
			}
			else if (disk_scroll_needed) {
				const float used_x = ImGui::GetCursorPosX();
				const float tail_w = ImMax(8.f, scroll_content_w - used_x);
				ImGui::SameLine(0.f, 0.f);
				ImGui::Dummy(ImVec2(tail_w, card_h));
			}

			ImGui::EndChild();
			ImGui::EndChild();
		}
	}

	void DrawContentBorder()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window == nullptr) {
			return;
		}
		const ImVec2 p0 = window->Pos + ImVec2(kBorderPad, kBorderPad);
		const ImVec2 p1 = window->Pos + window->Size - ImVec2(kBorderPad, kBorderPad);
		DrawNeonRect(window->DrawList, p0, p1, kRounding, 1.0f);
	}

	void RenderCentralDashboard()
	{
		UpdateSystemMetrics();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float pad = 6.f;
		const float content_w = ImMax(80.f, avail.x - pad * 2.f);

		float hero_h = 76.f;
		float stats_h = 72.f;
		float mid_h = 100.f;
		float disk_h = 120.f;
		ComputeDashboardHeights(avail.y, content_w, &hero_h, &stats_h, &mid_h, &disk_h);

		ImGui::PushClipRect(
			ImGui::GetCursorScreenPos(),
			ImGui::GetCursorScreenPos() + avail,
			false);
		const float inner_w = ImMax(80.f, content_w);
		float y = 0.f;

		ImGui::SetCursorPos(ImVec2(pad, y));
		RenderHeroBar(ImVec2(inner_w, hero_h));
		y += hero_h + kSectionGap;

		ImGui::SetCursorPos(ImVec2(pad, y));
		RenderQuickStatsRow(ImVec2(inner_w, stats_h));
		y += stats_h + kSectionGap;

		const bool stack_panels = inner_w < 560.f;
		const float row_h = mid_h;
		if (stack_panels) {
			ImGui::SetCursorPos(ImVec2(pad, y));
			RenderSystemPanel(ImVec2(inner_w, row_h * 0.48f));
			y += row_h * 0.48f + kSectionGap;
			ImGui::SetCursorPos(ImVec2(pad, y));
			RenderFilesPanel(ImVec2(inner_w, row_h * 0.52f));
			y += row_h * 0.52f + kSectionGap;
		}
		else {
			const float left_w = inner_w * 0.42f;
			const float right_w = inner_w - left_w - kSectionGap;
			ImGui::SetCursorPos(ImVec2(pad, y));
			RenderSystemPanel(ImVec2(left_w, row_h));
			ImGui::SameLine(0.f, kSectionGap);
			RenderFilesPanel(ImVec2(right_w, row_h));
			y += row_h + kSectionGap;
		}

		ImGui::SetCursorPos(ImVec2(pad, y));
		RenderDiskRow(ImVec2(inner_w, disk_h));
		ImGui::Dummy(ImVec2(inner_w + pad * 2.f, 1.f));
		ImGui::PopClipRect();
	}

	void RenderMainPageFooter()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 line_p0(window->Pos.x + 12.f, window->Pos.y + 2.f);
		const ImVec2 line_p1(window->Pos.x + window->Size.x - 12.f, line_p0.y + 1.f);
		DrawGradientHLine(window->DrawList, line_p0, line_p1);

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
		const float footer_logo_y = ImGui::GetCursorPosY();
		float footer_text_x = window->Pos.x + 12.f;
		if (Logo::HPS_Logo.texture != 0) {
			ImGui::SetCursorPos(ImVec2(12.f, footer_logo_y));
			ImGui::Image((ImTextureID)(intptr_t)Logo::HPS_Logo.texture,
				ImVec2(ClearPageUI::kFooterLogoW, ClearPageUI::kFooterLogoH));
			footer_text_x += ClearPageUI::kFooterLogoW + 12.f;
		}
		else {
			ImGui::SetCursorPosX(12.f);
			ImGui::TextDisabled("[O] HALF PEOPLE TECH");
			footer_text_x = ImGui::GetItemRectMax().x + 8.f;
		}

		CleanHistorySummary hist{};
		CleanHistory::GetSummary(&hist);
		char status[160];
		if (hist.has_last) {
			char freed[24];
			FormatCleanSize(hist.last.freed_bytes, freed, sizeof(freed));
			snprintf(status, sizeof(status), HTR(FooterMonitoringFmt),
				freed, hist.session_count);
		}
		else {
			strncpy_s(status, HTR(FooterNoHistory), _TRUNCATE);
		}
		constexpr float kLangComboW = 168.f;
		const float combo_x = avail.x - kLangComboW - 8.f;
		ImGui::SetCursorPos(ImVec2(combo_x, footer_logo_y + 2.f));
		Hi18n::DrawLanguageCombo("##main_page_lang_combo", kLangComboW);

		const ImVec2 status_sz = ImGui::CalcTextSize(status);
		const float status_right = combo_x - 10.f;
		const float status_x = window->Pos.x + status_right - status_sz.x;
		const float status_y = footer_logo_y
			+ (ClearPageUI::kFooterLogoH - status_sz.y) * 0.5f;
		if (status_x > footer_text_x + 8.f) {
			window->DrawList->AddText(ImVec2(status_x, status_y),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), status);
		}
		else {
			ImGui::SetCursorPos(ImVec2(12.f, footer_logo_y + ClearPageUI::kFooterLogoH + 2.f));
			ImGui::TextDisabled("%s", status);
		}
	}
}
