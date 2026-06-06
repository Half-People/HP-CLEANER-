#include "HCleanTask.h"
#include "HCleanTaskCommon.h"
#include "HPage.h"
#include <cstring>

namespace {
constexpr const char* kCliDockerBuilderPrune = "@cli:docker-builder-prune";
constexpr const char* kCliDockerContainerPrune = "@cli:docker-container-prune";
constexpr const char* kCliDockerImagePrune = "@cli:docker-image-prune";
constexpr const char* kCliDockerVolumePrune = "@cli:docker-volume-prune";
constexpr const char* kCliDockerSystemPruneAll = "@cli:docker-system-prune-all";

bool DetailPathContains(const char* path, const char* needle)
{
	return path != nullptr && needle != nullptr && std::strstr(path, needle) != nullptr;
}

bool RunDockerCliDetail(const char* path)
{
	if (path == nullptr) {
		return false;
	}
	if (std::strcmp(path, kCliDockerBuilderPrune) == 0) {
		return HCleanRunDockerBuilderPrune();
	}
	if (std::strcmp(path, kCliDockerContainerPrune) == 0) {
		return HCleanRunDockerContainerPrune();
	}
	if (std::strcmp(path, kCliDockerImagePrune) == 0) {
		return HCleanRunDockerImagePrune();
	}
	if (std::strcmp(path, kCliDockerVolumePrune) == 0) {
		return HCleanRunDockerVolumePrune();
	}
	if (std::strcmp(path, kCliDockerSystemPruneAll) == 0) {
		return HCleanRunDockerSystemPruneAll();
	}
	return false;
}

bool IsNpmCacheDetailPath(const char* path)
{
	return DetailPathContains(path, "npm-cache");
}

bool IsPnpmStoreDetailPath(const char* path)
{
	return DetailPathContains(path, "pnpm\\store") || DetailPathContains(path, "pnpm/store");
}

bool IsPnpmCacheDetailPath(const char* path)
{
	return DetailPathContains(path, "pnpm\\cache") || DetailPathContains(path, "pnpm/cache");
}

bool IsNugetPackagesDetailPath(const char* path)
{
	return DetailPathContains(path, ".nuget\\packages") || DetailPathContains(path, ".nuget/packages");
}

bool IsNugetHttpCacheDetailPath(const char* path)
{
	return DetailPathContains(path, "NuGet\\v3-cache") || DetailPathContains(path, "NuGet/v3-cache")
		|| DetailPathContains(path, "NuGet\\Cache") || DetailPathContains(path, "NuGet/Cache");
}
} // namespace

REG_CLEAN_CATEGORY(dev_game, "開發者 - 遊戲開發", 35)
REG_CLEAN_CATEGORY(dev_web, "開發者 - 網頁開發", 36)
REG_CLEAN_CATEGORY(dev_ai, "開發者 - 人工智慧開發", 37)
REG_CLEAN_CATEGORY(dev_backend, "開發者 - 後端開發", 38)
REG_CLEAN_CATEGORY(dev_platform, "開發者 - 平台開發", 39)

class NpmCacheTask : public HCleanDetailListTask {
public:
	NpmCacheTask()
	{
		SetScanTarget(800LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "dev_npm_cache"; }
	const char* GetName() const override { return "npm 快取"; }
	const char* GetPurpose() const override { return "清理 npm 全域快取目錄"; }
	const char* GetTooltip() const override
	{
		return "預設僅清理 npm-cache；npm 本機目錄預設關閉，避免影響全域安裝";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\npm-cache", "npm-cache", 520LL * 1024 * 1024, true,
			"npm 下載快取",
			"下次 install 可能重新下載");
		AddDetail("%APPDATA%\\npm-cache", "Roaming npm-cache", 180LL * 1024 * 1024, true,
			"Roaming 下的 npm 快取",
			"下次 install 可能重新下載");
		AddDetail("%LOCALAPPDATA%\\npm", "npm 本機", 100LL * 1024 * 1024, false,
			"npm 全域安裝與本機資料",
			"可能影響全域安裝的 CLI 工具",
			true);
	}
	bool Clean() override
	{
		EnsureDetails();
		bool run_npm_cli = false;
		for (size_t i = 0; i < detail_count_; ++i) {
			if (details_[i].selected && IsNpmCacheDetailPath(details_[i].path)) {
				run_npm_cli = true;
				break;
			}
		}
		if (run_npm_cli && HCleanRunNpmCacheClean()) {
			HLOG_INFO("npm cache clean CLI succeeded");
		}
		return CleanSelectedDetailsWithProgress(GetName());
	}
};

class PipCacheTask : public HCleanDetailListTask {
public:
	PipCacheTask() { SetScanTarget(600LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_pip_cache"; }
	const char* GetName() const override { return "pip 快取"; }
	const char* GetPurpose() const override { return "清理 pip wheel 與快取"; }
	const char* GetTooltip() const override { return "含 pip\\cache 與 Local\\pip\\Cache"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\pip\\Cache", "pip Cache", 400LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\pypoetry\\Cache", "Poetry 快取", 120LL * 1024 * 1024, false);
		AddDetail("%APPDATA%\\pip", "pip 設定目錄", 80LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class NuGetCacheTask : public HCleanDetailListTask {
public:
	NuGetCacheTask()
	{
		SetScanTarget(1200LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "dev_nuget_cache"; }
	const char* GetName() const override { return "NuGet 快取"; }
	const char* GetPurpose() const override { return "清理 NuGet HTTP 快取（預設不刪 packages）"; }
	const char* GetTooltip() const override
	{
		return "預設僅 v3/HTTP 快取；packages 目錄預設關閉，刪除會迫使所有專案重新還原";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\NuGet\\v3-cache", "v3-cache", 200LL * 1024 * 1024, true,
			"NuGet HTTP 下載快取",
			"下次還原可能重新下載中繼資料");
		AddDetail("%LOCALAPPDATA%\\NuGet\\Cache", "NuGet Cache", 100LL * 1024 * 1024, true,
			"NuGet 暫存快取",
			"不影響已還原的 packages");
		AddDetail("%USERPROFILE%\\.nuget\\packages", "packages", 900LL * 1024 * 1024, false,
			"NuGet 全域 packages 倉庫",
			"警告：刪除後所有 .NET 專案需完整重新還原",
			true);
	}
	bool Clean() override
	{
		EnsureDetails();
		bool run_http_cli = false;
		bool run_packages_cli = false;
		for (size_t i = 0; i < detail_count_; ++i) {
			if (!details_[i].selected || details_[i].path == nullptr) {
				continue;
			}
			if (IsNugetPackagesDetailPath(details_[i].path)) {
				run_packages_cli = true;
			}
			else if (IsNugetHttpCacheDetailPath(details_[i].path)) {
				run_http_cli = true;
			}
		}
		if (run_http_cli && HCleanRunNugetHttpCacheClear()) {
			HLOG_INFO("dotnet nuget http-cache clear succeeded");
		}
		if (run_packages_cli && HCleanRunNugetGlobalPackagesClear()) {
			HLOG_INFO("dotnet nuget global-packages clear succeeded");
		}
		return CleanSelectedDetailsWithProgress(GetName());
	}
};

class DockerCacheTask : public HCleanDetailListTask {
public:
	DockerCacheTask()
	{
		SetScanTarget(4096LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "dev_docker"; }
	const char* GetName() const override { return "Docker 資料"; }
	const char* GetPurpose() const override { return "細分清理 Docker 建置快取、CLI 與 Desktop 日誌"; }
	const char* GetTooltip() const override
	{
		return "預設僅建置快取與日誌；容器/映像/Volume/完整 prune 預設關閉，可能影響進行中專案";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail(kCliDockerBuilderPrune, "建置快取 (CLI)", 512LL * 1024 * 1024, true,
			"docker builder prune — 僅 BuildKit 建置快取",
			"相對安全；進行中 build 可能需重建");
		AddDetail(kCliDockerContainerPrune, "已停止容器 (CLI)", 256LL * 1024 * 1024, false,
			"docker container prune — 移除已停止容器",
			"容器內未備份資料將遺失",
			true);
		AddDetail(kCliDockerImagePrune, "Dangling 映像 (CLI)", 384LL * 1024 * 1024, false,
			"docker image prune — 未標記的 dangling 映像",
			"可能影響依賴這些映像的建置",
			true);
		AddDetail(kCliDockerVolumePrune, "未使用 Volume (CLI)", 512LL * 1024 * 1024, false,
			"docker volume prune — 未掛載的 Volume",
			"警告：常含資料庫與專案持久化資料！",
			true);
		AddDetail(kCliDockerSystemPruneAll, "完整 system prune (CLI)", 1024LL * 1024 * 1024, false,
			"docker system prune -a — 完整清理未使用資源",
			"極高風險：可能移除進行中專案所需的映像與網路",
			true);
		AddDetail("%LOCALAPPDATA%\\Docker\\log", "Desktop 日誌", 120LL * 1024 * 1024, true,
			"Docker Desktop 執行日誌",
			"可安全刪除");
		AddDetail("%LOCALAPPDATA%\\Docker\\tmp", "Desktop 暫存", 80LL * 1024 * 1024, true,
			"Docker Desktop 暫存檔",
			"可安全刪除");
		AddDetail("%LOCALAPPDATA%\\Docker", "整個 Desktop 資料", 2400LL * 1024 * 1024, false,
			"LocalAppData\\Docker 完整目錄",
			"極高風險：含 WSL 映像與容器資料，可能無法啟動 Docker",
			true);
		AddDetail("%PROGRAMDATA%\\Docker", "Docker ProgramData", 1200LL * 1024 * 1024, false,
			"系統層 Docker 設定與資料",
			"可能影響 Docker Desktop 正常運作",
			true);
		AddDetail("%USERPROFILE%\\.docker", "使用者 .docker", 496LL * 1024 * 1024, false,
			"Docker CLI 設定與 context",
			"可能遺失登入狀態與自訂設定",
			true);
	}
	bool Clean() override
	{
		EnsureDetails();
		bool any_cli_ok = false;
		for (size_t i = 0; i < detail_count_; ++i) {
			if (!details_[i].selected || details_[i].path == nullptr || !HCleanIsCliDetailPath(details_[i].path)) {
				continue;
			}
			if (RunDockerCliDetail(details_[i].path)) {
				any_cli_ok = true;
				HLOG_INFO("Docker CLI detail succeeded: {}", details_[i].path);
			}
		}
		const bool file_ok = CleanSelectedDetailsWithProgress(GetName());
		if (any_cli_ok && last_freed_bytes_ == 0) {
			last_freed_bytes_ = cached_bytes_ > 0 ? cached_bytes_ : 0;
			cached_bytes_ = 0;
		}
		return file_ok || any_cli_ok;
	}
};

class VsComponentModelCacheTask : public HCleanDetailListTask {
public:
	VsComponentModelCacheTask() { SetScanTarget(400LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_vs_component_cache"; }
	const char* GetName() const override { return "Visual Studio 快取"; }
	const char* GetPurpose() const override { return "清理 VS ComponentModelCache"; }
	const char* GetTooltip() const override { return "請先關閉 Visual Studio；僅清理 ComponentModelCache 等可重建快取"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		char vs_cmp[MAX_PATH * 4] = {};
		if (HCleanFindVsComponentModelCacheUtf8(vs_cmp, sizeof(vs_cmp))) {
			AddDetail(vs_cmp, "ComponentModelCache", 220LL * 1024 * 1024, true,
				"Visual Studio MEF 元件快取",
				"下次啟動 VS 可能略慢");
		}
		AddDetail("%LOCALAPPDATA%\\Microsoft\\VisualStudio\\Cache", "VS 共用 Cache", 120LL * 1024 * 1024, true,
			"Visual Studio 共用快取",
			"不影響專案與原始碼");
		AddDetail("%LOCALAPPDATA%\\Microsoft\\VisualStudio\\ServiceHub\\Cache", "ServiceHub Cache",
			80LL * 1024 * 1024, true,
			"VS 背景服務快取",
			"服務會自動重建");
		AddDetail("%TEMP%\\VSFeedbackIntelliCodeLogs", "IntelliCode 日誌", 60LL * 1024 * 1024, false,
			"IntelliCode 回饋日誌",
			"可安全刪除");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class GradleCacheTask : public HCleanDetailListTask {
public:
	GradleCacheTask() { SetScanTarget(2200LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_gradle_cache"; }
	const char* GetName() const override { return "Gradle 快取"; }
	const char* GetPurpose() const override { return "清理 Gradle 依賴與建置快取"; }
	const char* GetTooltip() const override
	{
		return "預設僅 daemon 日誌；caches 預設關閉，刪除會迫使重新下載依賴";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%USERPROFILE%\\.gradle\\caches", "Gradle caches", 1700LL * 1024 * 1024, false,
			"Gradle 套件與轉換快取",
			"警告：刪除後首次建置需重新下載，可能影響進行中專案",
			true);
		AddDetail("%USERPROFILE%\\.gradle\\daemon", "Gradle daemon logs", 300LL * 1024 * 1024, true,
			"Gradle 常駐程序日誌與暫存",
			"刪除後 daemon 會重新初始化");
		AddDetail("%USERPROFILE%\\.gradle\\wrapper\\dists", "Wrapper dists", 200LL * 1024 * 1024, false,
			"Gradle Wrapper 下載版本",
			"刪除後會重新下載對應 Gradle 版本",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class UnityDeveloperCacheTask : public HCleanDetailListTask {
public:
	UnityDeveloperCacheTask() { SetScanTarget(1800LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_unity_cache"; }
	const char* GetName() const override { return "Unity 開發快取"; }
	const char* GetPurpose() const override { return "清理 Unity 開發過程常見快取"; }
	const char* GetTooltip() const override { return "包含 ShaderCache/GI/Package Cache，首次開專案可能重建"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Unity\\cache\\packages", "Unity package cache", 900LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Unity\\cache\\shadercache", "Unity shader cache", 600LL * 1024 * 1024, true);
		AddDetail("%APPDATA%\\Unity\\Editor", "Unity Editor 暫存", 40LL * 1024 * 1024, false,
			"Unity 編輯器狀態與暫存",
			"不影響專案 Assets");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class WebBuildCacheTask : public HCleanDetailListTask {
public:
	WebBuildCacheTask() { SetScanTarget(900LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_web_build_cache"; }
	const char* GetName() const override { return "前端建置快取"; }
	const char* GetPurpose() const override { return "清理常見前端工具建置快取"; }
	const char* GetTooltip() const override { return "含 Next.js、Vite、Webpack 快取；下次 build 可能較慢"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Temp\\vite", "Vite temp", 280LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Temp\\webpack", "Webpack temp", 320LL * 1024 * 1024, true);
		AddDetail("%USERPROFILE%\\.next\\cache", "Next.js cache", 300LL * 1024 * 1024, false);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class AiModelCacheTask : public HCleanDetailListTask {
public:
	AiModelCacheTask() { SetScanTarget(3072LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_ai_model_cache"; }
	const char* GetName() const override { return "AI 模型快取"; }
	const char* GetPurpose() const override { return "清理本機 AI 模型與套件快取"; }
	const char* GetTooltip() const override { return "可能釋放大量空間；下次推理/訓練需重新下載"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%USERPROFILE%\\.cache\\huggingface\\hub", "HuggingFace hub", 2048LL * 1024 * 1024, false,
			"本機 AI 模型與 tokenizer 快取",
			"警告：刪除後推理/訓練需重新下載大型模型",
			true);
		AddDetail("%LOCALAPPDATA%\\pip\\Cache", "Python 套件 cache", 500LL * 1024 * 1024, true,
			"Python wheel 下載快取",
			"下次 pip install 可能重新下載");
		AddDetail("%USERPROFILE%\\.cache\\torch", "Torch cache", 524LL * 1024 * 1024, false,
			"PyTorch 預訓練權重快取",
			"刪除後需重新下載模型權重",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class BackendBuildCacheTask : public HCleanDetailListTask {
public:
	BackendBuildCacheTask() { SetScanTarget(1200LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_backend_build_cache"; }
	const char* GetName() const override { return "後端建置暫存"; }
	const char* GetPurpose() const override { return "清理後端常見建置與暫存輸出"; }
	const char* GetTooltip() const override { return "含 ASP.NET/IIS Express log 與暫存；不刪除原始碼"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%USERPROFILE%\\AppData\\Local\\Temp\\Temporary ASP.NET Files", "ASP.NET temp", 700LL * 1024 * 1024, true);
		AddDetail("%USERPROFILE%\\Documents\\IISExpress\\TraceLogFiles", "IISExpress logs", 280LL * 1024 * 1024, true);
		AddDetail("%LOCALAPPDATA%\\Temp\\dotnet", "dotnet temp", 220LL * 1024 * 1024, false);
		AddDetailIfExists("%USERPROFILE%\\.bash_history", "Bash 命令歷史", 4LL * 1024 * 1024, false,
			"Git Bash / WSL 命令紀錄",
			"刪除後無法回溯舊指令",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class CargoRegistryCacheTask : public HCleanDetailListTask {
public:
	CargoRegistryCacheTask() { SetScanTarget(1500LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_cargo_cache"; }
	const char* GetName() const override { return "Cargo 快取"; }
	const char* GetPurpose() const override { return "清理 Rust crate 下載快取"; }
	const char* GetTooltip() const override { return "下次 cargo build 會重新下載依賴"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%USERPROFILE%\\.cargo\\registry", "registry", 1100LL * 1024 * 1024, true,
			"crates.io 下載快取",
			"首次編譯可能變慢");
		AddDetail("%USERPROFILE%\\.cargo\\git", "git checkouts", 300LL * 1024 * 1024, false,
			"Git 依賴 checkout",
			"刪除後需重新 fetch",
			true);
		AddDetail("%USERPROFILE%\\.rustup\\tmp", "rustup tmp", 100LL * 1024 * 1024, false,
			"rustup 暫存",
			"可安全清理");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class GoBuildCacheTask : public HCleanDetailListTask {
public:
	GoBuildCacheTask() { SetScanTarget(800LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_go_build_cache"; }
	const char* GetName() const override { return "Go 建置快取"; }
	const char* GetPurpose() const override { return "清理 go build 與模組快取"; }
	const char* GetTooltip() const override { return "等同 go clean -cache 的常見目錄"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\go-build", "go-build", 520LL * 1024 * 1024, true,
			"Go 編譯快取",
			"下次建置需重新編譯");
		AddDetail("%USERPROFILE%\\go\\pkg\\mod\\cache", "module cache", 220LL * 1024 * 1024, true,
			"Go module 下載快取",
			"依賴會重新下載");
		AddDetail("%USERPROFILE%\\go\\pkg\\sumdb", "sumdb", 60LL * 1024 * 1024, false,
			"checksum 資料庫快取",
			"驗證時可能重新抓取");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class YarnCacheTask : public HCleanDetailListTask {
public:
	YarnCacheTask() { SetScanTarget(700LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_yarn_cache"; }
	const char* GetName() const override { return "Yarn 快取"; }
	const char* GetPurpose() const override { return "清理 Yarn 全域快取"; }
	const char* GetTooltip() const override { return "常見於 %LOCALAPPDATA%\\Yarn\\Cache"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\Yarn\\Cache", "Yarn Cache", 480LL * 1024 * 1024, true,
			"Yarn 套件快取",
			"yarn install 會重新下載");
		AddDetail("%APPDATA%\\npm-cache", "npm 共用快取", 120LL * 1024 * 1024, false,
			"Yarn 可能共用的 npm 快取",
			"與 npm 快取任務重疊時擇一即可");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

class PnpmStoreCacheTask : public HCleanDetailListTask {
public:
	PnpmStoreCacheTask()
	{
		SetScanTarget(1200LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "dev_pnpm_store"; }
	const char* GetName() const override { return "pnpm store"; }
	const char* GetPurpose() const override { return "清理 pnpm 下載快取（store 預設關閉）"; }
	const char* GetTooltip() const override
	{
		return "預設僅 pnpm cache；store prune 預設關閉，執行後各專案需重新 install";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%LOCALAPPDATA%\\pnpm\\cache", "pnpm cache", 200LL * 1024 * 1024, true,
			"pnpm 下載快取",
			"下載速度可能暫時變慢");
		AddDetail("%LOCALAPPDATA%\\pnpm\\store", "pnpm store", 900LL * 1024 * 1024, false,
			"pnpm 全域 content-addressable store",
			"警告：刪除/prune 後各專案需重新 pnpm install",
			true);
	}
	bool Clean() override
	{
		EnsureDetails();
		bool run_store_cli = false;
		bool run_cache_cli = false;
		for (size_t i = 0; i < detail_count_; ++i) {
			if (!details_[i].selected || details_[i].path == nullptr) {
				continue;
			}
			if (IsPnpmStoreDetailPath(details_[i].path)) {
				run_store_cli = true;
			}
			else if (IsPnpmCacheDetailPath(details_[i].path)) {
				run_cache_cli = true;
			}
		}
		if (run_store_cli && HCleanRunPnpmStorePrune()) {
			HLOG_INFO("pnpm store prune succeeded");
		}
		if (run_cache_cli && HCleanRunPnpmCacheClean()) {
			HLOG_INFO("pnpm cache clean succeeded");
		}
		return CleanSelectedDetailsWithProgress(GetName());
	}
};

class MavenRepositoryCacheTask : public HCleanDetailListTask {
public:
	MavenRepositoryCacheTask() { SetScanTarget(2048LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_maven_cache"; }
	const char* GetName() const override { return "Maven 本機倉庫"; }
	const char* GetPurpose() const override { return "清理 .m2/repository 快取"; }
	const char* GetTooltip() const override
	{
		return "預設僅 wrapper dists；repository 預設關閉，刪除會迫使所有專案重新下載依賴";
	}
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetail("%USERPROFILE%\\.m2\\wrapper\\dists", "wrapper dists", 200LL * 1024 * 1024, true,
			"Maven Wrapper 發行版",
			"刪除後會重新下載 wrapper");
		AddDetail("%USERPROFILE%\\.m2\\repository", "repository", 1800LL * 1024 * 1024, false,
			"Maven 完整本機倉庫",
			"警告：刪除後所有 Maven 專案需重新下載依賴",
			true);
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(GradleCacheTask, dev_game, 0)
REG_CLEAN_TASK(UnityDeveloperCacheTask, dev_game, 10)
REG_CLEAN_TASK(CargoRegistryCacheTask, dev_game, 20)

REG_CLEAN_TASK(NpmCacheTask, dev_web, 0)
REG_CLEAN_TASK(WebBuildCacheTask, dev_web, 10)
REG_CLEAN_TASK(YarnCacheTask, dev_web, 20)
REG_CLEAN_TASK(PnpmStoreCacheTask, dev_web, 30)

REG_CLEAN_TASK(PipCacheTask, dev_ai, 0)
REG_CLEAN_TASK(AiModelCacheTask, dev_ai, 10)

REG_CLEAN_TASK(NuGetCacheTask, dev_backend, 0)
REG_CLEAN_TASK(BackendBuildCacheTask, dev_backend, 10)
REG_CLEAN_TASK(GoBuildCacheTask, dev_backend, 20)
REG_CLEAN_TASK(MavenRepositoryCacheTask, dev_backend, 30)

REG_CLEAN_TASK(DockerCacheTask, dev_platform, 0)
REG_CLEAN_TASK(VsComponentModelCacheTask, dev_platform, 10)

class JetBrainsIdeCacheTask : public HCleanDetailListTask {
public:
	JetBrainsIdeCacheTask()
	{
		SetScanTarget(2048LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "dev_jetbrains_cache"; }
	const char* GetName() const override { return "JetBrains IDE"; }
	const char* GetPurpose() const override { return "清理 IntelliJ / PyCharm 等 IDE 快取"; }
	const char* GetTooltip() const override { return "動態探索各版本 IDE 的 caches / log / tmp"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateJetBrainsIdeCachePaths(paths, labels, kMaxDetails);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			const bool selected = i < 8;
			AddDetailPath(paths[i], labels[i], 120LL * 1024 * 1024, selected,
				"JetBrains IDE 快取或日誌",
				"下次索引或啟動可能略慢");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class EditorIdeCacheTask : public HCleanDetailListTask {
public:
	EditorIdeCacheTask() { SetScanTarget(1200LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_editor_ide_cache"; }
	const char* GetName() const override { return "VS Code / Cursor"; }
	const char* GetPurpose() const override { return "清理 VS Code 與 Cursor 快取"; }
	const char* GetTooltip() const override { return "Session Storage 預設關閉；可能需重新登入"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const struct { const char* root; const char* prefix; } k_roots[] = {
			{ "%APPDATA%\\Code", "VS Code" },
			{ "%APPDATA%\\Cursor", "Cursor" },
		};
		static const char* k_safe_subs[] = {
			"Cache", "CachedData", "Code Cache", "GPUCache", "logs", "Crashpad",
		};
		static const char* k_ext_cache[] = {
			"CachedExtensionVSIXs", "CachedExtensions", "CachedProfilesData",
		};
		static const char* k_destructive_subs[] = { "Session Storage", "Local Storage" };

		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};

		for (const auto& root : k_roots) {
			size_t count = HCleanEnumerateElectronAppCaches(root.root, root.prefix,
				paths, labels, kMaxDetails - detail_count_);
			for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
				AddDetailPath(paths[i], labels[i], 100LL * 1024 * 1024, true,
					"編輯器快取或日誌",
					"擴充功能可能略慢載入");
			}
			count = HCleanEnumerateSubdirsWithCache(root.root, k_ext_cache, 3,
				paths, labels, kMaxDetails - detail_count_, root.prefix);
			for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
				AddDetailPath(paths[i], labels[i], 80LL * 1024 * 1024, false,
					"擴充功能安裝快取",
					"擴充可能需重新解壓",
					true);
			}
			count = HCleanEnumerateSubdirsWithCache(root.root, k_destructive_subs, 2,
				paths, labels, kMaxDetails - detail_count_, root.prefix);
			for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
				AddDetailPath(paths[i], labels[i], 40LL * 1024 * 1024, false,
					"登入工作階段與本機儲存",
					"可能需重新登入帳號",
					true);
			}
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class PostmanCacheTask : public HCleanDetailListTask {
public:
	PostmanCacheTask() { SetScanTarget(480LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_postman_cache"; }
	const char* GetName() const override { return "Postman"; }
	const char* GetPurpose() const override { return "清理 Postman 快取與日誌"; }
	const char* GetTooltip() const override { return "不刪除 Collection 與環境變數"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		const size_t count = HCleanEnumerateElectronAppCaches("%APPDATA%\\Postman", "Postman",
			paths, labels, kMaxDetails);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			AddDetailPath(paths[i], labels[i], 80LL * 1024 * 1024, i < 3,
				"Postman Electron 快取或日誌",
				"API 回應快取會重建");
		}
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class AndroidStudioCacheTask : public HCleanDetailListTask {
public:
	AndroidStudioCacheTask()
	{
		SetScanTarget(2048LL * 1024 * 1024);
		SetWalkLimits(kHCleanLargeWalkLimits);
	}
	const char* GetId() const override { return "dev_android_studio_cache"; }
	const char* GetName() const override { return "Android Studio"; }
	const char* GetPurpose() const override { return "清理 Android Studio 與 Android SDK 快取"; }
	const char* GetTooltip() const override { return "caches 預設關閉；刪除後首次同步可能較慢"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		static const char* k_subs[] = { "caches", "log", "tmp" };
		char paths[kMaxDetails][MAX_PATH * 4] = {};
		char labels[kMaxDetails][128] = {};
		size_t count = HCleanEnumerateWildcardChildSubdirs(
			"%LOCALAPPDATA%\\Google", L"AndroidStudio*", k_subs, 3, "Android Studio",
			paths, labels, kMaxDetails - detail_count_);
		for (size_t i = 0; i < count && detail_count_ < kMaxDetails; ++i) {
			const bool destructive = strstr(labels[i], "caches") != nullptr;
			AddDetailPath(paths[i], labels[i], 150LL * 1024 * 1024, !destructive,
				"Android Studio 快取或日誌",
				destructive ? "警告：刪除後 IDE 需重建索引" : "可安全清理",
				destructive);
		}
		AddDetailIfExists("%USERPROFILE%\\.android\\cache", "Android SDK cache", 400LL * 1024 * 1024, false,
			"Android 模擬器與 SDK 快取",
			"模擬器首次啟動可能略慢",
			true);
	}
	bool Clean() override { return CleanSelectedDetailsWithProgress(GetName()); }
};

class GitHubDesktopCacheTask : public HCleanDetailListTask {
public:
	GitHubDesktopCacheTask() { SetScanTarget(320LL * 1024 * 1024); }
	const char* GetId() const override { return "dev_github_desktop_cache"; }
	const char* GetName() const override { return "GitHub Desktop"; }
	const char* GetPurpose() const override { return "清理 GitHub Desktop 快取"; }
	const char* GetTooltip() const override { return "不刪除本機 clone 的 repository"; }
	bool IsEnabledByDefault() const override { return false; }
	void BuildDetails() const override
	{
		AddDetailIfExists("%LOCALAPPDATA%\\GitHubDesktop\\Cache", "GitHub Desktop Cache", 180LL * 1024 * 1024, true,
			"UI 與更新快取",
			"不影響已 clone 的 repo");
		AddDetailIfExists("%LOCALAPPDATA%\\GitHubDesktop\\logs", "GitHub Desktop 日誌", 80LL * 1024 * 1024, true,
			"用戶端日誌",
			"可安全清理");
		AddDetailIfExists("%APPDATA%\\GitHub Desktop\\Cache", "Roaming Cache", 60LL * 1024 * 1024, false,
			"Roaming 快取",
			"不影響登入");
	}
	bool Clean() override { return CleanSelectedDetails(); }
};

REG_CLEAN_TASK(JetBrainsIdeCacheTask, dev_platform, 20)
REG_CLEAN_TASK(EditorIdeCacheTask, dev_platform, 30)
REG_CLEAN_TASK(PostmanCacheTask, dev_platform, 40)
REG_CLEAN_TASK(AndroidStudioCacheTask, dev_platform, 50)
REG_CLEAN_TASK(GitHubDesktopCacheTask, dev_platform, 60)
