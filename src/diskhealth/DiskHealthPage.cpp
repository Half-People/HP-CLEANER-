#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "Hi18n.h"
#include "DiskHealthUI.h"
#include "DiskHealthScan.h"
#include "DiskHealthTest.h"

class DiskHealthPage_ : public HPage
{
public:
	void Render() override
	{
		RenderPageHeader();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
		const ImGuiChildFlags body_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
		const ImGuiWindowFlags body_scroll = ImGuiWindowFlags_AlwaysVerticalScrollbar;
		const float body_h = (std::max)(0.f, avail.y);
		ImGui::BeginChild("##DiskHealthBody", ImVec2(avail.x, body_h), body_flags, body_scroll);
		DiskHealthUI::RenderContent();
		ImGui::EndChild();
		ImGui::PopStyleVar();
	}

	void init(nlohmann::json& context) override
	{
		(void)context;
		DiskHealthScan::Init();
		HLOG_INFO("DiskHealthPage initialized");
	}

	void release() override
	{
		DiskHealthTest::Shutdown();
		DiskHealthScan::Shutdown();
		HLOG_INFO("DiskHealthPage released");
	}
};

REG_PAGEN_NAV(DiskHealthPage_, "DiskHealthPage", u8"硬盤健康度檢測", 15)
