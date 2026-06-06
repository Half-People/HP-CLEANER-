#pragma once
#ifndef HUI_PATH_BREADCRUMB_H
#define HUI_PATH_BREADCRUMB_H

#include <imgui.h>

// 自訂路徑麵包屑：將 scope 路徑拆成可點擊區段，點擊後透過 callback 導航
namespace HUiPathBreadcrumb {

	struct Style {
		float bar_height = 30.f;
		float segment_pad_x = 8.f;
		float segment_pad_y = 4.f;
		float separator_gap = 2.f;
		ImVec4 text_normal = ImVec4(0.75f, 0.88f, 0.88f, 1.f);
		ImVec4 text_hover = ImVec4(0.f, 0.95f, 0.95f, 1.f);
		ImVec4 text_current = ImVec4(0.f, 0.9f, 0.9f, 1.f);
		ImVec4 sep_color = ImVec4(0.35f, 0.5f, 0.5f, 1.f);
		ImVec4 segment_hover_bg = ImVec4(0.08f, 0.14f, 0.14f, 1.f);
		ImVec4 segment_current_bg = ImVec4(0.04f, 0.10f, 0.10f, 1.f);
		ImVec4 bar_bg = ImVec4(0.04f, 0.06f, 0.06f, 1.f);
	};

	using NavigateCallback = void (*)(const wchar_t* path_wide, void* user_data);

	// 繪製麵包屑列；若使用者點擊某區段並觸發導航則回傳 true
	bool Draw(const char* id, const wchar_t* scope_path_wide, float width,
		NavigateCallback on_navigate, void* user_data, const Style* style_override = nullptr);

}

#endif
