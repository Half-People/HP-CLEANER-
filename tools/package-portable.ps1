param(
    [string]$Platform = "x64",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot ("$Platform\$Configuration")
$exePath = Join-Path $buildDir "HP_CLEANER++.exe"
$portableRoot = Join-Path $projectRoot "dist"
$portableDir = Join-Path $portableRoot ("HP_CLEANER++-portable-$Platform")

if (-not (Test-Path $exePath)) {
    throw "找不到輸出檔：$exePath，請先完成建置。"
}

if (Test-Path $portableDir) {
    Remove-Item -Path $portableDir -Recurse -Force
}

New-Item -ItemType Directory -Path $portableDir | Out-Null

Copy-Item -Path $exePath -Destination $portableDir
Copy-Item -Path (Join-Path $projectRoot "assets") -Destination $portableDir -Recurse
Copy-Item -Path (Join-Path $projectRoot "doc\portable-launch.md") -Destination $portableDir

Write-Host "Portable 輸出完成：$portableDir"
