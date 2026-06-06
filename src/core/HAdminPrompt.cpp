#define IMGUI_DEFINE_MATH_OPERATORS
#include "HAdminPrompt.h"
#include "HAppShell.h"
#include "HCleanTask.h"
#include "HElevationBroker.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>

namespace {
	bool g_startup_queued = false;
	bool g_modal_open = false;
	bool g_open_pending = false;
	HAdminPrompt::Scene g_scene = HAdminPrompt::Scene::Manual;
	HAdminPrompt::Result g_result = HAdminPrompt::Result::None;

	bool g_passed_clean = false;
	bool g_passed_disk = false;
	bool g_passed_startup = false;
	bool g_passed_optimize = false;

	bool g_retry_clean_after_gate = false;

	const char* kPopupId = "HP Admin Prompt";

	const char* TitleForScene(HAdminPrompt::Scene scene)
	{
		switch (scene) {
		case HAdminPrompt::Scene::Startup: return I18N(u8"建議以管理員身分執行");
		case HAdminPrompt::Scene::Clean: return I18N(u8"清理建議管理員權限");
		case HAdminPrompt::Scene::DiskHealth: return I18N(u8"硬碟健康度建議管理員權限");
		case HAdminPrompt::Scene::Optimize: return I18N(u8"系統優化建議管理員權限");
		default: return I18N(u8"管理員權限");
		}
	}

	const char* BodyForScene(HAdminPrompt::Scene scene)
	{
		switch (scene) {
		case HAdminPrompt::Scene::Startup:
			return I18N_JOIN(
				u8"以管理員身分執行可清理更多系統目錄，並改善 SMART／實體碟讀取完整性。\n\n",
				u8"選擇「UAC 提權」時，本程式會在背景建立管理員代理服務，主視窗與系統匣圖示保持運行，",
				u8"不會關閉後重新啟動。");
		case HAdminPrompt::Scene::Clean:
			return I18N_JOIN(
				u8"授予管理員權限可清理更多系統目錄（例如 Windows Update 快取）。\n\n",
				u8"選擇「UAC 提權」將啟用背景管理員代理，目前視窗不會關閉。");
		case HAdminPrompt::Scene::DiskHealth:
			return I18N_JOIN(
				u8"硬碟健康度檢測需要讀取 SMART、實體碟與壞軌掃描，建議以管理員身分執行。\n\n",
				u8"若不以管理員執行，部分資料可能無法讀取。");
		case HAdminPrompt::Scene::Optimize:
			return I18N_JOIN(
				u8"變更部分背景服務、建立還原點與讀取本機啟動項時，管理員權限較完整。\n\n",
				u8"選擇「UAC 提權」會啟用背景管理員代理，本視窗不會關閉；亦可先以目前權限繼續。");
		default:
			return I18N(u8"建議以系統管理員身分執行本程式以取得完整功能。");
		}
	}

	bool IsPassed(HAdminPrompt::Scene scene)
	{
		switch (scene) {
		case HAdminPrompt::Scene::Clean: return g_passed_clean;
		case HAdminPrompt::Scene::DiskHealth: return g_passed_disk;
		case HAdminPrompt::Scene::Startup: return g_passed_startup;
		case HAdminPrompt::Scene::Optimize: return g_passed_optimize;
		default: return false;
		}
	}

	void SetPassed(HAdminPrompt::Scene scene)
	{
		switch (scene) {
		case HAdminPrompt::Scene::Clean: g_passed_clean = true; break;
		case HAdminPrompt::Scene::DiskHealth: g_passed_disk = true; break;
		case HAdminPrompt::Scene::Startup: g_passed_startup = true; break;
		case HAdminPrompt::Scene::Optimize: g_passed_optimize = true; break;
		default: break;
		}
	}

	void CloseModal(HAdminPrompt::Result result)
	{
		g_result = result;
		g_modal_open = false;
		ImGui::CloseCurrentPopup();
		HLOG_INFO("HAdminPrompt: closed scene={} result={}", static_cast<int>(g_scene), static_cast<int>(result));
	}
}

namespace HAdminPrompt {
	void QueueStartupIfNeeded()
	{
		if (g_startup_queued || HCleanIsRunningAsAdmin()) {
			return;
		}
		g_startup_queued = true;
		Queue(Scene::Startup);
	}

	void Queue(Scene scene)
	{
		if (HCleanIsRunningAsAdmin()) {
			return;
		}
		g_scene = scene;
		g_open_pending = true;
		g_result = Result::None;
		HLOG_INFO("HAdminPrompt: queued scene={}", static_cast<int>(scene));
	}

	bool IsModalOpen()
	{
		return g_modal_open || g_open_pending;
	}

	Result PollResult()
	{
		const Result r = g_result;
		if (r != Result::None) {
			g_result = Result::None;
		}
		return r;
	}

	bool TryGate(Scene scene)
	{
		if (HCleanHasElevatedAccess()) {
			return true;
		}
		if (IsPassed(scene)) {
			return true;
		}

		const Result r = PollResult();
		if (r == Result::ContinueWithoutAdmin) {
			SetPassed(scene);
			return true;
		}
		if (r == Result::Cancel || r == Result::ElevateNewInstance) {
			return false;
		}
		if (IsModalOpen()) {
			return false;
		}

		Queue(scene);
		if (scene == Scene::Clean) {
			g_retry_clean_after_gate = true;
		}
		return false;
	}

	void Render()
	{
		if (g_open_pending) {
			ImGui::OpenPopup(kPopupId);
			g_open_pending = false;
			g_modal_open = true;
		}

		if (!g_modal_open) {
			return;
		}

		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const float max_w = ImMin(520.f, display.x - 48.f);
		ImGui::SetNextWindowSize(ImVec2(max_w, 0.f), ImGuiCond_Always);
		ImGui::SetNextWindowPos(display * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		if (!ImGui::BeginPopupModal(kPopupId, nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
			return;
		}

		ImGui::TextColored(HUiTheme::cyan_neon(), "%s", TitleForScene(g_scene));
		ImGui::Spacing();
		ImGui::PushTextWrapPos(max_w - 32.f);
		ImGui::TextUnformatted(BodyForScene(g_scene));
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const float btn_w = (max_w - 40.f) / 3.f - 4.f;
		if (ImGui::Button(I18N(u8"UAC 提權"), ImVec2(btn_w, 0.f))) {
			HLOG_INFO("HAdminPrompt: user chose UAC elevation (background broker)");
			if (HAppShellRequestAdminElevation(false)) {
				SetPassed(g_scene);
				CloseModal(Result::ElevateNewInstance);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(I18N(u8"仍以目前權限"), ImVec2(btn_w, 0.f))) {
			SetPassed(g_scene);
			CloseModal(Result::ContinueWithoutAdmin);
			if (g_scene == Scene::Clean && g_retry_clean_after_gate) {
				g_retry_clean_after_gate = false;
				RequestCleanSelectedTasks();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(I18N(u8"取消"), ImVec2(btn_w, 0.f))) {
			g_retry_clean_after_gate = false;
			CloseModal(Result::Cancel);
		}

		ImGui::EndPopup();
	}
}
