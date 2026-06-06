#pragma once
#include <imgui.h>
#include <cstdint>

class HCleanTask;

// ClearPage 專用 UI：底欄、內容區、清理項目卡片 widget
namespace ClearPageUI {
	constexpr float kFooterHeight = 152.0f;
	constexpr float kFooterLogoW = 120.0f;
	constexpr float kFooterLogoH = 48.0f;
	constexpr float kItemCardW = 248.0f;
	// 固定高度：底列（詳細+大小）與進度區永遠預留，掃描中不變形
	constexpr float kItemCardH = 148.0f;

	struct CleanItemData {
		const char* title = "";
		const char* message = "";
		const char* tooltip = "";
		const char* size_text = "";
		bool* selected = nullptr;
		HCleanTask* task = nullptr;
	};

	void RenderFooter();
	void RenderContent();

	// 由系統優化頁導向時顯示建議橫幅（不自動勾選任務）
	void SetCleanSuggestionHint(const char* preset_id, int64_t estimated_bytes);

	bool CleanItemWidget(const char* id, CleanItemData& item, const ImVec2& size = ImVec2(kItemCardW, kItemCardH));

	// 分類區塊標題列：自訂主勾選、標題、分隔線、掃描、展開 chevron
	bool CategorySectionHeader(const char* id, const char* category_id, const char* display_name,
		bool* expanded, bool* request_rescan = nullptr);

	void RenderTaskDetailModal(HCleanTask* task);
	void OpenFolderInExplorer(const char* utf8_path);
}
