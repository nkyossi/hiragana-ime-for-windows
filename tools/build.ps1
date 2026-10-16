# Builds Hiragana IME for Windows with Visual Studio 2022 (C++ workload),
# using the CMake and Ninja that come with Visual Studio.
#
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Arch x64|x86|both] [-Config Release|Debug] [-Test]
#
# Build trees go to %LOCALAPPDATA%\HiraganaIME-build\<arch> so that they are
# not synced by OneDrive.

param(
    [ValidateSet('x64', 'x86', 'both')] [string] $Arch = 'both',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')] [string] $Config = 'Release',
    [switch] $Test
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ tools was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

$archs = if ($Arch -eq 'both') { @('x64', 'x86') } else { @($Arch) }

# The ONNX Runtime C++ distribution and the exported model are optional; the
# LLM-assisted conversion is built only when both are present.
function Find-Ort([string]$arch) {
    $base = Join-Path $env:LOCALAPPDATA 'HiraganaIME-build\onnxruntime'
    if (-not (Test-Path $base)) { return $null }
    $dir = Get-ChildItem -Path $base -Recurse -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "onnxruntime-win-$arch-*" } |
        Where-Object { Test-Path (Join-Path $_.FullName 'include\onnxruntime_cxx_api.h') } |
        Sort-Object FullName | Select-Object -Last 1
    if ($dir) { return $dir.FullName }
    return $null
}
$model = Join-Path $env:LOCALAPPDATA 'HiraganaIME-build\llm\model-single.onnx'
if (-not (Test-Path $model)) { $model = '' }

# unidic-lite's dicdir (250 MB) is only needed at run time by the MeCab
# tokenizer, so it is not part of the repository.  Point HIRAGANA_MECAB_DIC_DIR
# at a copy, or let this look for one.
function Find-MecabDict {
    if ($env:HIRAGANA_MECAB_DIC_DIR -and (Test-Path $env:HIRAGANA_MECAB_DIC_DIR)) {
        return $env:HIRAGANA_MECAB_DIC_DIR
    }
    $fixed = Join-Path $env:LOCALAPPDATA 'HiraganaIME-build\llm\dic'
    if (Test-Path (Join-Path $fixed 'sys.dic')) { return $fixed }
    $py = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Programs\Python') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'Lib\site-packages\unidic_lite\dicdir' } |
        Where-Object { Test-Path (Join-Path $_ 'sys.dic') } |
        Select-Object -First 1
    if ($py) { return $py }
    return ''
}

foreach ($a in $archs) {
    $build = Join-Path $env:LOCALAPPDATA "HiraganaIME-build\$a"
    $vcarch = if ($a -eq 'x64') { 'x64' } else { 'x64_x86' }
    $tests = if ($a -eq 'x64') { 'ON' } else { 'OFF' }
    $ort = Find-Ort $a
    $dic = Find-MecabDict
    $llm = "-DHIRAGANA_ONNXRUNTIME_DIR=`"$ort`" -DHIRAGANA_LLM_MODEL=`"$model`" -DHIRAGANA_MECAB_DIC_DIR=`"$dic`""
    Write-Host "== $a ($Config) -> $build"
    $cmd = "call `"$vcvars`" $vcarch >nul && " +
           "`"$cmake`" -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" " +
           "-DCMAKE_BUILD_TYPE=$Config -DHIRAGANA_BUILD_TESTS=$tests $llm && " +
           "`"$cmake`" --build `"$build`""
    if ($Test -and $a -eq 'x64') {
        $cmd += " && cd /d `"$build`" && `"$ctest`" --output-on-failure"
    }
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "build failed for $a" }
}
