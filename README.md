# ひらがなIME for Windows

> **非公式（unofficial）**
>
> このプロジェクト（リポジトリ名 `hiragana-ime-for-windows`）は、[ibus-hiragana](https://github.com/esrille/ibus-hiragana) と [FuriganaPad](https://github.com/esrille/furiganapad) を Windows 向けに移植したものです。どちらも Esrille 株式会社がつくったものです。
>
> このプロジェクトは、Esrille 株式会社とは関係がありません。不具合をみつけたときは、本家ではなく、この移植版のリポジトリに報告してください。Esrille のロゴはつかっていません。

[ibus-hiragana](https://github.com/esrille/ibus-hiragana) は、Linux と IBus 向けの日本語入力システムです。このプロジェクトでは、それを Windows の Text Services Framework（TSF）に移植しました。

ふつうのIMEでは、文字を入力してから漢字に変換し、確定します。このIMEでは、**すでに入力した文字をあとから漢字に変換する**ことができます。カーソルの前にある文字を変換するので、入力と変換をわけてかんがえられます。このような入力方法を「モードレス」とよびます。

このリポジトリには、[FuriganaPad](https://github.com/esrille/furiganapad) の Windows 版も入っています。これを「ふりがなパッド」とよびます。

ふりがなパッドでは、ひらがなIMEで漢字に変換すると、もとのよみをルビとして自動でつけます。Markdown（`.md`）形式の文書をよみかきすることもできます。

## インストール

1. [Releases](https://github.com/nkyossi/hiragana-ime-for-windows/releases) から `HiraganaIME-for-Windows-<版>.zip` をダウンロードし、展開します。
2. `install.cmd` をダブルクリックします。管理者として実行するかどうかをたずねられます。
3. Win+スペースを押して、「ひらがなIME」にきりかえます。すでにひらいているアプリでは、新しいIMEをよみこむために、アプリをひらきなおしてください。

アンインストールするときは、`uninstall.cmd` を実行してください。

対応するOSは、Windows 10（バージョン1903以降）と Windows 11 です。いずれも64bit版に対応しています。32bit版のアプリでもつかえます。

なお、プログラムには電子署名をつけていません。

## 使い方

| キー | はたらき |
|:--|:--|
| 半角/全角、英数、Caps Lock（USキーボード） | 英数（A）とひらがな（あ）を切り替える |
| 無変換 / Shift+無変換 / Ctrl+無変換 | 英数（A）/ 全角英数（Ａ）/ あ→ア→ｱ を切り替える |
| ひらがな / Shift+ひらがな | ひらがな（あ）/ カタカナ（ア）を切り替える |
| スペース（ローマ字入力） | カーソルの前にあるひらがなを漢字に変換する |
| Shift+スペース | 動詞・形容詞として変換する。続けて送りがなを入力する |
| ↓ ↑ / PageDown PageUp | 変換候補をえらぶ |
| Tab / Shift+Tab | 変換する範囲をせまくする / 広くする |
| Enter | 変換を確定する |
| Esc | 変換を取りやめて、読みにもどす |
| 右Shift | カーソルの前にあるひらがなをカタカナにする。Enterを押すと辞書に登録する |

変換候補のウィンドウでは、マウスをつかって候補をえらぶこともできます。

タスクバーにある「A / あ」の表示を右クリックすると、メニューが出ます。ここから入力モードのきりかえ、設定、ユーザー辞書の編集、ヘルプの表示ができます。

くわしいつかいかたは、ibus-hiragana のヘルプ（インストール先の `docs\index.html`）をよんでください。ふりがなパッドのつかいかたは、`furiganapad-docs\index.html` にあります。

## 設定

設定は、次のプログラムから変更できます。

`C:\Program Files\HiraganaIME\hiragana-setup.exe`

タスクバーの右クリックメニューから「設定」をえらんでも、おなじ画面をひらけます。

設定できる項目は、次のとおりです。

- **入力方式**：ローマ字入力 / かな入力（JIS配列）/ かな入力（ニュースティックニー配列）
- **キーボードの種類**：自動 / 日本語（106/109）/ 英語（101/104）/ Dvorak
- **漢字辞書**：小学校1年生からおとなまで。学年別漢字配当表にもとづく辞書をえらべます。
- **個人用辞書**：`%APPDATA%\HiraganaIME\my.dic`
- **おくりがなの許容範囲**
- **入力履歴の消去**
- **算用数字を半角で入力するかどうか**
- **英数モードで `^` と `~` を結合するかどうか**
- **`nn` のあつかい**
- **起動時の入力モード**
- **大規模言語モデル（LLM）による変換候補のえらび方**。くわしくは、下の「LLM による候補えらび」を読んでください。

設定、個人用辞書、入力履歴は、`%APPDATA%\HiraganaIME` に保存されます。

設定を保存すると、ひらいているアプリにも自動で反映されます。

## ビルド

ビルドには、次のものが必要です。

- Visual Studio 2022（「C++ によるデスクトップ開発」をえらんでください）
- Python 3（テストにつかいます）

元のソースである `ibus-hiragana` と `furiganapad` は、Git のサブモジュールとしてとりこんでいます。辞書、キー配列、ヘルプの文書は、これらのソースからつかいます。

PowerShell で、次のコマンドを実行してください。

```powershell
git clone --recursive https://github.com/nkyossi/hiragana-ime-for-windows.git
cd hiragana-ime-for-windows
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Arch both -Test
```

ビルドしたファイルは、次の場所にできます。

- `%LOCALAPPDATA%\HiraganaIME-build\x64`
- `%LOCALAPPDATA%\HiraganaIME-build\x86`

OneDrive による同期をさけるため、ビルド先にはローカルのアプリデータ用フォルダをつかいます。

主なファイルは、次のとおりです。

- `hiragana-ime.dll`：TSF のテキストサービス
- `hiragana-setup.exe`：設定プログラム
- `furiganapad.exe`：ふりがなパッド
- `furiganapad-docs\`：ふりがなパッドのヘルプ

### テスト

主なテストは、次のとおりです。

- `core_tests`：辞書、学習結果の保存、複数のプロセスでの入力履歴の統合などをしらべます。辞書のテストは、`tests/test_dictionary.py` を移植したものです。
- `golden_*`：元の Python 版 `Dictionary` と C++ 版で、辞書にあるすべてのよみについて変換候補を比べます。
- `engine_golden`：元の Python 版エンジンと C++ 版エンジンに、同じキー操作をあたえます。そして、文書、未確定文字、候補ウィンドウの状態を、キーを1つ押すたびにくらべます。

元の Python 版エンジンは、`tests/golden/fake_gi.py` をつかって IBus のはたらきをまねた環境でうごかします。

実際のキー入力をしらべるテストでは、開発用にIMEを一時的に登録してから実行します。このテストでは、設定と入力履歴をいったん退避し、テストのあとでもとにもどします。

次のコマンドで実行できます。

```powershell
powershell -ExecutionPolicy Bypass -File tools\run-e2e.ps1
```

### ソースからインストールする / アンインストールする

ビルドしたものをインストールするときは、管理者としてひらいた PowerShell で、次のコマンドを実行してください。

```powershell
powershell -ExecutionPolicy Bypass -File installer\install.ps1
```

アンインストールするときは、次のコマンドを実行してください。

```powershell
powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1
```

`-RemoveUserData` をつけると、設定と辞書もけします。

配布用の ZIP ファイルは、`tools\make-release.ps1` でつくれます。`-Model` をつけると、LLM のモデルを入れた ZIP ファイルもつくれます。

インストールすると、現在のユーザーの「日本語」のキーボードに「ひらがなIME」が追加されます。また、スタートメニューに「ふりがなパッド」と「Hiragana IME Settings」のショートカットができます。

IMEをつかうときは、Win+スペースで「ひらがなIME」に切り替えてください。

## しくみ

主なソースの構成は、次のとおりです。

```text
src/core        … engine.py / dictionary.py / event.py を C++ に移植した変換エンジン。Windows に依存しません。
src/tsf         … TSF のテキストサービス（hiragana-ime.dll）。64bit版と32bit版があります。
src/setup       … 設定プログラム（hiragana-setup.exe）。
src/furiganapad  … ふりがなパッド（furiganapad.exe）。変換エンジンは画面表示のしくみに依存しません。
ibus-hiragana/  … 元のソース。辞書 dic/ とキー配列 layouts/ をそのままつかいます。
furiganapad/    … ふりがなパッドの元のソース。ヘルプの文書をそのままつかいます。
```

元のエンジンは、IBus の surrounding text というしくみをつかいます。これは、カーソルの前にある文字をよみかきするためのものです。

Windows 版では、TSF の編集セッションをつかいます。変換中の文字は、コンポジション（未確定文字）として表示します。そして、カーソルの前にある文字をよみ、変換後の文字におきかえます。

ただし、コマンドプロンプトなど、カーソルの前にある文字をよめないアプリもあります。そのようなアプリでは、元の版とおなじように、入力した文字をすべて未確定文字としてあつかいます。Enter を押すと、文字が確定します。

ふりがなパッドは、同じプロセス内でひらがなIMEと直接つながります。このしくみは、`src/common/DirectClient.h` にあります。

IMEは、Linux 版の IBus とおなじ操作を、おなじ順番でふりがなパッドに届けます。そのため、元の版のふりがなをつける処理を、そのまま移植できます。

### 元の版とのちがい

Windows 版には、元の版とちがうところもあります。

- 大規模言語モデル（LLM）は、IMEをつかうアプリのプロセスではなく、別のプロセス `hiragana-llm.exe` で動かします。くわしくは、下の「LLM による候補えらび」を読んでください。
- 学習した候補の順番は、かならず保存します。元の版では、辞書を浅くコピーするため、一部の学習結果が保存されないことがあります。
- 複数のアプリが、おなじ入力履歴ファイルをつかいます。保存するときは、ほかのアプリで学習した内容もあわせます。
- 入力先のアプリがきりかわるときは、変換中の候補を確定します。
- 右Altキーは、AltGr としてあつかいます。この動作は設定で無効にできます。
- ふりがなパッドでは、ルビのおおきさを本文の2分の1にしています。

## LLM による候補えらび

この機能は、元の版の `engine/llm.py` を C++ に移植したものです。

BERT というマスク言語モデルをつかい、変換候補をえらびます。モデルは `tohoku-nlp/bert-base-japanese-v3` です。これを ONNX Runtime で動かします。

また、形態素解析には MeCab をつかいます。辞書には unidic-lite をつかいます。

次のファイルは、元の版のものをそのまま読みこみます。

- `yougen_token.dic`
- `katuyou_token.dic`
- `kango_token.json`

### 使い方

1. 設定画面の「大規模言語モデル（LLM）」を開き、「必要なファイルを入手...」を押します。モデル（約450 MB）と unidic-lite の辞書（約250 MB）が `%LOCALAPPDATA%\HiraganaIME\llm` に入ります。管理者の権限はいりません。
2. 「変換候補の選択にLLMを利用する」をオンにします。

モデルは、このリポジトリの [Releases](https://github.com/nkyossi/hiragana-ime-for-windows/releases/tag/llm-model-1) にある `hiragana-llm-model-1.zip` から入手します。

この ZIP ファイルには、`tohoku-nlp/bert-base-japanese-v3` を ONNX 形式に変換したモデルが入っています。モデル自体に変更はありません。ライセンスは Apache License 2.0 です。

unidic-lite は PyPI から入手します。どちらも SHA-256 でファイルのただしさをたしかめます。

`get-llm.ps1` には、ほかの方法でモデルを用意するためのオプションもあります。

- `-ModelDir`：`model.onnx` と `vocab.txt` があるフォルダを指定します。
- `-Export`：Python をつかって Hugging Face からモデルを入手し、変換します。

### しくみ

モデルをうごかすには、約1 GBのメモリが必要です。そのため、IMEをつかうアプリごとにモデルを読みこむことはしません。

代わりに、64bit版の `hiragana-llm.exe` が、ログオン中のユーザーにつき1つだけうごきます。このプログラムがモデルをよみこみます。

各アプリのIMEは、64bit版でも32bit版でも、名前付きパイプをつかってこのプログラムにといあわせます。接続のしくみは、`src/common/LlmProtocol.h` にあります。

- サーバーは、IMEがはじめて必要としたときに起動します。モデルの読みこみには約1秒かかります。それまでは、LLMをつかわずに変換します。
- といあわせ1回あたりの処理時間は、平均で約90ミリ秒です。3秒たってもこたえがかえってこないときは、LLMをつかわずに変換します。
- 設定でLLMをオフにするか、30分つかわないと、サーバーは終了してメモリを解放します。
- ストアアプリ（AppContainer）からも接続できます。ただし、ストアアプリからサーバーを起動することはできません。ほかのアプリが起動したサーバーをつかいます。
- 管理者として実行しているアプリからも、サーバーは起動しません。
- 「LLMの計算にCUDAを利用する」は、CUDA版の ONNX Runtime を入れたときだけつかえます。CUDAをつかえない環境では、CPUで計算します。

### テスト

LLMに関するテストには、次のものがあります。

- `llm_tokenizer`：NFKC、MeCab によるわかちがき、WordPiece、`[UNK]` などの特殊トークンのあつかいをしらべます。
- `llm_assist`：`assist()` と `assist_yougen()` の確率をしらべます。テストは300件と200件です。
- `llm_remote`：おなじテストを `hiragana-llm.exe` とIMEの接続部分（`RemoteModel`）をとおして実行します。

### ビルド

次のものを用意すると、LLMの機能もビルドできます。

用意しなくても、ほかの機能はふつうにビルドできます。この場合、LLMの機能だけが無効になります。

1. **ONNX Runtime の C++ 版**（MIT License）

   次のコマンドで入手します。

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\fetch-onnxruntime.ps1
   ```

2. **モデル（テスト用）**

   `tools\export-llm-model.py` を実行し、`%LOCALAPPDATA%\HiraganaIME-build\llm\model-single.onnx` をつくります。実行には PyTorch と transformers が必要です。

3. **unidic-lite の辞書（テスト用）**

   `HIRAGANA_MECAB_DIC_DIR` に辞書のおき場所を指定します。Python の `unidic-lite` パッケージにある `unidic_lite\dicdir` をそのままつかえます。

MeCab 本体は、ビルドずみのものを `third_party/mecab-win` においています。ただし、辞書ははいっていません。

ONNX Runtime には32bit版の配布物がありません。そのため、`hiragana-llm.exe` は64bit版だけです。32bit版のアプリがつかうIMEも、この64bit版のサーバーに接続します。

## ライセンス

このリポジトリでは、フォルダによってライセンスがちがいます。

| フォルダ | 内容 | ライセンス |
|:--|:--|:--|
| `src/core`、`src/tsf`、`src/setup`、`src/common`、`src/llm` | ひらがなIME本体（ibus-hiragana の移植） | Apache License 2.0 |
| `src/furiganapad` | ふりがなパッド（FuriganaPad の移植） | GNU LGPL v2.1 以降 |
| `ibus-hiragana` | 元のソース。辞書 `dic/` とキー配列 `layouts/` を使用 | Apache License 2.0 |
| `furiganapad` | ふりがなパッドの元のソース。ヘルプの文書を使用 | GNU LGPL v2.1 以降 |
| `ibus-hiragana/dic` | 辞書ファイル | SKK-JISYO（GPLv2）、EDICT2 / katakana.dic（CC BY-SA 3.0） |
| `third_party/mecab-win` | MeCab 0.996（LLMの形態素解析に使用。ビルド時のみ） | BSD。GPL / LGPL / BSD の三重ライセンスからBSDをえらびます。同梱の `BSD` を参照してください。 |

主なライセンスは、次のとおりです。

- **プログラム本体**（`src/core`、`src/tsf`、`src/setup`、`src/common`、`src/llm`）：Apache License 2.0

  ibus-hiragana の著作権表示：Copyright 2017-2026 Esrille Inc.

- **ふりがなパッド**（`src/furiganapad`）：GNU Lesser General Public License v2.1 以降

  FuriganaPad の著作権表示：Copyright 2019-2025 Esrille Inc.  
  Windows 版への移植：2026年

- **辞書**：SKK-JISYO（GPLv2）、EDICT2 / katakana.dic（CC BY-SA 3.0）

くわしくは、次のファイルを読んでください。

- `ibus-hiragana/LICENSE`
- `ibus-hiragana/NOTICE`
- `ibus-hiragana/COPYING`（GPLv2 の辞書）
- `furiganapad/COPYING`（LGPL-2.1）
- `furiganapad/NOTICE`

Esrille のロゴはつかっていません。

LLMによる候補えらびでは、さらに次のものをつかいます。これらは、いずれもこのリポジトリにははいっていません。インストール先には、ONNX Runtime と MeCab のライセンス文書も置きます。

- ONNX Runtime：MIT License（Microsoft）
- MeCab：BSD（`third_party/mecab-win/BSD` を参照）
- unidic-lite の辞書：UniDic（同梱の COPYING を参照）
- `tohoku-nlp/bert-base-japanese-v3`：東北大学（モデルカードに記載されたライセンスにしたがいます）
