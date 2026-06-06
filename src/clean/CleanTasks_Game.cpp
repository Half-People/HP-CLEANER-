#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"
#include <cstdio>

REG_CLEAN_CATEGORY(game_platform, "遊戲 - 平台與啟動器", 15)
REG_CLEAN_CATEGORY(game_engine, "遊戲 - 引擎", 16)
REG_CLEAN_CATEGORY(game_genre, "遊戲 - 類型", 17)

class SteamShaderCacheTask : public HCleanDetailListTask {
public:
	SteamShaderCacheTask() { SetScanTarget(2048LL * 1024 * 1024); }
	const char* GetId() const override { return "game_steam_shader"; }
	const char* GetName() const override { return "Steam 著色器與下載"; }
	const char* GetPurpose() const override { return "清理所有 Steam 函式庫著色器與下載暫存"; }
	const char* GetTooltip() const override
	{
		return "自動解析 libraryfolders.vdf；首次進遊戲可能需重建著色器快取";
	}
	bool IsEnabledByDefault() const override { return true; }
	void BuildDetails() const override
	{
		AddSteamLibraryDetails(true);
		if (detail_count_ == 0) {
			AddDetail("C:\\Program Files (x86)\\Steam\\steamapps\\shadercache", "著色器快取（預設路徑）",
				1200LL * 1024 * 1024, true,
				"Steam 著色器編譯快取",
				"未偵測到 Steam 安裝時可能為 0");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class EpicGamesCacheTask : public HCleanDetailListTask {
public:
	EpicGamesCacheTask() { SetScanTarget(960LL * 1024 * 1024); }
	const char* GetId() const override { return "game_epic_cache"; }
	const char* GetName() const override { return "Epic Games 快取"; }
	const char* GetPurpose() const override { return "清理 Epic Launcher 與遊戲快取"; }
	const char* GetTooltip() const override { return "含 EpicGamesLauncher\\Data\\Cache 與 DerivedDataCache"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\EpicGamesLauncher\\Saved\\webcache", "Launcher Web 快取", 320LL * 1024 * 1024,
			true);
		AddDetail("%LOCALAPPDATA%\\EpicGamesLauncher\\Saved\\Logs", "Launcher 日誌", 180LL * 1024 * 1024, false);
		AddDetail("%LOCALAPPDATA%\\UnrealEngine\\Common\\DerivedDataCache", "DerivedDataCache", 360LL * 1024 * 1024,
			true);
		AddDetail("%PROGRAMDATA%\\Epic\\EpicGamesLauncher\\Data\\Cache", "共用快取", 100LL * 1024 * 1024, true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class XboxMsStoreTempTask : public HCleanDetailListTask {
public:
	XboxMsStoreTempTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "game_xbox_ms_store"; }
	const char* GetName() const override { return "Xbox / Microsoft Store"; }
	const char* GetPurpose() const override { return "清理 Xbox 與 MS Store 相關暫存"; }
	const char* GetTooltip() const override { return "含 Delivery Optimization 與 GamingServices 暫存"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Packages\\Microsoft.GamingApp_8wekyb3d8bbwe\\LocalCache", "Gaming App 快取",
			240LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Microsoft\\XboxLive", "Xbox Live 快取", 160LL * 1024 * 1024, true);
		AddDetail("C:\\ProgramData\\Microsoft\\Windows\\DeliveryOptimization\\Cache", "傳遞最佳化快取",
			180LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\GameBar", "Game Bar", 60LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class GameClipsTask : public HCleanDetailListTask {
public:
	GameClipsTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "game_clips"; }
	const char* GetName() const override { return "遊戲錄影與截圖"; }
	const char* GetPurpose() const override { return "清理 Xbox Game Bar 錄影與截圖"; }
	const char* GetTooltip() const override { return "預設關閉；將刪除 Captures 資料夾內媒體，請先備份"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%USERPROFILE%\\Videos\\Captures", "Captures 錄影", 2800LL * 1024 * 1024, true,
			nullptr, nullptr, true);
		AddDetail("%LOCALAPPDATA%\\Packages\\Microsoft.XboxGamingOverlay_8wekyb3d8bbwe\\LocalState", "Gaming Overlay",
			800LL * 1024 * 1024, false, nullptr, nullptr, true);
		AddDetail("%USERPROFILE%\\Pictures\\Screenshots", "螢幕擷取", 496LL * 1024 * 1024, false,
			nullptr, nullptr, true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class UnityEngineCacheTask : public HCleanDetailListTask {
public:
	UnityEngineCacheTask() { SetScanTarget(2200LL * 1024 * 1024); }
	const char* GetId() const override { return "game_engine_unity"; }
	const char* GetName() const override { return "Unity 引擎快取"; }
	const char* GetPurpose() const override { return "清理 Unity 引擎與專案共用快取"; }
	const char* GetTooltip() const override { return "刪除後可能觸發 reimport 與 shader 重建"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Unity\\cache\\shadercache", "Unity shader cache", 1200LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Unity\\cache\\packages", "Unity package cache", 700LL * 1024 * 1024, true);
		AddDetail("%APPDATA%\\Unity\\Editor\\Asset Store-5.x", "Asset Store cache", 300LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class UnrealEngineCacheTask : public HCleanDetailListTask {
public:
	UnrealEngineCacheTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "game_engine_unreal"; }
	const char* GetName() const override { return "Unreal 引擎快取"; }
	const char* GetPurpose() const override { return "清理 Unreal 常見 DerivedDataCache 與中介輸出"; }
	const char* GetTooltip() const override { return "首次開專案可能重新編譯 shader 與快取"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddUnrealEngineCacheDetails(true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class GodotEngineCacheTask : public HCleanDetailListTask {
public:
	GodotEngineCacheTask() { SetScanTarget(900LL * 1024 * 1024); }
	const char* GetId() const override { return "game_engine_godot"; }
	const char* GetName() const override { return "Godot 引擎快取"; }
	const char* GetPurpose() const override { return "清理 Godot 匯入與編輯器快取"; }
	const char* GetTooltip() const override { return "刪除後下次開專案可能重新匯入資源"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%APPDATA%\\Godot\\editor_data\\cache", "Godot editor cache", 420LL * 1024 * 1024, true,
			"Godot 編輯器匯入與快取",
			"下次開專案可能重新匯入資源");
		AddDetail("%APPDATA%\\Godot\\cache", "Godot cache", 320LL * 1024 * 1024, true,
			"Godot 執行階段快取",
			"不影響專案檔案");
		AddDetailIfExists("%APPDATA%\\Godot\\shader_cache", "Godot shader cache", 280LL * 1024 * 1024, true,
			"Godot 著色器快取",
			"首次執行可能重新編譯 shader");
		AddDetailIfExists("%LOCALAPPDATA%\\Godot", "Godot Local", 160LL * 1024 * 1024, false,
			"Godot 本機快取與暫存",
			"不刪除專案原始檔");
		AddDetail("%LOCALAPPDATA%\\Temp\\godot", "Godot temp", 160LL * 1024 * 1024, false,
			"Godot 暫存檔",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class FpsGenreCacheTask : public HCleanDetailListTask {
public:
	FpsGenreCacheTask() { SetScanTarget(1800LL * 1024 * 1024); }
	const char* GetId() const override { return "game_genre_fps"; }
	const char* GetName() const override { return "FPS 遊戲快取"; }
	const char* GetPurpose() const override { return "清理 FPS 類遊戲常見 shader 與 replay 快取"; }
	const char* GetTooltip() const override { return "不含存檔；主要清理可重建快取與回放暫存"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\NVIDIA\\DXCache", "NVIDIA DX shader cache", 900LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\D3DSCache", "D3D shader cache", 500LL * 1024 * 1024, true);
		AddDetail("%USERPROFILE%\\Documents\\Call of Duty\\players\\cache", "FPS replay cache", 400LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class MmoGenreCacheTask : public HCleanDetailListTask {
public:
	MmoGenreCacheTask() { SetScanTarget(2500LL * 1024 * 1024); }
	const char* GetId() const override { return "game_genre_mmo"; }
	const char* GetName() const override { return "MMO 遊戲快取"; }
	const char* GetPurpose() const override { return "清理 MMO 客戶端啟動器補丁與日誌暫存"; }
	const char* GetTooltip() const override { return "下次啟動器可能重新校驗檔案"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%PROGRAMDATA%\\Battle.net\\Agent\\data\\cache", "Battle.net cache", 1200LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Temp\\Blizzard", "Blizzard temp", 700LL * 1024 * 1024, true);
		AddDetail("%USERPROFILE%\\Documents\\My Games", "My Games logs/cache", 600LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class SandboxGenreCacheTask : public HCleanDetailListTask {
public:
	SandboxGenreCacheTask() { SetScanTarget(1400LL * 1024 * 1024); }
	const char* GetId() const override { return "game_genre_sandbox"; }
	const char* GetName() const override { return "沙盒遊戲快取"; }
	const char* GetPurpose() const override { return "清理沙盒類遊戲常見快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除世界存檔；僅刪可重建快取與 log"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%APPDATA%\\.minecraft\\logs", "Minecraft logs", 350LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Temp\\Roblox", "Roblox temp", 600LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class RiotClientCacheTask : public HCleanDetailListTask {
public:
	RiotClientCacheTask() { SetScanTarget(1200LL * 1024 * 1024); }
	const char* GetId() const override { return "game_riot_cache"; }
	const char* GetName() const override { return "Riot Client 快取"; }
	const char* GetPurpose() const override { return "清理 Riot / League 客戶端快取"; }
	const char* GetTooltip() const override { return "請先關閉 Riot Client；不刪除帳號與遊戲本體"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Riot Games\\Riot Client\\Crashes", "Riot 當機快取", 200LL * 1024 * 1024, false,
			"客戶端當機回報",
			"可安全清理");
		AddDetail("%LOCALAPPDATA%\\Riot Games\\Riot Client\\Logs", "Riot 日誌", 180LL * 1024 * 1024, true,
			"啟動器日誌",
			"不影響登入");
		AddDetail("%LOCALAPPDATA%\\Riot Games\\Riot Client\\HttpCache", "HttpCache", 520LL * 1024 * 1024, true,
			"啟動器 HTTP 快取",
			"新聞與資源可能重新下載");
		AddDetail("%LOCALAPPDATA%\\League of Legends\\Cache", "LoL Cache", 300LL * 1024 * 1024, true,
			"英雄聯盟快取",
			"首次進遊戲可能略慢");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class GogGalaxyCacheTask : public HCleanDetailListTask {
public:
	GogGalaxyCacheTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "game_gog_cache"; }
	const char* GetName() const override { return "GOG Galaxy 快取"; }
	const char* GetPurpose() const override { return "清理 GOG Galaxy 快取與日誌"; }
	const char* GetTooltip() const override { return "關閉 Galaxy 後清理效果較佳"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%PROGRAMDATA%\\GOG.com\\Galaxy\\logs", "Galaxy 日誌", 120LL * 1024 * 1024, true,
			"GOG Galaxy 日誌",
			"不影響已安裝遊戲");
		AddDetail("%PROGRAMDATA%\\GOG.com\\Galaxy\\cache", "Galaxy cache", 360LL * 1024 * 1024, true,
			"啟動器快取",
			"商店頁可能重新載入");
		AddDetail("%LOCALAPPDATA%\\GOG.com\\Galaxy\\webcache", "webcache", 160LL * 1024 * 1024, true,
			"內嵌瀏覽器快取",
			"不刪除遊戲檔案");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class UbisoftConnectCacheTask : public HCleanDetailListTask {
public:
	UbisoftConnectCacheTask() { SetScanTarget(900LL * 1024 * 1024); }
	const char* GetId() const override { return "game_ubisoft_cache"; }
	const char* GetName() const override { return "Ubisoft Connect 快取"; }
	const char* GetPurpose() const override { return "清理 Ubisoft 啟動器快取"; }
	const char* GetTooltip() const override { return "不刪除遊戲存檔與安裝檔"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Ubisoft Game Launcher\\cache", "啟動器 cache", 420LL * 1024 * 1024, true,
			"Ubisoft Connect 快取",
			"啟動器可能重新下載資源");
		AddDetail("%LOCALAPPDATA%\\Ubisoft Game Launcher\\logs", "啟動器日誌", 180LL * 1024 * 1024, true,
			"啟動器日誌",
			"可安全清理");
		AddDetail("%PROGRAMDATA%\\Ubisoft\\UbisoftConnect\\cache", "共用 cache", 300LL * 1024 * 1024, false,
			"ProgramData 快取",
			"不影響已安裝遊戲");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(SteamShaderCacheTask, game_platform, 0)
REG_CLEAN_TASK(EpicGamesCacheTask, game_platform, 10)
REG_CLEAN_TASK(XboxMsStoreTempTask, game_platform, 20)
REG_CLEAN_TASK(GameClipsTask, game_platform, 30)
REG_CLEAN_TASK(RiotClientCacheTask, game_platform, 40)
REG_CLEAN_TASK(GogGalaxyCacheTask, game_platform, 50)
REG_CLEAN_TASK(UbisoftConnectCacheTask, game_platform, 60)

class BattleNetCacheTask : public HCleanDetailListTask {
public:
	BattleNetCacheTask() { SetScanTarget(1800LL * 1024 * 1024); }
	const char* GetId() const override { return "game_battlenet_cache"; }
	const char* GetName() const override { return "Battle.net"; }
	const char* GetPurpose() const override { return "清理 Battle.net 啟動器快取與日誌"; }
	const char* GetTooltip() const override { return "下載快取預設關閉；可能需重新校驗遊戲"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%PROGRAMDATA%\\Battle.net\\Agent\\data\\cache", "Agent 快取",
			800LL * 1024 * 1024, true,
			"Battle.net Agent 下載快取",
			"啟動器可能重新下載補丁資訊");
		AddDetailIfExists("%PROGRAMDATA%\\Battle.net\\Agent\\logs", "Agent 日誌",
			120LL * 1024 * 1024, true,
			"Agent 服務日誌",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Battle.net\\Cache", "用戶端 Cache",
			400LL * 1024 * 1024, true,
			"Battle.net UI 快取",
			"商店頁可能重新載入");
		AddDetailIfExists("%LOCALAPPDATA%\\Battle.net\\Logs", "用戶端日誌",
			100LL * 1024 * 1024, true,
			"啟動器日誌",
			"不影響登入");
		AddDetailIfExists("%LOCALAPPDATA%\\Battle.net\\BrowserCache", "BrowserCache",
			380LL * 1024 * 1024, false,
			"內嵌瀏覽器快取",
			"可能需重新登入 Battle.net",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class RockstarLauncherCacheTask : public HCleanDetailListTask {
public:
	RockstarLauncherCacheTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "game_rockstar_cache"; }
	const char* GetName() const override { return "Rockstar Launcher"; }
	const char* GetPurpose() const override { return "清理 Rockstar 啟動器快取"; }
	const char* GetTooltip() const override { return "不刪除遊戲存檔；關閉 Launcher 後清理"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_subs[] = { "Cache", "logs", "CrashDumps" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateSubdirsWithCache(
			"%LOCALAPPDATA%\\Rockstar Games\\Launcher", k_subs, 3,
			paths, labels, kMaxDetails, "Rockstar");
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, true,
				"Rockstar Launcher 快取或日誌",
				"不影響遊戲存檔");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class BethesdaLauncherCacheTask : public HCleanDetailListTask {
public:
	BethesdaLauncherCacheTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "game_bethesda_cache"; }
	const char* GetName() const override { return "Bethesda Launcher"; }
	const char* GetPurpose() const override { return "清理 Bethesda 啟動器快取"; }
	const char* GetTooltip() const override { return "下載快取預設關閉"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Bethesda.net Launcher\\Cache", "Launcher Cache",
			280LL * 1024 * 1024, true,
			"啟動器 UI 快取",
			"商店頁可能重新載入");
		AddDetailIfExists("%LOCALAPPDATA%\\Bethesda.net Launcher\\Logs", "Launcher 日誌",
			100LL * 1024 * 1024, true,
			"啟動器日誌",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Bethesda.net Launcher\\DownloadCache", "下載快取",
			260LL * 1024 * 1024, false,
			"進行中或暫停下載",
			"中斷下載需重新開始",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class GpuVendorCacheTask : public HCleanDetailListTask {
public:
	GpuVendorCacheTask() { SetScanTarget(2048LL * 1024 * 1024); }
	const char* GetId() const override { return "game_gpu_vendor_cache"; }
	const char* GetName() const override { return "GPU 驅動快取"; }
	const char* GetPurpose() const override { return "清理 NVIDIA / AMD 著色器與驅動快取"; }
	const char* GetTooltip() const override { return "遊戲首次啟動可能需重新編譯著色器"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\NVIDIA\\DXCache", "NVIDIA DXCache", 600LL * 1024 * 1024, true,
			"DirectX 著色器快取",
			"首次進遊戲可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\NVIDIA\\GLCache", "NVIDIA GLCache", 400LL * 1024 * 1024, true,
			"OpenGL 著色器快取",
			"OpenGL 程式可能重新編譯");
		AddDetailIfExists("%LOCALAPPDATA%\\NVIDIA Corporation\\NV_Cache", "NVIDIA NV_Cache (Local)",
			300LL * 1024 * 1024, true,
			"NVIDIA App / GFE 本機快取",
			"不影響驅動本身");
		AddDetailIfExists("%PROGRAMDATA%\\NVIDIA Corporation\\NV_Cache", "NVIDIA NV_Cache (Common)",
			200LL * 1024 * 1024, false,
			"共用 NV 快取",
			"多使用者環境可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\AMD\\DxCache", "AMD DxCache", 400LL * 1024 * 1024, true,
			"AMD DirectX 快取",
			"首次進遊戲可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\AMD\\VkCache", "AMD VkCache", 300LL * 1024 * 1024, true,
			"AMD Vulkan 快取",
			"Vulkan 遊戲可能重新編譯");
		AddDetailIfExists("%LOCALAPPDATA%\\AMD\\CN\\NewsFeed", "AMD 新聞快取", 80LL * 1024 * 1024, false,
			"AMD Software 新聞 Feed",
			"不影響驅動功能");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class OverwolfCacheTask : public HCleanDetailListTask {
public:
	OverwolfCacheTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "game_overwolf_cache"; }
	const char* GetName() const override { return "Overwolf"; }
	const char* GetPurpose() const override { return "清理 Overwolf 快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除已安裝 App 設定"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Overwolf\\Cache", "Overwolf Cache", 280LL * 1024 * 1024, true,
			"Overwolf 平台快取",
			"Overlay 可能略慢載入");
		AddDetailIfExists("%LOCALAPPDATA%\\Overwolf\\Log", "Overwolf 日誌", 120LL * 1024 * 1024, true,
			"平台與 App 日誌",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Overwolf\\Temp", "Overwolf 暫存", 80LL * 1024 * 1024, false,
			"暫存檔",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(BattleNetCacheTask, game_platform, 70)
REG_CLEAN_TASK(RockstarLauncherCacheTask, game_platform, 80)
REG_CLEAN_TASK(BethesdaLauncherCacheTask, game_platform, 90)
REG_CLEAN_TASK(GpuVendorCacheTask, game_platform, 100)
REG_CLEAN_TASK(OverwolfCacheTask, game_platform, 110)

REG_CLEAN_TASK(UnityEngineCacheTask, game_engine, 0)
REG_CLEAN_TASK(UnrealEngineCacheTask, game_engine, 10)
REG_CLEAN_TASK(GodotEngineCacheTask, game_engine, 20)

REG_CLEAN_TASK(FpsGenreCacheTask, game_genre, 0)
REG_CLEAN_TASK(MmoGenreCacheTask, game_genre, 10)
REG_CLEAN_TASK(SandboxGenreCacheTask, game_genre, 20)
