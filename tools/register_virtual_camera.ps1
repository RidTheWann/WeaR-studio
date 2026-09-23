param(
    [ValidateSet("Install", "Uninstall")]
    [string]$Action = "Install",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

if (-not $IsWindows) {
    throw "This script requires Windows."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$dllSource = Join-Path $repoRoot "build\bin\virtualcamera\WeaRVirtualCamera.dll"
$installDir = Join-Path ${env:ProgramFiles} "WeaR Studio"
$dllTarget = Join-Path $installDir "WeaRVirtualCamera.dll"
$regsvr = Join-Path ${env:windir} "System32\regsvr32.exe"

if ($Action -eq "Install") {
    if (-not (Test-Path $dllSource)) {
        throw "Virtual camera DLL was not found: $dllSource. Build the project in $Configuration first."
    }

    New-Item -ItemType Directory -Force $installDir | Out-Null
    Copy-Item $dllSource $dllTarget -Force

    & $regsvr "/s" $dllTarget
    if ($LASTEXITCODE -ne 0) {
        throw "regsvr32 failed with exit code $LASTEXITCODE"
    }

    Write-Host "Installed WeaR virtual camera media source: $dllTarget"
    Write-Host "Start WeaR Studio and use Controls -> Start Virtual Camera."
}
else {
    if (Test-Path $dllTarget) {
        & $regsvr "/s" "/u" $dllTarget
        if ($LASTEXITCODE -ne 0) {
            throw "regsvr32 unregister failed with exit code $LASTEXITCODE"
        }

        Remove-Item $dllTarget -Force
    }

    Write-Host "Unregistered WeaR virtual camera media source."
}
