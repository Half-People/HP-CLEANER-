#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(advanced, "進階清理", 12)

class AdvancedLegacyWebTask : public HCleanDetailListTask {
public:
	AdvancedLegacyWebTask() { SetScanTarget(320LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_legacy_web"; }
	const char* GetName() const override { return "舊版 IE / Web 殘留"; }
	const char* GetPurpose() const override { return "清理 Internet Explorer 與舊版 Web 快取目錄"; }
	const char* GetTooltip() const override { return "參考業界常見清理項；不含 Cookie（預設跳過）"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\History", "瀏覽歷史", 80LL * 1024 * 1024, true,
			"IE / Edge 舊版歷史索引",
			"歷史清單會清空");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\Temporary Internet Files",
			"Temporary Internet Files", 120LL * 1024 * 1024, true,
			"舊版 IE 離線快取",
			"部分網頁可能重新載入");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Feeds", "RSS 摘要", 24LL * 1024 * 1024, false,
			"Windows RSS Feeds 快取",
			"摘要可能需重新訂閱");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Internet Explorer\\DOMStore", "IE DOMStore",
			48LL * 1024 * 1024, true,
			"IE DOM 儲存",
			"部分網站設定可能遺失",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Internet Explorer\\Recovery", "IE Recovery",
			32LL * 1024 * 1024, true,
			"IE 工作階段復原",
			"未儲存分頁復原資料會遺失",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Internet Explorer\\ImageStore", "IE ImageStore",
			40LL * 1024 * 1024, false,
			"IE 圖片快取",
			"可安全清理");
		AddFileDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Internet Explorer\\brndlog.txt", "IE 品牌日誌",
			4LL * 1024 * 1024, true,
			"IE 診斷日誌",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdvancedLegacyPluginsTask : public HCleanDetailListTask {
public:
	AdvancedLegacyPluginsTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_legacy_plugins"; }
	const char* GetName() const override { return "Flash / Java / Silverlight"; }
	const char* GetPurpose() const override { return "清理已淘汰執行環境的快取"; }
	const char* GetTooltip() const override { return "參考業界常見清理項；僅快取目錄"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Macromedia\\Flash Player", "Flash Player (Macromedia)",
			120LL * 1024 * 1024, true,
			"Adobe Flash 本機快取",
			"已停止支援，可安全清理");
		AddDetailIfExists("%APPDATA%\\Adobe\\Flash Player", "Flash Player (Adobe)",
			80LL * 1024 * 1024, true,
			"Adobe Flash 快取",
			"可安全清理");
		AddDetailIfExists("%APPDATA%\\Sun\\Java\\Deployment\\cache", "Java Deployment 快取",
			160LL * 1024 * 1024, true,
			"Java Applet 下載快取",
			"舊版 Java 程式可能重新下載");
		AddDetailIfExists("%USERPROFILE%\\.java\\deployment\\cache", "Java 使用者快取",
			80LL * 1024 * 1024, true,
			"Java 部署快取",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Silverlight", "Silverlight 快取",
			64LL * 1024 * 1024, true,
			"Microsoft Silverlight 快取",
			"已停止支援，可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdvancedRdpRemoteTask : public HCleanDetailListTask {
public:
	AdvancedRdpRemoteTask() { SetScanTarget(256LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_rdp_remote"; }
	const char* GetName() const override { return "RDP / 遠端桌面快取"; }
	const char* GetPurpose() const override { return "清理 Terminal Server Client 與連線裝置快取"; }
	const char* GetTooltip() const override { return "不刪除已儲存的 RDP 連線設定 (.rdp)"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Terminal Server Client\\Cache", "RDP 位圖快取",
			120LL * 1024 * 1024, true,
			"Terminal Server Client 快取",
			"遠端桌面縮圖會重新產生");

		static const char* k_cdp_subs[] = { "Cache", "LocalState\\Cache" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateWildcardChildSubdirs(
			"%LOCALAPPDATA%\\ConnectedDevicesPlatform", L"*", k_cdp_subs, 2, "CDP",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 40LL * 1024 * 1024, true,
				"連線裝置平台快取",
				"跨裝置同步可能略慢");
		}
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdvancedOfficeMediaTask : public HCleanDetailListTask {
public:
	AdvancedOfficeMediaTask() { SetScanTarget(960LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_office_media"; }
	const char* GetName() const override { return "LibreOffice / 舊版媒體"; }
	const char* GetPurpose() const override { return "清理 LibreOffice、Winamp、RealPlayer、iTunes 快取"; }
	const char* GetTooltip() const override { return "參考業界常見清理項；不含郵件資料"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\LibreOffice\\4\\user\\cache", "LibreOffice 快取",
			120LL * 1024 * 1024, true,
			"LibreOffice 渲染快取",
			"首次開啟文件可能略慢");
		AddDetailIfExists("%APPDATA%\\LibreOffice\\4\\user\\backup", "LibreOffice 備份",
			80LL * 1024 * 1024, false,
			"自動備份副本",
			"可能無法還原舊版",
			true);
		AddDetailIfExists("%APPDATA%\\OpenOffice\\4\\user\\cache", "OpenOffice 快取",
			80LL * 1024 * 1024, true,
			"OpenOffice 快取",
			"可安全清理");

		AddDetailIfExists("%APPDATA%\\Winamp\\Cache", "Winamp 快取", 64LL * 1024 * 1024, true,
			"Winamp 媒體快取",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Winamp\\Cache", "Winamp Local 快取", 48LL * 1024 * 1024, true,
			"Winamp 本機快取",
			"可安全清理");

		AddDetailIfExists("%APPDATA%\\Real\\RealPlayer\\Cache", "RealPlayer 快取",
			80LL * 1024 * 1024, true,
			"RealPlayer 快取",
			"可安全清理");
		AddDetailIfExists("%LOCALAPPDATA%\\Real\\RealPlayer\\Cache", "RealPlayer Local 快取",
			60LL * 1024 * 1024, true,
			"RealPlayer 本機快取",
			"可安全清理");

		AddDetailIfExists("%LOCALAPPDATA%\\Apple Computer\\iTunes\\Cache", "iTunes 快取",
			200LL * 1024 * 1024, true,
			"iTunes 媒體快取",
			"封面與預覽可能重新下載");
		AddDetailIfExists("%LOCALAPPDATA%\\Apple Computer\\iTunes\\SubscriptionPlayCache",
			"iTunes 訂閱快取", 120LL * 1024 * 1024, false,
			"Apple Music 訂閱快取",
			"串流緩衝會重建",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Apple Computer\\iTunes\\Downloads", "iTunes 下載暫存",
			160LL * 1024 * 1024, false,
			"進行中下載暫存",
			"中斷下載需重新開始",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdvancedSkypePidginTask : public HCleanDetailListTask {
public:
	AdvancedSkypePidginTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_skype_pidgin"; }
	const char* GetName() const override { return "Skype / Pidgin"; }
	const char* GetPurpose() const override { return "清理 Skype 快取與 Pidgin 日誌"; }
	const char* GetTooltip() const override { return "不含聊天紀錄本體；Skype 安裝快取預設關閉"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%APPDATA%\\Microsoft\\Skype for Desktop\\Cache", "Skype 桌面版 Cache",
			180LL * 1024 * 1024, true,
			"Skype Electron 快取",
			"首次啟動可能略慢");
		AddDetailIfExists("%APPDATA%\\Microsoft\\Skype for Desktop\\logs", "Skype 桌面版日誌",
			80LL * 1024 * 1024, true,
			"Skype 執行日誌",
			"不影響登入");
		AddDetailIfExists("%APPDATA%\\Skype\\Cache", "Skype 舊版 Cache",
			120LL * 1024 * 1024, true,
			"舊版 Skype 快取",
			"可安全清理");

		static const wchar_t* k_uwp_subs[] = { L"LocalCache", L"LocalState\\logs", L"TempState" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t uwp_count = HCleanEnumerateUwpPackagePaths("Microsoft.SkypeApp_*", k_uwp_subs, 3,
			"Skype UWP", paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < uwp_count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 80LL * 1024 * 1024, true,
				"Store 版 Skype 快取",
				"不刪除帳號");
		}

		AddDetailIfExists("%APPDATA%\\.purple\\logs", "Pidgin 日誌", 48LL * 1024 * 1024, true,
			"Pidgin IM 日誌",
			"不影響帳號設定");
		AddDetailIfExists("%APPDATA%\\.purple\\icons", "Pidgin 圖示快取", 24LL * 1024 * 1024, false,
			"Pidgin 頭像快取",
			"頭像可能重新下載");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class AdvancedVmTransferTask : public HCleanDetailListTask {
public:
	AdvancedVmTransferTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_vm_transfer"; }
	const char* GetName() const override { return "虛擬化 / 傳輸工具"; }
	const char* GetPurpose() const override { return "清理 VirtualBox、VMware、WinSCP 暫存"; }
	const char* GetTooltip() const override { return "僅日誌與暫存；不刪除虛擬硬碟或 WinSCP 工作階段"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\VirtualBox\\Logs", "VirtualBox 日誌",
			120LL * 1024 * 1024, true,
			"VirtualBox 執行日誌",
			"不影響虛擬機");
		AddDetailIfExists("%LOCALAPPDATA%\\VirtualBox\\cache", "VirtualBox 快取",
			80LL * 1024 * 1024, false,
			"VirtualBox UI 快取",
			"可安全清理");

		AddDetailIfExists("%APPDATA%\\VMware\\logs", "VMware 日誌", 100LL * 1024 * 1024, true,
			"VMware Workstation 日誌",
			"不影響虛擬機");
		AddFileDetailIfExists("%APPDATA%\\VMware\\inventory.vmls", "VMware inventory",
			20LL * 1024 * 1024, false,
			"最近開啟清單",
			"最近清單會重置",
			true);

		AddDetailIfExists("%APPDATA%\\Martin Prikryl\\WinSCP 2\\Temporary", "WinSCP 暫存",
			80LL * 1024 * 1024, true,
			"WinSCP 傳輸暫存",
			"進行中傳輸可能中斷",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdvancedExplorerExtrasTask : public HCleanDetailListTask {
public:
	AdvancedExplorerExtrasTask() { SetScanTarget(160LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_explorer_extras"; }
	const char* GetName() const override { return "Explorer 附加快取"; }
	const char* GetPurpose() const override { return "清理光碟燒錄暫存等 Explorer 附屬目錄"; }
	const char* GetTooltip() const override { return "參考業界常見 Explorer 附屬快取路徑"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\Burn\\Burn", "光碟燒錄暫存",
			48LL * 1024 * 1024, true,
			"待燒錄檔案暫存",
			"進行中燒錄可能中斷",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AdvancedDeepScanLiteTask : public HCleanDetailListTask {
public:
	AdvancedDeepScanLiteTask()
	{
		SetScanTarget(512LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "advanced_deepscan_lite"; }
	const char* GetName() const override { return "深度掃描（輕量）"; }
	const char* GetPurpose() const override { return "清理使用者目錄常見 Thumbs.db 與孤立暫存"; }
	const char* GetTooltip() const override {
		return "預設關閉；非全碟深度掃描，僅常見使用者資料夾";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_thumbs[] = {
			"%USERPROFILE%\\Desktop\\Thumbs.db",
			"%USERPROFILE%\\Documents\\Thumbs.db",
			"%USERPROFILE%\\Pictures\\Thumbs.db",
			"%USERPROFILE%\\Videos\\Thumbs.db",
			"%USERPROFILE%\\Downloads\\Thumbs.db",
			"%USERPROFILE%\\Music\\Thumbs.db",
		};
		for (size_t i = 0; i < sizeof(k_thumbs) / sizeof(k_thumbs[0]) && detail_count_ < kMaxDetails; ++i) {
			AddFileDetailIfExists(k_thumbs[i], "Thumbs.db", 8LL * 1024 * 1024, false,
				"資料夾縮圖快取 (Thumbs.db)",
				"該資料夾縮圖會重新產生");
		}
		AddDetailIfExists("%USERPROFILE%\\AppData\\Local\\Temp", "使用者設定檔 Temp",
			200LL * 1024 * 1024, false,
			"設定檔下 Local\\Temp",
			"與 user_temp 部分重疊",
			true);
		AddDetailIfExists("%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache\\Content.MSO",
			"Office 網頁快取", 48LL * 1024 * 1024, false,
			"Office 嵌入物件快取",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class AdvancedThirdPartyLogsTask : public HCleanDetailListTask {
public:
	AdvancedThirdPartyLogsTask() { SetScanTarget(128LL * 1024 * 1024); }
	const char* GetId() const override { return "advanced_third_party_logs"; }
	const char* GetName() const override { return "第三方清理工具日誌"; }
	const char* GetPurpose() const override { return "清理 CCleaner 等第三方清理工具自身日誌"; }
	const char* GetTooltip() const override { return "僅刪除日誌；不影響清理工具設定"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%PROGRAMDATA%\\Piriform\\CCleaner\\Log", "CCleaner 日誌",
			40LL * 1024 * 1024, true,
			"CCleaner 執行日誌",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(AdvancedLegacyWebTask, advanced, 0)
REG_CLEAN_TASK(AdvancedLegacyPluginsTask, advanced, 10)
REG_CLEAN_TASK(AdvancedRdpRemoteTask, advanced, 20)
REG_CLEAN_TASK(AdvancedOfficeMediaTask, advanced, 30)
REG_CLEAN_TASK(AdvancedSkypePidginTask, advanced, 40)
REG_CLEAN_TASK(AdvancedVmTransferTask, advanced, 50)
REG_CLEAN_TASK(AdvancedExplorerExtrasTask, advanced, 60)
REG_CLEAN_TASK(AdvancedDeepScanLiteTask, advanced, 70)
REG_CLEAN_TASK(AdvancedThirdPartyLogsTask, advanced, 80)
