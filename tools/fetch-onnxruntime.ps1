# Downloads the ONNX Runtime C++ distribution used by the LLM-assisted
# conversion of HiraganaIME.
#
#   powershell -ExecutionPolicy Bypass -File tools\fetch-onnxruntime.ps1 [-Arch x64]
#
# The files go to %LOCALAPPDATA%\HiraganaIME-build\onnxruntime\<version>\<arch>;
# tools\build.ps1 picks them up automatically.  The runtime is not distributed
# with this repository (it is MIT licensed by Microsoft).

param(
    [ValidateSet('x64')] [string] $Arch = 'x64',
    [string] $Version = '1.31.0',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

$name = "onnxruntime-win-$Arch-$Version"
$base = Join-Path $env:LOCALAPPDATA "HiraganaIME-build\onnxruntime\$Version"
$target = Join-Path $base "$Arch\$name"

if ((Test-Path (Join-Path $target 'include\onnxruntime_cxx_api.h')) -and -not $Force) {
    Write-Host "already present: $target"
    exit 0
}

New-Item -ItemType Directory -Force -Path $base | Out-Null
$zip = Join-Path $base "$name.zip"
$url = "https://github.com/microsoft/onnxruntime/releases/download/v$Version/$name.zip"

Write-Host "downloading $url"
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 600
Expand-Archive -Path $zip -DestinationPath (Join-Path $base $Arch) -Force
Remove-Item $zip -Force

if (-not (Test-Path (Join-Path $target 'include\onnxruntime_cxx_api.h'))) {
    throw "the archive did not contain the expected layout; look under $base\$Arch"
}

Write-Host "installed: $target"
