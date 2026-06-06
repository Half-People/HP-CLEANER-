#define IMGUI_DEFINE_MATH_OPERATORS
#include "AboutPageUI.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HUiTheme.h"
#include <imgui_internal.h>
#include <shellapi.h>
#include <cstdio>
#include <vector>

namespace {
	using namespace HUiTheme;

	constexpr float kRounding = 6.f;
	constexpr const char* kAppVersion = "1.0.0.0";

	struct ThirdPartyLib {
		const char* name;
		const char* version;
		const char* license;
		const char* usage;
		const char* github_url;
	};

	static const ThirdPartyLib kThirdPartyLibs[] = {
		{ "Dear ImGui", "1.92.9", "MIT", u8"即時 UI 與互動介面", "https://github.com/ocornut/imgui" },
		{ "ImPlot", "0.16 / 1.1 WIP", "MIT", u8"CPU、記憶體與磁碟活動圖表", "https://github.com/epezent/implot" },
		{ "nlohmann/json", "3.12.0", "MIT", u8"清理設定與清理歷史持久化", "https://github.com/nlohmann/json" },
		{ "spdlog", "1.17.0", "MIT", u8"執行期日誌與除錯輸出", "https://github.com/gabime/spdlog" },
		{ "stb_image", "2.30", "Public Domain / MIT", u8"內嵌 PNG 資源載入（Logo 等）", "https://github.com/nothings/stb" },
		{ "Microsoft Direct3D 9", "Windows SDK", u8"系統授權條款", u8"硬體加速視窗渲染", nullptr },
		{ "Windows API", "—", u8"系統授權條款", u8"檔案系統、磁碟、SMART、程序偵測等", nullptr },
	};

	static void OpenUrlInBrowser(const char* url_utf8)
	{
		if (url_utf8 == nullptr || url_utf8[0] == '\0') {
			return;
		}
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, url_utf8, -1, nullptr, 0);
		if (wlen <= 0) {
			return;
		}
		std::vector<wchar_t> wurl(static_cast<size_t>(wlen));
		MultiByteToWideChar(CP_UTF8, 0, url_utf8, -1, wurl.data(), wlen);
		ShellExecuteW(nullptr, L"open", wurl.data(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	static const char* IntroText()
	{
		return I18N_JOIN(
			u8"HP CLEANER++ 協助您掃描並清理 Windows 暫存、瀏覽器與應用程式快取，",
			u8"並提供磁碟健康度（SMART）、儲存空間分析與清理歷史紀錄。",
			u8"清理前可預覽各項目大小並自訂勾選；執行時於背景工作，不阻塞介面。");
	}

	static ImVec2 FitImageSize(int img_w, int img_h, float max_w, float max_h)
	{
		if (img_w <= 0 || img_h <= 0) {
			return ImVec2(max_w, max_h);
		}
		const float aspect = static_cast<float>(img_w) / static_cast<float>(img_h);
		float w = max_w;
		float h = w / aspect;
		if (h > max_h) {
			h = max_h;
			w = h * aspect;
		}
		return ImVec2(w, h);
	}

	static void DrawPanelChrome(ImDrawList* dl, const ImRect& bb, bool bright = false)
	{
		dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), kRounding);
		const ImU32 left = ImGui::GetColorU32(ImVec4(0.f, bright ? 0.95f : 0.7f, 0.9f, bright ? 0.45f : 0.22f));
		const ImU32 right = ImGui::GetColorU32(cyan_neon());
		dl->AddRectFilledMultiColor(
			bb.Min + ImVec2(1.f, 1.f), ImVec2(bb.Max.x - 1.f, bb.Min.y + 3.f),
			left, right, right, left);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), kRounding, 0, 1.f);
	}

	static void DrawWrappedText(ImDrawList* dl, const ImVec2& pos, float max_w,
		ImU32 col, const char* text)
	{
		if (text == nullptr || text[0] == '\0') {
			return;
		}
		const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, max_w);
		const ImRect clip(pos, pos + ImVec2(max_w, ts.y + 4.f));
		dl->PushClipRect(clip.Min, clip.Max, true);
		dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, col, text, nullptr, max_w);
		dl->PopClipRect();
	}

	static void ReservePanelHeight(float content_w, float block_h)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(content_w, block_h));
		ImGui::SetCursorScreenPos(p0);
	}

	static void AdvancePanelHeight(float content_w, float block_h)
	{
		ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, block_h));
		ImGui::Dummy(ImVec2(content_w, 0.f));
	}

	static void DrawLogoImage(ImDrawList* dl, ImTextureID tex, int img_w, int img_h,
		const ImVec2& pos, const ImVec2& size)
	{
		if (tex == 0 || size.x <= 0.f || size.y <= 0.f) {
			return;
		}
		dl->AddImage(tex, pos, pos + size);
	}
}

namespace AboutPageUI {
	float DrawHeroPanel(float content_w)
	{
		const bool narrow = content_w < kNarrowBreakpoint;
		const float pad = 16.f;
		const float logo_max_h = narrow ? 56.f : 72.f;
		const float line_h = ImGui::GetTextLineHeight();
		const float text_lines_h = line_h * 4.f + 8.f;

		float hero_h = 0.f;
		if (narrow) {
			hero_h = pad * 2.f + logo_max_h + 10.f + text_lines_h;
		}
		else {
			hero_h = pad * 2.f + ImMax(logo_max_h, text_lines_h);
		}

		ReservePanelHeight(content_w, hero_h);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImRect bb(p0, p0 + ImVec2(content_w, hero_h));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		DrawPanelChrome(dl, bb, true);

		if (narrow) {
			float x = bb.Min.x + pad;
			float y = bb.Min.y + pad;

			if (Logo::HP_Cleaner_Logo.texture != 0) {
				const ImVec2 app_sz = FitImageSize(
					Logo::HP_Cleaner_Logo.image_width, Logo::HP_Cleaner_Logo.image_height,
					content_w * 0.42f, logo_max_h);
				DrawLogoImage(dl, (ImTextureID)(intptr_t)Logo::HP_Cleaner_Logo.texture,
					Logo::HP_Cleaner_Logo.image_width, Logo::HP_Cleaner_Logo.image_height,
					ImVec2(x, y), app_sz);
				x += app_sz.x + 16.f;
			}
			if (Logo::HPS_Logo.texture != 0) {
				const ImVec2 studio_sz = FitImageSize(
					Logo::HPS_Logo.image_width, Logo::HPS_Logo.image_height,
					content_w * 0.38f, logo_max_h);
				DrawLogoImage(dl, (ImTextureID)(intptr_t)Logo::HPS_Logo.texture,
					Logo::HPS_Logo.image_width, Logo::HPS_Logo.image_height,
					ImVec2(x, y), studio_sz);
			}

			y = bb.Min.y + pad + logo_max_h + 10.f;
			const float text_w = content_w - pad * 2.f;
			dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(cyan_neon()), "HP CLEANER++");
			y += line_h;
			char ver_line[48];
			snprintf(ver_line, sizeof(ver_line), I18N(u8"版本 %s"), kAppVersion);
			dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(ImGuiCol_Text), ver_line);
			y += line_h;
			dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
				I18N(u8"HalfPeople Studio · 系統清理與磁碟健康工具"));
			y += line_h;
			dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
				"© HalfPeople Studio. All rights reserved.");
		}
		else {
			float x = bb.Min.x + pad;
			const float center_y = bb.GetCenter().y;

			if (Logo::HP_Cleaner_Logo.texture != 0) {
				const ImVec2 app_sz = FitImageSize(
					Logo::HP_Cleaner_Logo.image_width, Logo::HP_Cleaner_Logo.image_height,
					180.f, logo_max_h);
				DrawLogoImage(dl, (ImTextureID)(intptr_t)Logo::HP_Cleaner_Logo.texture,
					Logo::HP_Cleaner_Logo.image_width, Logo::HP_Cleaner_Logo.image_height,
					ImVec2(x, center_y - app_sz.y * 0.5f), app_sz);
				x += app_sz.x + 20.f;
			}

			dl->AddLine(
				ImVec2(x, bb.Min.y + 20.f), ImVec2(x, bb.Max.y - 20.f),
				ImGui::GetColorU32(cyan_dark()), 1.f);
			x += 20.f;

			if (Logo::HPS_Logo.texture != 0) {
				const ImVec2 studio_sz = FitImageSize(
					Logo::HPS_Logo.image_width, Logo::HPS_Logo.image_height,
					140.f, logo_max_h);
				DrawLogoImage(dl, (ImTextureID)(intptr_t)Logo::HPS_Logo.texture,
					Logo::HPS_Logo.image_width, Logo::HPS_Logo.image_height,
					ImVec2(x, center_y - studio_sz.y * 0.5f), studio_sz);
				x += studio_sz.x + 20.f;
			}

			const float text_x = x;
			const float text_w = bb.Max.x - text_x - pad;
			float y = bb.Min.y + 18.f;
			dl->AddText(ImVec2(text_x, y), ImGui::GetColorU32(cyan_neon()), "HP CLEANER++");
			y += line_h;
			char ver_line[48];
			snprintf(ver_line, sizeof(ver_line), I18N(u8"版本 %s"), kAppVersion);
			dl->AddText(ImVec2(text_x, y), ImGui::GetColorU32(ImGuiCol_Text), ver_line);
			y += line_h;
			DrawWrappedText(dl, ImVec2(text_x, y), text_w, ImGui::GetColorU32(ImGuiCol_TextDisabled),
				I18N(u8"HalfPeople Studio · 系統清理與磁碟健康工具"));
			y += line_h;
			dl->AddText(ImVec2(text_x, y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
				"© HalfPeople Studio. All rights reserved.");
		}

		AdvancePanelHeight(content_w, hero_h);
		return hero_h;
	}

	float DrawIntroPanel(float content_w)
	{
		const float pad = 14.f;
		const float title_h = ImGui::GetTextLineHeight();
		const float wrap_w = content_w - pad * 2.f;
		const float body_h = ImGui::CalcTextSize(IntroText(), nullptr, false, wrap_w).y;
		const float block_h = pad + title_h + 8.f + body_h + pad;

		ReservePanelHeight(content_w, block_h);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImRect bb(p0, p0 + ImVec2(content_w, block_h));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		DrawPanelChrome(dl, bb);

		float y = bb.Min.y + pad;
		dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(cyan_neon()), I18N(u8"產品簡介"));
		y += title_h + 8.f;
		DrawWrappedText(dl, ImVec2(bb.Min.x + pad, y), wrap_w,
			ImGui::GetColorU32(ImGuiCol_Text), IntroText());

		AdvancePanelHeight(content_w, block_h);
		return block_h;
	}

	static void DrawKvRow(ImDrawList* dl, const ImRect& bb, float label_w,
		const char* label, const char* value, bool alt_row)
	{
		if (alt_row) {
			dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.05f, 0.08f, 0.08f, 1.f)));
		}
		dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Max.x, bb.Max.y),
			ImGui::GetColorU32(cyan_dark()), 1.f);
		const float pad_y = 6.f;
		dl->AddText(ImVec2(bb.Min.x + 10.f, bb.Min.y + pad_y),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
		const char* val = (value != nullptr && value[0] != '\0') ? value : "—";
		const float val_w = bb.GetWidth() - label_w - 16.f;
		DrawWrappedText(dl, ImVec2(bb.Min.x + label_w, bb.Min.y + pad_y), val_w,
			ImGui::GetColorU32(ImGuiCol_Text), val);
	}

	float DrawDeviceInfoPanel(float content_w, const AboutDeviceInfoSnapshot& d)
	{
		const float pad = 12.f;
		const float title_h = ImGui::GetTextLineHeight();
		const float subtitle_h = ImGui::GetTextLineHeight();
		const float label_w = 132.f;
		const float row_h = ImGui::GetTextLineHeightWithSpacing() + 12.f;

		char os_line[160];
		if (d.os_display_version[0] != '\0') {
			snprintf(os_line, sizeof(os_line), "%s · %s · %s",
				d.os_product, d.os_display_version, d.os_version);
		}
		else {
			snprintf(os_line, sizeof(os_line), "%s · %s", d.os_product, d.os_version);
		}

		struct KvItem { const char* key; const char* val; };
		const KvItem rows[] = {
			{ I18N(u8"作業系統"), os_line },
			{ I18N(u8"電腦名稱"), d.computer_name },
			{ I18N(u8"目前使用者"), d.user_name },
			{ I18N(u8"處理器"), d.cpu_name },
			{ I18N(u8"CPU 拓撲"), d.cpu_topology },
			{ I18N(u8"記憶體"), d.ram_summary },
			{ I18N(u8"顯示卡"), d.gpu_name },
			{ I18N(u8"系統架構"), d.system_arch },
			{ I18N(u8"權限"), d.admin_status },
			{ I18N(u8"Windows 目錄"), d.system_drive },
			{ I18N(u8"機器識別碼"), d.machine_id },
		};
		const int row_count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
		const float table_h = row_h * static_cast<float>(row_count);
		const float block_h = pad + title_h + 6.f + subtitle_h + 10.f + table_h + pad;

		ReservePanelHeight(content_w, block_h);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImRect bb(p0, p0 + ImVec2(content_w, block_h));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		DrawPanelChrome(dl, bb);

		float y = bb.Min.y + pad;
		dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(cyan_neon()), I18N(u8"本機裝置資訊"));
		y += title_h + 6.f;
		dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
			I18N(u8"以下為執行本程式時偵測到的系統與硬體摘要。"));
		y += subtitle_h + 10.f;

		const ImRect table_bb(ImVec2(bb.Min.x + pad, y), ImVec2(bb.Max.x - pad, y + table_h));
		dl->AddRect(table_bb.Min, table_bb.Max, ImGui::GetColorU32(cyan_dark()), 4.f, 0, 1.f);

		for (int i = 0; i < row_count; ++i) {
			const ImRect row_bb(
				ImVec2(table_bb.Min.x, table_bb.Min.y + row_h * static_cast<float>(i)),
				ImVec2(table_bb.Max.x, table_bb.Min.y + row_h * static_cast<float>(i + 1)));
			DrawKvRow(dl, row_bb, label_w, rows[i].key, rows[i].val, (i % 2) == 1);
		}

		AdvancePanelHeight(content_w, block_h);
		return block_h;
	}

	static float MeasureLibCardHeight(const ThirdPartyLib& lib, float col_w)
	{
		const float pad = 10.f;
		const float wrap_w = ImMax(40.f, col_w - pad * 2.f);
		const float header_h = ImGui::GetTextLineHeight() * 2.f + 10.f;
		const float usage_h = ImGui::CalcTextSize(lib.usage, nullptr, false, wrap_w).y;
		return pad + header_h + usage_h + 6.f + pad;
	}

	static void DrawLibCard(ImDrawList* dl, const ImRect& bb, const ThirdPartyLib& lib)
	{
		dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(card_bg()), 4.f);
		dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(cyan_dark()), 4.f, 0, 1.f);

		const float pad = 10.f;

		float y = bb.Min.y + pad;
		dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(cyan_neon()), lib.name);
		const ImVec2 name_ts = ImGui::CalcTextSize(lib.name);

		char ver[64];
		snprintf(ver, sizeof(ver), " %s", lib.version);
		dl->AddText(ImVec2(bb.Min.x + pad + name_ts.x + 4.f, y),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), ver);

		y += ImGui::GetTextLineHeight() + 4.f;
		char lic[48];
		snprintf(lic, sizeof(lic), I18N(u8"授權：%s"), I18N(lib.license));
		dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(ImVec4(0.7f, 0.85f, 0.85f, 1.f)), lic);

		y += ImGui::GetTextLineHeight() + 6.f;
		const float wrap_w = bb.GetWidth() - pad * 2.f;
		const char* usage_txt = (lib.usage != nullptr && lib.usage[0] != '\0')
			? I18N(lib.usage) : "";
		DrawWrappedText(dl, ImVec2(bb.Min.x + pad, y), wrap_w,
			ImGui::GetColorU32(ImGuiCol_Text), usage_txt);
	}

	float DrawThirdPartyPanel(float content_w)
	{
		const float pad = 12.f;
		const float title_h = ImGui::GetTextLineHeight();
		const float subtitle_h = ImGui::GetTextLineHeightWithSpacing() * 2.f;
		const int lib_count = static_cast<int>(sizeof(kThirdPartyLibs) / sizeof(kThirdPartyLibs[0]));

		const bool two_col = content_w >= 720.f;
		const float gap = 8.f;
		const float col_w = two_col ? (content_w - pad * 2.f - gap) * 0.5f : (content_w - pad * 2.f);

		float card_h = 88.f;
		for (int i = 0; i < lib_count; ++i) {
			card_h = ImMax(card_h, MeasureLibCardHeight(kThirdPartyLibs[i], col_w));
		}

		const int rows = two_col ? (lib_count + 1) / 2 : lib_count;
		float grid_h = 0.f;
		for (int row = 0; row < rows; ++row) {
			float row_h = 88.f;
			for (int i = 0; i < lib_count; ++i) {
				const int r = two_col ? (i / 2) : i;
				if (r != row) {
					continue;
				}
				row_h = ImMax(row_h, MeasureLibCardHeight(kThirdPartyLibs[i], col_w));
			}
			grid_h += row_h;
			if (row + 1 < rows) {
				grid_h += gap;
			}
		}
		const float foot_h = ImGui::GetTextLineHeightWithSpacing() * 2.f;
		const float block_h = pad + title_h + 6.f + subtitle_h + 10.f + grid_h + 10.f + foot_h + pad;

		ReservePanelHeight(content_w, block_h);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImRect bb(p0, p0 + ImVec2(content_w, block_h));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		DrawPanelChrome(dl, bb);

		float y = bb.Min.y + pad;
		dl->AddText(ImVec2(bb.Min.x + pad, y), ImGui::GetColorU32(cyan_neon()), I18N(u8"第三方元件與授權"));
		y += title_h + 6.f;
		DrawWrappedText(dl, ImVec2(bb.Min.x + pad, y), content_w - pad * 2.f,
			ImGui::GetColorU32(ImGuiCol_TextDisabled),
			I18N(u8"下列開源或系統元件用於本程式；完整授權文字請見專案目錄內對應 LICENSE 檔案。"));
		y += subtitle_h + 10.f;

		float grid_y = y;
		for (int row = 0; row < rows; ++row) {
			float row_h = 88.f;
			for (int i = 0; i < lib_count; ++i) {
				const int r = two_col ? (i / 2) : i;
				if (r == row) {
					row_h = ImMax(row_h, MeasureLibCardHeight(kThirdPartyLibs[i], col_w));
				}
			}
			for (int i = 0; i < lib_count; ++i) {
				const int col = two_col ? (i % 2) : 0;
				const int r = two_col ? (i / 2) : i;
				if (r != row) {
					continue;
				}
				const float cx = bb.Min.x + pad + static_cast<float>(col) * (col_w + gap);
				const float cy = grid_y;
				const float h = MeasureLibCardHeight(kThirdPartyLibs[i], col_w);
				const ImRect card_bb(ImVec2(cx, cy), ImVec2(cx + col_w, cy + h));
				DrawLibCard(dl, card_bb, kThirdPartyLibs[i]);
			}
			grid_y += row_h + gap;
		}

		y += grid_h + 10.f;
		DrawWrappedText(dl, ImVec2(bb.Min.x + pad, y), content_w - pad * 2.f,
			ImGui::GetColorU32(ImGuiCol_TextDisabled),
			I18N_JOIN(
				u8"字型：內嵌中文顯示字型（資源 IDR_FONT1）。",
				u8"若您再分發本軟體，請一併保留上述授權聲明與 NOTICE 檔案。"));

		AdvancePanelHeight(content_w, block_h);
		return block_h;
	}

	void DrawThirdPartyGithubLinks(float content_w)
	{
		const int lib_count = static_cast<int>(sizeof(kThirdPartyLibs) / sizeof(kThirdPartyLibs[0]));
		bool any = false;
		for (int i = 0; i < lib_count; ++i) {
			if (kThirdPartyLibs[i].github_url != nullptr) {
				any = true;
				break;
			}
		}
		if (!any) {
			return;
		}

		ImGui::TextDisabled(I18N(u8"開源專案倉庫"));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content_w);
		for (int i = 0; i < lib_count; ++i) {
			const ThirdPartyLib& lib = kThirdPartyLibs[i];
			if (lib.github_url == nullptr) {
				continue;
			}
			ImGui::PushID(i);
			char btn[96] = {};
			snprintf(btn, sizeof(btn), "%s · GitHub", lib.name);
			if (ImGui::SmallButton(btn)) {
				OpenUrlInBrowser(lib.github_url);
			}
			ImGui::SameLine(0.f, 8.f);
			ImGui::PopID();
		}
		ImGui::PopTextWrapPos();
		ImGui::NewLine();
	}
}
