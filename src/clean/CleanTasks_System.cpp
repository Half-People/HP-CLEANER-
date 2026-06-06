#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(system, "系統", 0)

class WindowsUpdateCacheTask : public HCleanDetailListTask {
public:
	WindowsUpdateCacheTask()
	{
		SetScanTarget(1280LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "system_win_update_cache"; }
	const char* GetName() const override { return "Windows 更新快取"; }
	const char* GetPurpose() const override { return "清理 Windows Update 下載與安裝殘留"; }
	const char* GetTooltip() const override
	{
		return "清理前會嘗試停止 wuauserv 與 BITS；需要管理員權限效果較佳";
	}
	bool IsEnabledByDefault() const override { return true; }
	void BuildDetails() const override
	{
		AddDetail("%WINDIR%\\SoftwareDistribution\\Download", "下載快取", 512LL * 1024 * 1024, true,
			nullptr, nullptr, true);
		AddDetail("%WINDIR%\\SoftwareDistribution\\DataStore", "DataStore", 384LL * 1024 * 1024, true,
			nullptr, nullptr, true);
		AddDetail("%WINDIR%\\SoftwareDistribution\\PostRebootEventCache.V2", "重開機事件快取",
			256LL * 1024 * 1024, true, nullptr, nullptr, true);
		AddDetail("%WINDIR%\\Logs\\WindowsUpdate", "更新日誌", 128LL * 1024 * 1024, false,
			nullptr, nullptr, true);
	}
	bool Clean() override
	{
		HCleanStopWindowsUpdateServices();
		const bool ok = CleanSelectedDetailsWithProgress(GetName());
		HCleanStartWindowsUpdateServices();
		return ok;
	}
};

class SystemTempTask : public HCleanDetailListTask {
public:
	SystemTempTask()
	{
		SetScanTarget(420LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "system_temp"; }
	const char* GetName() const override { return "系統暫存"; }
	const char* GetPurpose() const override { return "清理 Windows\\Temp 與系統層暫存"; }
	const char* GetTooltip() const override { return "可能包含安裝程式暫存，部分檔案若被佔用將跳過"; }
	bool IsEnabledByDefault() const override { return true; }
	void BuildDetails() const override
	{
		AddDetail("%WINDIR%\\Temp", "系統 Temp (%WINDIR%)", 220LL * 1024 * 1024, true);
		AddDetail("%WINDIR%\\Prefetch", "Prefetch", 96LL * 1024 * 1024, false,
			"應用程式啟動預讀快取",
			"清理後部分程式首次啟動可能變慢",
			true);
		AddDetail("%PROGRAMDATA%\\Microsoft\\Windows\\WER\\Temp", "WER 暫存", 48LL * 1024 * 1024, false,
			"錯誤回報暫存",
			"除錯時可能缺少當機相關資料",
			true);
		AddDetail("%WINDIR%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Temp", "網路服務 Temp",
			40LL * 1024 * 1024, true);
		AddDetail("%WINDIR%\\ServiceProfiles\\LocalService\\AppData\\Local\\Temp", "本機服務 Temp",
			32LL * 1024 * 1024, true);
		AddDetail("%WINDIR%\\System32\\config\\systemprofile\\AppData\\Local\\Microsoft\\Windows\\INetCache",
			"系統 WebCache", 32LL * 1024 * 1024, false,
			"系統設定檔 Web 快取",
			"可安全清理");
		AddDetail("%WINDIR%\\System32\\config\\systemprofile\\AppData\\LocalLow\\Microsoft\\CryptnetUrlCache",
			"CryptnetUrlCache", 24LL * 1024 * 1024, false,
			"憑證 URL 快取",
			"憑證驗證可能略慢");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class ThumbnailCacheTask : public HCleanDetailListTask {
public:
	ThumbnailCacheTask() { SetScanTarget(180LL * 1024 * 1024); }
	const char* GetId() const override { return "system_thumbnail"; }
	const char* GetName() const override { return "縮圖快取"; }
	const char* GetPurpose() const override { return "清理 Explorer 產生的縮圖資料庫"; }
	const char* GetTooltip() const override { return "刪除後資料夾縮圖會重新產生，不影響原始檔案"; }
	bool IsEnabledByDefault() const override { return true; }
	void BuildDetails() const override
	{
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t thumb_count = HCleanEnumerateExplorerThumbcachePaths(paths, labels, kMaxDetails);
		for (size_t i = 0; i < thumb_count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 40LL * 1024 * 1024, true,
				"Explorer thumbcache 資料庫",
				"縮圖會重新產生");
		}
		char icon_paths[kMaxDetails][MAX_PATH * 4] = {};
		char icon_labels[kMaxDetails][128] = {};
		const size_t icon_count = HCleanEnumerateExplorerIconcachePaths(icon_paths, icon_labels,
			kMaxDetails - detail_count_);
		for (size_t i = 0; i < icon_count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(icon_paths[i], icon_labels[i], 32LL * 1024 * 1024, true,
				"Explorer iconcache 資料庫",
				"桌面圖示會重新產生");
		}
		if (detail_count_ == 0) {
			AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer", "Explorer 縮圖", 120LL * 1024 * 1024, true);
		}
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\IconCache", "圖示快取", 32LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\Caches", "快取資料", 28LL * 1024 * 1024, true);
	}
	bool Clean() override
	{
		const bool ok = CleanSelectedDetailsWithProgress(GetName());
		HCleanRefreshThumbnailIconCache();
		return ok;
	}
};
class WerReportsTask : public HCleanDetailListTask {
public:
	WerReportsTask() { SetScanTarget(320LL * 1024 * 1024); }
	const char* GetId() const override { return "system_wer"; }
	const char* GetName() const override { return "Windows 錯誤回報"; }
	const char* GetPurpose() const override { return "清理 WER 報告與暫存"; }
	const char* GetTooltip() const override { return "含 ReportQueue 與 ReportArchive，除錯時可能仍需保留較新報告"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%PROGRAMDATA%\\Microsoft\\Windows\\WER\\ReportQueue", "ReportQueue", 180LL * 1024 * 1024,
			true, nullptr, nullptr, true);
		AddDetail("%PROGRAMDATA%\\Microsoft\\Windows\\WER\\ReportArchive", "ReportArchive", 96LL * 1024 * 1024,
			true, nullptr, nullptr, true);
		AddDetail("%PROGRAMDATA%\\Microsoft\\Windows\\WER", "WER 根目錄", 44LL * 1024 * 1024, false,
			nullptr, nullptr, true);
		AddDetail("%WINDIR%\\Logs\\WaasMedic", "WaasMedic 日誌", 32LL * 1024 * 1024, false,
			"Windows 更新修復日誌",
			"除錯時可能缺少舊紀錄",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class DirectXShaderCacheTask : public HCleanDetailListTask {
public:
	DirectXShaderCacheTask() { SetScanTarget(512LL * 1024 * 1024); }
	const char* GetId() const override { return "system_dx_shader"; }
	const char* GetName() const override { return "DirectX 著色器快取"; }
	const char* GetPurpose() const override { return "清理系統層 D3D 著色器快取"; }
	const char* GetTooltip() const override { return "遊戲首次啟動可能需重新編譯著色器"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\D3DSCache", "D3D 快取", 280LL * 1024 * 1024, true);
		AddDetail("%PROGRAMDATA%\\Microsoft\\D3DSCache", "D3D 系統快取", 120LL * 1024 * 1024, false,
			"系統層 DirectX 著色器快取",
			"遊戲首次啟動可能需重新編譯");
		AddDetail("%LOCALAPPDATA%\\NVIDIA\\DXCache", "NVIDIA DXCache", 140LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\AMD\\DxCache", "AMD DxCache", 92LL * 1024 * 1024, true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class DeliveryOptimizationCacheTask : public HCleanDetailListTask {
public:
	DeliveryOptimizationCacheTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "system_delivery_optimization"; }
	const char* GetName() const override { return "傳遞最佳化快取"; }
	const char* GetPurpose() const override { return "清理 Windows Delivery Optimization 下載快取"; }
	const char* GetTooltip() const override
	{
		return "可釋放更新下載空間；後續更新可能需要重新下載部分內容";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%PROGRAMDATA%\\Microsoft\\Windows\\DeliveryOptimization\\Cache", "DO Cache",
			520LL * 1024 * 1024, true,
			"Windows Update 與 Microsoft Store 下載快取",
			"清理後更新內容可能需要重新下載");
		AddDetail("%WINDIR%\\SoftwareDistribution\\DeliveryOptimization", "DO Metadata",
			200LL * 1024 * 1024, false,
			"傳遞最佳化中繼資料",
			"短期內可能降低已下載片段重用率");
	}
	bool Clean() override
	{
		HCleanClearDeliveryOptimizationCache();
		return CleanSelectedDetailsWithProgress(GetName());
	}
};

class SystemFontCacheTask : public HCleanDetailListTask {
public:
	SystemFontCacheTask() { SetScanTarget(96LL * 1024 * 1024); }
	const char* GetId() const override { return "system_font_cache"; }
	const char* GetName() const override { return "字型快取"; }
	const char* GetPurpose() const override { return "清理 Windows 字型快取"; }
	const char* GetTooltip() const override { return "字型會在需要時重新產生，不影響已安裝字型"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\FontCache", "FontCache", 64LL * 1024 * 1024, true,
			"系統與應用程式字型渲染快取",
			"首次顯示部分字型可能略慢");
		AddDetail("%WINDIR%\\ServiceProfiles\\LocalService\\AppData\\Local\\FontCache", "服務 FontCache",
			32LL * 1024 * 1024, false,
			"本機服務帳戶字型快取",
			"通常可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class SystemWindowsLogsTask : public HCleanDetailListTask {
public:
	SystemWindowsLogsTask() { SetScanTarget(256LL * 1024 * 1024); }
	const char* GetId() const override { return "system_windows_logs"; }
	const char* GetName() const override { return "Windows 日誌"; }
	const char* GetPurpose() const override { return "清理 CBS / DISM 等可重建日誌"; }
	const char* GetTooltip() const override { return "僅刪除目錄第一層舊日誌檔，不影響系統設定"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%WINDIR%\\Logs\\CBS", "CBS 日誌", 120LL * 1024 * 1024, true,
			"元件存放區維護日誌",
			"疑難排解時可能缺少舊紀錄",
			true);
		AddDetail("%WINDIR%\\Logs\\DISM", "DISM 日誌", 80LL * 1024 * 1024, true,
			"映像修復日誌",
			"不影響已安裝功能",
			true);
		AddDetail("%WINDIR%\\Logs\\MoSetup", "功能更新日誌", 56LL * 1024 * 1024, false,
			"功能更新安裝紀錄",
			"清理後無法回溯舊版安裝細節",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class MicrosoftStoreCacheTask : public HCleanDetailListTask {
public:
	MicrosoftStoreCacheTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "system_ms_store_cache"; }
	const char* GetName() const override { return "Microsoft Store 快取"; }
	const char* GetPurpose() const override { return "清理 Store 應用下載與暫存"; }
	const char* GetTooltip() const override { return "Store 應用首次啟動可能重新下載資源"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Packages\\Microsoft.WindowsStore_8wekyb3d8bbwe\\LocalCache",
			"Windows Store LocalCache", 280LL * 1024 * 1024, true,
			"Microsoft Store 本機快取",
			"商店瀏覽可能暫時變慢");
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\AppCache", "AppCache", 120LL * 1024 * 1024, true,
			"UWP 應用共用快取",
			"部分應用需重新載入資源");
		AddDetail("%PROGRAMDATA%\\Microsoft\\Windows\\AppRepository", "AppRepository 暫存", 80LL * 1024 * 1024,
			false,
			"應用存放庫中繼資料",
			"清理後系統可能重新索引");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class SystemCrashDumpsTask : public HCleanDetailListTask {
public:
	SystemCrashDumpsTask() { SetScanTarget(4096LL * 1024 * 1024); }
	const char* GetId() const override { return "system_crash_dumps"; }
	const char* GetName() const override { return "系統當機傾印"; }
	const char* GetPurpose() const override { return "清理 Minidump 與完整記憶體傾印"; }
	const char* GetTooltip() const override { return "MEMORY.DMP 預設關閉；除錯舊當機時可能需保留"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%WINDIR%\\Minidump", "Minidump", 512LL * 1024 * 1024, true,
			"系統小型當機傾印",
			"除錯舊當機時可能缺少傾印",
			true);
		AddFileDetailIfExists("%WINDIR%\\MEMORY.DMP", "MEMORY.DMP", 2048LL * 1024 * 1024, false,
			"完整記憶體傾印",
			"刪除後無法分析該次當機",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class WindowsUpgradeLeftoverTask : public HCleanDetailListTask {
public:
	WindowsUpgradeLeftoverTask() { SetScanTarget(8192LL * 1024 * 1024); }
	const char* GetId() const override { return "system_upgrade_leftover"; }
	const char* GetName() const override { return "Windows 升級殘留"; }
	const char* GetPurpose() const override { return "清理 $Windows.~BT / ~WS 等升級暫存"; }
	const char* GetTooltip() const override { return "預設關閉；可能影響復原至舊版 Windows"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%SystemDrive%\\$Windows.~BT", "$Windows.~BT", 4096LL * 1024 * 1024, false,
			"功能更新安裝暫存",
			"刪除後可能無法復原舊版",
			true);
		AddDetailIfExists("%SystemDrive%\\$Windows.~WS", "$Windows.~WS", 2048LL * 1024 * 1024, false,
			"Windows 10 升級暫存",
			"刪除後可能無法復原舊版",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class DefenderScanHistoryTask : public HCleanDetailListTask {
public:
	DefenderScanHistoryTask() { SetScanTarget(256LL * 1024 * 1024); }
	const char* GetId() const override { return "system_defender_history"; }
	const char* GetName() const override { return "Defender 掃描紀錄"; }
	const char* GetPurpose() const override { return "清理 Windows Defender 掃描歷史日誌"; }
	const char* GetTooltip() const override { return "僅清理 History 下舊日誌；不影響隔離區"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows Defender\\Scans\\History\\Service",
			"掃描服務日誌", 120LL * 1024 * 1024, true,
			"Defender 掃描紀錄",
			"不影響即時防護");
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Windows Defender\\Scans\\History\\Results",
			"掃描結果日誌", 96LL * 1024 * 1024, false,
			"歷次掃描結果",
			"不影響隔離檔案");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class WindowsSearchTempTask : public HCleanDetailListTask {
public:
	WindowsSearchTempTask() { SetScanTarget(128LL * 1024 * 1024); }
	const char* GetId() const override { return "system_search_temp"; }
	const char* GetName() const override { return "Windows 搜尋暫存"; }
	const char* GetPurpose() const override { return "清理 Windows Search 索引暫存"; }
	const char* GetTooltip() const override { return "清理後搜尋可能短暫重建索引"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		// 僅清理 Temp；勿刪除 Applications\\Windows 使用中索引（可能導致 SearchHost 異常）。
		AddDetailIfExists("%PROGRAMDATA%\\Microsoft\\Search\\Data\\Temp", "Search Temp", 48LL * 1024 * 1024, true,
			"系統搜尋暫存",
			"索引會重建");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class SystemUwpCommonCacheTask : public HCleanDetailListTask {
public:
	SystemUwpCommonCacheTask() { SetScanTarget(960LL * 1024 * 1024); }
	const char* GetId() const override { return "system_uwp_cache"; }
	const char* GetName() const override { return "UWP 應用快取"; }
	const char* GetPurpose() const override { return "清理常用 Store 應用 LocalCache / TempState"; }
	const char* GetTooltip() const override { return "含電腦管家、相片、Widgets 等 UWP 快取"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateCommonUwpCachePaths(paths, labels, kMaxDetails);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, i < 4,
				"UWP 應用本機快取",
				"應用首次啟動可能重新下載資源");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class WindowsInstallerPatchCacheTask : public HCleanDetailListTask {
public:
	WindowsInstallerPatchCacheTask() { SetScanTarget(512LL * 1024 * 1024); }
	const char* GetId() const override { return "system_installer_patch"; }
	const char* GetName() const override { return "Windows Installer 修補快取"; }
	const char* GetPurpose() const override { return "清理 Installer $PatchCache$ 殘留"; }
	const char* GetTooltip() const override { return "預設關閉；修復/解除安裝部分程式可能需重新下載修補"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%WINDIR%\\Installer\\$PatchCache$", "$PatchCache$", 400LL * 1024 * 1024, false,
			"MSI 修補程式快取",
			"修復安裝可能需重新下載修補",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(WindowsUpdateCacheTask, system, 0)
REG_CLEAN_TASK(SystemTempTask, system, 10)
REG_CLEAN_TASK(ThumbnailCacheTask, system, 20)
REG_CLEAN_TASK(WerReportsTask, system, 30)
REG_CLEAN_TASK(DirectXShaderCacheTask, system, 40)
REG_CLEAN_TASK(DeliveryOptimizationCacheTask, system, 50)
REG_CLEAN_TASK(SystemFontCacheTask, system, 60)
REG_CLEAN_TASK(SystemWindowsLogsTask, system, 70)
REG_CLEAN_TASK(MicrosoftStoreCacheTask, system, 80)
REG_CLEAN_TASK(SystemCrashDumpsTask, system, 85)
REG_CLEAN_TASK(WindowsUpgradeLeftoverTask, system, 86)
REG_CLEAN_TASK(DefenderScanHistoryTask, system, 87)
REG_CLEAN_TASK(WindowsSearchTempTask, system, 88)
REG_CLEAN_TASK(SystemUwpCommonCacheTask, system, 89)
REG_CLEAN_TASK(WindowsInstallerPatchCacheTask, system, 91)

class BitsDownloaderCacheTask : public HCleanDetailListTask {
public:
	BitsDownloaderCacheTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "system_bits_cache"; }
	const char* GetName() const override { return "BITS 下載快取"; }
	const char* GetPurpose() const override { return "清理 Background Intelligent Transfer 暫存"; }
	const char* GetTooltip() const override { return "可能中斷進行中的背景下載；預設關閉"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%ALLUSERSPROFILE%\\Microsoft\\Network\\Downloader", "BITS Downloader", 400LL * 1024 * 1024, false,
			"Windows Update / Store 背景下載暫存",
			"進行中下載可能需重新開始",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class OneDriveCacheTask : public HCleanDetailListTask {
public:
	OneDriveCacheTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "system_onedrive_cache"; }
	const char* GetName() const override { return "OneDrive 快取"; }
	const char* GetPurpose() const override { return "清理 OneDrive 日誌與本機快取"; }
	const char* GetTooltip() const override { return "不刪除雲端檔案；僅清理本機快取與日誌"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\OneDrive\\logs", "OneDrive 日誌", 120LL * 1024 * 1024, true,
			"同步用戶端日誌",
			"不影響雲端檔案");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\OneDrive\\setup\\logs", "安裝日誌", 80LL * 1024 * 1024, true,
			"OneDrive 安裝/更新日誌",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\OneDrive\\cache", "OneDrive cache", 400LL * 1024 * 1024, false,
			"本機同步快取",
			"檔案可能需重新下載預覽",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class MicrosoftOfficeCacheTask : public HCleanDetailListTask {
public:
	MicrosoftOfficeCacheTask() { SetScanTarget(960LL * 1024 * 1024); }
	const char* GetId() const override { return "system_office_cache"; }
	const char* GetName() const override { return "Microsoft Office 快取"; }
	const char* GetPurpose() const override { return "清理 Office 檔案快取與字型快取"; }
	const char* GetTooltip() const override { return "OfficeFileCache 預設關閉；可能需重新產生預覽"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Office\\16.0\\OfficeFileCache", "Office 檔案快取",
			600LL * 1024 * 1024, false,
			"Word/Excel/PPT 本機預覽快取",
			"首次開啟文件可能略慢",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\FontCache", "Office FontCache", 120LL * 1024 * 1024, true,
			"Office 字型渲染快取",
			"字型會重新產生");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Office\\16.0\\Wef", "Office Wef 快取",
			240LL * 1024 * 1024, true,
			"Office Web 延伸快取",
			"外掛可能略慢載入");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class WindowsTerminalLogsTask : public HCleanDetailListTask {
public:
	WindowsTerminalLogsTask() { SetScanTarget(160LL * 1024 * 1024); }
	const char* GetId() const override { return "system_terminal_logs"; }
	const char* GetName() const override { return "Terminal / PowerShell 日誌"; }
	const char* GetPurpose() const override { return "清理 Windows Terminal 與 PowerShell 日誌"; }
	const char* GetTooltip() const override { return "不影響設定與設定檔"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Packages\\Microsoft.WindowsTerminal_8wekyb3d8bbwe\\LocalState\\logs",
			"Windows Terminal 日誌", 80LL * 1024 * 1024, true,
			"Terminal 執行日誌",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\PowerShell\\7\\logs", "PowerShell 7 日誌",
			40LL * 1024 * 1024, true,
			"PowerShell 7 日誌",
			"不影響設定檔");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\PowerShell\\logs", "Windows PowerShell 日誌",
			40LL * 1024 * 1024, false,
			"Windows PowerShell 5.x 日誌",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(BitsDownloaderCacheTask, system, 90)
REG_CLEAN_TASK(OneDriveCacheTask, system, 100)
REG_CLEAN_TASK(MicrosoftOfficeCacheTask, system, 110)
REG_CLEAN_TASK(WindowsTerminalLogsTask, system, 120)
