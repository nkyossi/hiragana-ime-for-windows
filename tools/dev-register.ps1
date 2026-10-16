# For development: makes the current user load the text service from the
# build directory instead of "C:\Program Files\HiraganaIME", without
# administrator rights. installer\install.ps1 must have been run once so that
# the TSF profile is registered.
#
#   powershell -ExecutionPolicy Bypass -File tools\dev-register.ps1             # use the x64 build
#   powershell -ExecutionPolicy Bypass -File tools\dev-register.ps1 -Unregister # back to the installed DLL
#
# COM looks up HKCU\Software\Classes before HKLM, so only this user's 64-bit
# processes are affected. Restart an application to make it load the other DLL.

param(
    [switch] $Unregister,
    [string] $BuildDir = (Join-Path $env:LOCALAPPDATA 'HiraganaIME-build')
)

$ErrorActionPreference = 'Stop'
$key = 'HKCU:\Software\Classes\CLSID\{1C841C5A-1337-4E4E-BC44-5B0EA1769018}'

if ($Unregister) {
    if (Test-Path $key) { Remove-Item $key -Recurse -Force }
    Write-Host 'Removed the per-user registration; the installed DLL is used again.'
    return
}

$dll = Join-Path $BuildDir 'x64\src\tsf\hiragana-ime.dll'
if (-not (Test-Path $dll)) { throw "$dll is missing. Build first." }
New-Item -Path "$key\InprocServer32" -Force | Out-Null
Set-ItemProperty -Path $key -Name '(default)' -Value 'Hiragana IME (development)'
Set-ItemProperty -Path "$key\InprocServer32" -Name '(default)' -Value $dll
Set-ItemProperty -Path "$key\InprocServer32" -Name 'ThreadingModel' -Value 'Apartment'
Write-Host "The current user now loads $dll"
