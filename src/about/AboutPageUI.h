#pragma once
#ifndef ABOUT_PAGE_UI_H
#define ABOUT_PAGE_UI_H

#include "AboutDeviceInfo.h"
#include <imgui.h>

namespace AboutPageUI {

	constexpr float kNarrowBreakpoint = 620.f;

	// 回傳佔用高度（僅 Dummy 推進版面，避免與下一區塊重疊）
	float DrawHeroPanel(float content_w);
	float DrawIntroPanel(float content_w);
	float DrawDeviceInfoPanel(float content_w, const AboutDeviceInfoSnapshot& device);
	float DrawThirdPartyPanel(float content_w);
	void DrawThirdPartyGithubLinks(float content_w);

}

#endif
