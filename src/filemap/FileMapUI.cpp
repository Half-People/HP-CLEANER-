#define IMGUI_DEFINE_MATH_OPERATORS
#include "FileMapUI.h"
#include "FileMapScan.h"
#include "FileMapTree.h"
#include "HCleanTask.h"
#include "HPage.h"
#include "Hi18n.h"
#include "HUiPathBreadcrumb.h"
#include <imgui_internal.h>
#include <shellapi.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace FileMapUI {
	namespace Theme {
		const ImVec4 cyan_neon(0.00f, 0.90f, 0.90f, 1.0f);
		const ImVec4 cyan_dark(0.00f, 0.40f, 0.40f, 1.0f);
		const ImVec4 panel_bg(0.04f, 0.07f, 0.07f, 1.0f);
		const ImVec4 row_hover(0.08f, 0.14f, 0.14f, 1.0f);
	}

	namespace {
		enum class MeasureMode { BySize, ByCount };
		enum class ColorMode { ByExtension, ByType };

		MeasureMode g_measure = MeasureMode::BySize;
		ColorMode g_color_mode = ColorMode::ByExtension;
		struct TreemapCell {
			int item_index = -1;
			ImRect rect;
		};

		constexpr float kTreemapLabelMinW = 64.f;
		constexpr float kTreemapLabelMinH = 28.f;
		constexpr float kTreeRowHeight = 22.f;
		constexpr float kTreeIndent = 16.f;
		constexpr float kTreeArrowWidth = 14.f;
		constexpr float kTreeSwatchWidth = 10.f;

		static wchar_t g_last_synced_scope[1024] = {};
		static bool g_properties_from_tree = false;

		static std::wstring NormalizeMapPath(const wchar_t* path)
		{
			if (path == nullptr || path[0] == L'\0') {
				return {};
			}
			std::wstring s(path);
			for (wchar_t& ch : s) {
				if (ch == L'/') {
					ch = L'\\';
				}
			}
			while (s.size() > 3 && s.back() == L'\\') {
				s.pop_back();
			}
			return s;
		}

		static bool PathsEqualMap(const wchar_t* a, const wchar_t* b)
		{
			return _wcsicmp(NormalizeMapPath(a).c_str(), NormalizeMapPath(b).c_str()) == 0;
		}

		struct AccessPopupUiState {
			bool open = false;
			bool open_pending = false;
			FileMapScan::ScopeListAccess info = {};
		};

		static AccessPopupUiState g_access_popup = {};

		static void QueueAccessPopup(const FileMapScan::ScopeListAccess& info)
		{
			g_access_popup.info = info;
			g_access_popup.open_pending = true;
		}

		static void QueueAccessPopupForPath(const wchar_t* path_wide, bool access_denied,
			const char* headline, const char* extra_detail)
		{
			FileMapScan::ScopeListAccess info = FileMapScan::ProbeScopeListAccess(path_wide);
			if (headline != nullptr && headline[0] != '\0') {
				strncpy_s(info.headline_utf8, headline, _TRUNCATE);
			}
			if (extra_detail != nullptr && extra_detail[0] != '\0') {
				strncpy_s(info.detail_utf8, extra_detail, _TRUNCATE);
			}
			info.access_denied = access_denied || info.access_denied;
			info.can_list = false;
			QueueAccessPopup(info);
		}

		static void PathWideToUtf8(const wchar_t* path_wide, char* out, size_t out_size)
		{
			if (out == nullptr || out_size == 0) {
				return;
			}
			out[0] = '\0';
			if (path_wide == nullptr || path_wide[0] == L'\0') {
				return;
			}
			WideCharToMultiByte(CP_UTF8, 0, path_wide, -1, out, static_cast<int>(out_size), nullptr, nullptr);
		}

		static bool TryRequestScope(const wchar_t* path_wide)
		{
			if (path_wide == nullptr || path_wide[0] == L'\0') {
				HLOG_INFO("FileMap: 導航失敗（空路徑）");
				return false;
			}
			char path_utf8[1024] = {};
			PathWideToUtf8(path_wide, path_utf8, sizeof(path_utf8));
			if (!FileMapScan::RequestScanScope(path_wide)) {
				HLOG_INFO("FileMap: 無法進入 '{}'", path_utf8);
				FileMapScan::ScopeListAccess blocked = {};
				if (FileMapScan::ConsumeScopeAccessPopup(&blocked)) {
					QueueAccessPopup(blocked);
				}
				return false;
			}
			HLOG_INFO("FileMap: 已請求進入 '{}'", path_utf8);
			return true;
		}

		static bool IsItemAccessBlocked(const FileMapScan::ChildItem& item)
		{
			return item.is_directory
				&& item.measure_status == FileMapScan::MeasureStatus::AccessDenied;
		}

		static void DrawAccessDeniedModal()
		{
			if (g_access_popup.open_pending) {
				ImGui::OpenPopup("##filemap_access_modal");
				g_access_popup.open_pending = false;
				g_access_popup.open = true;
			}
			if (!g_access_popup.open) {
				return;
			}

			ImGui::SetNextWindowSize(ImVec2(440.f, 0.f), ImGuiCond_Always);
			const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize;
			if (ImGui::BeginPopupModal("##filemap_access_modal", nullptr, flags)) {
				const FileMapScan::ScopeListAccess& info = g_access_popup.info;
				const ImVec4 title_color = info.access_denied
					? ImVec4(1.f, 0.45f, 0.35f, 1.f)
					: ImVec4(1.f, 0.75f, 0.35f, 1.f);
				ImGui::TextColored(title_color, "%s",
					info.headline_utf8[0] != '\0' ? info.headline_utf8 : I18N(u8"無法存取資料夾"));
				ImGui::Spacing();
				if (info.path_utf8[0] != '\0') {
					ImGui::TextDisabled(I18N(u8"路徑"));
					ImGui::TextWrapped("%s", info.path_utf8);
					ImGui::Spacing();
				}
				if (info.detail_utf8[0] != '\0') {
					ImGui::TextWrapped("%s", info.detail_utf8);
				}
				else {
					ImGui::TextWrapped(I18N(u8"無法列出或檢視此資料夾的內容，Treemap 已阻止進入。"));
				}
				if (info.win32_error != 0) {
					ImGui::Spacing();
					ImGui::TextDisabled(I18N(u8"錯誤碼：%lu"), static_cast<unsigned long>(info.win32_error));
				}
				ImGui::Spacing();
				if (ImGui::Button(I18N(u8"確定"), ImVec2(120.f, 0))) {
					g_access_popup.open = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			else if (g_access_popup.open) {
				g_access_popup.open = false;
			}
		}

		static float ItemWeight(const FileMapScan::ChildItem& item, MeasureMode mode)
		{
			if (mode == MeasureMode::ByCount) {
				return static_cast<float>(item.item_count > 0 ? item.item_count : 1);
			}
			return static_cast<float>(item.size_bytes > 0 ? item.size_bytes : 1);
		}

		static ImU32 ColorForExtension(const char* ext)
		{
			if (ext != nullptr && strcmp(ext, u8"[Win保留]") == 0) {
				return ImGui::GetColorU32(ImVec4(0.82f, 0.62f, 0.22f, 0.95f));
			}
			unsigned hash = 2166136261u;
			if (ext != nullptr) {
				for (const char* p = ext; *p != '\0'; ++p) {
					hash ^= static_cast<unsigned char>(*p);
					hash *= 16777619u;
				}
			}
			const float hue = static_cast<float>(hash % 360) / 360.f;
			float r = 0.f, g = 0.f, b = 0.f;
			ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.85f, r, g, b);
			return ImGui::GetColorU32(ImVec4(r, g, b, 0.92f));
		}

		static bool ScanMeasureIsDisplayable(FileMapScan::MeasureStatus status)
		{
			return status != FileMapScan::MeasureStatus::Pending;
		}

		static FileMapScan::ChildItem ChildItemFromTreeNode(const FileMapTree::Node& node)
		{
			FileMapScan::ChildItem item = {};
			wcsncpy_s(item.full_path, node.path_wide, _TRUNCATE);
			strncpy_s(item.name_utf8, node.name_utf8, _TRUNCATE);
			strncpy_s(item.extension_utf8, node.extension_utf8, _TRUNCATE);
			item.is_directory = node.is_directory;
			item.size_bytes = node.size_bytes;
			item.measure_status = node.measure_status;

			FileMapScan::ChildItem measured = {};
			if (FileMapScan::TryLookupMeasured(node.path_wide, &measured)
				&& ScanMeasureIsDisplayable(measured.measure_status)) {
				return measured;
			}
			return item;
		}

		static ImU32 ColorForItem(const FileMapScan::ChildItem& item, ColorMode mode)
		{
			if (strcmp(item.extension_utf8, u8"[Win保留]") == 0) {
				return ColorForExtension(u8"[Win保留]");
			}
			if (mode == ColorMode::ByType) {
				return item.is_directory
					? ImGui::GetColorU32(ImVec4(0.15f, 0.45f, 0.55f, 0.95f))
					: ImGui::GetColorU32(ImVec4(0.25f, 0.55f, 0.35f, 0.95f));
			}
			return ColorForExtension(item.extension_utf8);
		}

		static bool IsWindowsReservedTag(const char* extension_utf8)
		{
			return extension_utf8 != nullptr && strcmp(extension_utf8, u8"[Win保留]") == 0;
		}

		static void LayoutSliceDiceEqualTiles(size_t count, float x, float y, float w, float h,
			bool horizontal, std::vector<ImRect>& out)
		{
			if (count == 0) {
				return;
			}
			const float ew = ImMax(w, 0.01f);
			const float eh = ImMax(h, 0.01f);
			if (horizontal) {
				const float slice = ew / static_cast<float>(count);
				for (size_t i = 0; i < count; ++i) {
					out.push_back(ImRect(
						ImVec2(x + slice * static_cast<float>(i), y),
						ImVec2(x + slice * static_cast<float>(i + 1), y + eh)));
				}
			}
			else {
				const float slice = eh / static_cast<float>(count);
				for (size_t i = 0; i < count; ++i) {
					out.push_back(ImRect(
						ImVec2(x, y + slice * static_cast<float>(i)),
						ImVec2(x + ew, y + slice * static_cast<float>(i + 1))));
				}
			}
		}

		static void LayoutSliceDiceRecurse(const std::vector<float>& weights, size_t begin, size_t end,
			float x, float y, float w, float h, bool horizontal, std::vector<ImRect>& out)
		{
			const size_t count = end - begin;
			if (count == 0) {
				return;
			}
			if (count == 1) {
				const float ew = ImMax(w, 0.01f);
				const float eh = ImMax(h, 0.01f);
				out.push_back(ImRect(ImVec2(x, y), ImVec2(x + ew, y + eh)));
				return;
			}
			if (w < 1.f || h < 1.f) {
				LayoutSliceDiceEqualTiles(count, x, y, w, h, horizontal, out);
				return;
			}

			float sum = 0.f;
			for (size_t i = begin; i < end; ++i) {
				sum += weights[i];
			}
			if (sum <= 0.f) {
				const float slice_w = w / static_cast<float>(count);
				for (size_t i = 0; i < count; ++i) {
					out.push_back(ImRect(
						ImVec2(x + slice_w * static_cast<float>(i), y),
						ImVec2(x + slice_w * static_cast<float>(i + 1), y + h)));
				}
				return;
			}

			const float half = sum * 0.5f;
			float acc = 0.f;
			size_t split = begin + 1;
			for (size_t i = begin; i < end; ++i) {
				acc += weights[i];
				if (acc >= half) {
					split = i + 1;
					break;
				}
			}
			if (split <= begin) {
				split = begin + 1;
			}
			if (split >= end) {
				split = end - 1;
			}

			float first_sum = 0.f;
			for (size_t i = begin; i < split; ++i) {
				first_sum += weights[i];
			}
			const float ratio = first_sum / sum;

			if (horizontal) {
				const float w1 = w * ratio;
				LayoutSliceDiceRecurse(weights, begin, split, x, y, w1, h, !horizontal, out);
				LayoutSliceDiceRecurse(weights, split, end, x + w1, y, w - w1, h, !horizontal, out);
			}
			else {
				const float h1 = h * ratio;
				LayoutSliceDiceRecurse(weights, begin, split, x, y, w, h1, !horizontal, out);
				LayoutSliceDiceRecurse(weights, split, end, x, y + h1, w, h - h1, !horizontal, out);
			}
		}

		static void LayoutSliceDice(const std::vector<float>& weights, float x, float y, float w, float h,
			bool horizontal, std::vector<ImRect>& out)
		{
			out.clear();
			if (weights.empty() || w <= 0.f || h <= 0.f) {
				return;
			}
			LayoutSliceDiceRecurse(weights, 0, weights.size(), x, y, w, h, horizontal, out);
			if (out.size() != weights.size()) {
				out.clear();
				LayoutSliceDiceEqualTiles(weights.size(), x, y, w, h, horizontal, out);
			}
		}

		static int HitTestTreemapCell(const ImVec2& mouse, const std::vector<TreemapCell>& cells)
		{
			int best = -1;
			float best_area = FLT_MAX;
			for (const TreemapCell& cell : cells) {
				if (!cell.rect.Contains(mouse)) {
					continue;
				}
				const float area = cell.rect.GetWidth() * cell.rect.GetHeight();
				if (area < best_area) {
					best_area = area;
					best = cell.item_index;
				}
			}
			return best;
		}

		static void FormatItemSizeText(const FileMapScan::ChildItem& item, char* buf, size_t buf_size);

		static void FormatTreeNodeSizeText(const FileMapTree::Node& node, char* buf, size_t buf_size)
		{
			if (buf == nullptr || buf_size == 0) {
				return;
			}
			if (strcmp(node.extension_utf8, u8"[磁碟]") == 0) {
				strncpy_s(buf, buf_size, I18N(u8"本機磁碟"), _TRUNCATE);
				return;
			}
			if (IsWindowsReservedTag(node.extension_utf8)) {
				strncpy_s(buf, buf_size, I18N(u8"系統保留"), _TRUNCATE);
				return;
			}
			if (node.load_state == FileMapTree::LoadState::Loading) {
				strncpy_s(buf, buf_size, I18N(u8"載入中…"), _TRUNCATE);
				return;
			}
			if (node.load_state == FileMapTree::LoadState::Failed) {
				if (node.measure_status == FileMapScan::MeasureStatus::AccessDenied) {
					strncpy_s(buf, buf_size, I18N(u8"無權限"), _TRUNCATE);
				}
				else {
					strncpy_s(buf, buf_size, I18N(u8"無法讀取"), _TRUNCATE);
				}
				return;
			}

			const FileMapScan::ChildItem probe = ChildItemFromTreeNode(node);
			if (ScanMeasureIsDisplayable(probe.measure_status)) {
				FormatItemSizeText(probe, buf, buf_size);
				if (node.is_directory && node.load_state == FileMapTree::LoadState::Loaded
					&& strcmp(buf, u8"空資料夾") == 0 && probe.size_bytes > 0) {
					char core[40] = {};
					FormatCleanSize(static_cast<int64_t>(probe.size_bytes), core, sizeof(core));
					strncpy_s(buf, buf_size, core, _TRUNCATE);
				}
				else if (node.is_directory && strcmp(buf, u8"空資料夾") == 0) {
					strncpy_s(buf, buf_size, I18N(u8"(空)"), _TRUNCATE);
				}
				return;
			}

			if (node.load_state == FileMapTree::LoadState::Unloaded && node.is_directory) {
				strncpy_s(buf, buf_size, "—", _TRUNCATE);
				return;
			}
			FormatItemSizeText(probe, buf, buf_size);
			if (node.is_directory && node.load_state == FileMapTree::LoadState::Loaded
				&& strcmp(buf, u8"空資料夾") == 0) {
				strncpy_s(buf, buf_size, I18N(u8"(空)"), _TRUNCATE);
			}
		}

		static void DrawTreeExpandGlyph(ImDrawList* dl, const ImRect& arrow_bb, bool expanded, ImU32 color)
		{
			const ImVec2 c = arrow_bb.GetCenter();
			const float s = 4.f;
			if (expanded) {
				dl->AddTriangleFilled(
					ImVec2(c.x - s, c.y - 1.5f),
					ImVec2(c.x + s, c.y - 1.5f),
					ImVec2(c.x, c.y + s),
					color);
			}
			else {
				dl->AddTriangleFilled(
					ImVec2(c.x - 1.5f, c.y - s),
					ImVec2(c.x - 1.5f, c.y + s),
					ImVec2(c.x + s, c.y),
					color);
			}
		}

		static void FormatItemSizeText(const FileMapScan::ChildItem& item, char* buf, size_t buf_size)
		{
			if (buf == nullptr || buf_size == 0) {
				return;
			}
			switch (item.measure_status) {
			case FileMapScan::MeasureStatus::Pending:
			case FileMapScan::MeasureStatus::Measuring:
				strncpy_s(buf, buf_size, I18N(u8"測量中…"), _TRUNCATE);
				return;
			case FileMapScan::MeasureStatus::AccessDenied:
				strncpy_s(buf, buf_size, I18N(u8"無法存取"), _TRUNCATE);
				return;
			case FileMapScan::MeasureStatus::Failed:
				strncpy_s(buf, buf_size, I18N(u8"測量失敗"), _TRUNCATE);
				return;
			case FileMapScan::MeasureStatus::Partial:
				if (item.size_bytes > 0) {
					char core[40] = {};
					FormatCleanSize(static_cast<int64_t>(item.size_bytes), core, sizeof(core));
					snprintf(buf, buf_size, "≥ %s", core);
				}
				else if (item.is_directory) {
					strncpy_s(buf, buf_size, I18N(u8"子資料夾未測完"), _TRUNCATE);
				}
				else {
					strncpy_s(buf, buf_size, I18N(u8"未測完"), _TRUNCATE);
				}
				return;
			default:
				break;
			}
			if (item.size_bytes == 0 && item.is_directory) {
				strncpy_s(buf, buf_size, u8"空資料夾", _TRUNCATE);
				return;
			}
			FormatCleanSize(static_cast<int64_t>(item.size_bytes), buf, buf_size);
		}

		static void TreeNodeToTooltip(const FileMapTree::Node& node, char* out, size_t out_size)
		{
			char size_buf[56] = {};
			FormatTreeNodeSizeText(node, size_buf, sizeof(size_buf));
			char path_utf8[1024] = {};
			WideCharToMultiByte(CP_UTF8, 0, node.path_wide, -1, path_utf8, sizeof(path_utf8), nullptr, nullptr);
			if (IsWindowsReservedTag(node.extension_utf8)) {
				snprintf(out, out_size, I18N(u8"%s  [Win保留]\n%s\n%s\n（Windows 保留目錄／連結點，相容性介面）"),
					node.name_utf8, size_buf, path_utf8);
			}
			else {
				snprintf(out, out_size, "%s\n%s\n%s", node.name_utf8, size_buf, path_utf8);
			}
		}

		static bool DrawCustomTreeRow(const FileMapTree::VisibleRow& row, FileMapScan::Snapshot& snap,
			float row_width)
		{
			using namespace Theme;
			const float row_h = kTreeRowHeight;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const ImRect row_bb(pos, pos + ImVec2(row_width, row_h));
			ImGui::ItemSize(row_bb);
			if (!ImGui::ItemAdd(row_bb, ImGui::GetID(row.node_id))) {
				return false;
			}

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const bool hovered = ImGui::IsMouseHoveringRect(row_bb.Min, row_bb.Max);

			if (row.node_id == FileMapTree::kRowPlaceholderLoading
				|| row.node_id == FileMapTree::kRowPlaceholderFailed
				|| row.node_id == FileMapTree::kRowPlaceholderAccessDenied
				|| row.node_id == FileMapTree::kRowPlaceholderEmpty) {
				const char* msg = I18N(u8"（載入中…）");
				if (row.node_id == FileMapTree::kRowPlaceholderAccessDenied) {
					msg = I18N(u8"（無權限，無法列出內容）");
				}
				else if (row.node_id == FileMapTree::kRowPlaceholderFailed) {
					msg = I18N(u8"（無法讀取此資料夾）");
				}
				else if (row.node_id == FileMapTree::kRowPlaceholderEmpty) {
					msg = I18N(u8"（空資料夾）");
					if (row.placeholder_parent_id != 0) {
						FileMapTree::Node parent = {};
						if (FileMapTree::CopyNode(row.placeholder_parent_id, parent)
							&& strcmp(parent.extension_utf8, u8"[磁碟]") == 0) {
							msg = I18N(u8"（此磁碟下無可列項目）");
						}
					}
				}
				const float x = row_bb.Min.x + static_cast<float>(row.depth) * kTreeIndent + kTreeArrowWidth + 6.f;
				dl->AddText(ImVec2(x, row_bb.Min.y + 3.f), ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.55f, 1.f)), msg);
				return false;
			}

			FileMapTree::Node node = {};
			if (!FileMapTree::CopyNode(row.node_id, node)) {
				return false;
			}

			const uint32_t selected_id = FileMapTree::GetSelectedNodeId();
			const bool selected = (selected_id == row.node_id);
			if (selected) {
				dl->AddRectFilled(row_bb.Min, row_bb.Max, ImGui::GetColorU32(row_hover));
			}
			else if (hovered) {
				dl->AddRectFilled(row_bb.Min, row_bb.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.10f, 0.10f, 1.f)));
			}

			const float indent_x = row_bb.Min.x + static_cast<float>(row.depth) * kTreeIndent;
			const ImRect arrow_bb(indent_x, row_bb.Min.y, indent_x + kTreeArrowWidth, row_bb.Max.y);
			if (row.has_children) {
				DrawTreeExpandGlyph(dl, arrow_bb, row.expanded, ImGui::GetColorU32(cyan_neon));
			}

			const FileMapScan::ChildItem color_item = ChildItemFromTreeNode(node);
			const ImU32 accent = ColorForItem(color_item, g_color_mode);
			const ImRect swatch_bb(arrow_bb.Max.x + 2.f, row_bb.Min.y + 6.f,
				arrow_bb.Max.x + 2.f + kTreeSwatchWidth, row_bb.Max.y - 6.f);
			dl->AddRectFilled(swatch_bb.Min, swatch_bb.Max, accent, 2.f);

			char size_buf[48] = {};
			FormatTreeNodeSizeText(node, size_buf, sizeof(size_buf));
			const ImVec2 size_ts = ImGui::CalcTextSize(size_buf);
			const float size_x = row_bb.Max.x - size_ts.x - 4.f;
			const char* size_txt = (strcmp(size_buf, u8"空資料夾") == 0) ? I18N(size_buf) : size_buf;
			dl->AddText(ImVec2(size_x, row_bb.Min.y + 3.f), ImGui::GetColorU32(ImVec4(0.55f, 0.75f, 0.75f, 1.f)), size_txt);

			const float name_x = swatch_bb.Max.x + 5.f;
			const float name_max_w = ImMax(20.f, size_x - name_x - 6.f);
			const ImVec4 name_rgba = ImGui::ColorConvertU32ToFloat4(accent);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
				ImMin(name_rgba.x + 0.35f, 1.f),
				ImMin(name_rgba.y + 0.35f, 1.f),
				ImMin(name_rgba.z + 0.35f, 1.f), 1.f));
			ImGui::RenderTextEllipsis(dl, ImVec2(name_x, row_bb.Min.y + 3.f), ImVec2(name_x + name_max_w, row_bb.Max.y),
				size_x - 4.f, node.name_utf8, nullptr, nullptr);
			if (IsWindowsReservedTag(node.extension_utf8)) {
				const char* badge = I18N(u8" Win保留");
				const ImVec2 name_ts = ImGui::CalcTextSize(node.name_utf8);
				const float badge_x = ImMin(name_x + name_ts.x + 4.f, size_x - 52.f);
				dl->AddText(ImVec2(badge_x, row_bb.Min.y + 3.f),
					ImGui::GetColorU32(ImVec4(0.85f, 0.65f, 0.25f, 1.f)), badge);
			}
			ImGui::PopStyleColor();

			if (hovered) {
				char tip[1100] = {};
				TreeNodeToTooltip(node, tip, sizeof(tip));
				ImGui::SetTooltip("%s", tip);
			}

			auto sync_selection_index = [&]() {
				int match = -1;
				for (size_t i = 0; i < snap.children.size(); ++i) {
					if (PathsEqualMap(snap.children[i].full_path, node.path_wide)) {
						match = static_cast<int>(i);
						break;
					}
				}
				if (match < 0) {
					for (size_t i = 0; i < snap.children.size(); ++i) {
						if (_stricmp(snap.children[i].name_utf8, node.name_utf8) == 0) {
							match = static_cast<int>(i);
							break;
						}
					}
				}
				FileMapScan::SetSelectedIndex(match);
			};

			auto try_enter_directory = [&]() -> bool {
				if (!node.is_directory) {
					return false;
				}
				if (PathsEqualMap(node.path_wide, snap.scope_path)) {
					return true;
				}
				if (node.load_state == FileMapTree::LoadState::Failed
					&& node.measure_status == FileMapScan::MeasureStatus::AccessDenied) {
					QueueAccessPopupForPath(node.path_wide, true, I18N(u8"無權限存取此資料夾"), nullptr);
					return false;
				}
				char path_utf8[1024] = {};
				PathWideToUtf8(node.path_wide, path_utf8, sizeof(path_utf8));
				HLOG_INFO("FileMap: 樹狀圖雙擊進入 '{}'", path_utf8);
				return TryRequestScope(node.path_wide);
			};

			const bool in_row = row_bb.Contains(ImGui::GetIO().MousePos);
			bool activated = false;
			if (in_row && row.has_children && arrow_bb.Contains(ImGui::GetIO().MousePos)
				&& ImGui::IsMouseClicked(0)) {
				FileMapTree::ToggleExpanded(row.node_id);
				activated = true;
			}
			else if (in_row && ImGui::IsMouseDoubleClicked(0)) {
				g_properties_from_tree = true;
				FileMapTree::SetSelectedNodeId(row.node_id);
				sync_selection_index();
				try_enter_directory();
				activated = true;
			}
			else if (in_row && ImGui::IsMouseClicked(0)) {
				g_properties_from_tree = true;
				FileMapTree::SetSelectedNodeId(row.node_id);
				sync_selection_index();
				activated = true;
			}
			return activated;
		}

		static void DrawCustomFileTree(FileMapScan::Snapshot& snap, float width)
		{
			std::vector<FileMapTree::VisibleRow> rows;
			FileMapTree::BuildVisibleRows(rows);

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					ImGui::PushID(i);
					DrawCustomTreeRow(rows[static_cast<size_t>(i)], snap, width);
					ImGui::PopID();
				}
			}
		}

		static std::vector<float> BuildTreemapWeights(const FileMapScan::Snapshot& snap)
		{
			std::vector<float> weights;
			weights.reserve(snap.children.size());
			for (const auto& item : snap.children) {
				weights.push_back(ItemWeight(item, g_measure));
			}
			return weights;
		}

		static void PaintTreemapCells(ImDrawList* dl, const FileMapScan::Snapshot& snap,
			const ImRect& plot_bb, bool draw_labels, std::vector<TreemapCell>& out_cells)
		{
			out_cells.clear();
			if (snap.children.empty()) {
				return;
			}

			const std::vector<float> weights = BuildTreemapWeights(snap);
			std::vector<ImRect> rects;
			LayoutSliceDice(weights, plot_bb.Min.x + 1.f, plot_bb.Min.y + 1.f,
				plot_bb.GetWidth() - 2.f, plot_bb.GetHeight() - 2.f, true, rects);

			const int selected_idx = FileMapScan::GetSelectedIndex();
			const size_t n = (std::min)(rects.size(), snap.children.size());
			for (size_t i = 0; i < n; ++i) {
				const FileMapScan::ChildItem& item = snap.children[i];
				ImRect r = rects[i];
				const float min_gap = 1.f;
				if (r.GetWidth() < min_gap) {
					r.Max.x = r.Min.x + min_gap;
				}
				if (r.GetHeight() < min_gap) {
					r.Max.y = r.Min.y + min_gap;
				}

				const ImU32 fill = ColorForItem(item, g_color_mode);
				dl->AddRectFilled(r.Min, r.Max, fill, 1.f);
				const bool selected = selected_idx == static_cast<int>(i);
				if (selected) {
					dl->AddRect(r.Min, r.Max, ImGui::GetColorU32(Theme::cyan_neon), 1.f, 0, 2.5f);
				}
				else {
					dl->AddRect(r.Min, r.Max, IM_COL32(0, 0, 0, 140), 1.f, 0, 1.f);
				}

				if (draw_labels && r.GetWidth() >= kTreemapLabelMinW && r.GetHeight() >= kTreemapLabelMinH) {
					char line1[128] = {};
					snprintf(line1, sizeof(line1), "%s", item.name_utf8);
					dl->PushClipRect(r.Min, r.Max, true);
					dl->AddText(ImVec2(r.Min.x + 3.f, r.Min.y + 2.f), IM_COL32(255, 255, 255, 240), line1);
					if (r.GetHeight() >= kTreemapLabelMinH + 16.f) {
						char line2[64] = {};
						FormatItemSizeText(item, line2, sizeof(line2));
						dl->AddText(ImVec2(r.Min.x +  3.f, r.Min.y + 16.f), IM_COL32(220, 255, 255, 220), line2);
					}
					dl->PopClipRect();
				}

				out_cells.push_back({ static_cast<int>(i), r });
			}
		}

		static bool HandleTreemapInteraction(const FileMapScan::Snapshot& snap,
			const ImRect& plot_bb, const std::vector<TreemapCell>& cells, bool allow_enter)
		{
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			if (!plot_bb.Contains(mouse)) {
				return false;
			}

			const int hovered = HitTestTreemapCell(mouse, cells);
			if (hovered >= 0) {
				const auto& hit = snap.children[static_cast<size_t>(hovered)];
				char tip[384] = {};
				char sz[48] = {};
				FormatItemSizeText(hit, sz, sizeof(sz));
				snprintf(tip, sizeof(tip), "%s\n%s", hit.name_utf8, sz);
				ImGui::SetTooltip("%s", tip);
			}

			if (hovered >= 0 && ImGui::IsMouseClicked(0)) {
				g_properties_from_tree = false;
				FileMapScan::SetSelectedIndex(hovered);
				const auto& hit = snap.children[static_cast<size_t>(hovered)];
				const uint32_t tree_id = FileMapTree::FindNodeIdByPath(hit.full_path);
				if (tree_id != 0) {
					FileMapTree::SetSelectedNodeId(tree_id);
				}
				else {
					FileMapTree::SetSelectedNodeId(0);
				}
			}
			if (allow_enter && hovered >= 0 && ImGui::IsMouseDoubleClicked(0)) {
				const auto& hit = snap.children[static_cast<size_t>(hovered)];
				if (hit.is_directory) {
					if (!PathsEqualMap(hit.full_path, snap.scope_path)) {
						char path_utf8[1024] = {};
						PathWideToUtf8(hit.full_path, path_utf8, sizeof(path_utf8));
						HLOG_INFO("FileMap: Treemap 雙擊進入 '{}'", path_utf8);
						TryRequestScope(hit.full_path);
					}
					return true;
				}
			}
			return false;
		}

		static std::vector<size_t> SortedIndicesByWeight(const FileMapScan::Snapshot& snap, MeasureMode mode)
		{
			std::vector<size_t> indices(snap.children.size());
			for (size_t i = 0; i < indices.size(); ++i) {
				indices[i] = i;
			}
			std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
				return ItemWeight(snap.children[a], mode) > ItemWeight(snap.children[b], mode);
			});
			return indices;
		}

		static std::wstring ParentScope(const std::wstring& scope)
		{
			if (scope.size() <= 3) {
				return scope;
			}
			std::wstring trimmed = scope;
			while (!trimmed.empty() && trimmed.back() == L'\\') {
				trimmed.pop_back();
			}
			const size_t pos = trimmed.find_last_of(L'\\');
			if (pos == std::wstring::npos) {
				return scope;
			}
			if (pos <= 2 && trimmed.size() >= 2 && trimmed[1] == L':') {
				std::wstring root = trimmed.substr(0, pos + 1);
				if (root.back() != L'\\') {
					root += L'\\';
				}
				return root;
			}
			return trimmed.substr(0, pos + 1);
		}

		static void OpenInExplorer(const wchar_t* path)
		{
			if (path == nullptr || path[0] == L'\0') {
				return;
			}
			ShellExecuteW(nullptr, L"open", L"explorer.exe", path, nullptr, SW_SHOWNORMAL);
		}

		static void DrawToolbar(FileMapScan::Snapshot& snap)
		{
			using namespace Theme;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, panel_bg);
			if (ImGui::BeginChild("##filemap_toolbar", ImVec2(0, kToolbarHeight), true,
				ImGuiWindowFlags_NoScrollbar)) {
				if (ImGui::Button(I18N(u8"重新掃描"), ImVec2(88, 0))) {
					HLOG_INFO("FileMap: 使用者按下重新掃描 scope='{}'", snap.scope_utf8);
					FileMapScan::RequestScanScope(snap.scope_path, true);
				}
				ImGui::SameLine();
				if (ImGui::Button(I18N(u8"上一層"), ImVec2(72, 0))) {
					const std::wstring parent = ParentScope(snap.scope_path);
					char parent_utf8[1024] = {};
					PathWideToUtf8(parent.c_str(), parent_utf8, sizeof(parent_utf8));
					HLOG_INFO("FileMap: 上一層 → '{}'", parent_utf8);
					TryRequestScope(parent.c_str());
				}
				ImGui::SameLine();
				if (ImGui::Button(I18N(u8"在檔案總管開啟"), ImVec2(120, 0))) {
					OpenInExplorer(snap.scope_path);
				}

				ImGui::SameLine(0, 18);
				const bool by_size = g_measure == MeasureMode::BySize;
				if (ImGui::RadioButton(I18N(u8"依大小"), by_size)) {
					g_measure = MeasureMode::BySize;
				}
				ImGui::SameLine();
				if (ImGui::RadioButton(I18N(u8"依檔案數"), !by_size)) {
					g_measure = MeasureMode::ByCount;
				}

				ImGui::SameLine(0, 18);
				const bool by_ext = g_color_mode == ColorMode::ByExtension;
				if (ImGui::RadioButton(I18N(u8"副檔名著色"), by_ext)) {
					g_color_mode = ColorMode::ByExtension;
				}
				ImGui::SameLine();
				if (ImGui::RadioButton(I18N(u8"類型著色"), !by_ext)) {
					g_color_mode = ColorMode::ByType;
				}

				if (snap.scanning) {
					ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180);
					ImGui::ProgressBar(snap.progress, ImVec2(170, 0), snap.status_text);
				}
				else if (snap.status_text[0] != '\0') {
					ImGui::SameLine();
					ImGui::TextDisabled("%s", snap.status_text);
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		static void DrawLeftPanel(FileMapScan::Snapshot& snap, float width, float height)
		{
			using namespace Theme;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, panel_bg);
			if (ImGui::BeginChild("##filemap_tree", ImVec2(width, height), true)) {
				ImGui::TextColored(cyan_neon, "%s", I18N(u8"檔案樹"));
				ImGui::TextDisabled(I18N(u8"單擊選取；雙擊資料夾進入；點箭頭展開／收合"));
				ImGui::Separator();

				static char filter_buf[128] = {};
				if (filter_buf[0] == '\0' && FileMapTree::GetFilterText()[0] != '\0') {
					strncpy_s(filter_buf, FileMapTree::GetFilterText(), _TRUNCATE);
				}
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputTextWithHint("##tree_filter", I18N(u8"篩選名稱 / 副檔名 / 路徑…"), filter_buf, sizeof(filter_buf))) {
					FileMapTree::SetFilterText(filter_buf);
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					FileMapTree::SetFilterText(filter_buf);
				}

				static int filter_mode_ui = 0;
				if (ImGui::RadioButton(I18N(u8"全部"), filter_mode_ui == 0)) {
					filter_mode_ui = 0;
					FileMapTree::SetFilterMode(FileMapTree::FilterMode::All);
				}
				ImGui::SameLine();
				if (ImGui::RadioButton(I18N(u8"資料夾"), filter_mode_ui == 1)) {
					filter_mode_ui = 1;
					FileMapTree::SetFilterMode(FileMapTree::FilterMode::FoldersOnly);
				}
				ImGui::SameLine();
				if (ImGui::RadioButton(I18N(u8"檔案"), filter_mode_ui == 2)) {
					filter_mode_ui = 2;
					FileMapTree::SetFilterMode(FileMapTree::FilterMode::FilesOnly);
				}

				const float list_h = ImMax(80.f, ImGui::GetContentRegionAvail().y);
				if (ImGui::BeginChild("##filemap_tree_list", ImVec2(0, list_h), false)) {
					DrawCustomFileTree(snap, ImGui::GetContentRegionAvail().x);
				}
				ImGui::EndChild();
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		static void OnPathBreadcrumbNavigate(const wchar_t* path_wide, void*)
		{
			if (path_wide == nullptr || path_wide[0] == L'\0') {
				return;
			}
			char path_utf8[1024] = {};
			PathWideToUtf8(path_wide, path_utf8, sizeof(path_utf8));
			HLOG_INFO("FileMap: 麵包屑導航 '{}'", path_utf8);
			TryRequestScope(path_wide);
		}

		static void DrawPathBreadcrumbBar(const FileMapScan::Snapshot& snap, float width)
		{
			HUiPathBreadcrumb::Draw("##filemap_path_bc", snap.scope_path, width,
				OnPathBreadcrumbNavigate, nullptr);
		}

		static void DrawTreemap(FileMapScan::Snapshot& snap, float width, float height,
			std::vector<TreemapCell>& out_cells)
		{
			using namespace Theme;
			out_cells.clear();

			const float header_h = ImGui::GetTextLineHeightWithSpacing() + 4.f;
			const float plot_h = ImMax(48.f, height - header_h - 4.f);

			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.03f, 0.03f, 1.f));
			if (ImGui::BeginChild("##filemap_treemap", ImVec2(width, height), true,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
				ImGui::TextColored(cyan_neon, "%s", I18N(u8"文件占比（Treemap）"));
				ImGui::SameLine();
				ImGui::TextDisabled(I18N(u8"點擊區塊選取；雙擊資料夾進入"));

				const ImVec2 plot_size(ImMax(32.f, ImGui::GetContentRegionAvail().x), plot_h);
				const ImVec2 plot_min = ImGui::GetCursorScreenPos();
				const ImRect plot_bb(plot_min, plot_min + plot_size);
				ImGui::InvisibleButton("##treemap_plot_hit", plot_size);
				const bool plot_hovered = ImGui::IsItemHovered();

				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(plot_bb.Min, plot_bb.Max, ImGui::GetColorU32(ImVec4(0.03f, 0.05f, 0.05f, 1.f)), 4.f);
				dl->AddRect(plot_bb.Min, plot_bb.Max, ImGui::GetColorU32(cyan_dark), 4.f, 0, 1.2f);

				if (snap.scanning && snap.children.empty()) {
					const char* text = I18N(u8"正在分析此層級…");
					const ImVec2 ts = ImGui::CalcTextSize(text);
					dl->AddText(plot_bb.GetCenter() - ts * 0.5f, ImGui::GetColorU32(cyan_neon), text);
				}
				else if (snap.scope_block == FileMapScan::ScopeBlockReason::AccessDenied) {
					const char* text = I18N(u8"無權限檢視此資料夾");
					const ImVec2 ts = ImGui::CalcTextSize(text);
					dl->AddText(plot_bb.GetCenter() - ts * 0.5f, ImGui::GetColorU32(ImVec4(1.f, 0.5f, 0.4f, 1.f)), text);
				}
				else if (snap.children.empty()) {
					const char* text = (snap.scope_block != FileMapScan::ScopeBlockReason::None)
						? I18N(u8"無法讀取此資料夾內容")
						: I18N(u8"此資料夾為空（無子項目）");
					const ImVec2 ts = ImGui::CalcTextSize(text);
					dl->AddText(plot_bb.GetCenter() - ts * 0.5f, ImGui::GetColorU32(ImVec4(0.6f, 0.6f, 0.6f, 1.f)), text);
				}
				else {
					PaintTreemapCells(dl, snap, plot_bb, true, out_cells);
					const bool mouse_in_plot = plot_bb.Contains(ImGui::GetIO().MousePos);
					if (plot_hovered || mouse_in_plot) {
						HandleTreemapInteraction(snap, plot_bb, out_cells, !snap.scanning);
					}
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		static void DrawExtensionLegend(const FileMapScan::Snapshot& snap, float width, float height)
		{
			using namespace Theme;
			struct ExtRow {
				char ext[32];
				int count = 0;
				uint64_t total = 0;
			};

			std::unordered_map<std::string, ExtRow> map;
			for (const auto& item : snap.children) {
				const std::string key = item.extension_utf8;
				auto& row = map[key];
				if (row.ext[0] == '\0') {
					strncpy_s(row.ext, item.extension_utf8, _TRUNCATE);
				}
				++row.count;
				row.total += item.size_bytes;
			}

			std::vector<ExtRow> rows;
			rows.reserve(map.size());
			for (auto& kv : map) {
				rows.push_back(kv.second);
			}
			std::sort(rows.begin(), rows.end(), [](const ExtRow& a, const ExtRow& b) {
				return a.total > b.total;
			});

			ImGui::PushStyleColor(ImGuiCol_ChildBg, panel_bg);
			if (ImGui::BeginChild("##filemap_ext", ImVec2(width, height), true)) {
				ImGui::TextColored(cyan_neon, "%s", I18N(u8"副檔名 / 類型"));
				ImGui::Separator();
				const float table_h = ImMax(60.f, ImGui::GetContentRegionAvail().y);
				if (ImGui::BeginTable("##filemap_ext_table", 3,
					ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
					ImVec2(0, table_h))) {
					ImGui::TableSetupColumn(I18N(u8"色"), ImGuiTableColumnFlags_WidthFixed, 28.f);
					ImGui::TableSetupColumn(I18N(u8"類型"), ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn(I18N(u8"統計"), ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableHeadersRow();
					int row_id = 0;
					for (const ExtRow& row : rows) {
						ImGui::TableNextRow();
						ImGui::PushID(row_id++);
						ImGui::TableNextColumn();
						FileMapScan::ChildItem color_probe = {};
						strncpy_s(color_probe.extension_utf8, row.ext, _TRUNCATE);
						color_probe.is_directory = (strcmp(row.ext, u8"[資料夾]") == 0);
						const ImU32 col = ColorForItem(color_probe, g_color_mode);
						const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(col);
						ImGui::ColorButton("##legend_swatch", rgba, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(row.ext);
						ImGui::TableNextColumn();
						char sz[48] = {};
						FormatCleanSize(static_cast<int64_t>(row.total), sz, sizeof(sz));
						ImGui::TextDisabled(I18N(u8"%d 項 · %s"), row.count, sz);
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		static void DrawProperties(const FileMapScan::Snapshot& snap, float width, float height)
		{
			using namespace Theme;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, panel_bg);
			if (ImGui::BeginChild("##filemap_props", ImVec2(width, height), true)) {
				ImGui::TextColored(cyan_neon, "%s", I18N(u8"內容"));
				ImGui::Separator();

				FileMapTree::Node tree_node = {};
				const bool tree_sel = FileMapTree::CopyNode(FileMapTree::GetSelectedNodeId(), tree_node);
				const bool snap_item_valid = snap.selected_index >= 0
					&& snap.selected_index < static_cast<int>(snap.children.size());

				if (!tree_sel && !snap_item_valid) {
					ImGui::TextDisabled(I18N(u8"點擊 Treemap 或左側檔案樹以檢視屬性"));
					ImGui::Spacing();
					ImGui::Text(I18N(u8"目前路徑"));
					ImGui::TextWrapped("%s", snap.scope_utf8);
					ImGui::EndChild();
					ImGui::PopStyleColor();
					return;
				}

				FileMapScan::ChildItem item_storage = {};
				const FileMapScan::ChildItem* item_ptr = nullptr;
				const bool use_tree = g_properties_from_tree && tree_sel;
				if (use_tree) {
					item_storage = ChildItemFromTreeNode(tree_node);
					item_ptr = &item_storage;
				}
				else if (snap_item_valid) {
					item_ptr = &snap.children[static_cast<size_t>(snap.selected_index)];
				}
				else if (tree_sel) {
					item_storage = ChildItemFromTreeNode(tree_node);
					item_ptr = &item_storage;
				}
				if (item_ptr == nullptr) {
					ImGui::TextDisabled(I18N(u8"無法載入選取項目屬性"));
					ImGui::EndChild();
					ImGui::PopStyleColor();
					return;
				}
				const FileMapScan::ChildItem& item = *item_ptr;
				char size_buf[64] = {};
				FormatItemSizeText(item, size_buf, sizeof(size_buf));

				ImGui::Text(I18N(u8"名稱"));
				ImGui::TextWrapped("%s", item.name_utf8);
				if (IsWindowsReservedTag(item.extension_utf8)) {
					ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.f),
						I18N(u8"標籤：Windows 保留目錄（相容性連結點）"));
				}
				ImGui::Text(I18N(u8"類型"));
				ImGui::TextUnformatted(item.is_directory ? I18N(u8"資料夾") : I18N(u8"檔案"));
				ImGui::Text(I18N(u8"副檔名"));
				ImGui::TextUnformatted(
					(item.extension_utf8[0] == '[' || item.extension_utf8[0] == '(')
						? I18N(item.extension_utf8) : item.extension_utf8);
				ImGui::Text(I18N(u8"大小"));
				ImGui::TextUnformatted(size_buf);
				if (item.measure_status == FileMapScan::MeasureStatus::Partial) {
					ImGui::TextDisabled(I18N(u8"（大小可能僅含部分內容）"));
				}
				else if (item.measure_status == FileMapScan::MeasureStatus::AccessDenied) {
					ImGui::TextDisabled(I18N(u8"（無權限讀取此資料夾內容）"));
				}
				else if (item.measure_status != FileMapScan::MeasureStatus::Ok) {
					ImGui::TextDisabled(I18N(u8"（大小尚未完成或無法讀取）"));
				}
				if (g_measure == MeasureMode::ByCount) {
					ImGui::Text(I18N(u8"檔案數（估算）"));
					ImGui::Text("%llu", static_cast<unsigned long long>(item.item_count));
				}
				ImGui::Text(I18N(u8"完整路徑"));
				char path_utf8[1024] = {};
				WideCharToMultiByte(CP_UTF8, 0, item.full_path, -1, path_utf8, sizeof(path_utf8), nullptr, nullptr);
				ImGui::TextWrapped("%s", path_utf8);

				WIN32_FILE_ATTRIBUTE_DATA fad = {};
				if (GetFileAttributesExW(item.full_path, GetFileExInfoStandard, &fad)) {
					SYSTEMTIME st = {};
					FileTimeToSystemTime(&fad.ftLastWriteTime, &st);
					ImGui::Text(I18N(u8"修改時間"));
					ImGui::Text("%04u-%02u-%02u %02u:%02u:%02u",
						st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
				}

				ImGui::Spacing();
				if (ImGui::Button(I18N(u8"在檔案總管顯示"), ImVec2(-1, 0))) {
					OpenInExplorer(item.full_path);
				}
				if (item.is_directory && ImGui::Button(I18N(u8"進入此資料夾"), ImVec2(-1, 0))) {
					if (IsItemAccessBlocked(item)) {
						QueueAccessPopupForPath(item.full_path, true, I18N(u8"無權限存取此資料夾"), nullptr);
					}
					else {
						TryRequestScope(item.full_path);
					}
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		static void DrawStatusBar(const FileMapScan::Snapshot& snap)
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::panel_bg);
			if (ImGui::BeginChild("##filemap_status", ImVec2(0, kStatusBarHeight), true,
				ImGuiWindowFlags_NoScrollbar)) {
				uint64_t total_size = 0;
				uint64_t total_count = 0;
				for (const auto& c : snap.children) {
					total_size += c.size_bytes;
					total_count += c.item_count;
				}
				char total_sz[48] = {};
				FormatCleanSize(static_cast<int64_t>(total_size), total_sz, sizeof(total_sz));
				ImGui::Text(I18N(u8"項目：%zu"), snap.children.size());
				ImGui::SameLine(0, 16);
				ImGui::Text(I18N(u8"總大小：%s"), total_sz);
				ImGui::SameLine(0, 16);
				ImGui::Text(I18N(u8"總檔案數（估算）：%llu"), static_cast<unsigned long long>(total_count));
				ImGui::SameLine(0, 16);
				ImGui::TextDisabled(I18N(u8"路徑：%s"), snap.scope_utf8);
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}
	}

	void RenderContent()
	{
		FileMapTree::Tick();

		FileMapScan::ScopeListAccess blocked_popup = {};
		if (FileMapScan::ConsumeScopeAccessPopup(&blocked_popup)) {
			QueueAccessPopup(blocked_popup);
		}

		FileMapScan::Snapshot snap = FileMapScan::GetSnapshot();
		snap.selected_index = FileMapScan::GetSelectedIndex();

		if (snap.scope_path[0] != L'\0' && !PathsEqualMap(g_last_synced_scope, snap.scope_path)) {
			char scope_utf8[1024] = {};
			PathWideToUtf8(snap.scope_path, scope_utf8, sizeof(scope_utf8));
			HLOG_INFO("FileMap: scope 變更為 '{}'", scope_utf8);
			wcsncpy_s(g_last_synced_scope, snap.scope_path, _TRUNCATE);
			FileMapTree::ExpandAndSelectPath(snap.scope_path);
		}

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float body_h = avail.y - kToolbarHeight - kStatusBarHeight;
		const float center_plot_h = ImMax(120.f, body_h - kPathBreadcrumbHeight - 4.f);

		DrawToolbar(snap);

		const float left_w = 268.f;
		const float right_w = 280.f;
		const float center_w = ImMax(200.f, avail.x - left_w - right_w - 16.f);
		const float right_split = body_h * 0.42f;

		ImGui::BeginGroup();
		DrawLeftPanel(snap, left_w, body_h);
		ImGui::SameLine();
		ImGui::BeginGroup();
		DrawPathBreadcrumbBar(snap, center_w);
		std::vector<TreemapCell> cells;
		DrawTreemap(snap, center_w, center_plot_h, cells);
		(void)cells;
		ImGui::EndGroup();
		ImGui::SameLine();
		ImGui::BeginGroup();
		DrawExtensionLegend(snap, right_w, right_split);
		DrawProperties(snap, right_w, body_h - right_split - 4.f);
		ImGui::EndGroup();
		ImGui::EndGroup();

		DrawStatusBar(snap);
		DrawAccessDeniedModal();
	}
}
