#include "Hi18nBuiltin.h"
#include <cstring>

namespace Hi18nBuiltin {
	namespace {
		struct Row {
			const char* zh_tw;
			const char* zh_cn;
			const char* en_us;
			const char* ja_jp;
		};

		// 內建預設翻譯（與 Hi18n::Key 順序一致）
		static const Row kBuiltin[] = {
			{ "首頁", "首页", "Home", "ホーム" },
			{ "系統清理工具", "系统清理工具", "System Cleaner", "システムクリーナー" },
			{ "系統優化", "系统优化", "System Optimize", "システム最適化" },
			{ "硬盤健康度檢測", "硬盘健康度检测", "Disk Health", "ディスク健康診断" },
			{ "文件地圖", "文件地图", "File Map", "ファイルマップ" },
			{ "清理歷史", "清理历史", "Clean History", "クリーン履歴" },
			{ "關於我們", "关于我们", "About", "について" },

			{ "語言", "语言", "Language", "言語" },
			{ "繁體中文", "繁體中文", "Traditional Chinese", "繁體中文" },
			{ "简体中文", "简体中文", "Simplified Chinese", "简体中文" },
			{ "English", "English", "English", "English" },
			{ "日本語", "日本語", "Japanese", "日本語" },

			{ "系統", "系统", "System", "システム" },
			{ "即時", "即时", "Live", "リアルタイム" },
			{ "系統文件", "系统文件", "System Files", "システムファイル" },
			{ "磁碟", "磁盘", "Disks", "ドライブ" },
			{ "釋放內存", "释放内存", "Free Memory", "メモリ解放" },
			{ "處理中…", "处理中…", "Working…", "処理中…" },
			{ "（以管理員執行可額外清理系統快取）",
				"（以管理员执行可额外清理系统缓存）",
				"(Run as admin to purge more system cache)",
				"（管理者として実行するとシステムキャッシュを追加削除できます）" },
			{ "整理程序工作集；管理員可清理更多系統快取",
				"整理程序工作集；管理员可清理更多系统缓存",
				"Trim process working set; admin can purge more cache",
				"プロセスのワーキングセットを整理。管理者はより多くのキャッシュを削除可能" },
			{ "重新分析", "重新分析", "Rescan", "再分析" },
			{ "系統清理工具 →", "系统清理工具 →", "System Cleaner →", "システムクリーナー →" },
			{ "掃描中…", "扫描中…", "Scanning…", "スキャン中…" },
			{ "已用", "已用", "Used", "使用済み" },
			{ "可用", "可用", "Free", "空き" },
			{ "%s 概覽", "%s 概览", "%s overview", "%s 概要" },
			{ "%s 儲存分類", "%s 存储分类", "%s storage", "%s ストレージ分類" },
			{ "掃描中… %.0f%%", "扫描中… %.0f%%", "Scanning… %.0f%%", "スキャン中… %.0f%%" },
			{ "系統", "系统", "System", "システム" },
			{ "程式", "程序", "Apps", "アプリ" },
			{ "使用者", "用户", "User", "ユーザー" },
			{ "暫存", "暂存", "Temp", "一時" },
			{ "其他", "其他", "Other", "その他" },
			{ "可用", "可用", "Free", "空き" },
			{ "系統清理 · 磁碟健康 · 即時監控",
				"系统清理 · 磁盘健康 · 即时监控",
				"Cleaner · Disk health · Live monitor",
				"クリーナー · ディスク健康 · リアルタイム監視" },
			{ "系統清理", "系统清理", "System Cleaner", "システムクリーナー" },
			{ "清理歷史", "清理历史", "Clean History", "クリーン履歴" },
			{ "硬碟健康", "硬盘健康", "Disk Health", "ディスク健康" },
			{ "可清理（已選）", "可清理（已选）", "Reclaimable (selected)", "削除可能（選択済み）" },
			{ "前往系統清理可調整", "前往系统清理可调整", "Adjust in System Cleaner",
				"システムクリーナーで調整" },
			{ "上次釋放", "上次释放", "Last freed", "前回の解放量" },
			{ "累計釋放 / 可掃描", "累计释放 / 可扫描", "Total freed / scannable",
				"累計解放 / スキャン可能" },
			{ "尚未清理", "尚未清理", "Not cleaned yet", "未クリーン" },
			{ "掃描後顯示", "扫描后显示", "Shown after scan", "スキャン後に表示" },
			{ "前往系統清理可調整", "前往系统清理可调整", "Adjust in System Cleaner",
				"システムクリーナーで調整" },
			{ "即時 · %zu 個磁碟", "即时 · %zu 个磁盘", "Live · %zu drives",
				"リアルタイム · %zu ドライブ" },
			{ "未偵測到本機固定或卸除式磁碟",
				"未检测到本机固定或可移动磁盘",
				"No fixed or removable drives detected",
				"固定またはリムーバブルドライブが検出されませんでした" },
			{ "活動 %.0f%%", "活动 %.0f%%", "Activity %.0f%%", "アクティビティ %.0f%%" },
			{ "已用", "已用", "used", "使用" },
			{ "即時監控中  |  上次清理釋放 %s  |  共 %d 次紀錄",
				"即时监控中  |  上次清理释放 %s  |  共 %d 次记录",
				"Live  |  Last freed %s  |  %d sessions",
				"リアルタイム監視中  |  前回 %s 解放  |  %d 件の記録" },
			{ "即時監控中  |  尚無清理紀錄  |  使用上方「系統清理」開始",
				"即时监控中  |  尚无清理记录  |  使用上方「系统清理」开始",
				"Live  |  No clean history  |  Use System Cleaner above",
				"リアルタイム監視中  |  履歴なし  |  上の「システムクリーナー」から開始" },
		};

		static_assert(static_cast<int>(Hi18n::Key::Count) == static_cast<int>(sizeof(kBuiltin) / sizeof(kBuiltin[0])),
			"Hi18nBuiltin key/table mismatch");
	}

	int SlotFromCode(const char* code)
	{
		if (code == nullptr || code[0] == '\0') {
			return -1;
		}
		if (_stricmp(code, "zh-TW") == 0 || _stricmp(code, "zh_TW") == 0) {
			return static_cast<int>(Slot::ZhTW);
		}
		if (_stricmp(code, "zh-CN") == 0 || _stricmp(code, "zh_CN") == 0) {
			return static_cast<int>(Slot::ZhCN);
		}
		if (_stricmp(code, "en-US") == 0 || _stricmp(code, "en_US") == 0
			|| _stricmp(code, "en") == 0) {
			return static_cast<int>(Slot::EnUS);
		}
		if (_stricmp(code, "ja-JP") == 0 || _stricmp(code, "ja_JP") == 0
			|| _stricmp(code, "ja") == 0) {
			return static_cast<int>(Slot::JaJP);
		}
		return -1;
	}

	const char* CodeFromSlot(Slot slot)
	{
		switch (slot) {
		case Slot::ZhCN: return "zh-CN";
		case Slot::EnUS: return "en-US";
		case Slot::JaJP: return "ja-JP";
		default: return "zh-TW";
		}
	}

	const char* Get(Slot slot, Hi18n::Key key)
	{
		const int idx = static_cast<int>(key);
		if (idx < 0 || idx >= static_cast<int>(Hi18n::Key::Count)) {
			return "";
		}
		const Row& row = kBuiltin[idx];
		switch (slot) {
		case Slot::ZhCN: return row.zh_cn;
		case Slot::EnUS: return row.en_us;
		case Slot::JaJP: return row.ja_jp;
		default: return row.zh_tw;
		}
	}

} // namespace Hi18nBuiltin
