#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(appdata, "AppData 快取", 20)

class AppDataGenericCacheTask : public HCleanDetailListTask {
public:
	AppDataGenericCacheTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "appdata_generic"; }
	const char* GetName() const override { return "AppData 通用快取掃描"; }
	const char* GetPurpose() const override { return "靜態路徑 + 依大小排序動態掃描 %APPDATA% 快取"; }
	const char* GetTooltip() const override {
		return "混合模式：先釘釘/飛書等靜態路徑，再以實測大小補滿動態發現";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\DingTalk\\cache", "釘釘 cache", 180LL * 1024 * 1024, true,
			"釘釘快取", "不影響帳號");
		AddDetailIfExists("%APPDATA%\\LarkShell\\sdk_storage\\log", "飛書 log", 120LL * 1024 * 1024, true,
			"飛書日誌", "可安全清理");

		static const char* k_skip[] = {
			"Adobe", "Antigravity", "Antigravity IDE", "bilibili", "Blender Foundation",
			"Code", "Cursor", "Discord", "EasyAntiCheat", "GitHub Desktop", "Godot",
			"Live2D", "Microsoft", "NetEase", "obs-studio", "Postman", "Slack",
			"Spotify", "Telegram Desktop", "Tencent", "Unity", "Unreal Engine", "Zoom",
			".minecraft", "npm", "npm-cache", "DingTalk", "LarkShell",
		};
		const size_t room = kMaxDetails - detail_count_;
		AddSortedRoamingDiscoveryDetails(room, k_skip, sizeof(k_skip) / sizeof(k_skip[0]));
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AppDataProductivityTask : public HCleanDetailListTask {
public:
	AppDataProductivityTask() { SetScanTarget(2048LL * 1024 * 1024); }
	const char* GetId() const override { return "appdata_productivity"; }
	const char* GetName() const override { return "AppData 生產力工具"; }
	const char* GetPurpose() const override {
		return "Notion、Typora、Sublime、壓縮工具、Everything、Notepad++ 等";
	}
	const char* GetTooltip() const override { return "備份目錄預設關閉；FileZilla 快取不刪 sessions"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Notion\\Cache", "Notion Cache", 120LL * 1024 * 1024, true,
			"Notion 快取", "首次開啟可能略慢");
		AddDetailIfExists("%APPDATA%\\Notion\\logs", "Notion 日誌", 80LL * 1024 * 1024, true,
			"Notion 日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Notion\\log", "Notion log", 40LL * 1024 * 1024, true,
			"Notion log", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Typora\\typora-user-images", "Typora 暫存圖",
			80LL * 1024 * 1024, true, "貼上圖片暫存", "不影響筆記本體");
		AddDetailIfExists("%APPDATA%\\Typora\\backups", "Typora 備份", 120LL * 1024 * 1024, false,
			"自動備份", "可能無法還原舊版", true);

		AddDetailIfExists("%APPDATA%\\Sublime Text\\Cache", "Sublime Cache",
			60LL * 1024 * 1024, true, "索引快取", "首次開檔可能略慢");
		AddDetailIfExists("%APPDATA%\\Sublime Text\\Index", "Sublime Index",
			40LL * 1024 * 1024, false, "符號索引", "需重建索引");

		AddDetailIfExists("%APPDATA%\\WinRAR\\Temp", "WinRAR Temp", 120LL * 1024 * 1024, true,
			"解壓暫存", "不影響設定");
		AddDetailIfExists("%APPDATA%\\7-Zip\\History", "7-Zip 歷史", 20LL * 1024 * 1024, false,
			"最近檔案清單", "僅清除歷史紀錄");

		AddDetailIfExists("%APPDATA%\\Everything\\Cache", "Everything Cache",
			80LL * 1024 * 1024, true, "索引快取", "可能需重建索引");
		AddDetailIfExists("%APPDATA%\\Everything\\Logs", "Everything 日誌", 40LL * 1024 * 1024, true,
			"搜尋工具日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Notepad++\\backup", "Notepad++ 備份",
			60LL * 1024 * 1024, false, "未儲存備份", "可能遺失未存檔內容", true);

		AddDetailIfExists("%APPDATA%\\FileZilla\\cache", "FileZilla cache",
			80LL * 1024 * 1024, true, "FTP 快取", "不刪 sessions 設定");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AppDataPeripheralsTask : public HCleanDetailListTask {
public:
	AppDataPeripheralsTask() { SetScanTarget(3072LL * 1024 * 1024); }
	const char* GetId() const override { return "appdata_peripherals"; }
	const char* GetName() const override { return "AppData 周邊與串流"; }
	const char* GetPurpose() const override {
		return "Parsec、Rainmeter、Voicemod、SteelSeries、Corsair、Elgato、遠端桌面等";
	}
	const char* GetTooltip() const override {
		return "Windsurf 僅 Cache/logs；不含 workspaceStorage。Wallpaper Engine 快取預設關閉。";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Parsec\\cache", "Parsec cache", 120LL * 1024 * 1024, true,
			"Parsec 快取", "不影響配對設定");
		AddDetailIfExists("%APPDATA%\\Parsec\\log", "Parsec log", 60LL * 1024 * 1024, true,
			"Parsec 日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Parsec\\logs", "Parsec logs", 60LL * 1024 * 1024, true,
			"Parsec logs", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Rainmeter\\Skins\\@Backup", "Rainmeter 備份",
			40LL * 1024 * 1024, false, "面板備份", "不影響使用中面板");
		AddDetailIfExists("%APPDATA%\\Rainmeter\\Logs", "Rainmeter 日誌", 30LL * 1024 * 1024, true,
			"桌面小工具日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Voicemod\\Cache", "Voicemod Cache", 120LL * 1024 * 1024, true,
			"變聲器快取", "不影響音效設定");
		AddDetailIfExists("%APPDATA%\\Voicemod\\logs", "Voicemod 日誌", 40LL * 1024 * 1024, true,
			"Voicemod 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\SteelSeries\\GG\\Cache", "SteelSeries Cache",
			120LL * 1024 * 1024, true, "GG 快取", "不影響裝置設定");
		AddDetailIfExists("%APPDATA%\\SteelSeries\\GG\\Logs", "SteelSeries 日誌",
			80LL * 1024 * 1024, true, "GG 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Corsair\\Logs", "Corsair 日誌", 80LL * 1024 * 1024, true,
			"iCUE 日誌", "不影響設定");
		AddDetailIfExists("%APPDATA%\\Corsair\\CUE", "Corsair CUE 快取", 100LL * 1024 * 1024, false,
			"iCUE 快取", "可能需重新載入設定");

		AddDetailIfExists("%APPDATA%\\Elgato\\StreamDeck\\Cache", "Stream Deck Cache",
			80LL * 1024 * 1024, true, "Stream Deck 快取", "按鈕設定不受影響");
		AddDetailIfExists("%APPDATA%\\Elgato\\Logs", "Elgato 日誌", 40LL * 1024 * 1024, true,
			"Elgato 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\TeamViewer\\Logs", "TeamViewer 日誌",
			100LL * 1024 * 1024, true, "遠端連線日誌", "不影響信任清單");
		AddDetailIfExists("%APPDATA%\\AnyDesk\\cache", "AnyDesk cache",
			60LL * 1024 * 1024, true, "AnyDesk 快取", "不刪連線設定");
		AddDetailIfExists("%APPDATA%\\AnyDesk\\thumbnails", "AnyDesk 縮圖",
			40LL * 1024 * 1024, false, "遠端桌面縮圖", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Voicemeeter\\Log", "Voicemeeter 日誌",
			40LL * 1024 * 1024, true, "混音器日誌", "不影響路由設定");

		AddDetailIfExists("%APPDATA%\\Wallpaper Engine\\cache", "Wallpaper Engine cache",
			400LL * 1024 * 1024, false, "動態桌布快取", "桌布可能重新下載", true);

		AddDetailIfExists("%APPDATA%\\Figma\\Desktop\\Cache", "Figma Desktop Cache",
			280LL * 1024 * 1024, true, "Figma 桌面版快取", "首次開檔可能略慢");
		AddDetailIfExists("%APPDATA%\\Figma\\Desktop\\logs", "Figma 日誌",
			60LL * 1024 * 1024, true, "Figma 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Windsurf\\Cache", "Windsurf Cache", 180LL * 1024 * 1024, true,
			"Windsurf 快取", "不含 workspaceStorage");
		AddDetailIfExists("%APPDATA%\\Windsurf\\Code Cache", "Windsurf Code Cache",
			120LL * 1024 * 1024, true, "Windsurf Code Cache", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Windsurf\\GPUCache", "Windsurf GPUCache",
			80LL * 1024 * 1024, true, "Windsurf GPUCache", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Windsurf\\logs", "Windsurf 日誌", 60LL * 1024 * 1024, true,
			"Windsurf 日誌", "可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AppDataCreativeAudioTask : public HCleanDetailListTask {
public:
	AppDataCreativeAudioTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "appdata_creative"; }
	const char* GetName() const override { return "AppData 創作與音訊"; }
	const char* GetPurpose() const override {
		return "Krita、GIMP、Autodesk、Cinema 4D、SketchUp、Reaper、FL Studio、Audacity";
	}
	const char* GetTooltip() const override { return "Autodesk 為各產品子目錄快取；不刪專案檔"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\krita\\cache", "Krita cache", 120LL * 1024 * 1024, true,
			"Krita 快取", "不影響作品");
		AddDetailIfExists("%APPDATA%\\krita\\crash", "Krita crash", 40LL * 1024 * 1024, true,
			"當機報告", "可安全清理");

		AddDetailIfExists("%APPDATA%\\GIMP\\2.10\\cache", "GIMP cache", 80LL * 1024 * 1024, true,
			"GIMP 快取", "不影響 XCF");

		static const char* k_autodesk_subs[] = { "Cache", "cache", "Logs", "logs", "Temp", "temp" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateWildcardChildSubdirs(
			"%APPDATA%\\Autodesk", L"*", k_autodesk_subs, 6, "Autodesk",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 200LL * 1024 * 1024, i < 6,
				"Autodesk 產品快取", "首次開啟可能重新產生快取");
		}

		AddDetailIfExists("%APPDATA%\\MAXON\\Cinema 4D R26_64bit\\cache", "C4D cache",
			200LL * 1024 * 1024, true, "Cinema 4D 快取", "不影響場景");
		AddDetailIfExists("%APPDATA%\\MAXON\\logs", "MAXON 日誌", 60LL * 1024 * 1024, true,
			"MAXON 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\SketchUp\\SketchUp 2024\\SketchUp\\Cache",
			"SketchUp Cache", 180LL * 1024 * 1024, true, "SketchUp 快取", "不影響模型");
		AddDetailIfExists("%APPDATA%\\SketchUp\\SketchUp 2023\\SketchUp\\Cache",
			"SketchUp 2023 Cache", 120LL * 1024 * 1024, false, "舊版快取", "可安全清理");

		AddDetailIfExists("%APPDATA%\\REAPER\\Cache", "Reaper Cache", 80LL * 1024 * 1024, true,
			"Reaper 快取", "不影響專案");
		AddDetailIfExists("%APPDATA%\\REAPER\\logs", "Reaper 日誌", 40LL * 1024 * 1024, true,
			"Reaper 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Image-Line\\FL Studio\\Cache", "FL Studio Cache",
			200LL * 1024 * 1024, true, "FL Studio 快取", "不影響工程檔");
		AddDetailIfExists("%APPDATA%\\Image-Line\\FL Studio\\Logs", "FL Studio 日誌",
			60LL * 1024 * 1024, true, "FL Studio 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Audacity\\Cache", "Audacity Cache",
			80LL * 1024 * 1024, true, "Audacity 快取", "不影響專案");
		AddDetailIfExists("%APPDATA%\\Audacity\\logs", "Audacity 日誌", 30LL * 1024 * 1024, true,
			"Audacity 日誌", "可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AppDataGameModTask : public HCleanDetailListTask {
public:
	AppDataGameModTask() { SetScanTarget(3072LL * 1024 * 1024); }
	const char* GetId() const override { return "appdata_game_mod"; }
	const char* GetName() const override { return "AppData 遊戲與 Mod 工具"; }
	const char* GetPurpose() const override {
		return "Battle.net、Vortex、Mod Organizer、itch、Playnite 等";
	}
	const char* GetTooltip() const override { return "Battle.net webcache 預設關閉；Mod 快取可能需重新下載"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Battle.net\\Cache", "Battle.net Cache",
			200LL * 1024 * 1024, true, "啟動器快取", "商店頁可能重新載入");
		AddDetailIfExists("%APPDATA%\\Battle.net\\Logs", "Battle.net 日誌",
			80LL * 1024 * 1024, true, "啟動器日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Battle.net\\BrowserCache", "Battle.net BrowserCache",
			320LL * 1024 * 1024, false, "內嵌瀏覽器快取", "可能需重新登入", true);

		AddDetailIfExists("%APPDATA%\\Vortex\\cache", "Vortex cache",
			400LL * 1024 * 1024, true, "Mod 管理器快取", "部署可能略慢");
		AddDetailIfExists("%APPDATA%\\Vortex\\logs", "Vortex 日誌", 80LL * 1024 * 1024, true,
			"Mod 管理器日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\ModOrganizer\\webcache", "MO webcache",
			120LL * 1024 * 1024, false, "Mod Organizer webcache", "可能需重新登入 Nexus", true);
		AddDetailIfExists("%APPDATA%\\ModOrganizer\\logs", "MO 日誌", 60LL * 1024 * 1024, true,
			"Mod Organizer 日誌", "可安全清理");

		AddDetailIfExists("%APPDATA%\\itch\\logs", "itch 日誌", 60LL * 1024 * 1024, true,
			"itch 啟動器日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\itch\\broth", "itch broth", 80LL * 1024 * 1024, false,
			"itch 暫存", "下載可能重新開始", true);

		AddDetailIfExists("%APPDATA%\\Playnite\\cache", "Playnite cache",
			180LL * 1024 * 1024, true, "遊戲庫快取", "封面可能重新下載");
		AddDetailIfExists("%APPDATA%\\Playnite\\logs", "Playnite 日誌", 40LL * 1024 * 1024, true,
			"Playnite 日誌", "可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AppDataCommSupplementTask : public HCleanDetailListTask {
public:
	AppDataCommSupplementTask() { SetScanTarget(2048LL * 1024 * 1024); }
	const char* GetId() const override { return "appdata_comm"; }
	const char* GetName() const override { return "AppData 通訊補充"; }
	const char* GetPurpose() const override {
		return "Signal、Element、Proton、Twitch、Thunderbird、Termius 等";
	}
	const char* GetTooltip() const override { return "Thunderbird 設定檔快取；Signal/Element 預設僅 logs/Cache"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Signal\\logs", "Signal 日誌", 60LL * 1024 * 1024, true,
			"Signal 日誌", "不影響訊息");
		AddDetailIfExists("%APPDATA%\\Signal\\Cache", "Signal Cache", 120LL * 1024 * 1024, true,
			"Signal 快取", "媒體可能重新下載");
		AddDetailIfExists("%APPDATA%\\Signal\\temp", "Signal temp", 40LL * 1024 * 1024, false,
			"Signal 暫存", "可安全清理");

		AddDetailIfExists("%APPDATA%\\Element\\Cache", "Element Cache", 180LL * 1024 * 1024, true,
			"Matrix 用戶端快取", "首次開啟可能略慢");
		AddDetailIfExists("%APPDATA%\\Element\\Code Cache", "Element Code Cache",
			80LL * 1024 * 1024, true, "Element Code Cache", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Element\\logs", "Element 日誌", 60LL * 1024 * 1024, true,
			"Element 日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\Element\\IndexedDB", "Element IndexedDB",
			120LL * 1024 * 1024, false, "Element IndexedDB", "可能需重新登入", true);

		AddDetailIfExists("%APPDATA%\\ProtonMail\\Bridge\\logs", "Proton Bridge 日誌",
			60LL * 1024 * 1024, true, "ProtonMail Bridge 日誌", "不影響帳號");
		AddDetailIfExists("%APPDATA%\\ProtonMail\\Bridge\\Cache", "Proton Bridge Cache",
			80LL * 1024 * 1024, false, "Bridge 快取", "可能需重新同步", true);

		AddDetailIfExists("%APPDATA%\\Twitch\\Cache", "Twitch Cache",
			200LL * 1024 * 1024, true, "Twitch 桌面版快取", "直播可能重新緩衝");
		AddDetailIfExists("%APPDATA%\\Twitch\\logs", "Twitch 日誌", 60LL * 1024 * 1024, true,
			"Twitch 日誌", "可安全清理");

		static const char* k_tb_cache[] = { "cache2", "cache" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateWildcardChildSubdirs(
			"%APPDATA%\\Thunderbird\\Profiles", L"*", k_tb_cache, 2, "Thunderbird",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 280LL * 1024 * 1024, true,
				"Thunderbird 設定檔快取", "郵件首次開啟可能略慢");
		}

		AddDetailIfExists("%APPDATA%\\Termius\\Cache", "Termius Cache", 80LL * 1024 * 1024, true,
			"SSH 用戶端快取", "不刪主機設定");
		AddDetailIfExists("%APPDATA%\\Termius\\logs", "Termius 日誌", 40LL * 1024 * 1024, true,
			"Termius 日誌", "可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(AppDataGenericCacheTask, appdata, 0)
REG_CLEAN_TASK(AppDataProductivityTask, appdata, 10)
REG_CLEAN_TASK(AppDataPeripheralsTask, appdata, 20)
REG_CLEAN_TASK(AppDataCreativeAudioTask, appdata, 30)
REG_CLEAN_TASK(AppDataGameModTask, appdata, 40)
REG_CLEAN_TASK(AppDataCommSupplementTask, appdata, 50)
