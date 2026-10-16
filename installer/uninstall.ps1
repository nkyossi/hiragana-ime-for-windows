# Removes Hiragana IME for Windows. Run as administrator:
#
#   powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1 [-RemoveUserData]
#
# -RemoveUserData also deletes %APPDATA%\HiraganaIME (settings, user dictionary
# and input history) and %LOCALAPPDATA%\HiraganaIME (the files of the LLM) of
# the current user.

param(
    [switch] $RemoveUserData
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Assert-Administrator

Remove-FromLanguageList
Remove-StartMenuShortcuts
Invoke-Regsvr32 -Unregister
Write-Host 'Unregistered the text service.'
Stop-LlmServer

if (Test-Path $InstallDir) {
    # Applications may still have the DLL loaded; such files are moved aside
    # and deleted at the next restart.
    Get-ChildItem $InstallDir -Recurse -File | ForEach-Object { Remove-OrDefer $_.FullName }
    Get-ChildItem $InstallDir -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending |
        ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
    Remove-Item $InstallDir -Force -ErrorAction SilentlyContinue
}

if ($RemoveUserData) {
    foreach ($data in (Join-Path $env:APPDATA 'HiraganaIME'), (Join-Path $env:LOCALAPPDATA 'HiraganaIME')) {
        if (Test-Path $data) {
            Remove-Item $data -Recurse -Force
            Write-Host "Removed $data"
        }
    }
}

Write-Host 'Done. Restart Windows to remove files that were in use.'
