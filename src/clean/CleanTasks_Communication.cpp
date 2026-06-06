#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(communication, "通訊與協作", 19)

class TelegramCacheTask : public HCleanDetailListTask {
public:
	TelegramCacheTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_telegram"; }
	const char* GetName() const override { return "Telegram Desktop"; }
	const char* GetPurpose() const override { return "清理 Telegram 媒體快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除聊天紀錄；關閉 Telegram 後效果較佳"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Telegram Desktop\\tdata\\user_data\\cache", "媒體快取",
			480LL * 1024 * 1024, true,
			"已下載媒體與縮圖快取",
			"頻道預覽可能需重新下載");
		AddDetailIfExists("%APPDATA%\\Telegram Desktop\\log", "日誌", 80LL * 1024 * 1024, true,
			"用戶端執行日誌",
			"不影響登入與訊息");
		AddDetailIfExists("%APPDATA%\\Telegram Desktop\\tdata\\temp", "暫存", 80LL * 1024 * 1024, false,
			"傳輸暫存檔",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class SlackCacheTask : public HCleanDetailListTask {
public:
	SlackCacheTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_slack"; }
	const char* GetName() const override { return "Slack"; }
	const char* GetPurpose() const override { return "清理 Slack 快取與日誌"; }
	const char* GetTooltip() const override { return "請先關閉 Slack；不刪除工作區與訊息"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateElectronAppCaches("%APPDATA%\\Slack", "Slack",
			paths, labels, kMaxDetails);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			const bool selected = i < 3;
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, selected,
				"Slack Electron 快取或日誌",
				"首次開啟頻道可能略慢");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class TeamsCacheTask : public HCleanDetailListTask {
public:
	TeamsCacheTask() { SetScanTarget(1200LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_teams"; }
	const char* GetName() const override { return "Microsoft Teams"; }
	const char* GetPurpose() const override { return "清理 Teams 經典版、新版與 UWP 快取"; }
	const char* GetTooltip() const override { return "可能需重新登入；請先完全退出 Teams"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Microsoft\\Teams\\Cache", "Teams 經典 Cache",
			280LL * 1024 * 1024, true,
			"舊版 Teams 媒體快取",
			"會議預覽可能重新載入");
		AddDetailIfExists("%APPDATA%\\Microsoft\\Teams\\logs", "Teams 經典日誌",
			120LL * 1024 * 1024, true,
			"舊版 Teams 日誌",
			"可安全清理");
		AddDetailIfExists("%APPDATA%\\Microsoft\\Teams\\Service Worker", "Teams Service Worker",
			160LL * 1024 * 1024, false,
			"舊版 Teams 背景快取",
			"可能需重新登入",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Teams\\Cache", "Teams 2.0 Cache",
			320LL * 1024 * 1024, true,
			"新版 Teams 快取",
			"首次啟動可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Teams\\logs", "Teams 2.0 日誌",
			100LL * 1024 * 1024, true,
			"新版 Teams 日誌",
			"不影響帳號");

		static const wchar_t* k_uwp_subs[] = {
			L"LocalCache",
			L"LocalState\\logs",
			L"TempState",
		};
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t uwp_count = HCleanEnumerateUwpPackagePaths("MSTeams_*", k_uwp_subs, 3, "Teams UWP",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < uwp_count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 100LL * 1024 * 1024, true,
				"Store 版 Teams 快取",
				"不刪除聊天紀錄");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class WhatsAppCacheTask : public HCleanDetailListTask {
public:
	WhatsAppCacheTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_whatsapp"; }
	const char* GetName() const override { return "WhatsApp Desktop"; }
	const char* GetPurpose() const override { return "清理 WhatsApp UWP 快取"; }
	const char* GetTooltip() const override { return "不刪除聊天備份；關閉 WhatsApp 後清理"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const wchar_t* k_subs[] = {
			L"LocalCache",
			L"LocalState\\logs",
			L"TempState",
			L"AC\\Temp",
		};
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateUwpPackagePaths("5319275A.WhatsAppDesktop_*", k_subs, 4,
			"WhatsApp", paths, labels, kMaxDetails);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, i < 2,
				"WhatsApp Store 版快取或日誌",
				"媒體預覽可能重新下載");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class ZoomCacheTask : public HCleanDetailListTask {
public:
	ZoomCacheTask() { SetScanTarget(560LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_zoom"; }
	const char* GetName() const override { return "Zoom"; }
	const char* GetPurpose() const override { return "清理 Zoom 日誌與快取"; }
	const char* GetTooltip() const override { return "不刪除錄影檔；關閉 Zoom 後清理"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_subs[] = { "logs", "cache", "CrashDumps", "data\\logs" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateSubdirsWithCache("%APPDATA%\\Zoom", k_subs, 4,
			paths, labels, kMaxDetails, "Zoom");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 100LL * 1024 * 1024, i < 2,
				"Zoom 快取或日誌",
				"不影響帳號登入");
		}
		AddDetailIfExists("%APPDATA%\\Zoom\\data\\VirtualBkgnd_Default", "虛擬背景快取",
			80LL * 1024 * 1024, false,
			"虛擬背景預覽快取",
			"背景可能需重新下載");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class LineCacheTask : public HCleanDetailListTask {
public:
	LineCacheTask() { SetScanTarget(400LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_line"; }
	const char* GetName() const override { return "LINE"; }
	const char* GetPurpose() const override { return "清理 LINE 快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除聊天紀錄；關閉 LINE 後清理"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\LINE\\Cache", "LINE Cache", 240LL * 1024 * 1024, true,
			"LINE 媒體與 UI 快取",
			"貼圖預覽可能重新下載");
		AddDetailIfExists("%LOCALAPPDATA%\\LINE\\Data\\log", "LINE 日誌", 80LL * 1024 * 1024, true,
			"用戶端日誌",
			"不影響登入");
		AddDetailIfExists("%LOCALAPPDATA%\\LINE\\Data\\sticker", "貼圖快取",
			80LL * 1024 * 1024, false,
			"已下載貼圖快取",
			"貼圖可能需重新下載",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class TencentCommCacheTask : public HCleanDetailListTask {
public:
	TencentCommCacheTask() { SetScanTarget(960LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_tencent"; }
	const char* GetName() const override { return "WeChat / QQ"; }
	const char* GetPurpose() const override { return "清理微信與 QQ 暫存與日誌"; }
	const char* GetTooltip() const override { return "不刪除聊天紀錄；WeChat 快取預設關閉"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Tencent\\WeChat\\log", "WeChat 日誌", 120LL * 1024 * 1024, true,
			"WeChat 執行日誌",
			"不影響登入");
		AddDetailIfExists("%APPDATA%\\Tencent\\WeChat\\All Users\\config", "WeChat 設定快取",
			80LL * 1024 * 1024, false,
			"WeChat 設定與快取索引",
			"可能需重新同步設定",
			true);
		AddDetailIfExists("%APPDATA%\\Tencent\\WeChat\\All Users\\temp", "WeChat 暫存",
			200LL * 1024 * 1024, false,
			"WeChat 傳輸暫存",
			"進行中傳輸可能中斷",
			true);
		AddDetailIfExists("%APPDATA%\\Tencent\\QQ\\Temp", "QQ Temp", 280LL * 1024 * 1024, true,
			"QQ 暫存檔",
			"可安全清理");
		AddDetailIfExists("%APPDATA%\\Tencent\\QQ\\Logs", "QQ 日誌", 100LL * 1024 * 1024, true,
			"QQ 用戶端日誌",
			"不影響帳號");
		AddDetailIfExists("%APPDATA%\\Tencent\\QQ\\Cache", "QQ Cache", 180LL * 1024 * 1024, false,
			"QQ 媒體快取",
			"頭像與預覽可能重新下載",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class BilibiliCacheTask : public HCleanDetailListTask {
public:
	BilibiliCacheTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_bilibili"; }
	const char* GetName() const override { return "嗶哩嗶哩"; }
	const char* GetPurpose() const override { return "清理 Bilibili 用戶端快取"; }
	const char* GetTooltip() const override { return "含 UWP 與桌面版常見快取路徑"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\bilibili\\Cache", "桌面版 Cache", 320LL * 1024 * 1024, true,
			"Bilibili 桌面版快取",
			"影片預覽可能重新載入");
		AddDetailIfExists("%APPDATA%\\bilibili\\logs", "桌面版日誌", 80LL * 1024 * 1024, true,
			"桌面版日誌",
			"可安全清理");

		static const wchar_t* k_uwp_subs[] = { L"LocalCache", L"LocalState\\logs", L"TempState" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateUwpPackagePaths("Bilibili*", k_uwp_subs, 3, "Bilibili UWP",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 100LL * 1024 * 1024, true,
				"Store 版 Bilibili 快取",
				"不刪除帳號資料");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

REG_CLEAN_TASK(TelegramCacheTask, communication, 0)
REG_CLEAN_TASK(SlackCacheTask, communication, 10)
REG_CLEAN_TASK(TeamsCacheTask, communication, 20)
REG_CLEAN_TASK(WhatsAppCacheTask, communication, 30)
REG_CLEAN_TASK(ZoomCacheTask, communication, 40)
REG_CLEAN_TASK(LineCacheTask, communication, 50)
REG_CLEAN_TASK(TencentCommCacheTask, communication, 60)
REG_CLEAN_TASK(BilibiliCacheTask, communication, 70)

class NetEaseCloudMusicCacheTask : public HCleanDetailListTask {
public:
	NetEaseCloudMusicCacheTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "comm_netease_music"; }
	const char* GetName() const override { return "網易雲音樂"; }
	const char* GetPurpose() const override { return "清理網易雲音樂快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除已下載歌曲；快取預設關閉"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\NetEase\\CloudMusic\\Cache", "快取", 280LL * 1024 * 1024, false,
			"串流與封面快取",
			"封面與預覽可能重新下載",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\NetEase\\CloudMusic\\Log", "日誌", 80LL * 1024 * 1024, true,
			"用戶端日誌",
			"不影響登入");
		AddDetailIfExists("%APPDATA%\\NetEase\\CloudMusic\\cache", "Roaming 快取", 120LL * 1024 * 1024, false,
			"Roaming 快取資料",
			"首次播放可能略慢",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

REG_CLEAN_TASK(NetEaseCloudMusicCacheTask, communication, 80)
