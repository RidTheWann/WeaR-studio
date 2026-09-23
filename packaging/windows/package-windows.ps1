param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [Parameter(Mandatory = $true)]
    [string]$FFmpegRoot,

    [string]$StageDir = ""
)

$ErrorActionPreference = "Stop"

$resolvedBuildDir = (Resolve-Path $BuildDir).Path
$releaseDir = Join-Path $resolvedBuildDir "bin\Release"

if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $StageDir = Join-Path $resolvedBuildDir "installer-stage"
}

$stage = [System.IO.Path]::GetFullPath($StageDir)
$exe = Join-Path $stage "WeaR-Studio.exe"

if (-not (Test-Path (Join-Path $releaseDir "WeaR-Studio.exe"))) {
    throw "Release executable was not found: $releaseDir"
}

if (-not (Test-Path $FFmpegRoot)) {
    throw "FFmpeg root was not found: $FFmpegRoot"
}

if (Test-Path $stage) {
    Remove-Item -Recurse -Force $stage
}
New-Item -ItemType Directory -Force $stage | Out-Null

Write-Host "Staging Release files..."
Copy-Item -Path (Join-Path $releaseDir "*") -Destination $stage -Recurse -Force

$windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    throw "windeployqt.exe was not found on PATH."
}

Write-Host "Deploying Qt runtime with windeployqt..."
& $windeployqt.Source --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw $exe

if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Write-Host "Bundling FFmpeg runtime DLLs..."
$ffmpegDlls = Get-ChildItem -Path (Join-Path $FFmpegRoot "bin") -Filter "*.dll" -File
if ($ffmpegDlls.Count -eq 0) {
    throw "No FFmpeg DLLs were found under $FFmpegRoot\bin."
}

foreach ($dll in $ffmpegDlls) {
    Copy-Item $dll.FullName (Join-Path $stage $dll.Name) -Force
}

$pluginDir = Join-Path $resolvedBuildDir "bin\plugins"
if (Test-Path $pluginDir) {
    $stagePluginDir = Join-Path $stage "plugins"
    New-Item -ItemType Directory -Force $stagePluginDir | Out-Null
    Copy-Item -Path (Join-Path $pluginDir "*") -Destination $stagePluginDir -Recurse -Force
}

$manifest = [ordered]@{
    application = "WeaR Studio"
    version = "0.1"
    executable = "WeaR-Studio.exe"
    qtDeployment = "windeployqt"
    ffmpegDllCount = $ffmpegDlls.Count
    packagedAtUtc = [DateTime]::UtcNow.ToString("o")
}

$manifest | ConvertTo-Json | Set-Content -Path (Join-Path $stage "package-manifest.json") -Encoding UTF8

Write-Host "Windows package staged at: $stage"
