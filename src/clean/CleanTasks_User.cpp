#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"

REG_CLEAN_CATEGORY(user, "使用者", 10)

class UserTempTask : public HCleanDetailListTask {
public:
	UserTempTask()
	{
		SetScanTarget(2400LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "user_temp"; }
	const char* GetName() const override { return "使用者暫存"; }
	const char* GetPurpose() const override { return "清理 %TEMP% 與本機 AppData 暫存"; }
	const char* GetTooltip() const override { return "包含常用軟體產生的暫存檔，執行中程式可能鎖定部分檔案"; }
	bool IsEnabledByDefault() const override { return true; }
	void BuildDetails() const override
	{
		AddDetail("%TEMP%", "使用者 Temp", 1800LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Temp", "本機 Temp", 420LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache", "INet 快取", 120LL * 1024 * 1024, false);
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache\\Content.IE5", "Content.IE5", 40LL * 1024 * 1024,
			false,
			"舊版 IE 快取索引",
			"可安全清理");
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\WebCache", "WebCache", 160LL * 1024 * 1024, false,
			"Edge/IE WebCache 資料庫",
			"部分網頁可能重新載入");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class RecentFilesTask : public HCleanDetailListTask {
public:
	RecentFilesTask() { SetScanTarget(80LL * 1024 * 1024); }
	const char* GetId() const override { return "user_recent"; }
	const char* GetName() const override { return "最近使用的檔案"; }
	const char* GetPurpose() const override { return "清除「最近」清單與捷徑紀錄"; }
	const char* GetTooltip() const override { return "不刪除實際檔案，僅清理捷徑與最近清單"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%APPDATA%\\Microsoft\\Windows\\Recent", "Recent 捷徑", 48LL * 1024 * 1024, true);
		AddDetail("%APPDATA%\\Microsoft\\Windows\\Recent\\AutomaticDestinations", "自動目的地", 20LL * 1024 * 1024,
			true);
		AddDetail("%APPDATA%\\Microsoft\\Windows\\Recent\\CustomDestinations", "自訂目的地", 12LL * 1024 * 1024,
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class RecycleBinTask : public HCleanDetailListTask {
public:
	RecycleBinTask() { SetScanTarget(512LL * 1024 * 1024); }
	const char* GetId() const override { return "user_recycle_bin"; }
	const char* GetName() const override { return "資源回收筒"; }
	const char* GetPurpose() const override { return "清空資源回收筒內容"; }
	const char* GetTooltip() const override { return "掃描僅估算；清理將永久刪除回收筒內所有項目"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddRecycleBinDetails(true);
	}
	bool Clean() override
	{
		EnsureDetails();
		const int64_t estimate_freed = cached_bytes_ >= 0 ? cached_bytes_ : 0;
		const bool ok = HCleanEmptyRecycleBin();
		last_freed_bytes_ = ok ? estimate_freed : 0;
		for (size_t i = 0; i < detail_count_; ++i) {
			details_[i].bytes = 0;
		}
		cached_bytes_ = 0;
		scan_.state = HCleanScanState::Done;
		scan_.percent = 100.f;
		scan_.status_text = ok ? "已清理" : "清理失敗";
		details_ready_ = true;
		HLOG_INFO("Clean recycle bin: task '{}', ok={}, estimated freed {} bytes",
			GetId(), ok, last_freed_bytes_);
		return ok;
	}
};

class UserLogTask : public HCleanDetailListTask {
public:
	UserLogTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "user_logs"; }
	const char* GetName() const override { return "日誌檔案"; }
	const char* GetPurpose() const override { return "清理應用程式與工具產生的舊日誌"; }
	const char* GetTooltip() const override { return "僅處理可安全刪除的日誌，不影響目前執行中的程式"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\CrashDumps", "當機傾印", 200LL * 1024 * 1024, true,
			"應用程式當機產生的 .dmp",
			"除錯舊當機時可能缺少傾印",
			true);
		AddDetailIfExists("%WINDIR%\\Minidump", "系統 Minidump", 180LL * 1024 * 1024, false,
			"系統層小型傾印",
			"除錯系統當機時可能需保留",
			true);
		AddDetail("%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer", "Explorer 日誌", 48LL * 1024 * 1024, false,
			"檔案總管相關日誌",
			"不影響檔案本身");
		AddDetail("%APPDATA%\\discord\\logs", "Discord 日誌", 120LL * 1024 * 1024, false,
			"Discord 用戶端日誌",
			"僅刪除第一層 .log，不影響帳號");
		AddDetail("%LOCALAPPDATA%\\Microsoft\\CLR_v4.0\\UsageLogs", ".NET UsageLogs", 32LL * 1024 * 1024,
			false,
			".NET 執行階段使用紀錄",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(UserTempTask, user, 0)
REG_CLEAN_TASK(RecentFilesTask, user, 10)
REG_CLEAN_TASK(RecycleBinTask, user, 20)
REG_CLEAN_TASK(UserLogTask, user, 30)
