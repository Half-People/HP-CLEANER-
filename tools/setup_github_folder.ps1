# Sync project sources into Github/ for GitHub publishing.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gh = Join-Path $root "Github"

$excludeDirs = @(
    ".vs", "x64", "HP_CLEANER++", "Github", "doc", "build", "out", ".git"
)

function Ensure-Dir([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Copy-Tree([string]$Src, [string]$Dst) {
    if (-not (Test-Path $Src)) { return }
    Ensure-Dir $Dst
    robocopy $Src $Dst /E /NFL /NDL /NJH /NJS /NC /NS /NP /XD $excludeDirs | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $Src -> $Dst" }
}

Write-Host "Target: $gh"
if (Test-Path $gh) {
    Write-Host "Removing existing Github folder..."
    Remove-Item -Recurse -Force $gh
}

# Top-level layout
$dirs = @(
    "cmake", "src/application", "src/core", "src/i18n", "src/clean", "src/optimize",
    "src/diskhealth", "src/filemap", "src/mainpage", "src/about", "src/history", "src/ui",
    "third_party/imgui", "third_party/implot", "third_party/json", "third_party/spdlog", "third_party/stb",
    "assets", "config/i18n", "tools", "docs"
)
foreach ($d in $dirs) { Ensure-Dir (Join-Path $gh $d) }

# Third-party vendored deps
Copy-Tree (Join-Path $root "imgui") (Join-Path $gh "third_party/imgui")
Copy-Tree (Join-Path $root "implot") (Join-Path $gh "third_party/implot")
Copy-Tree (Join-Path $root "api\json") (Join-Path $gh "third_party/json")
Copy-Tree (Join-Path $root "api\spdlog") (Join-Path $gh "third_party/spdlog")
Copy-Tree (Join-Path $root "api\stb") (Join-Path $gh "third_party/stb")

# Assets, i18n config, tools, docs
Copy-Tree (Join-Path $root "assets") (Join-Path $gh "assets")
Copy-Tree (Join-Path $root "i18n_samples") (Join-Path $gh "config/i18n")
Copy-Tree (Join-Path $root "tools") (Join-Path $gh "tools")
if (Test-Path (Join-Path $root "doc\porject.md")) {
    Copy-Item (Join-Path $root "doc\porject.md") (Join-Path $gh "docs\developer-notes.zh-TW.md")
}

# Resources at repo root (RC paths reference assets\)
Copy-Item (Join-Path $root "HP_CLEANER++.rc") (Join-Path $gh "HP_CLEANER++.rc")
Copy-Item (Join-Path $root "resource.h") (Join-Path $gh "resource.h")

# Source file routing
$map = @{
    "application" = @(
        "main.cpp", "HAppLaunch.cpp", "HAppLaunch.h", "HAppPaths.cpp", "HAppPaths.h",
        "HAppRegistration.cpp", "HAppRegistration.h", "HAppSettings.cpp", "HAppSettings.h",
        "HAppShell.cpp", "HAppShell.h", "HAppTray.cpp", "HAppTray.h",
        "HAppSingleInstance.cpp", "HAppSingleInstance.h", "HElevationBroker.cpp", "HElevationBroker.h"
    )
    "core" = @(
        "HPage.cpp", "HPage.h", "HPageStyle.h", "HLogCmdConsole.cpp", "HLogCmdConsole.h",
        "HLogPipe.cpp", "HLogPipe.h", "HLogRing.cpp", "HLogRing.h",
        "HCrashHandler.cpp", "HCrashHandler.h", "HCrashReportUI.cpp", "HCrashReportUI.h",
        "HAdminPrompt.cpp", "HAdminPrompt.h", "HUserConfig.cpp", "HUserConfig.h",
        "HRC_Assets.cpp", "HRC_Assets.h", "ConfirmDeleteDirectorys_PopupPage.cpp",
        "HCleanRegistry.cpp", "HCleanTask.cpp", "HCleanTask.h"
    )
    "i18n" = @(
        "Hi18n.cpp", "Hi18n.h", "Hi18nBuiltin.cpp", "Hi18nBuiltin.h",
        "Hi18nBuiltinPages.cpp", "Hi18nBuiltinPages.h", "Hi18nLangPicker.cpp", "Hi18nLangPicker.h"
    )
    "clean" = @(
        "CleanTasks_AppData.cpp", "CleanTasks_Advanced.cpp", "CleanTasks_Browser.cpp",
        "CleanTasks_Communication.cpp", "CleanTasks_Developer.cpp", "CleanTasks_Deep.cpp",
        "CleanTasks_Discovery.cpp", "CleanTasks_Game.cpp", "CleanTasks_Software.cpp",
        "CleanTasks_System.cpp", "CleanTasks_User.cpp",
        "HCleanTaskCommon.cpp", "HCleanTaskCommon.h", "ClearPage.cpp", "ClearPageUI.cpp", "ClearPageUI.h",
        "CleanHistory.cpp", "CleanHistory.h"
    )
    "optimize" = @(
        "OptimizePage.cpp", "OptimizePageUI.cpp", "OptimizePageUI.h",
        "OptimizeScan.cpp", "OptimizeScan.h", "OptimizeNetworkScan.cpp", "OptimizeNetworkScan.h",
        "OptimizeStartupIcon.cpp", "OptimizeStartupIcon.h"
    )
    "diskhealth" = @(
        "DiskHealthPage.cpp", "DiskHealthScan.cpp", "DiskHealthScan.h",
        "DiskHealthTest.cpp", "DiskHealthTest.h", "DiskHealthUI.cpp", "DiskHealthUI.h"
    )
    "filemap" = @(
        "FileMapPage.cpp", "FileMapScan.cpp", "FileMapScan.h",
        "FileMapTree.cpp", "FileMapTree.h", "FileMapUI.cpp", "FileMapUI.h"
    )
    "mainpage" = @(
        "MainPage.cpp", "MainPageUI.cpp", "MainPageUI.h",
        "MainPageDiskScan.cpp", "MainPageDiskScan.h", "MainPageMemory.cpp", "MainPageMemory.h"
    )
    "about" = @(
        "AboutPage.cpp", "AboutPageUI.cpp", "AboutPageUI.h",
        "AboutDeviceInfo.cpp", "AboutDeviceInfo.h", "AboutAppLog.cpp", "AboutAppLog.h"
    )
    "history" = @("HistoryPage.cpp")
    "ui" = @(
        "HUiHistoryList.cpp", "HUiHistoryList.h", "HUiPathBreadcrumb.cpp", "HUiPathBreadcrumb.h",
        "HUiTheme.h", "HUninstallUI.cpp", "HUninstallUI.h"
    )
}

$copied = 0
$missing = @()
foreach ($group in $map.Keys) {
    $dest = Join-Path $gh "src\$group"
    foreach ($file in $map[$group]) {
        $srcPath = Join-Path $root $file
        if (-not (Test-Path $srcPath)) {
            $missing += $file
            continue
        }
        Copy-Item $srcPath (Join-Path $dest $file)
        $copied++
    }
}

# Remove setup script copy from tools (keep local sync script reference only in dev tree)
$ghSetup = Join-Path $gh "tools\setup_github_folder.ps1"
if (Test-Path $ghSetup) { Remove-Item $ghSetup -Force }

Write-Host "Copied $copied source files into Github/src/"
if ($missing.Count -gt 0) {
    Write-Warning "Missing files: $($missing -join ', ')"
}
Write-Host "Done."
