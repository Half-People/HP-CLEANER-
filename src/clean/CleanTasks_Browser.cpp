#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(browser, "瀏覽器與通訊", 25)

class EdgeCacheTask : public HCleanDetailListTask {
public:
	EdgeCacheTask() { SetScanTarget(900LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_edge_cache"; }
	const char* GetName() const override { return "Microsoft Edge 快取"; }
	const char* GetPurpose() const override { return "清理 Edge 所有設定檔快取"; }
	const char* GetTooltip() const override { return "不會清除書籤與密碼；關閉 Edge 後效果較佳"; }
	bool IsEnabledByDefault() const override { return true; }
	void BuildDetails() const override
	{
		AddChromiumProfileCaches("%LOCALAPPDATA%\\Microsoft\\Edge\\User Data", true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class ChromeCacheTask : public HCleanDetailListTask {
public:
	ChromeCacheTask() { SetScanTarget(1100LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_chrome_cache"; }
	const char* GetName() const override { return "Google Chrome 快取"; }
	const char* GetPurpose() const override { return "清理 Chrome 所有設定檔快取"; }
	const char* GetTooltip() const override { return "不會清除書籤與密碼；關閉 Chrome 後效果較佳"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddChromiumProfileCaches("%LOCALAPPDATA%\\Google\\Chrome\\User Data", true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class BrowserCookiesTask : public HCleanDetailListTask {
public:
	BrowserCookiesTask() { SetScanTarget(48LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_cookies"; }
	const char* GetName() const override { return "瀏覽器 Cookie"; }
	const char* GetPurpose() const override { return "清理 Edge / Chrome Cookie 資料庫"; }
	const char* GetTooltip() const override { return "預設關閉；可能導致網站登出，請謹慎勾選"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Network", "Edge Network", 24LL * 1024 * 1024,
			true,
			"Edge Cookie 與網路狀態資料",
			"可能導致網站登出",
			true);
		AddDetail("%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Network", "Chrome Network", 24LL * 1024 * 1024,
			true,
			"Chrome Cookie 與網路狀態資料",
			"可能導致網站登出",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class FirefoxCacheTask : public HCleanDetailListTask {
public:
	FirefoxCacheTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_firefox_cache"; }
	const char* GetName() const override { return "Mozilla Firefox 快取"; }
	const char* GetPurpose() const override { return "清理所有 Firefox 設定檔快取"; }
	const char* GetTooltip() const override { return "不清除書籤與密碼；建議先關閉 Firefox"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddFirefoxProfileCaches(true);
		AddDetail("%APPDATA%\\Mozilla\\Firefox\\Crash Reports", "Crash Reports", 120LL * 1024 * 1024, false,
			"Firefox 當機回報資料",
			"移除後將無法回溯舊當機資訊",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class BraveCacheTask : public HCleanDetailListTask {
public:
	BraveCacheTask() { SetScanTarget(900LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_brave_cache"; }
	const char* GetName() const override { return "Brave 快取"; }
	const char* GetPurpose() const override { return "清理 Brave 所有設定檔快取"; }
	const char* GetTooltip() const override { return "不會清除書籤與密碼；關閉 Brave 後效果較佳"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddChromiumProfileCaches("%LOCALAPPDATA%\\BraveSoftware\\Brave-Browser\\User Data", true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class DiscordCacheTask : public HCleanDetailListTask {
public:
	DiscordCacheTask() { SetScanTarget(640LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_discord_cache"; }
	const char* GetName() const override { return "Discord 快取"; }
	const char* GetPurpose() const override { return "清理 Discord 快取與暫存"; }
	const char* GetTooltip() const override { return "請先關閉 Discord；不刪除帳號與訊息資料"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%APPDATA%\\discord\\Cache", "Discord Cache", 360LL * 1024 * 1024, true,
			"Discord 媒體與 UI 快取",
			"首次開啟頻道可能重新下載預覽");
		AddDetail("%APPDATA%\\discord\\Code Cache", "Code Cache", 180LL * 1024 * 1024, true,
			"Electron 程式快取",
			"啟動後會逐步重建");
		AddDetail("%APPDATA%\\discord\\GPUCache", "GPUCache", 100LL * 1024 * 1024, false,
			"GPU 快取",
			"不影響登入狀態");
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class OperaCacheTask : public HCleanDetailListTask {
public:
	OperaCacheTask() { SetScanTarget(720LL * 1024 * 1024); }
	const char* GetId() const override { return "browser_opera_cache"; }
	const char* GetName() const override { return "Opera 快取"; }
	const char* GetPurpose() const override { return "清理 Opera 所有設定檔快取"; }
	const char* GetTooltip() const override { return "不會清除書籤與密碼；關閉 Opera 後效果較佳"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddChromiumProfileCaches("%APPDATA%\\Opera Software\\Opera Stable", true);
		AddChromiumProfileCaches("%LOCALAPPDATA%\\Opera Software\\Opera Stable", false);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

REG_CLEAN_TASK(EdgeCacheTask, browser, 0)
REG_CLEAN_TASK(ChromeCacheTask, browser, 10)
REG_CLEAN_TASK(BrowserCookiesTask, browser, 20)
REG_CLEAN_TASK(FirefoxCacheTask, browser, 30)
REG_CLEAN_TASK(BraveCacheTask, browser, 40)
REG_CLEAN_TASK(DiscordCacheTask, browser, 50)
REG_CLEAN_TASK(OperaCacheTask, browser, 60)
