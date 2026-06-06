#pragma once
#ifndef HUI_HISTORY_LIST_H
#define HUI_HISTORY_LIST_H

#include "CleanHistory.h"
#include <imgui.h>
#include <cstddef>

// 清理歷史：自訂卡片列表（時間軸式，一眼可見釋放量與成敗）
namespace HUiHistoryList {

	struct Style {
		float card_height = 152.f;
		float tag_row_h = 28.f;
		float card_gap = 8.f;
		float card_rounding = 6.f;
		float accent_width = 4.f;
		float pad = 12.f;
	};

	// 在可捲動區域內繪製全部紀錄；width/height 為列表區域大小
	void Draw(const char* id, const std::vector<CleanHistoryEntry>& entries,
		float width, float height, const Style* style_override = nullptr);

}

#endif
