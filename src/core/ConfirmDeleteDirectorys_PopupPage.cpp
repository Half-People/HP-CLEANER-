#include "HPage.h"

class ConfirmDeleteDirectorys : public HPage
{
public:
	virtual void Render()override {
		ImGui::Text("This is the AboutPage .");
		if (ImGui::Button("Home")) {
			HLOG_INFO("ConfirmDeleteDirectorys popup: user navigated to MainPage");
			open_page("MainPage");
		}
	}
	virtual void init(nlohmann::json& context) override
	{
		(void)context;
		HLOG_INFO("ConfirmDeleteDirectorys popup initialized");
	}
	virtual void release() override { HLOG_INFO("ConfirmDeleteDirectorys popup released"); }

private:

};

REG_POPUP_PAGE(ConfirmDeleteDirectorys)