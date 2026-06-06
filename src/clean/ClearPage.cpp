#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "Hi18n.h"
#include "ClearPageUI.h"
#include <imgui_internal.h>

class ClearPage_ : public HPage
{
public:
	virtual void Render() override
	{
		RenderPageHeader();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float footer_h = ClearPageUI::kFooterHeight;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
		if (ImGui::BeginChild("##ClearPageContent", ImVec2(avail.x, avail.y - footer_h), true)) {
			ClearPageUI::RenderContent();
			ImGui::EndChild();
		}

		if (ImGui::BeginChild("##ClearPageFooter", ImVec2(0.0f, footer_h), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ClearPageUI::RenderFooter();
			ImGui::EndChild();
		}
		ImGui::PopStyleVar();
	}

	virtual void init(nlohmann::json& context) override
	{
		(void)context;
		HLOG_INFO("ClearPage initialized");
	}
	virtual void release() override { HLOG_INFO("ClearPage released"); }
};

REG_PAGEN_NAV(ClearPage_, "ClearPage", u8"系統清理工具", 10)
