#define IMGUI_DEFINE_MATH_OPERATORS
#include "Hi18nLangPicker.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Hi18nLangPicker {
	namespace {
		using namespace HUiTheme;

		constexpr float kPickerH = 34.f;
		constexpr float kPickerMinW = 148.f;
		constexpr float kHeaderPadY = 4.f;
		constexpr float kHeaderRowH = 64.f;
	}

	bool DrawInHeader(float right_margin)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window == nullptr || window->SkipItems) {
			return false;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const char* preview = Hi18n::GetCurrentLanguageName();
		const float picker_w = ImMax(kPickerMinW, ImGui::CalcTextSize(preview).x + 72.f);

		const float row_right = window->WorkRect.Max.x - right_margin;
		const float picker_x = row_right - picker_w;
		const float picker_y = window->Pos.y + style.WindowPadding.y + kHeaderPadY
			+ (kHeaderRowH - kPickerH) * 0.5f;

		ImGui::PushClipRect(window->InnerRect.Min, window->InnerRect.Max, false);
		ImGui::SetCursorScreenPos(ImVec2(picker_x, picker_y));

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.09f, 0.10f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.04f, 0.07f, 0.08f, 0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border, cyan_dark());
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 7.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

		const bool changed = Hi18n::DrawLanguageCombo("##header_lang_picker", picker_w);

		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(3);
		ImGui::PopClipRect();
		return changed;
	}

} // namespace Hi18nLangPicker
