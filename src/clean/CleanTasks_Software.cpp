#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(software, "常用軟體", 18)

class LogitechGHUBCacheTask : public HCleanDetailListTask {
public:
	LogitechGHUBCacheTask() { SetScanTarget(512LL * 1024 * 1024); }
	const char* GetId() const override { return "software_logitech_ghub"; }
	const char* GetName() const override { return "Logitech G HUB"; }
	const char* GetPurpose() const override { return "清理 G HUB 日誌與暫存"; }
	const char* GetTooltip() const override { return "關閉 G HUB 後清理；不刪除裝置設定檔"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_subs[] = { "cache", "logs", "CrashDumps", "sentry-db" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateSubdirsWithCache("%LOCALAPPDATA%\\LGHUB", k_subs, 4,
			paths, labels, kMaxDetails, "G HUB Local");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 80LL * 1024 * 1024, true,
				"G HUB 用戶端快取或日誌",
				"下次啟動可能略慢；不影響按鍵設定");
		}
		count = HCleanEnumerateSubdirsWithCache("%PROGRAMDATA%\\LGHUB", k_subs, 4,
			paths, labels, kMaxDetails - detail_count_, "G HUB Common");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 60LL * 1024 * 1024, i == 0,
				"G HUB 共用快取或日誌",
				"不刪除裝置設定");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class EAAppCacheTask : public HCleanDetailListTask {
public:
	EAAppCacheTask() { SetScanTarget(2048LL * 1024 * 1024); }
	const char* GetId() const override { return "software_ea_app"; }
	const char* GetName() const override { return "EA / EA App"; }
	const char* GetPurpose() const override { return "清理 EA Desktop 快取與日誌"; }
	const char* GetTooltip() const override { return "下載快取預設關閉；清理後可能需重新登入或校驗遊戲"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Electronic Arts\\EA Desktop\\Cache", "EA Desktop 快取",
			480LL * 1024 * 1024, true,
			"EA App HTTP / UI 快取",
			"商店頁可能重新載入");
		AddDetailIfExists("%LOCALAPPDATA%\\Electronic Arts\\EA Desktop\\Logs", "EA Desktop 日誌",
			120LL * 1024 * 1024, true,
			"啟動器日誌",
			"可安全清理");
		AddDetailIfExists("%PROGRAMDATA%\\Electronic Arts\\EA Desktop\\Logs", "EA 共用日誌",
			80LL * 1024 * 1024, false,
			"ProgramData 日誌",
			"不影響已安裝遊戲");
		AddDetailIfExists("%LOCALAPPDATA%\\Electronic Arts\\EA Desktop\\DownloadCache", "EA 下載快取",
			900LL * 1024 * 1024, false,
			"進行中或暫停下載暫存",
			"中斷下載需重新開始",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Electronic Arts\\EA Desktop\\OfflineCache", "EA 離線快取",
			400LL * 1024 * 1024, false,
			"離線模式資源快取",
			"離線內容可能需重新下載",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Electronic Arts\\EA Desktop\\Telemetry", "EA 遙測暫存",
			60LL * 1024 * 1024, true,
			"遙測與分析暫存",
			"不影響帳號登入");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class MinecraftCacheTask : public HCleanDetailListTask {
public:
	MinecraftCacheTask() { SetScanTarget(1800LL * 1024 * 1024); }
	const char* GetId() const override { return "software_minecraft"; }
	const char* GetName() const override { return "Minecraft"; }
	const char* GetPurpose() const override { return "清理 Minecraft 啟動器與 Java 版日誌快取"; }
	const char* GetTooltip() const override { return "webcache2 預設關閉；可能需重新登入 Microsoft 帳號"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\.minecraft\\logs", "Java 版日誌", 350LL * 1024 * 1024, true,
			"遊戲與啟動器日誌",
			"不影響世界存檔");
		AddDetailIfExists("%APPDATA%\\.minecraft\\crash-reports", "當機報告", 80LL * 1024 * 1024, false,
			"當機傾印",
			"可安全清理");
		AddDetailIfExists("%APPDATA%\\.minecraft\\webcache2", "啟動器 webcache",
			450LL * 1024 * 1024, false,
			"啟動器登入與網頁快取",
			"可能需重新登入 Microsoft 帳號",
			true);
		AddDetailIfExists("%APPDATA%\\.minecraft\\launcher\\webcache", "舊版啟動器 webcache",
			200LL * 1024 * 1024, false,
			"舊版啟動器快取",
			"可能需重新登入",
			true);
		AddDetailIfExists("%APPDATA%\\.minecraft\\assets\\indexes", "資源索引快取",
			300LL * 1024 * 1024, false,
			"資源索引（可重建）",
			"下次啟動可能重新下載資源",
			true);

		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t uwp_count = HCleanEnumerateMinecraftUwpCachePaths(paths, labels,
			kMaxDetails - detail_count_);
		for (size_t i = 0; i < uwp_count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, true,
				"Microsoft Store 版 Minecraft 快取",
				"不刪除世界存檔");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdobeCacheTask : public HCleanDetailListTask {
public:
	AdobeCacheTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "software_adobe"; }
	const char* GetName() const override { return "Adobe 快取"; }
	const char* GetPurpose() const override { return "清理 Adobe 媒體快取與 Creative Cloud 暫存"; }
	const char* GetTooltip() const override { return "媒體快取刪除後 Premiere/AE 可能需重新產生 peak"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateAdobeProductCachePaths(paths, labels, kMaxDetails);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			const bool selected = i < 4;
			AddDetailPath(paths[i], labels[i], 500LL * 1024 * 1024, selected,
				"Adobe 產品媒體 / 暫存快取",
				"首次預覽媒體可能略慢");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AntigravityIdeCacheTask : public HCleanDetailListTask {
public:
	AntigravityIdeCacheTask() { SetScanTarget(1200LL * 1024 * 1024); }
	const char* GetId() const override { return "software_antigravity"; }
	const char* GetName() const override { return "Antigravity IDE"; }
	const char* GetPurpose() const override { return "清理 Google Antigravity IDE 快取與日誌"; }
	const char* GetTooltip() const override { return "Session/Local Storage 預設關閉；可能需重新登入"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const struct { const char* root; const char* prefix; } k_roots[] = {
			{ "%APPDATA%\\Antigravity", "Antigravity" },
			{ "%APPDATA%\\Antigravity IDE", "Antigravity IDE" },
		};
		static const char* k_safe_subs[] = {
			"Cache", "CachedData", "Code Cache", "GPUCache", "logs", "Crashpad",
		};
		static const char* k_destructive_subs[] = {
			"Session Storage", "Local Storage",
		};

		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};

		for (const auto& root : k_roots) {
			const size_t safe_count = HCleanEnumerateSubdirsWithCache(root.root, k_safe_subs, 6,
				paths, labels, kMaxDetails - detail_count_, root.prefix);
			for (size_t i = 0; i < safe_count && detail_count_ < kMaxDetails; ++i) {
				AddDetailPath(paths[i], labels[i], 150LL * 1024 * 1024, true,
					"IDE 快取或日誌",
					"擴充功能可能略慢載入");
			}
			const size_t risky_count = HCleanEnumerateSubdirsWithCache(root.root, k_destructive_subs, 2,
				paths, labels, kMaxDetails - detail_count_, root.prefix);
			for (size_t i = 0; i < risky_count && detail_count_ < kMaxDetails; ++i) {
				AddDetailPath(paths[i], labels[i], 40LL * 1024 * 1024, false,
					"登入工作階段與本機儲存",
					"可能需重新登入 Google 帳號",
					true);
			}
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class EasyAntiCheatCacheTask : public HCleanDetailListTask {
public:
	EasyAntiCheatCacheTask() { SetScanTarget(256LL * 1024 * 1024); }
	const char* GetId() const override { return "software_eac"; }
	const char* GetName() const override { return "EasyAntiCheat"; }
	const char* GetPurpose() const override { return "清理 EAC 日誌與暫存"; }
	const char* GetTooltip() const override { return "僅清理 log/cache；不碰遊戲本體或 EAC 核心檔"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_subs[] = { "Logs", "log", "cache", "temp" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateSubdirsWithCache("%PROGRAMDATA%\\EasyAntiCheat", k_subs, 4,
			paths, labels, kMaxDetails, "EAC ProgramData");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 40LL * 1024 * 1024, true,
				"反作弊服務日誌或暫存",
				"不影響遊戲啟動");
		}
		count = HCleanEnumerateSubdirsWithCache("%APPDATA%\\EasyAntiCheat", k_subs, 4,
			paths, labels, kMaxDetails - detail_count_, "EAC AppData");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 30LL * 1024 * 1024, true,
				"用戶端 EAC 日誌",
				"可安全清理");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class Live2DCacheTask : public HCleanDetailListTask {
public:
	Live2DCacheTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "software_live2d"; }
	const char* GetName() const override { return "Live2D Cubism"; }
	const char* GetPurpose() const override { return "清理 Live2D 編輯器快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除 .cmo3 / 模型專案檔"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_subs[] = { "Cache", "cache", "Logs", "logs", "temp" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateSubdirsWithCache("%APPDATA%\\Live2D", k_subs, 5,
			paths, labels, kMaxDetails, "Live2D Roaming");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 80LL * 1024 * 1024, true,
				"Live2D 編輯器快取或日誌",
				"下次開啟專案可能重新匯入預覽");
		}
		count = HCleanEnumerateSubdirsWithCache("%LOCALAPPDATA%\\Live2D Cubism", k_subs, 5,
			paths, labels, kMaxDetails - detail_count_, "Cubism Local");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, true,
				"Cubism 本機快取",
				"不影響模型原始檔");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(LogitechGHUBCacheTask, software, 0)
REG_CLEAN_TASK(EAAppCacheTask, software, 10)
REG_CLEAN_TASK(MinecraftCacheTask, software, 20)
REG_CLEAN_TASK(AdobeCacheTask, software, 30)
REG_CLEAN_TASK(AntigravityIdeCacheTask, software, 40)
REG_CLEAN_TASK(EasyAntiCheatCacheTask, software, 50)
REG_CLEAN_TASK(Live2DCacheTask, software, 60)

class SpotifyCacheTask : public HCleanDetailListTask {
public:
	SpotifyCacheTask() { SetScanTarget(1200LL * 1024 * 1024); }
	const char* GetId() const override { return "software_spotify"; }
	const char* GetName() const override { return "Spotify"; }
	const char* GetPurpose() const override { return "清理 Spotify 快取與離線暫存"; }
	const char* GetTooltip() const override { return "Storage 預設關閉；可能需重新下載離線歌曲"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Spotify\\Data", "Spotify Data", 480LL * 1024 * 1024, true,
			"串流快取與暫存",
			"首次播放可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\Spotify\\Storage", "Spotify Storage", 600LL * 1024 * 1024, false,
			"離線與索引快取",
			"離線歌曲可能需重新下載",
			true);
		AddDetailIfExists("%APPDATA%\\Spotify\\prefs", "Spotify 日誌目錄", 40LL * 1024 * 1024, false,
			"偏好設定旁日誌",
			"不影響帳號登入");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class CreativeMediaCacheTask : public HCleanDetailListTask {
public:
	CreativeMediaCacheTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "software_creative_media"; }
	const char* GetName() const override { return "創意與媒體工具"; }
	const char* GetPurpose() const override { return "清理 Blender、OBS、DaVinci、VLC 等快取"; }
	const char* GetTooltip() const override { return "不刪除專案檔；OBS/DaVinci 日誌預設勾選"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_blender_cache[] = { "cache" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateWildcardChildSubdirs(
			"%APPDATA%\\Blender Foundation\\Blender", L"*", k_blender_cache, 1, "Blender",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 200LL * 1024 * 1024, true,
				"Blender 版本快取",
				"下次渲染可能略慢");
		}

		static const char* k_obs_subs[] = { "logs", "crashes" };
		count = HCleanEnumerateSubdirsWithCache("%APPDATA%\\obs-studio", k_obs_subs, 2,
			paths, labels, kMaxDetails - detail_count_, "OBS");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 80LL * 1024 * 1024, true,
				"OBS 日誌或當機報告",
				"不影響場景設定");
		}
		AddDetailIfExists("%APPDATA%\\obs-studio\\plugin_config\\obs-browser\\Cache", "OBS 瀏覽器快取",
			60LL * 1024 * 1024, false,
			"OBS 內嵌瀏覽器來源快取",
			"瀏覽器來源可能重新載入");

		AddDetailIfExists("%APPDATA%\\Blackmagic Design\\DaVinci Resolve\\Cache", "DaVinci Cache",
			900LL * 1024 * 1024, true,
			"DaVinci 時間軸與代理快取",
			"首次播放時間軸可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\Blackmagic Design\\DaVinci Resolve\\Cache", "DaVinci Local Cache",
			400LL * 1024 * 1024, true,
			"DaVinci 本機快取",
			"不影響專案資料庫");

		AddDetailIfExists("%LOCALAPPDATA%\\vlc\\cache", "VLC 快取", 120LL * 1024 * 1024, true,
			"VLC 媒體快取",
			"串流緩衝會重建");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(SpotifyCacheTask, software, 70)
REG_CLEAN_TASK(CreativeMediaCacheTask, software, 80)
