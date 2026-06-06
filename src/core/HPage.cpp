#define IMGUI_DEFINE_MATH_OPERATORS
#include "HPage.h"
#include "HAppPaths.h"
#include "HUserConfig.h"
#include "MainPageDiskScan.h"
#include "DiskHealthScan.h"
#include "DiskHealthTest.h"
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <imgui_internal.h>

#include <spdlog/sinks/basic_file_sink.h>
#include "HLogRing.h"
#include "HAdminPrompt.h"
#include "HAppSettings.h"
#include "Hi18n.h"
#include "Hi18nLangPicker.h"
#include "HAppShell.h"
#include "HLogCmdConsole.h"
#include "HLogPipe.h"
#include <memory>

namespace {
	std::shared_ptr<spdlog::sinks::basic_file_sink_mt> g_file_sink;
	bool g_logging_initialized = false;

	void rebuild_spdlog_sinks()
	{
		std::vector<spdlog::sink_ptr> sinks;
		if (g_file_sink != nullptr) {
			sinks.push_back(g_file_sink);
		}
		if (sinks.empty()) {
			return;
		}
		HLogRingAttachToSinks(sinks);
		HLogPipeAttachToSinks(sinks);
		auto logger = std::make_shared<spdlog::logger>("HP CLEANER++", sinks.begin(), sinks.end());
		logger->set_level(spdlog::level::trace);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(logger);
	}
}

struct PageNavEntry {
	const char* label;
	const char* page_id;
	int order;
};


static std::unordered_map<std::string, HPage*>* page_registry_ctx;
static std::vector<PageNavEntry>* nav_registry_ctx;
static bool switched_page = false,popup_page = false;
static HPage* current_page = nullptr, *target_page = nullptr;



namespace Logo {
	 HRC::HTexture HPS_Logo;
	 HRC::HTexture HP_Cleaner_Logo;
}

void MainPage::BeginMainPage(){
	HAppPaths::EnsureAppDataDirs();
	LoadAllTaskDetailConfigs();
	HLOG_INFO("Application UI starting");
	open_page("MainPage");
	HAdminPrompt::QueueStartupIfNeeded(); 
	Logo::HPS_Logo = HRC::LoadTexture(MAKEINTRESOURCE(IDB_PNG2), TEXT("PNG"));
	Logo::HP_Cleaner_Logo = HRC::LoadTexture(MAKEINTRESOURCE(IDB_PNG1), TEXT("PNG"));
}

void MainPage::EndMainPage()
{
	MainPageDiskScan::Shutdown();
	DiskHealthTest::Shutdown();
	DiskHealthScan::Shutdown();
	HLOG_DEBUG("EndMainPage: release logos and page registry");
	HLOG_INFO("Application UI shutting down");
	HRC::FreeTexture(Logo::HPS_Logo);
	HRC::FreeTexture(Logo::HP_Cleaner_Logo);

	if (current_page) {
		current_page->release();
		current_page = nullptr;
	}
	if (target_page) {
		target_page->release();
		target_page = nullptr;
	}
	if (page_registry_ctx!= nullptr){
		delete page_registry_ctx;
		page_registry_ctx = nullptr;
	}
	if (nav_registry_ctx != nullptr) {
		delete nav_registry_ctx;
		nav_registry_ctx = nullptr;
	}
}

void MainPage::RenderMainPage(){
	ImGui::SetNextWindowPos(ImVec2());
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	if (ImGui::Begin("main_page",0,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar)){
		if (current_page == nullptr) {
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No page loaded. Please open a page.");
		}
		else{
			current_page->Render();
			RenderPageHeaderLangPicker();
		}
		HAdminPrompt::Render();

		if (popup_page && target_page != nullptr && target_page->IsPopup) {
			if (ImGui::BeginPopup("popup_page", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
				target_page->Render();
				ImGui::EndPopup();
			}
			else {
				popup_page = false;
				target_page->release();
				target_page = nullptr;
				HLOG_INFO("Popup page closed. Releasing current page and switching to target page");
			}
		}

		ImGui::End();
	}

	if (switched_page){
		if (target_page!=nullptr)
		{
			if (target_page->IsPopup&& popup_page){
				HLOG_INFO("Opening popup page '{}'", (void*)target_page);
				ImGui::OpenPopup("popup_page");
				switched_page = false;
			}
			else
			{
				HLOG_INFO("Releasing current page and switching to target page");
				if (current_page!=nullptr){
					current_page->release();

				}

				current_page = target_page;
				target_page = nullptr;
				switched_page = false;
			}
		}
		else{
			HLOG_ERROR("Target page is null. Cannot switch pages.");
		}
	}
}




void init_spdlog() {
	if (g_logging_initialized) {
		return;
	}
	g_logging_initialized = true;

	HAppPaths::EnsureAppDataDirs();
	const std::string log_file = HAppPaths::GetCurrentDailyLogFilePath();

	spdlog::drop_all();

	g_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, false);
	g_file_sink->set_pattern("[%H:%M:%S] [%n] [%L] [TID:%t] [PID:%P] [%s(%#)::%!] %v");

	rebuild_spdlog_sinks();

	spdlog::flush_every(std::chrono::seconds(2));

	HLOG_INFO("spdlog initialized, log file: {}", log_file);
	if (spdlog::default_logger() != nullptr) {
		spdlog::default_logger()->flush();
	}
}

void HInitLogging()
{
	init_spdlog();
}

void HLoggingRebuildSinks()
{
	if (!g_logging_initialized) {
		return;
	}
	rebuild_spdlog_sinks();
}

void HShutdownLogging()
{
	HLogCmdConsoleShutdown();
	if (spdlog::default_logger() != nullptr) {
		spdlog::default_logger()->flush();
	}
	spdlog::shutdown();
	g_file_sink.reset();
	g_logging_initialized = false;
}

bool HLoggingIsConsoleEnabled()
{
	return HAppSettingsGetConsoleLogger();
}

void HLoggingToggleConsole(bool enable)
{
	if (!g_logging_initialized) {
		HInitLogging();
	}
	if (enable) {
		HLogCmdConsoleLaunch();
		HAppSettingsSetConsoleLogger(true);
		HLOG_INFO("Log console launched (pipe stream)");
	}
	else {
		HLogCmdConsoleShutdown();
		HAppSettingsSetConsoleLogger(false);
		HLoggingRebuildSinks();
		HLOG_INFO("Log console preference disabled");
	}
	if (spdlog::default_logger() != nullptr) {
		spdlog::default_logger()->flush();
	}
}

void HLoggingOnConsoleClosedByUser()
{
	HLogCmdConsoleShutdown();
	HAppSettingsSetConsoleLogger(false);
	HLoggingRebuildSinks();
}

const char* get_current_page_id()
{
	if (current_page != nullptr) {
		if (current_page->page_id != nullptr) {
			return current_page->page_id;
		}
		HLOG_ERROR("Current page has null page_id. This should not happen. Returning null.");
	}
	return "null";
}

bool open_page(const char* page_name, nlohmann::json& context)
{
	HLOG_TRACE("Attempting to open page '{}'", page_name);
	auto it = page_registry_ctx->find(page_name);
	if (it != page_registry_ctx->end()) {
		HPage* page = it->second;
		if (current_page != page) {
			target_page = page;
			target_page->init(context);
			if (target_page->IsPopup) {
				popup_page = true;
			}
			switched_page = true;

			HLOG_INFO("Switched to page '{}'", page_name);
		}
		else{
			HLOG_WARN("Page '{}' is already open.", page_name);
		}
		return true;
	}
	else{
		HLOG_WARN("Page '{}' not found in registry.", page_name);
	}
	return false;
}

namespace PageHeaderLayout {
	constexpr float kPadX = 12.0f;
	constexpr float kPadY = 4.0f;
	constexpr float kLogoSize = 64.0f;
	constexpr float kNavBtnW = 120.0f;
	constexpr float kNavBtnH = 40.0f;
	constexpr float kNavGap = 8.0f;
	constexpr float kRowHeight = kLogoSize;
	constexpr ImVec2 kLogoVec(kLogoSize, kLogoSize);
	constexpr ImVec2 kNavBtnVec(kNavBtnW, kNavBtnH);
}

void RegistrationNavItem_internal(const char* label, const char* page_id, int order)
{
	if (nav_registry_ctx == nullptr) {
		nav_registry_ctx = new std::vector<PageNavEntry>();
	}
	else {
		for (PageNavEntry& entry : *nav_registry_ctx) {
			if (strcmp(entry.page_id, page_id) == 0) {
				HLOG_WARN("Nav item for page '{}' is already registered. Overwriting.", page_id);
				entry = { label, page_id, order };
				return;
			}
		}
	}
	nav_registry_ctx->push_back({ label, page_id, order });
	HLOG_INFO("Registering nav item '{}' -> '{}'", label, page_id);
}

void RenderPageHeader()
{
	using namespace PageHeaderLayout;

	const ImGuiStyle& style = ImGui::GetStyle();

	if (kPadY > 0.0f) {
		ImGui::Dummy(ImVec2(0.0f, kPadY));
	}

	const float row_y = ImGui::GetCursorPosY();
	const float nav_y = row_y + (kLogoSize - kNavBtnH) * 0.5f;

	ImGui::SetCursorPosX(style.WindowPadding.x + kPadX);

	if (Logo::HP_Cleaner_Logo.texture != 0) {
		ImGui::Image((ImTextureID)(intptr_t)Logo::HP_Cleaner_Logo.texture, kLogoVec);
	}
	else {
		ImGui::Dummy(kLogoVec);
	}

	ImGui::SameLine(0.0f, kNavGap);

	const char* lang_label = HTR(LangLabel);
	const char* lang_name = Hi18n::GetCurrentLanguageName();
	const float lang_reserve = ImMax(200.f,
		ImGui::CalcTextSize(lang_label).x + ImGui::CalcTextSize(lang_name).x + 72.f);
	const float nav_area_right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth()
		- style.WindowPadding.x - kPadX - lang_reserve;

	if (nav_registry_ctx != nullptr && !nav_registry_ctx->empty()) {
		std::vector<PageNavEntry> nav_items = *nav_registry_ctx;
		std::sort(nav_items.begin(), nav_items.end(),
			[](const PageNavEntry& a, const PageNavEntry& b) { return a.order < b.order; });

		for (size_t i = 0; i < nav_items.size(); ++i) {
			if (i > 0) {
				ImGui::SameLine(0.0f, kNavGap);
			}
			const float next_right = ImGui::GetItemRectMax().x + kNavGap + kNavBtnW;
			if (i > 0 && next_right > nav_area_right) {
				break;
			}
			ImGui::SetCursorPosY(nav_y);
			const char* nav_label = Hi18n::NavLabel(nav_items[i].page_id);
			if (nav_label == nullptr || nav_label[0] == '\0') {
				nav_label = I18N(nav_items[i].label);
			}
			PageButton(nav_label, nav_items[i].page_id, kNavBtnVec);
		}
	}

	ImGui::SetCursorPos(ImVec2(style.WindowPadding.x, row_y + kRowHeight + kPadY));
	ImGui::Separator();
}

void RenderPageHeaderLangPicker()
{
	using namespace PageHeaderLayout;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window == nullptr) {
		return;
	}
	ImGui::PushClipRect(window->InnerRect.Min, window->InnerRect.Max, false);
	Hi18nLangPicker::DrawInHeader(kPadX);
	ImGui::PopClipRect();
}

void PageButton(const char* page_name, const char* page_id, const ImVec2& size_arg)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;

	// 1. 使用唯一的 page_id 獲取 ImGuiID，避免雜湊衝突
	const ImGuiID id = window->GetID(page_id);

	const char* label_end = ImGui::FindRenderedTextEnd(page_name);
	const ImVec2 label_size = ImGui::CalcTextSize(page_name, label_end, false);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return;

	// 2. 獲取滑鼠與點擊狀態
	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

	// =================================================================
	// 🎨 核心視覺封裝：動態計算選中狀態與色彩注入 (Theming Logic)
	// =================================================================
	// 定義你的電競風色彩
	ImVec4 cyan_neon = ImVec4(0.00f, 0.90f, 0.90f, 1.0f); // 亮青
	ImVec4 cyan_dark = ImVec4(0.00f, 0.40f, 0.40f, 1.0f); // 暗青
	ImVec4 pure_black = ImVec4(0.03f, 0.03f, 0.03f, 1.0f); // 純黑
	ImVec4 active_bg = ImVec4(0.00f, 0.25f, 0.25f, 1.0f); // 選中底色
	ImVec4 hover_bg = ImVec4(0.00f, 0.35f, 0.35f, 1.0f); // 懸停底色

	const char* active_page_id = (current_page != nullptr && current_page->page_id != nullptr)
		? current_page->page_id
		: "null";
	bool is_selected = (strcmp(active_page_id, page_id) == 0);

	// 依據「選中狀態」、「懸停狀態」與「按住狀態」動態決定邊框與背景色
	ImU32 border_col;
	ImU32 bg_col;

	if (is_selected) {
		// 【當前分頁高亮狀態】
		border_col = ImGui::GetColorU32(cyan_neon); // 亮青色發光邊框
		bg_col = ImGui::GetColorU32(held ? active_bg : (hovered ? hover_bg : active_bg));
	}
	else {
		// 【未選中的普通分頁狀態】
		border_col = ImGui::GetColorU32(hovered ? cyan_neon : cyan_dark); // 懸停時邊框會微微發亮
		bg_col = ImGui::GetColorU32(held ? active_bg : (hovered ? ImVec4(0.05f, 0.15f, 0.15f, 1.0f) : pure_black));
	}

	// =================================================================
	// 3. 底層渲染 (Render) 
	// =================================================================
	ImGui::RenderNavCursor(bb, id);

	// 渲染按鈕背景
	ImGui::RenderFrame(bb.Min, bb.Max, bg_col, false, style.FrameRounding);

	// 💡 關鍵：手動繪製自定義的霓虹邊框（利用 window->DrawList 繪製精準的線條）
	// 這樣做即使全域 style.FrameBorderSize 被關掉，你的分頁按鈕依然能強行畫出發光邊框！
	window->DrawList->AddRect(bb.Min, bb.Max, border_col, style.FrameRounding, 0, 1.0f);

	// 渲染按鈕文字
	if (g.LogEnabled)
		ImGui::LogSetNextTextDecoration("[", "]");
	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, page_name, label_end, &label_size, style.ButtonTextAlign, &bb);

	// 4. 路由觸發
	if (pressed) {
		open_page(page_id);
	}
}

void RegistrationPage_internal(HPage* page, const char* page_name){
	init_spdlog();
	if (!(page_registry_ctx==nullptr||page_registry_ctx->empty()))
	{
		if (page_registry_ctx->find(page_name) != page_registry_ctx->end()) {
			HLOG_WARN("Page '{}' is already registered. Overwriting.", page_name);
		}
		else{
			HLOG_INFO("Registering page '{}'", page_name);
		}
		(*page_registry_ctx)[page_name] = page;
	}
	else if(page_registry_ctx == nullptr){
		HLOG_INFO("Initializing page registry and registering page '{}'", page_name);
		page_registry_ctx = new std::unordered_map<std::string, HPage*>();
		(*page_registry_ctx)[page_name] = page;
	}
	else
	{
		HLOG_ERROR("Page registry is in an invalid state. Cannot register page '{}'", page_name);
	}

}
