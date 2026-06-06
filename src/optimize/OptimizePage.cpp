#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "Hi18n.h"
#include "OptimizePageUI.h"
#include "OptimizeScan.h"
#include "OptimizeNetworkScan.h"

class OptimizePage_ : public HPage
{
public:
	void Render() override
	{
		RenderPageHeader();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
		if (ImGui::BeginChild("##OptimizePageBody", ImVec2(avail.x, avail.y), true,
			ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
			OptimizePageUI::RenderContent();
			ImGui::EndChild();
		}
		ImGui::PopStyleVar();
	}

	void init(nlohmann::json& context) override
	{
		(void)context;
		OptimizeScan::Init();
		OptimizeNetworkScan::Init();
		HLOG_INFO("OptimizePage initialized");
	}

	void release() override
	{
		OptimizeNetworkScan::Shutdown();
		OptimizeScan::Shutdown();
		HLOG_INFO("OptimizePage released");
	}
};

REG_PAGEN_NAV(OptimizePage_, "OptimizePage", u8"系統優化", 12)
