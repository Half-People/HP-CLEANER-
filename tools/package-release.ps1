param(
    [string]$Version = "1.0.0",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\$Configuration"
$exePath = Join-Path $buildDir "HP_CLEANER++.exe"
$i18nDir = Join-Path $buildDir "i18n"
$distRoot = Join-Path $projectRoot "dist"
$packageName = "HP_CLEANER++-$Version-win64"
$packageDir = Join-Path $distRoot $packageName
$zipPath = Join-Path $distRoot "$packageName.zip"

if (-not (Test-Path $exePath)) {
    throw "Missing build output: $exePath`nRun: cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64`n     cmake --build build --config $Configuration"
}

if (-not (Test-Path $i18nDir)) {
    throw "Missing i18n folder: $i18nDir (enable HP_CLEANER_COPY_RUNTIME_ASSETS and rebuild)"
}

if (Test-Path $packageDir) {
    Remove-Item -Path $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

Copy-Item -Path $exePath -Destination $packageDir
Copy-Item -Path $i18nDir -Destination (Join-Path $packageDir "i18n") -Recurse
Copy-Item -Path (Join-Path $projectRoot "docs\INSTALL.md") -Destination $packageDir
Copy-Item -Path (Join-Path $projectRoot "docs\INSTALL.zh-TW.md") -Destination $packageDir

if (Test-Path $zipPath) {
    Remove-Item -Path $zipPath -Force
}
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -Force

$hash = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLower()
$shaPath = "$zipPath.sha256"
Set-Content -Path $shaPath -Value "$hash  $(Split-Path -Leaf $zipPath)" -Encoding ascii -NoNewline

Write-Host "Package: $zipPath"
Write-Host "SHA256:  $shaPath"
Write-Host "Hash:    $hash"
