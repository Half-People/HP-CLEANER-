#include "HPage.h"
#include "MainPageUI.h"
#include "MainPageDiskScan.h"
#include "CleanHistory.h"

class MainPage_ : public HPage
{
public:
	void Render() override
	{
		RenderPageHeader();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float footer_h = MainPageUI::kFooterHeight;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
		if (ImGui::BeginChild("##MainPageBody", ImVec2(avail.x, avail.y - footer_h), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			MainPageUI::DrawContentBorder();
			MainPageUI::RenderCentralDashboard();
			ImGui::EndChild();
		}

		if (ImGui::BeginChild("##MainPageFooter", ImVec2(0.0f, footer_h), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			MainPageUI::RenderMainPageFooter();
			ImGui::EndChild();
		}
		ImGui::PopStyleVar();
	}

	void init(nlohmann::json& context) override
	{
		(void)context;
		MainPageDiskScan::Init();
		CleanHistory::Reload();
		HLOG_INFO("MainPage initialized");
	}

	void release() override { HLOG_INFO("MainPage released"); }
};

REG_PAGEN_NAV(MainPage_, "MainPage", "首頁", 0)
