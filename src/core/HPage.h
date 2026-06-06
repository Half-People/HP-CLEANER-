#pragma once
#ifndef HPAGE_H
#define HPAGE_H
#include <imgui.h>
#define SPDLOG_ACTIVE_LEVEL  SPDLOG_LEVEL_TRACE  //一定要在 spdlog.h 之前定義
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "HRC_Assets.h"


#define HLOG_TRACE(...)  SPDLOG_TRACE(__VA_ARGS__)
#define HLOG_DEBUG(...)  SPDLOG_DEBUG(__VA_ARGS__)
#define HLOG_INFO(...)   SPDLOG_INFO(__VA_ARGS__)
#define HLOG_WARN(...)   SPDLOG_WARN(__VA_ARGS__)
#define HLOG_ERROR(...)  SPDLOG_ERROR(__VA_ARGS__)

void HInitLogging();
void HShutdownLogging();
void HLoggingRebuildSinks();
constexpr UINT WM_HP_CONSOLE_CLOSED = WM_APP + 44;

bool HLoggingIsConsoleEnabled();
void HLoggingToggleConsole(bool enable);
void HLoggingOnConsoleClosedByUser();

namespace Logo {
	extern HRC::HTexture HPS_Logo;
	extern HRC::HTexture HP_Cleaner_Logo;
}


namespace MainPage {
	void BeginMainPage();
	void EndMainPage();
	void RenderMainPage();


}
const char* get_current_page_id();
bool open_page(const char* page_name, nlohmann::json& context);
static bool open_page(const char* page_name) {
	nlohmann::json j;
	return open_page(page_name, j);
}

class HPage
{
public:
	// interface
	virtual void Render(){}
	virtual void init(nlohmann::json& context){}
	virtual void release() {}

	bool IsPopup = false;
	const char* page_id = nullptr;
private:
};

void PageButton(const char* page_name, const char* page_id, const ImVec2& size_arg = ImVec2(120, 40));

// 共用頂部：Logo + 已註冊分頁導覽 + 分隔線（各 HPage 子類在 Render 開頭呼叫）
void RenderPageHeader();
// 語言選擇器須在分頁內容之後繪製，避免子視窗搶走點擊（MainPage::RenderMainPage 結尾呼叫）
void RenderPageHeaderLangPicker();

void RegistrationNavItem_internal(const char* label, const char* page_id, int order);

#define REG_NAV_ITEM(label, page_id) \
	static bool REG_NAV_ITEM_##__LINE__ = [](){ \
		RegistrationNavItem_internal(label, page_id, 0); \
		return true; \
	}();

#define REG_NAV_ITEM_ORDER(label, page_id, order) \
	static bool REG_NAV_ITEM_##__LINE__ = [](){ \
		RegistrationNavItem_internal(label, page_id, order); \
		return true; \
	}();

#define REG_PAGEN_NAV(page_class, page_id, nav_label, nav_order) \
	REG_PAGEN(page_class, page_id) \
	REG_NAV_ITEM_ORDER(nav_label, page_id, nav_order)

void RegistrationPage_internal(HPage* page,const char* page_name);

#define REGISTRATION_PAGE(page_class,IsPopup_,Name) \
	static page_class page_class##_instance; \
	static bool page_class##_registered = [](){\
		page_class##_instance.IsPopup = IsPopup_;\
		page_class##_instance.page_id = Name;\
		RegistrationPage_internal(&page_class##_instance, Name );\
		return true;\
	}();

#define REGISTRATION_POPUP_PAGE(page_class,name) REGISTRATION_PAGE(page_class, true,name )
#define REG_PAGE(page_class) REGISTRATION_PAGE(page_class, false, #page_class )
#define REG_POPUP_PAGE(page_class) REGISTRATION_POPUP_PAGE(page_class, #page_class)

#define REG_PAGEN(page_class,name) REGISTRATION_PAGE(page_class, false, name )
#define REG_POPUP_PAGEN(page_class,name) REGISTRATION_POPUP_PAGE(page_class,name)
#endif // !HPAGE_H