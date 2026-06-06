#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "Hi18n.h"
#include "FileMapUI.h"
#include "FileMapScan.h"
#include "FileMapTree.h"

class FileMapPage_ : public HPage
{
public:
	void Render() override
	{
		RenderPageHeader();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
		if (ImGui::BeginChild("##FileMapPageBody", ImVec2(avail.x, avail.y), true)) {
			FileMapUI::RenderContent();
			ImGui::EndChild();
		}
		ImGui::PopStyleVar();
	}

	void init(nlohmann::json& context) override
	{
		(void)context;
		FileMapTree::Init();
		FileMapScan::Init();
		HLOG_INFO("FileMapPage initialized");
	}

	void release() override
	{
		FileMapScan::Shutdown();
		FileMapTree::Shutdown();
		HLOG_INFO("FileMapPage released");
	}
};

REG_PAGEN_NAV(FileMapPage_, "FileMapPage", u8"文件地圖", 20)
