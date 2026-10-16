# Shared by install.ps1 and uninstall.ps1.

$InstallDir = Join-Path $env:ProgramFiles 'HiraganaIME'
$Clsid = '{1C841C5A-1337-4E4E-BC44-5B0EA1769018}'
$ProfileGuid = '{C27534CD-9A44-4271-BDDE-E9FC246A1DD5}'
$TipString = "0411:$Clsid$ProfileGuid"

Add-Type -Namespace HiraganaIME -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern bool MoveFileEx(string existing, string replacement, int flags);
'@

function Assert-Administrator {
    $principal = New-Object Security.Principal.WindowsPrincipal ([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script as administrator.'
    }
}

function Invoke-Regsvr32 {
    param([switch] $Unregister)
    $targets = @(
        @{ Exe = (Join-Path $env:SystemRoot 'System32\regsvr32.exe'); Dll = (Join-Path $InstallDir 'hiragana-ime.dll') },
        @{ Exe = (Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'); Dll = (Join-Path $InstallDir 'x86\hiragana-ime.dll') }
    )
    foreach ($t in $targets) {
        if (-not (Test-Path $t.Dll)) { continue }
        $arguments = @('/s')
        if ($Unregister) { $arguments += '/u' }
        $arguments += "`"$($t.Dll)`""
        $p = Start-Process -FilePath $t.Exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow
        if ($p.ExitCode -ne 0 -and -not $Unregister) {
            throw "regsvr32 failed for $($t.Dll) (exit code $($p.ExitCode))"
        }
    }
}

# hiragana-llm.exe holds the model; it is started again when needed.
function Stop-LlmServer {
    Get-Process -Name 'hiragana-llm' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

# Deletes a file, or moves it aside and deletes it at the next restart when it is in use.
function Remove-OrDefer([string] $path) {
    try {
        Remove-Item $path -Force -ErrorAction Stop
    } catch {
        $aside = "$path.$([DateTime]::Now.ToString('yyyyMMddHHmmss')).old"
        Move-Item $path $aside -Force
        [HiraganaIME.Native]::MoveFileEx($aside, $null, 4) | Out-Null  # MOVEFILE_DELAY_UNTIL_REBOOT
    }
}

# Copies a file, moving the old one aside when it is loaded by a running application.
function Copy-Replacing([string] $source, [string] $destination) {
    if (Test-Path $destination) { Remove-OrDefer $destination }
    Copy-Item $source $destination -Force
}

function Remove-StaleFiles {
    Get-ChildItem $InstallDir -Recurse -Filter '*.old' -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
}

function Add-ToLanguageList {
    $list = Get-WinUserLanguageList
    $ja = $list | Where-Object { $_.LanguageTag -eq 'ja' -or $_.LanguageTag -eq 'ja-JP' } | Select-Object -First 1
    if (-not $ja) {
        Write-Host 'Japanese is not in the language list; add the IME in Settings > Time & language > Language.'
        return
    }
    if ($ja.InputMethodTips -notcontains $TipString) {
        $ja.InputMethodTips.Add($TipString)
        Set-WinUserLanguageList $list -Force
        Write-Host 'Added Hiragana IME to the Japanese keyboards of the current user.'
    }
}

function Remove-FromLanguageList {
    $list = Get-WinUserLanguageList
    $changed = $false
    foreach ($lang in $list) {
        if ($lang.InputMethodTips -contains $TipString) {
            [void] $lang.InputMethodTips.Remove($TipString)
            $changed = $true
        }
    }
    if ($changed) {
        Set-WinUserLanguageList $list -Force
        Write-Host 'Removed Hiragana IME from the language list of the current user.'
    }
}

# Lets the text service read the settings and the user dictionary from apps
# running in an AppContainer (Store apps, the Start menu search box).
function Grant-UserDataAccess {
    $data = Join-Path $env:APPDATA 'HiraganaIME'
    New-Item -ItemType Directory -Force -Path $data | Out-Null
    & icacls.exe $data /grant '*S-1-15-2-1:(OI)(CI)RX' '*S-1-15-2-2:(OI)(CI)RX' /Q | Out-Null
}

$StartMenuDir = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Hiragana IME'

function New-Shortcut([string] $path, [string] $target, [string] $arguments = '') {
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $target
    if ($arguments) { $link.Arguments = $arguments }
    $link.WorkingDirectory = Split-Path -Parent $target
    $link.IconLocation = "$target,0"
    $link.Save()
}

# The display name of a program, e.g. "FuriganaPad" from its version resource.
function Get-DisplayName([string] $exe) {
    $name = (Get-Item $exe).VersionInfo.FileDescription
    if (-not $name) { $name = [System.IO.Path]::GetFileNameWithoutExtension($exe) }
    return $name
}

function Add-StartMenuShortcuts {
    New-Item -ItemType Directory -Force -Path $StartMenuDir | Out-Null
    $furiganapad = Join-Path $InstallDir 'furiganapad.exe'
    $setup = Join-Path $InstallDir 'hiragana-setup.exe'
    if (Test-Path $furiganapad) {
        New-Shortcut (Join-Path $StartMenuDir ((Get-DisplayName $furiganapad) + '.lnk')) $furiganapad
    }
    if (Test-Path $setup) {
        New-Shortcut (Join-Path $StartMenuDir 'Hiragana IME Settings.lnk') $setup
    }
    Write-Host "Added the Start menu shortcuts in $StartMenuDir"
}

function Remove-StartMenuShortcuts {
    if (Test-Path $StartMenuDir) {
        Remove-Item $StartMenuDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host 'Removed the Start menu shortcuts.'
    }
}

# Collects the files to install from the build trees (tools\build.ps1 -Arch
# both) and the source tree into $Image, laid out as in $InstallDir.
function New-InstallImage([string] $BuildDir, [string] $Root, [string] $Image) {
    $upstream = Join-Path $Root 'ibus-hiragana'
    $furiganapad_src = Join-Path $Root 'furiganapad'
    $x64 = Join-Path $BuildDir 'x64'
    $files = @{
        'hiragana-ime.dll'     = Join-Path $x64 'src\tsf\hiragana-ime.dll'
        'x86\hiragana-ime.dll' = Join-Path $BuildDir 'x86\src\tsf\hiragana-ime.dll'
        'hiragana-setup.exe'   = Join-Path $x64 'src\setup\hiragana-setup.exe'
        'furiganapad.exe'      = Join-Path $x64 'furiganapad.exe'
    }
    foreach ($f in $files.Values) {
        if (-not (Test-Path $f)) { throw "$f is missing. Build first: tools\build.ps1 -Arch both" }
    }
    if (Test-Path $Image) { Remove-Item $Image -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $Image 'x86'), (Join-Path $Image 'dic'), (Join-Path $Image 'layouts') | Out-Null
    foreach ($name in $files.Keys) { Copy-Item $files[$name] (Join-Path $Image $name) }

    Copy-Item (Join-Path $upstream 'dic\*.dic') (Join-Path $Image 'dic')
    Copy-Item (Join-Path $upstream 'layouts\*.json') (Join-Path $Image 'layouts')
    $docs = Join-Path $upstream 'docs'
    if (Test-Path $docs) { Copy-Item $docs $Image -Recurse }
    # FuriganaPad is LGPL-2.1-or-later; keep its license and notice beside the
    # Apache-2.0 files of the IME.
    $furiganapad_docs = Join-Path $x64 'furiganapad-docs'
    if (Test-Path $furiganapad_docs) { Copy-Item $furiganapad_docs $Image -Recurse }
    foreach ($pair in @(@{ From = (Join-Path $furiganapad_src 'COPYING'); To = 'furiganapad-COPYING.LGPL' },
                        @{ From = (Join-Path $furiganapad_src 'NOTICE'); To = 'furiganapad-NOTICE' })) {
        if (Test-Path $pair.From) { Copy-Item $pair.From (Join-Path $Image $pair.To) }
    }
    foreach ($f in 'LICENSE', 'NOTICE', 'COPYING') {
        $src = Join-Path $upstream $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $Image $f) }
    }
    Copy-Item (Join-Path $Root 'README.md') $Image -ErrorAction SilentlyContinue

    # The LLM-assisted conversion (64-bit only; built when ONNX Runtime was
    # found). The model and unidic-lite are not part of it: the setup program
    # gets them into %LOCALAPPDATA%\HiraganaIME\llm with get-llm.ps1.
    $tsf = Join-Path $x64 'src\tsf'
    if (Test-Path (Join-Path $tsf 'hiragana-llm.exe')) {
        Copy-Item (Join-Path $tsf 'hiragana-llm.exe'), (Join-Path $tsf 'onnxruntime.dll') $Image
        New-Item -ItemType Directory -Force -Path (Join-Path $Image 'data') | Out-Null
        Copy-Item (Join-Path $tsf 'data\*') (Join-Path $Image 'data')
        Copy-Item (Join-Path $Root 'installer\get-llm.ps1'), (Join-Path $Root 'tools\export-llm-model.py') $Image
        $ort = Get-ChildItem (Join-Path $BuildDir 'onnxruntime') -Recurse -Directory -Filter 'onnxruntime-win-x64-*' -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -Last 1
        if ($ort) {
            Copy-Item (Join-Path $ort.FullName 'LICENSE') (Join-Path $Image 'onnxruntime-LICENSE')
            Copy-Item (Join-Path $ort.FullName 'ThirdPartyNotices.txt') (Join-Path $Image 'onnxruntime-ThirdPartyNotices.txt')
        }
        $mecab = Join-Path $Root 'third_party\mecab-win\BSD'
        if (Test-Path $mecab) { Copy-Item $mecab (Join-Path $Image 'mecab-BSD') }
    }
}
