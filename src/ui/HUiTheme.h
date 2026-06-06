#pragma once
#include <imgui.h>

// 共用 Cyber 青色系（MainPage / ClearPage 等自繪 UI；函式形式以相容 C++14）
namespace HUiTheme {
	inline ImVec4 cyan_neon() { return ImVec4(0.00f, 0.90f, 0.90f, 1.0f); }
	inline ImVec4 cyan_mid() { return ImVec4(0.00f, 0.65f, 0.65f, 1.0f); }
	inline ImVec4 cyan_dark() { return ImVec4(0.00f, 0.40f, 0.40f, 1.0f); }
	inline ImVec4 bg_pure_black() { return ImVec4(0.03f, 0.03f, 0.03f, 1.0f); }
	inline ImVec4 card_bg() { return ImVec4(0.06f, 0.08f, 0.08f, 1.0f); }
	inline ImVec4 card_bg_hover() { return ImVec4(0.08f, 0.12f, 0.12f, 1.0f); }
	inline ImVec4 active_bg() { return ImVec4(0.00f, 0.25f, 0.25f, 1.0f); }
	inline ImVec4 hover_bg() { return ImVec4(0.00f, 0.35f, 0.35f, 1.0f); }
	inline ImVec4 header_bg() { return ImVec4(0.04f, 0.06f, 0.06f, 1.0f); }
	inline ImVec4 panel_bg() { return ImVec4(0.05f, 0.07f, 0.07f, 1.0f); }
	inline ImVec4 track_bg() { return ImVec4(0.02f, 0.08f, 0.08f, 1.0f); }

	inline ImU32 NeonGlow(float alpha) {
		return ImGui::GetColorU32(ImVec4(0.f, 0.9f, 0.9f, alpha));
	}
}
