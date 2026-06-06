#define IMGUI_DEFINE_MATH_OPERATORS
#include "AboutAppLog.h"
#include "Hi18n.h"
#include "HAppPaths.h"
#include "HLogRing.h"
#include "HPage.h"
#include "HUiTheme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace {
	using namespace HUiTheme;

	enum class LogLineLevel { Trace, Debug, Info, Warn, Error, Unknown };

	struct LogViewState {
		std::string source_label;
		std::string file_path;
		std::vector<std::string> lines;
	};

	bool g_auto_scroll = true;
	bool g_live_refresh = true;
	float g_log_scroll_y = 0.f;
	double g_last_refresh_time = 0.0;
	LogViewState g_view = {};

	LogLineLevel ParseLineLevel(const std::string& line)
	{
		std::string lower = line;
		for (char& c : lower) {
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}
		if (lower.find("[error]") != std::string::npos || lower.find("[err]") != std::string::npos) {
			return LogLineLevel::Error;
		}
		if (lower.find("[warning]") != std::string::npos || lower.find("[warn]") != std::string::npos) {
			return LogLineLevel::Warn;
		}
		if (lower.find("[info]") != std::string::npos) {
			return LogLineLevel::Info;
		}
		if (lower.find("[debug]") != std::string::npos) {
			return LogLineLevel::Debug;
		}
		if (lower.find("[trace]") != std::string::npos) {
			return LogLineLevel::Trace;
		}
		return LogLineLevel::Unknown;
	}

	ImVec4 ColorForLevel(LogLineLevel level)
	{
		switch (level) {
		case LogLineLevel::Error: return ImVec4(1.f, 0.42f, 0.35f, 1.f);
		case LogLineLevel::Warn: return ImVec4(1.f, 0.78f, 0.28f, 1.f);
		case LogLineLevel::Info: return ImVec4(0.75f, 0.92f, 0.92f, 1.f);
		case LogLineLevel::Debug: return ImVec4(0.55f, 0.72f, 0.78f, 1.f);
		case LogLineLevel::Trace: return ImVec4(0.45f, 0.58f, 0.62f, 1.f);
		default: return ImVec4(0.7f, 0.8f, 0.8f, 1.f);
		}
	}

	bool ReadTailShared(const std::wstring& path_wide, size_t max_bytes, std::string& out)
	{
		out.clear();
		const HANDLE file = CreateFileW(path_wide.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
			CloseHandle(file);
			return true;
		}

		const DWORD to_read = static_cast<DWORD>(
			(size.QuadPart > static_cast<LONGLONG>(max_bytes))
			? max_bytes : size.QuadPart);
		const LONGLONG start = size.QuadPart - to_read;

		LARGE_INTEGER seek = {};
		seek.QuadPart = start;
		SetFilePointerEx(file, seek, nullptr, FILE_BEGIN);

		std::vector<char> buf(to_read);
		DWORD read_bytes = 0;
		const BOOL ok = ReadFile(file, buf.data(), to_read, &read_bytes, nullptr);
		CloseHandle(file);
		if (!ok || read_bytes == 0) {
			return false;
		}

		out.assign(buf.data(), read_bytes);
		if (start > 0) {
			const size_t nl = out.find('\n');
			if (nl != std::string::npos && nl + 1 < out.size()) {
				out.erase(0, nl + 1);
			}
		}
		return true;
	}

	void SplitLines(const std::string& text, std::vector<std::string>& out_lines)
	{
		out_lines.clear();
		size_t start = 0;
		while (start < text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (!line.empty()) {
				out_lines.push_back(std::move(line));
			}
			start = end + 1;
		}
	}

	void Utf8ToWide(const std::string& utf8, std::wstring& out_wide)
	{
		out_wide.clear();
		if (utf8.empty()) {
			return;
		}
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		if (wlen <= 0) {
			return;
		}
		std::vector<wchar_t> wbuf(static_cast<size_t>(wlen));
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wbuf.data(), wlen);
		out_wide = wbuf.data();
	}

	static void DrawLogLineAt(int index, const ImVec2& pos, const std::string& line, float width)
	{
		const float line_h = ImGui::GetTextLineHeightWithSpacing();
		const ImRect bb(pos, pos + ImVec2(width, line_h));
		ImDrawList* line_dl = ImGui::GetWindowDrawList();
		if ((index % 2) == 1) {
			line_dl->AddRectFilled(bb.Min, bb.Max,
				ImGui::GetColorU32(ImVec4(0.04f, 0.06f, 0.06f, 1.f)));
		}
		const LogLineLevel lv = ParseLineLevel(line);
		line_dl->AddText(
			ImVec2(bb.Min.x + 4.f, bb.Min.y),
			ImGui::GetColorU32(ColorForLevel(lv)),
			line.c_str());
	}
}

namespace AboutAppLog {
	void Refresh()
	{
		HLogRingFlushFileSink();

		const size_t ring_count = HLogRingLineCount();
		const std::vector<std::string> ring_lines = HLogRingLastFormatted(600);
		if (!ring_lines.empty()) {
			g_view.source_label = u8"即時 Logger（記憶體緩衝）";
			g_view.file_path = HAppPaths::GetCurrentDailyLogFilePath();
			g_view.lines = ring_lines;
			return;
		}

		g_view.file_path = HAppPaths::GetLatestLogFilePath();
		const std::string expected = HAppPaths::GetCurrentDailyLogFilePath();

		std::wstring path_wide;
		Utf8ToWide(g_view.file_path, path_wide);

		std::string file_text;
		const bool read_ok = !path_wide.empty() && ReadTailShared(path_wide, 128 * 1024, file_text);

		if (read_ok && !file_text.empty()) {
			g_view.source_label = u8"日誌檔案";
			SplitLines(file_text, g_view.lines);
			return;
		}

		g_view.source_label = u8"（尚無日誌）";
		g_view.lines.clear();
		char msg[512] = {};
		snprintf(msg, sizeof(msg),
			I18N(u8"目前沒有可顯示的日誌。\n"
				u8"• 記憶體緩衝：%zu 行\n"
				u8"• 預期檔案：%s\n"
				u8"• 搜尋路徑：%s\n"
				u8"請在程式中操作一段時間後按「重新整理」，或確認 %%APPDATA%%\\HalfPeople\\HP CLEANER++\\logs 目錄可寫入。"),
			ring_count,
			expected.c_str(),
			g_view.file_path.c_str());
		g_view.lines.push_back(msg);
	}

	void Draw(float content_width)
	{
		if (g_live_refresh) {
			const double now = ImGui::GetTime();
			if (now - g_last_refresh_time >= 0.5) {
				g_last_refresh_time = now;
				Refresh();
			}
		}

		const float log_w = ImMax(120.f, content_width);
		const float line_h = ImGui::GetTextLineHeightWithSpacing();
		const float log_h = ImMax(180.f, line_h * 14.f);

		ImGui::TextColored(cyan_neon(), "%s", I18N(u8"執行日誌"));

		char status[96] = {};
		snprintf(status, sizeof(status), I18N(u8"%s · %zu 行"),
			I18N(g_view.source_label.c_str()), g_view.lines.size());
		ImGui::TextDisabled("%s", status);
		if (!g_view.file_path.empty()) {
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + log_w - 8.f);
			ImGui::TextDisabled(I18N(u8"檔案：%s"), g_view.file_path.c_str());
			ImGui::PopTextWrapPos();
		}

		if (ImGui::Button("重新整理")) {
			Refresh();
		}
		ImGui::SameLine();
		ImGui::Checkbox("即時更新", &g_live_refresh);
		ImGui::SameLine();
		ImGui::Checkbox("自動捲動", &g_auto_scroll);

		const ImVec2 scroll_pos = ImGui::GetCursorScreenPos();
		const ImVec2 scroll_size(log_w, log_h);
		const ImRect scroll_bb(scroll_pos, scroll_pos + scroll_size);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(scroll_bb.Min, scroll_bb.Max,
			ImGui::GetColorU32(ImVec4(0.02f, 0.03f, 0.03f, 1.f)), 4.f);
		dl->AddRect(scroll_bb.Min, scroll_bb.Max, ImGui::GetColorU32(cyan_dark()), 4.f, 0, 1.f);

		ImGui::SetCursorScreenPos(scroll_pos);
		ImGui::InvisibleButton("##about_log_scroll", scroll_size);
		if (ImGui::IsItemHovered()) {
			g_log_scroll_y -= ImGui::GetIO().MouseWheel * line_h * 3.f;
		}

		const ImRect clip_inner(scroll_bb.Min + ImVec2(6.f, 6.f), scroll_bb.Max - ImVec2(6.f, 6.f));
		const float inner_w = ImMax(32.f, clip_inner.GetWidth());
		const float inner_h = ImMax(32.f, clip_inner.GetHeight());
		const float content_h = line_h * static_cast<float>(g_view.lines.size());
		const float max_scroll = ImMax(0.f, content_h - inner_h);
		if (g_auto_scroll) {
			g_log_scroll_y = max_scroll;
		}
		g_log_scroll_y = ImClamp(g_log_scroll_y, 0.f, max_scroll);

		dl->PushClipRect(clip_inner.Min, clip_inner.Max, true);
		ImVec2 line_pos(clip_inner.Min.x, clip_inner.Min.y - g_log_scroll_y);
		for (int i = 0; i < static_cast<int>(g_view.lines.size()); ++i) {
			const ImRect line_bb(line_pos, line_pos + ImVec2(inner_w, line_h));
			if (line_bb.Max.y >= clip_inner.Min.y && line_bb.Min.y <= clip_inner.Max.y) {
				DrawLogLineAt(i, line_pos, g_view.lines[static_cast<size_t>(i)], inner_w);
			}
			line_pos.y += line_h;
		}
		dl->PopClipRect();

		ImGui::SetCursorScreenPos(ImVec2(scroll_pos.x, scroll_bb.Max.y));
	}
}
