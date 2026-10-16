# Installs Hiragana IME for Windows into "C:\Program Files\HiraganaIME" and
# registers the text service (64-bit and 32-bit). Run as administrator:
#
#   powershell -ExecutionPolicy Bypass -File install.ps1
#
# In a release package the files to install are in the "files" folder next to
# this script. In the source tree they are collected from the build trees.
#
# Options:
#   -BuildDir <dir>       where tools\build.ps1 put the binaries (source tree only)
#                         (default: %LOCALAPPDATA%\HiraganaIME-build)
#   -NoLanguageList       do not add the IME to the Japanese keyboard list of the current user

param(
    [string] $BuildDir = (Join-Path $env:LOCALAPPDATA 'HiraganaIME-build'),
    [switch] $NoLanguageList
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Assert-Administrator
# Files from a downloaded zip carry the mark of the web.
Get-ChildItem $PSScriptRoot -Recurse -File | Unblock-File -ErrorAction SilentlyContinue

$image = Join-Path $PSScriptRoot 'files'
$staged = $null
if (-not (Test-Path $image)) {
    $staged = Join-Path $env:TEMP "HiraganaIME-install-$PID"
    New-InstallImage -BuildDir $BuildDir -Root (Split-Path -Parent $PSScriptRoot) -Image $staged
    $image = $staged
}

try {
    Write-Host "Installing to $InstallDir"
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Remove-StaleFiles
    Stop-LlmServer

    # Unregister the previous version, if any, before replacing the files.
    Invoke-Regsvr32 -Unregister

    $base = (Resolve-Path $image).Path.TrimEnd('\')
    foreach ($file in Get-ChildItem $base -Recurse -File) {
        $destination = Join-Path $InstallDir $file.FullName.Substring($base.Length + 1)
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Replacing $file.FullName $destination
    }
} finally {
    if ($staged) { Remove-Item $staged -Recurse -Force -ErrorAction SilentlyContinue }
}

Invoke-Regsvr32
Write-Host 'Registered the text service.'
Grant-UserDataAccess

if (-not $NoLanguageList) {
    Add-ToLanguageList
}

Add-StartMenuShortcuts

Write-Host ''
Write-Host 'Done. Switch to "Hiragana IME" with Win+Space.'
Write-Host 'Settings: "C:\Program Files\HiraganaIME\hiragana-setup.exe" or right-click the input mode indicator in the taskbar.'
Write-Host 'FuriganaPad: "C:\Program Files\HiraganaIME\furiganapad.exe" or the Start menu.'
