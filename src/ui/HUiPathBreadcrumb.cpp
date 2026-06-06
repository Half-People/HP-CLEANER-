#define IMGUI_DEFINE_MATH_OPERATORS
#include "HUiPathBreadcrumb.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include <imgui_internal.h>
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
	struct Segment {
		std::wstring path;
		std::string label_utf8;
	};

	std::string WideToUtf8(const wchar_t* wide)
	{
		if (wide == nullptr || wide[0] == L'\0') {
			return {};
		}
		const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (needed <= 0) {
			return {};
		}
		std::vector<char> buf(static_cast<size_t>(needed));
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf.data(), needed, nullptr, nullptr);
		return std::string(buf.data());
	}

	void PushSegment(std::vector<Segment>& out, const std::wstring& path, const std::wstring& label_wide)
	{
		if (path.empty()) {
			return;
		}
		Segment seg;
		seg.path = path;
		seg.label_utf8 = WideToUtf8(label_wide.c_str());
		if (seg.label_utf8.empty()) {
			seg.label_utf8 = WideToUtf8(path.c_str());
		}
		out.push_back(std::move(seg));
	}

	void BuildSegments(const wchar_t* scope_path_wide, std::vector<Segment>& out)
	{
		out.clear();
		if (scope_path_wide == nullptr || scope_path_wide[0] == L'\0') {
			PushSegment(out, L"", W18N(u8"（未選擇路徑）"));
			return;
		}

		std::wstring path = scope_path_wide;
		while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
			path.pop_back();
		}

		if (path.size() >= 2 && path[1] == L':') {
			std::wstring accum = path.substr(0, 2);
			accum += L'\\';
			PushSegment(out, accum, accum.substr(0, 2));

			for (size_t pos = 3; pos < path.size();) {
				size_t next = path.find_first_of(L"\\/", pos);
				if (next == std::wstring::npos) {
					next = path.size();
				}
				const std::wstring part = path.substr(pos, next - pos);
				if (!part.empty()) {
					accum += part;
					accum += L'\\';
					PushSegment(out, accum, part);
				}
				pos = (next < path.size()) ? next + 1 : path.size();
			}
			return;
		}

		if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
			std::wstring accum = L"\\\\";
			size_t pos = 2;
			while (pos <= path.size()) {
				size_t next = path.find_first_of(L"\\/", pos);
				if (next == std::wstring::npos) {
					next = path.size();
				}
				if (next > pos) {
					accum += path.substr(pos, next - pos);
					if (next < path.size()) {
						accum += L'\\';
					}
					const std::wstring label = path.substr(pos, next - pos);
					PushSegment(out, accum, label.empty() ? accum : label);
				}
				if (next >= path.size()) {
					break;
				}
				pos = next + 1;
			}
			if (!out.empty() && out.back().path.back() != L'\\') {
				out.back().path += L'\\';
			}
			return;
		}

		PushSegment(out, path, path);
	}
}

namespace HUiPathBreadcrumb {
	bool Draw(const char* id, const wchar_t* scope_path_wide, float width,
		NavigateCallback on_navigate, void* user_data, const Style* style_override)
	{
		Style style = {};
		if (style_override != nullptr) {
			style = *style_override;
		}
		else {
			style.text_current = HUiTheme::cyan_neon();
			style.text_hover = HUiTheme::cyan_neon();
			style.bar_bg = HUiTheme::panel_bg();
			style.segment_hover_bg = HUiTheme::card_bg_hover();
		}

		std::vector<Segment> segments;
		BuildSegments(scope_path_wide, segments);

		const ImVec2 bar_size(ImMax(32.f, width), style.bar_height);
		ImGui::PushID(id);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, style.bar_bg);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 2.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.separator_gap, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 2.f));

		const ImGuiWindowFlags child_flags = ImGuiWindowFlags_HorizontalScrollbar
			| ImGuiWindowFlags_NoScrollbar;
		if (!ImGui::BeginChild("##bc_bar", bar_size, true, child_flags)) {
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor();
			ImGui::PopID();
			ImGui::Dummy(bar_size);
			return false;
		}

		bool navigated = false;
		const char* sep = "›";
		const float row_y = (style.bar_height - ImGui::GetTextLineHeight()) * 0.5f;
		ImGui::SetCursorPos(ImVec2(4.f, ImMax(0.f, row_y)));

		for (size_t i = 0; i < segments.size(); ++i) {
			const bool is_last = (i + 1 == segments.size());
			if (i > 0) {
				ImGui::SameLine(0.f, style.separator_gap);
				ImGui::PushStyleColor(ImGuiCol_Text, style.sep_color);
				ImGui::TextUnformatted(sep);
				ImGui::PopStyleColor();
				ImGui::SameLine(0.f, style.separator_gap);
			}

			ImGui::PushID(static_cast<int>(i));
			if (is_last) {
				ImGui::PushStyleColor(ImGuiCol_Text, style.text_current);
				ImGui::TextUnformatted(segments[i].label_utf8.c_str());
				ImGui::PopStyleColor();
			}
			else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.segment_hover_bg);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.segment_current_bg);
				ImGui::PushStyleColor(ImGuiCol_Text, style.text_normal);
				if (ImGui::SmallButton(segments[i].label_utf8.c_str())) {
					if (on_navigate != nullptr && !segments[i].path.empty()) {
						on_navigate(segments[i].path.c_str(), user_data);
						navigated = true;
					}
				}
				ImGui::PopStyleColor(4);
			}
			ImGui::PopID();
		}

		ImGui::EndChild();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor();
		ImGui::PopID();
		return navigated;
	}
}
