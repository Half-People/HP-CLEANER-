param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [switch]$Rebuild,
    [switch]$UseAltBuildRoot
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$slnPath = Join-Path $projectRoot "HP_CLEANER++.sln"

if (-not (Test-Path $slnPath)) {
    throw "找不到方案：$slnPath"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "找不到 vswhere。請安裝 Visual Studio 2022 並勾選「使用 C++ 的桌面開發」工作負載。"
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild -or -not (Test-Path $msbuild)) {
    throw "找不到 MSBuild.exe。請在 Visual Studio Installer 中安裝 MSBuild 與 C++ 建置工具。"
}

Write-Host "MSBuild: $msbuild"
Write-Host "方案:    $slnPath"
Write-Host "設定:    $Configuration | $Platform"


$altRoot = $null
$intDir = $null
$outDir = $null
if ($UseAltBuildRoot) {
    $altRoot = Join-Path $env:USERPROFILE "hp_cleaner_build"
    $intDir = (Join-Path $altRoot "$Platform\$Configuration") + [IO.Path]::DirectorySeparatorChar
    $outDir = (Join-Path $altRoot "out\$Platform\$Configuration") + [IO.Path]::DirectorySeparatorChar
    New-Item -ItemType Directory -Force $intDir, $outDir | Out-Null
    Write-Host "AltBuildRoot: $altRoot"
}

$msbuildArgs = @(
    $slnPath
    "/m"
    "/nologo"
    "/v:m"
    "/p:Configuration=$Configuration"
    "/p:Platform=$Platform"
)

if ($UseAltBuildRoot -and $altRoot) {
    $msbuildArgs += "/p:IntDir=$intDir"
    $msbuildArgs += "/p:IntermediateOutputPath=$intDir"
    $msbuildArgs += "/p:OutDir=$outDir"
    $msbuildArgs += "/p:GenerateDebugInformation=false"
    $msbuildArgs += "/p:ProgramDataBaseFileName="
    $msbuildArgs += "/p:ResourceOutputFileName=$intDir" + "AppResources.res"
}

if ($Rebuild) {
    $msbuildArgs += "/t:Rebuild"
}

& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "建置失敗（結束碼 $LASTEXITCODE）。"
}


if ($UseAltBuildRoot -and $altRoot) {
    $built = Join-Path $outDir "HP_CLEANER++.exe"
    if (Test-Path $built) {
        $destDir = Join-Path $projectRoot "$Platform\$Configuration"
        New-Item -ItemType Directory -Force $destDir | Out-Null
        Copy-Item -Force $built (Join-Path $destDir "HP_CLEANER++.exe")
    }
}

$exePath = Join-Path $projectRoot "$Platform\$Configuration\HP_CLEANER++.exe"
Write-Host "建置成功：$exePath"
