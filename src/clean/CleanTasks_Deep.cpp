#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(deep, "深度清理", 11)

class DeepCbsPantherLogsTask : public HCleanDetailListTask {
public:
	DeepCbsPantherLogsTask() { SetScanTarget(512LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "deep_cbs_panther_logs"; }
	const char* GetName() const override { return "CBS / 安裝日誌"; }
	const char* GetPurpose() const override { return "清理元件存放與功能更新留下的 CBS、Panther 日誌"; }
	const char* GetTooltip() const override {
		return "僅日誌與暫存；不執行 DISM 元件清理；建議以管理員執行";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%WINDIR%\\Logs\\CBS", "CBS 日誌", 256LL * 1024 * 1024, true,
			"元件服務安裝/修復日誌",
			"疑難排解時可能缺少歷史紀錄");
		AddDetailIfExists("%WINDIR%\\Panther", "Panther 安裝日誌", 384LL * 1024 * 1024, true,
			"Windows 安裝與就地升級日誌",
			"升級失敗分析資料可能消失",
			true);
		AddDetailIfExists("%WINDIR%\\Logs\\DISM", "DISM 日誌", 96LL * 1024 * 1024, true,
			"DISM 執行紀錄",
			"可安全清理");
		AddDetailIfExists("%WINDIR%\\Logs\\MoSetup", "功能更新日誌", 64LL * 1024 * 1024, false,
			"功能更新安裝紀錄",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DeepWinSxSManifestTask : public HCleanDetailListTask {
public:
	DeepWinSxSManifestTask() { SetScanTarget(256LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "deep_winsxs_manifest"; }
	const char* GetName() const override { return "WinSxS 清單快取"; }
	const char* GetPurpose() const override { return "清理 WinSxS ManifestCache 等可重建快取（非整體元件刪除）"; }
	const char* GetTooltip() const override {
		return "不取代 DISM / 清理元件存放區；僅 ManifestCache 等快取目錄";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%WINDIR%\\WinSxS\\ManifestCache", "ManifestCache", 180LL * 1024 * 1024, false,
			"元件清單快取",
			"首次修復元件可能略慢",
			true);
		AddDetailIfExists("%WINDIR%\\WinSxS\\Backup", "WinSxS Backup", 96LL * 1024 * 1024, false,
			"元件備份片段",
			"修復時可能需重新下載",
			true);
		AddDetailIfExists("%WINDIR%\\WinSxS\\Temp", "WinSxS Temp", 48LL * 1024 * 1024, true,
			"WinSxS 暫存",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class DeepMediaCacheTask : public HCleanDetailListTask {
public:
	DeepMediaCacheTask() { SetScanTarget(1024LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "deep_media_cache"; }
	const char* GetName() const override { return "媒體與縮圖快取"; }
	const char* GetPurpose() const override { return "清理相片 App、縮圖資料庫與常見剪輯軟體代理快取"; }
	const char* GetTooltip() const override { return "縮圖會重建；剪輯代理刪除後需重新產生"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer",
			"Explorer 縮圖快取", 320LL * 1024 * 1024, false,
			"thumbcache_*.db 等",
			"資料夾預覽會重建",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Packages\\Microsoft.Windows.Photos_8wekyb3d8bbwe\\LocalCache",
			"Windows 相片 LocalCache", 240LL * 1024 * 1024, true,
			"相片 App 快取",
			"預覽可能重新下載");
		AddDetailIfExists("%LOCALAPPDATA%\\Packages\\Microsoft.ZuneVideo_8wekyb3d8bbwe\\LocalCache",
			"電影與電視快取", 180LL * 1024 * 1024, false,
			"媒體 App 快取",
			"串流預覽可能略慢",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Adobe\\Common\\Media Cache Files",
			"Premiere 媒體快取", 2048LL * 1024 * 1024, false,
			"Premiere 代理與媒體快取",
			"時間軸預覽需重新產生",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Spotify\\Storage",
			"Spotify 離線快取", 512LL * 1024 * 1024, false,
			"已快取串流片段",
			"離線歌曲需重新下載",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DeepPlatformWslDockerTask : public HCleanDetailListTask {
public:
	DeepPlatformWslDockerTask() { SetScanTarget(16384LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "deep_platform_wsl_docker"; }
	const char* GetName() const override { return "WSL / Docker 資料"; }
	const char* GetPurpose() const override {
		return "掃描 WSL 發行版與 Docker 資料目錄；預設不刪除 vhdx（僅日誌與可重建快取）";
	}
	const char* GetTooltip() const override {
		return "ext4.vhdx 預設關閉；Docker 請用開發者任務的 CLI 清理";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Docker\\log", "Docker Desktop 日誌", 120LL * 1024 * 1024, true,
			"Docker 用戶端日誌",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Docker\\run", "Docker run 狀態", 48LL * 1024 * 1024, false,
			"執行狀態暫存",
			"重啟 Docker 可能重置",
			true);
		AddDetailIfExists("%USERPROFILE%\\.docker\\buildx\\cache", "Docker buildx 快取", 512LL * 1024 * 1024, false,
			"buildx 建置快取",
			"下次建置會重新下載層",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\wsl", "WSL 本機狀態", 256LL * 1024 * 1024, false,
			"WSL 設定與狀態",
			"可能需重新設定預設發行版",
			true);

		static const wchar_t* k_wsl_subs[] = { L"LocalState", L"LocalState\\temp" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t wsl_count = HCleanEnumerateUwpPackagePaths("Canonical*", k_wsl_subs, 2, "WSL",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < wsl_count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 512LL * 1024 * 1024, false,
				"WSL Store 套件資料",
				"ext4.vhdx 預設不選；請手動確認");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DeepDefenderQuarantineTask : public HCleanDetailListTask {
public:
	DeepDefenderQuarantineTask() { SetScanTarget(512LL * 1024 * 1024); }
	const char* GetId() const override { return "deep_defender_quarantine"; }
	const char* GetName() const override { return "Defender 隔離區"; }
	const char* GetPurpose() const override { return "清理 Windows Defender 隔離的威脅檔案（非掃描紀錄）"; }
	const char* GetTooltip() const override {
		return "與「Defender 掃描紀錄」不同；刪除後無法還原隔離檔";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows Defender\\Quarantine",
			"隔離檔案", 400LL * 1024 * 1024, false,
			"已隔離的惡意程式樣本",
			"無法還原至隔離前狀態",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows Defender\\Scans\\History\\Store",
			"掃描存放", 96LL * 1024 * 1024, false,
			"掃描歷史存放",
			"不影響即時防護",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class DeepDownloadsRetentionTask : public HCleanDetailListTask {
public:
	DeepDownloadsRetentionTask() { SetScanTarget(2048LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "deep_downloads_stale"; }
	const char* GetName() const override { return "下載資料夾過期檔"; }
	const char* GetPurpose() const override {
		return "Downloads 內超過 90 天未修改的 .tmp/.log/.bak/.old";
	}
	const char* GetTooltip() const override {
		return "與「過期暫存檔發現」互補；僅限下載資料夾；預設關閉";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddSortedStaleFileDiscoveryDetails(kMaxDetails);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DeepSystemProfileCachesTask : public HCleanDetailListTask {
public:
	DeepSystemProfileCachesTask() { SetScanTarget(384LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "deep_system_profile_caches"; }
	const char* GetName() const override { return "系統設定檔快取"; }
	const char* GetPurpose() const override {
		return "補強 NetworkService / LocalService / systemprofile 等難以測量的系統路徑";
	}
	const char* GetTooltip() const override { return "需管理員權限效果較佳；部分路徑可能不存在"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%WINDIR%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Temp",
			"NetworkService Temp", 48LL * 1024 * 1024, true,
			"網路服務帳戶暫存",
			"服務執行中可能跳過");
		AddDetailIfExists("%WINDIR%\\ServiceProfiles\\LocalService\\AppData\\Local\\Temp",
			"LocalService Temp", 48LL * 1024 * 1024, true,
			"本機服務帳戶暫存",
			"服務執行中可能跳過");
		AddDetailIfExists("%WINDIR%\\ServiceProfiles\\LocalService\\AppData\\Local\\FontCache",
			"LocalService 字型快取", 32LL * 1024 * 1024, false,
			"系統服務字型快取",
			"字型可能短暫重新載入",
			true);
		AddDetailIfExists("%WINDIR%\\System32\\config\\systemprofile\\AppData\\Local\\Microsoft\\Windows\\INetCache",
			"systemprofile INetCache", 32LL * 1024 * 1024, false,
			"系統設定檔 Web 快取",
			"可安全清理");
		AddDetailIfExists("%WINDIR%\\System32\\config\\systemprofile\\AppData\\LocalLow\\Microsoft\\CryptnetUrlCache",
			"CryptnetUrlCache", 24LL * 1024 * 1024, false,
			"憑證 URL 快取",
			"憑證驗證可能略慢");
		AddDetailIfExists("%WINDIR%\\SoftwareDistribution\\PostRebootEventCache.V2",
			"WU 重開機事件快取", 64LL * 1024 * 1024, false,
			"更新服務重開機事件",
			"與 Windows 更新快取任務重疊",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class DeepProgramDataTask : public HCleanDetailListTask {
public:
	DeepProgramDataTask()
	{
		SetScanTarget(8192LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "deep_program_data"; }
	const char* GetName() const override { return "ProgramData 精選"; }
	const char* GetPurpose() const override {
		return "清理 %ProgramData% 常見大項與動態發現快取（與 All Users\\Application Data 為同一路徑）";
	}
	const char* GetTooltip() const override {
		return "舊路徑 C:\\Users\\All Users\\Application Data 實為連結；請以此任務為準；Package Cache 預設關閉";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows\\DeliveryOptimization\\Cache",
			"傳遞最佳化快取", 512LL * 1024 * 1024, true,
			"Windows Update P2P 下載快取",
			"更新可能重新下載");
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows\\DeliveryOptimization\\Logs",
			"傳遞最佳化日誌", 64LL * 1024 * 1024, true,
			"DO 服務日誌",
			"可安全清理");
		AddDetailIfExists("%PROGRAMDATA%\\Package Cache", "安裝套件快取", 2048LL * 1024 * 1024, false,
			"MSI/安裝程式保留檔",
			"修復安裝可能需重新下載",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows Defender\\Scans\\History",
			"Defender 掃描歷史", 128LL * 1024 * 1024, false,
			"掃描紀錄",
			"不影響即時防護",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows Defender\\Quarantine",
			"Defender 隔離區", 256LL * 1024 * 1024, false,
			"已隔離樣本",
			"無法還原",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows\\WER",
			"錯誤回報 (WER)", 96LL * 1024 * 1024, true,
			"當機回報暫存",
			"除錯資料可能減少");
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows\\Caches",
			"Windows 系統快取", 128LL * 1024 * 1024, false,
			"系統層快取",
			"可安全清理");
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\EdgeUpdate\\Download",
			"Edge 更新下載", 180LL * 1024 * 1024, true,
			"Edge 更新暫存",
			"可安全清理");
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Network\\Downloader",
			"網路下載暫存", 96LL * 1024 * 1024, true,
			"系統下載暫存",
			"可安全清理");
		AddDetailIfExists("%PROGRAMDATA%\\NVIDIA Corporation\\NV_Cache",
			"NVIDIA 著色器快取", 512LL * 1024 * 1024, false,
			"驅動著色器快取",
			"遊戲可能重新編譯",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\AMD\\DxCache", "AMD DxCache", 256LL * 1024 * 1024, false,
			"AMD 著色器快取",
			"遊戲可能重新編譯",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\D3DSCache", "D3D 系統快取", 120LL * 1024 * 1024, false,
			"Direct3D 快取",
			"可安全清理");
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows\\ClipSVC",
			"授權/商店快取", 80LL * 1024 * 1024, false,
			"ClipSVC 快取",
			"Store 應用可能重新驗證",
			true);
		AddDetailIfExists("%PROGRAMDATA%\\Piriform\\CCleaner\\Log", "CCleaner 日誌", 40LL * 1024 * 1024, true,
			"第三方清理日誌",
			"可安全清理");

		static const char* k_skip[] = { "Microsoft", "Package Cache" };
		const size_t room = kMaxDetails - detail_count_;
		if (room > 0) {
			AddSortedProgramDataDiscoveryDetails(room, k_skip, 2);
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

REG_CLEAN_TASK(DeepCbsPantherLogsTask, deep, 0)
REG_CLEAN_TASK(DeepWinSxSManifestTask, deep, 10)
REG_CLEAN_TASK(DeepMediaCacheTask, deep, 20)
REG_CLEAN_TASK(DeepPlatformWslDockerTask, deep, 30)
REG_CLEAN_TASK(DeepDefenderQuarantineTask, deep, 40)
REG_CLEAN_TASK(DeepDownloadsRetentionTask, deep, 50)
REG_CLEAN_TASK(DeepSystemProfileCachesTask, deep, 60)
REG_CLEAN_TASK(DeepProgramDataTask, deep, 70)
