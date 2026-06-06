#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "Hi18n.h"
#include <imgui_internal.h>
#include "HUiTheme.h"
#include "AboutPageUI.h"
#include "AboutDeviceInfo.h"
#include "AboutAppLog.h"

class AboutPage : public HPage
{
public:
	void Render() override
	{
		RenderPageHeader();
		ImGui::Spacing();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float content_w = ImMax(200.f, avail.x);

		if (ImGui::BeginChild("##AboutContent", avail, ImGuiChildFlags_None)) {
			const float inner_w = ImMax(200.f, ImGui::GetContentRegionAvail().x);
			const float gap = 10.f;

			AboutPageUI::DrawHeroPanel(inner_w);
			ImGui::Dummy(ImVec2(0.f, gap));
			AboutPageUI::DrawIntroPanel(inner_w);
			ImGui::Dummy(ImVec2(0.f, gap));
			AboutPageUI::DrawDeviceInfoPanel(inner_w, AboutDeviceInfo::Get());
			ImGui::Dummy(ImVec2(0.f, gap));
			AboutPageUI::DrawThirdPartyPanel(inner_w);
			AboutPageUI::DrawThirdPartyGithubLinks(inner_w);
			AboutAppLog::Draw(inner_w);
			ImGui::Dummy(ImVec2(0.f, 4.f));
			ImGui::EndChild();
		}
	}

	void init(nlohmann::json& context) override
	{
		(void)context;
		AboutDeviceInfo::Refresh();
		AboutAppLog::Refresh();
		HLOG_INFO("AboutPage initialized");
	}

	void release() override { HLOG_INFO("AboutPage released"); }
};

REG_PAGE(AboutPage)
REG_NAV_ITEM_ORDER(u8"關於我們", "AboutPage", 100)
