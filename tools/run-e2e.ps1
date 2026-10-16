# Runs the end-to-end tests (tests/tsf_test.cpp) with the text service of the
# build directory: a RichEdit control (TSF), an EDIT control (IMM32) and
# FuriganaPad. They type with SendInput; do not touch the keyboard or the
# mouse while they run.
#
#   powershell -ExecutionPolicy Bypass -File tools\run-e2e.ps1 [-Modes richedit,edit,furiganapad] [-Llm]
#
# The settings and the input history of the current user are saved first and
# restored afterwards; the tests use the romaji layout. -Llm turns use-llm on,
# so that the text service starts hiragana-llm.exe of the build directory (the
# model must be in %LOCALAPPDATA%\HiraganaIME\llm).

param(
    [string[]] $Modes = @('richedit', 'edit', 'furiganapad'),
    [string] $BuildDir = (Join-Path $env:LOCALAPPDATA 'HiraganaIME-build\x64'),
    [switch] $Llm
)

$ErrorActionPreference = 'Stop'
$data = Join-Path $env:APPDATA 'HiraganaIME'
$backup = Join-Path $env:TEMP 'HiraganaIME-e2e-backup'
$test = Join-Path $BuildDir 'tsf_test.exe'

& (Join-Path $PSScriptRoot 'dev-register.ps1') -BuildDir (Split-Path $BuildDir -Parent)

if (Test-Path $backup) { Remove-Item $backup -Recurse -Force }
New-Item -ItemType Directory -Force -Path $backup, $data | Out-Null
foreach ($item in 'settings.json', 'dic') {
    $path = Join-Path $data $item
    if (Test-Path $path) { Copy-Item $path $backup -Recurse }
}

$failed = 0
try {
    $settings = @{}
    $settingsPath = Join-Path $data 'settings.json'
    if (Test-Path $settingsPath) {
        $json = Get-Content $settingsPath -Raw | ConvertFrom-Json
        foreach ($p in $json.PSObject.Properties) { $settings[$p.Name] = $p.Value }
    }
    $settings['layout'] = 'roomazi'
    $settings['keyboard'] = 'auto'
    $settings['use-llm'] = [bool] $Llm
    if ($env:HIRAGANA_E2E_DEBUG) { $settings['logging-level'] = 'DEBUG' }
    ($settings | ConvertTo-Json) | Set-Content -Encoding ascii $settingsPath
    if ($Llm) {
        # Start the server first so that every conversion of the tests uses
        # the model; it takes a second or two to load it.
        $env:HIRAGANA_E2E_LLM = '1'
        Start-Process (Join-Path $BuildDir 'src\tsf\hiragana-llm.exe') | Out-Null
        Start-Sleep -Seconds 5
    } else {
        $env:HIRAGANA_E2E_LLM = ''
    }

    foreach ($mode in $Modes) {
        $out = Join-Path $env:TEMP "tsf_test_$mode.txt"
        $arguments = @($mode)
        if ($mode -eq 'furiganapad') { $arguments += "`"$(Join-Path $BuildDir 'furiganapad.exe')`"" }
        # Not -Wait: it also waits for the descendants, and the text service in
        # the test may start hiragana-llm.exe, which keeps running.
        $p = Start-Process $test -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput $out
        $null = $p.Handle  # keeps the exit code available (Windows PowerShell 5.1)
        $p.WaitForExit()
        Get-Content $out -Encoding utf8 | Where-Object { $_ -match '^(PASS|FAIL)|failure' }
        if ($p.ExitCode -ne 0) { $failed += 1 }
    }
} finally {
    Start-Sleep -Milliseconds 500
    foreach ($item in 'settings.json', 'dic') {
        $path = Join-Path $data $item
        if (Test-Path $path) { Remove-Item $path -Recurse -Force }
        $saved = Join-Path $backup $item
        if (Test-Path $saved) { Copy-Item $saved $data -Recurse }
    }
    # The server of the build directory, if the tests started it
    Get-Process -Name 'hiragana-llm' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -like "$BuildDir\*" } | Stop-Process -Force -ErrorAction SilentlyContinue
    & (Join-Path $PSScriptRoot 'dev-register.ps1') -Unregister
}
if ($failed) { throw "$failed test program(s) failed" }
