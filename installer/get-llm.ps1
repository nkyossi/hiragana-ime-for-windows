# Gets the files of the LLM-assisted conversion of Hiragana IME into
# %LOCALAPPDATA%\HiraganaIME\llm:
#
#   model.onnx (+ model.onnx.data)  cl-tohoku/bert-base-japanese-v3 as ONNX (about 450 MB)
#   vocab.txt                        its vocabulary
#   unidic\                          the unidic-lite dictionary for MeCab (about 250 MB)
#
#   powershell -ExecutionPolicy Bypass -File get-llm.ps1 [-ModelUrl <zip>] [-ModelDir <dir>] [-Export]
#
# unidic-lite comes from PyPI. The model comes from the release of the
# repository by default, from -ModelDir (a directory with model.onnx or
# model-single.onnx and vocab.txt), from -ModelUrl (a zip of them), or with
# -Export it is converted from the Hugging Face Hub with Python (torch and
# transformers are installed into a private virtual environment).
# Administrator rights are not needed.

param(
    [string] $ModelUrl = '',
    [string] $ModelDir = '',
    [switch] $Export
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# The model is a release asset of the repository (tools\make-release.ps1 -Model).
$DefaultModelUrl = 'https://github.com/nkyossi/hiragana-ime-for-windows/releases/download/llm-model-1/hiragana-llm-model-1.zip'
$DefaultModelSha256 = '234630b816c4be24309a7b81db62de6937ad8e05e73b907f8557b3a030962c19'
$UnidicUrl = 'https://files.pythonhosted.org/packages/55/2b/8cf7514cb57d028abcef625afa847d60ff1ffbf0049c36b78faa7c35046f/unidic-lite-1.0.8.tar.gz'
$UnidicSha256 = 'db9d4572d9fdd4d00a97949d4b0741ec480ee05a7e7e2e32f547500dae27b245'

$target = Join-Path $env:LOCALAPPDATA 'HiraganaIME\llm'
$work = Join-Path $env:LOCALAPPDATA 'HiraganaIME\llm-download'
New-Item -ItemType Directory -Force -Path $target, $work | Out-Null

function Done-Pause {
    if ($Host.Name -eq 'ConsoleHost' -and [Environment]::UserInteractive) {
        Write-Host ''
        Read-Host 'Enter キーで閉じます' | Out-Null
    }
}

function Install-Model([string] $dir) {
    $single = Join-Path $dir 'model-single.onnx'
    $model = Join-Path $dir 'model.onnx'
    if (Test-Path $single) {
        Copy-Item $single (Join-Path $target 'model.onnx') -Force
        Remove-Item (Join-Path $target 'model.onnx.data') -ErrorAction SilentlyContinue
    } elseif (Test-Path $model) {
        Copy-Item $model (Join-Path $target 'model.onnx') -Force
        $data = Join-Path $dir 'model.onnx.data'
        if (Test-Path $data) { Copy-Item $data $target -Force }
    } else {
        throw "$dir に model.onnx がありません。"
    }
    $vocab = Join-Path $dir 'vocab.txt'
    if (-not (Test-Path $vocab)) { throw "$dir に vocab.txt がありません。" }
    Copy-Item $vocab $target -Force
}

function Export-Model {
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $python) { throw 'Python が見つかりません。https://www.python.org/ から入れてください。' }
    $venv = Join-Path $work 'venv'
    if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) {
        Write-Host 'Python の仮想環境を作っています...'
        & $python -m venv $venv
    }
    $vpy = Join-Path $venv 'Scripts\python.exe'
    Write-Host 'torch と transformers を入れています (数GBあります)...'
    & $vpy -m pip install --quiet --upgrade pip
    & $vpy -m pip install --quiet torch --index-url https://download.pytorch.org/whl/cpu
    & $vpy -m pip install --quiet transformers fugashi unidic-lite onnx onnxruntime numpy
    if ($LASTEXITCODE -ne 0) { throw 'パッケージを入れられませんでした。' }
    $script = Join-Path $PSScriptRoot 'export-llm-model.py'
    if (-not (Test-Path $script)) { $script = Join-Path $PSScriptRoot '..\tools\export-llm-model.py' }
    $out = Join-Path $work 'model'
    Write-Host 'モデルを ONNX に変換しています...'
    & $vpy -I $script $out
    if ($LASTEXITCODE -ne 0) { throw 'モデルを変換できませんでした。' }
    Install-Model $out
}

try {
    # unidic-lite
    if (-not (Test-Path (Join-Path $target 'unidic\sys.dic'))) {
        Write-Host 'unidic-lite を PyPI から入手しています (約 47 MB)...'
        $tgz = Join-Path $work 'unidic-lite.tar.gz'
        Invoke-WebRequest -Uri $UnidicUrl -OutFile $tgz -UseBasicParsing -TimeoutSec 1200
        $hash = (Get-FileHash $tgz -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $UnidicSha256) { throw "unidic-lite のハッシュが一致しません: $hash" }
        $extract = Join-Path $work 'unidic'
        if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $extract | Out-Null
        & tar.exe -xzf $tgz -C $extract 'unidic-lite-1.0.8/unidic_lite/dicdir'
        if ($LASTEXITCODE -ne 0) { throw 'unidic-lite を展開できませんでした。' }
        $dic = Join-Path $target 'unidic'
        if (Test-Path $dic) { Remove-Item $dic -Recurse -Force }
        Move-Item (Join-Path $extract 'unidic-lite-1.0.8\unidic_lite\dicdir') $dic
        Remove-Item $tgz, $extract -Recurse -Force
    }
    Write-Host 'unidic-lite: OK'

    # the model
    $have = (Test-Path (Join-Path $target 'model.onnx')) -and (Test-Path (Join-Path $target 'vocab.txt'))
    if ($ModelDir) {
        Install-Model $ModelDir
    } elseif ($Export) {
        Export-Model
    } elseif ($have -and -not $ModelUrl) {
        Write-Host 'モデルはすでにあります。'
    } else {
        if (-not $ModelUrl) { $ModelUrl = $DefaultModelUrl }
        Write-Host "モデルを入手しています (約 410 MB): $ModelUrl"
        $zip = Join-Path $work 'model.zip'
        Invoke-WebRequest -Uri $ModelUrl -OutFile $zip -UseBasicParsing -TimeoutSec 3600
        if ($ModelUrl -eq $DefaultModelUrl) {
            $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($hash -ne $DefaultModelSha256) { throw "モデルのハッシュが一致しません: $hash" }
        }
        $out = Join-Path $work 'model'
        if (Test-Path $out) { Remove-Item $out -Recurse -Force }
        Expand-Archive $zip -DestinationPath $out -Force
        Install-Model $out
        Remove-Item $zip, $out -Recurse -Force
    }
    Write-Host 'モデル: OK'
    if (Test-Path $work) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Host ''
    Write-Host "LLM のファイルを $target に用意しました。"
    Write-Host '設定画面の「変換候補の選択にLLMを利用する」をオンにすると使えます。'
} catch {
    Write-Host ''
    Write-Host "エラー: $($_.Exception.Message)" -ForegroundColor Red
    Done-Pause
    exit 1
}
Done-Pause
