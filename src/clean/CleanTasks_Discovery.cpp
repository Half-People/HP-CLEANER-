#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(discovery, "智慧發現", 12)

namespace {
	static const char* k_skip_dedicated_apps[] = {
		"Adobe", "Antigravity", "Antigravity IDE", "bilibili", "Blender Foundation",
		"Code", "Cursor", "Discord", "EasyAntiCheat", "GitHub Desktop", "Godot",
		"Google", "JetBrains", "Live2D", "Microsoft", "Mozilla", "NetEase", "obs-studio",
		"Packages", "Postman", "Slack", "Spotify", "Telegram Desktop", "Tencent", "Unity",
		"Unreal Engine", "Zoom", ".minecraft", "npm", "npm-cache", "Temp", "MicrosoftEdge",
	};
	constexpr size_t k_skip_count = sizeof(k_skip_dedicated_apps) / sizeof(k_skip_dedicated_apps[0]);

	size_t RemainingDetailSlots(const HCleanDetailListTask* task)
	{
		task->GetDetailEntryCount();
		return HCleanDetailListTask::kMaxDetails - task->GetDetailEntryCount();
	}
}

class DiscoveryRoamingHybridTask : public HCleanDetailListTask {
public:
	DiscoveryRoamingHybridTask() { SetScanTarget(6144LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "discovery_roaming_hybrid"; }
	const char* GetName() const override { return "Roaming 混合清理"; }
	const char* GetPurpose() const override {
		return "靜態常見 %APPDATA% 路徑 + 擴大關鍵字/候選池的動態發現";
	}
	const char* GetTooltip() const override {
		return "先加入釘釘/飛書等靜態路徑；動態部分掃更多快取目錄名並依實測大小取前 N 項";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\DingTalk\\cache", "釘釘 cache", 180LL * 1024 * 1024, true,
			"釘釘快取", "不影響帳號");
		AddDetailIfExists("%APPDATA%\\DingTalk\\log", "釘釘 log", 80LL * 1024 * 1024, true,
			"釘釘日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\LarkShell\\sdk_storage\\log", "飛書 log", 120LL * 1024 * 1024, true,
			"飛書日誌", "可安全清理");
		AddDetailIfExists("%APPDATA%\\LarkShell\\Browser\\Cache", "飛書 Cache", 160LL * 1024 * 1024, true,
			"飛書快取", "首次開啟可能略慢");
		AddSortedRoamingDiscoveryDetails(RemainingDetailSlots(this), k_skip_dedicated_apps, k_skip_count);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DiscoveryLocalHybridTask : public HCleanDetailListTask {
public:
	DiscoveryLocalHybridTask() { SetScanTarget(8192LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "discovery_local_hybrid"; }
	const char* GetName() const override { return "LocalAppData 混合清理"; }
	const char* GetPurpose() const override {
		return "雲端/WSL/.NET 靜態路徑 + LocalAppData / ProgramData 動態發現";
	}
	const char* GetTooltip() const override {
		return "靜態優先；動態掃描 Local 與 ProgramData，依實測大小排序取前 N 項";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Google\\DriveFS\\cache", "Google Drive cache", 480LL * 1024 * 1024, true,
			"雲端硬碟快取", "可能需重新同步");
		AddDetailIfExists("%LOCALAPPDATA%\\Dropbox\\.dropbox.cache", "Dropbox cache", 360LL * 1024 * 1024, true,
			"Dropbox 快取", "可能需重新同步");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\MSBuild\\Multiprocessor", "MSBuild 快取", 220LL * 1024 * 1024,
			true, "MSBuild 中介快取", "首次建置可能略慢");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\VisualStudio\\BackupFiles", "VS 備份", 180LL * 1024 * 1024,
			false, "Visual Studio 備份", "可能無法還原未存檔", true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\PowerShell\\PSReadLine", "PSReadLine 歷史", 12LL * 1024 * 1024,
			false, "PowerShell 命令歷史", "清除後無法回溯命令", true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\CLR_v4.0\\UsageLogs", ".NET UsageLogs", 48LL * 1024 * 1024, true,
			".NET 執行階段紀錄", "可安全清理");
		const size_t room = RemainingDetailSlots(this);
		const size_t local_slots = (room + 1) / 2;
		const size_t prog_slots = room / 2;
		AddSortedLocalDiscoveryDetails(local_slots, k_skip_dedicated_apps, k_skip_count);
		AddSortedProgramDataDiscoveryDetails(prog_slots, k_skip_dedicated_apps, k_skip_count);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DiscoveryUwpAllTask : public HCleanDetailListTask {
public:
	DiscoveryUwpAllTask() { SetScanTarget(4096LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "discovery_uwp_all"; }
	const char* GetName() const override { return "UWP 全量發現"; }
	const char* GetPurpose() const override {
		return "掃描已安裝 UWP 套件 LocalCache/TempState/RoamingState 等";
	}
	const char* GetTooltip() const override { return "依實測大小排序；Microsoft Store 應用可能重新載入資源"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddSortedUwpDiscoveryDetails(kMaxDetails);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DiscoveryInstallRegistryTask : public HCleanDetailListTask {
public:
	DiscoveryInstallRegistryTask() { SetScanTarget(2048LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "discovery_install_registry"; }
	const char* GetName() const override { return "安裝目錄發現"; }
	const char* GetPurpose() const override { return "從解除安裝登錄讀 InstallLocation 並找 Cache/logs"; }
	const char* GetTooltip() const override { return "依登錄檔動態發現；依大小排序取前 24 項"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddSortedInstallLocationDiscoveryDetails(kMaxDetails);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DiscoveryRegistryCleanupTask : public HCleanDetailListTask {
public:
	DiscoveryRegistryCleanupTask() { SetScanTarget(48LL * 1024 * 1024); }
	const char* GetId() const override { return "discovery_registry_cleanup"; }
	const char* GetName() const override { return "登錄快取（安全項）"; }
	const char* GetPurpose() const override { return "清理 MUICache、RecentDocs 等可重建登錄項"; }
	const char* GetTooltip() const override { return "不刪除已安裝軟體設定；RecentDocs 會清除最近文件紀錄"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("@registry:muicache", "MUICache", 24LL * 1024 * 1024, true,
			"程式名稱/UI 語系快取", "首次開啟部分程式可能略慢");
		AddDetail("@registry:recentdocs", "RecentDocs", 12LL * 1024 * 1024, false,
			"最近文件登錄紀錄", "檔案總管最近文件清單可能清空", true);
		AddDetail("@cli:dns-flush", "DNS 快取刷新", 1, false,
			"執行 ipconfig /flushdns", "可能短暫影響 DNS 解析");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DiscoveryStaleFilesTask : public HCleanDetailListTask {
public:
	DiscoveryStaleFilesTask() { SetScanTarget(1024LL * 1024 * 1024); SetWalkLimits(kHCleanLargeWalkLimits); }
	const char* GetId() const override { return "discovery_stale_files"; }
	const char* GetName() const override { return "過期暫存檔發現"; }
	const char* GetPurpose() const override {
		return "Downloads/Desktop/Pictures 等超過 30 天的 .tmp/.log/.bak";
	}
	const char* GetTooltip() const override { return "預設關閉；標記破壞性；依檔案大小排序"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddSortedStaleFileDiscoveryDetails(kMaxDetails);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

REG_CLEAN_TASK(DiscoveryRoamingHybridTask, discovery, 0)
REG_CLEAN_TASK(DiscoveryLocalHybridTask, discovery, 10)
REG_CLEAN_TASK(DiscoveryUwpAllTask, discovery, 20)
REG_CLEAN_TASK(DiscoveryInstallRegistryTask, discovery, 30)
REG_CLEAN_TASK(DiscoveryRegistryCleanupTask, discovery, 40)
REG_CLEAN_TASK(DiscoveryStaleFilesTask, discovery, 50)
