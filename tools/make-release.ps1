# Makes the release packages of Hiragana IME for Windows from the build trees
# (tools\build.ps1 -Arch both):
#
#   HiraganaIME-for-Windows-<version>.zip   the installer package
#   hiragana-llm-model-<n>.zip              (-Model) the model for the LLM-assisted
#                                           conversion, which installer\get-llm.ps1 downloads
#
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1 [-Model]
#
# The packages go to %LOCALAPPDATA%\HiraganaIME-build\dist.

param(
    [string] $BuildDir = (Join-Path $env:LOCALAPPDATA 'HiraganaIME-build'),
    [switch] $Model,
    [string] $ModelDir = (Join-Path $env:LOCALAPPDATA 'HiraganaIME\llm'),
    [int] $ModelRevision = 1
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'installer\common.ps1')

$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(HiraganaIME VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$dist = Join-Path $BuildDir 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# The installer package
$name = "HiraganaIME-for-Windows-$version"
$package = Join-Path $dist $name
if (Test-Path $package) { Remove-Item $package -Recurse -Force }
New-Item -ItemType Directory -Force -Path $package | Out-Null
New-InstallImage -BuildDir $BuildDir -Root $root -Image (Join-Path $package 'files')
foreach ($f in 'install.ps1', 'uninstall.ps1', 'common.ps1', 'install.cmd', 'uninstall.cmd', 'README.txt') {
    Copy-Item (Join-Path $root "installer\$f") $package
}
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $package -DestinationPath $zip
Write-Host "$zip"
Write-Host "  SHA-256: $((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant())"

# The model
if ($Model) {
    $staging = Join-Path $dist 'llm-model'
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $staging | Out-Null
    $single = Join-Path $ModelDir 'model-single.onnx'
    if (Test-Path $single) {
        Copy-Item $single (Join-Path $staging 'model.onnx')
    } else {
        Copy-Item (Join-Path $ModelDir 'model.onnx') $staging
        $data = Join-Path $ModelDir 'model.onnx.data'
        if (Test-Path $data) { Copy-Item $data $staging }
    }
    Copy-Item (Join-Path $ModelDir 'vocab.txt') $staging
    Copy-Item (Join-Path $root 'installer\MODEL-README.txt') (Join-Path $staging 'README.txt')
    $modelZip = Join-Path $dist "hiragana-llm-model-$ModelRevision.zip"
    if (Test-Path $modelZip) { Remove-Item $modelZip -Force }
    # .NET's ZipFile handles files over 2 GB; Compress-Archive of Windows PowerShell may not.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($staging, $modelZip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    Remove-Item $staging -Recurse -Force
    Write-Host "$modelZip"
    Write-Host "  SHA-256: $((Get-FileHash $modelZip -Algorithm SHA256).Hash.ToLowerInvariant())"
}
