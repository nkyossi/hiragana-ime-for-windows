# ひらがなIME for Windows

> **非公式 (unofficial)**
>
> このプロジェクト（リポジトリ名 `hiragana-ime-for-windows`）は、[ibus-hiragana](https://github.com/esrille/ibus-hiragana) と
> [FuriganaPad](https://github.com/esrille/furiganapad)（いずれも Esrille 株式会社製）を、Windows 向けに非公式に移植したものです。
> Esrille 株式会社とは関係がありません。不具合の報告は本家ではなく、この移植版のリポジトリへお願いします。Esrille のロゴは使用していません。

[ibus-hiragana](https://github.com/esrille/ibus-hiragana)（Linux/IBus用）を Windows の Text Services Framework (TSF) に移植した日本語入力システムです。

ふつうのIMEのように「入力してから変換して確定する」のではなく、**すでに入力した文字（カーソルの前の文字）をあとから漢字に変換する**「モードレス」な入力ができます。

同じリポジトリに、**ふりがなパッド**（[FuriganaPad](https://github.com/esrille/furiganapad)、Esrille製）の Windows 版も入っています。ふりがなパッドは、ひらがなIMEで漢字に変換すると、もとのよみをルビとして自動で付けます。Markdown（`.md`）の読み書きにも対応しています。

## インストール

1. [Releases](https://github.com/nkyossi/hiragana-ime-for-windows/releases) から `HiraganaIME-for-Windows-<版>.zip` をダウンロードして展開します。
2. `install.cmd` をダブルクリックします（管理者の確認が出ます）。
3. Win+スペースで「ひらがなIME」に切り替えます。使っていたアプリは、開き直すと新しい IME を読み込みます。

アンインストールは `uninstall.cmd` です。Windows 10 (1903 以降) / 11 の 64bit 版で動きます（32bit のアプリでも使えます）。プログラムには署名をしていません。

## 使い方

| キー | はたらき |
|:--|:--|
| 半角/全角、英数、Caps Lock（USキーボード） | 英数(A) ↔ ひらがな(あ) の切り替え |
| 無変換 / Shift+無変換 / Ctrl+無変換 | 英数(A) / 全角英数(Ａ) / あ→ア→ｱ の切り替え |
| ひらがな / Shift+ひらがな | ひらがな(あ) / カタカナ(ア) |
| スペース（ローマ字入力） | カーソルの前のひらがなを漢字に変換 |
| Shift+スペース | 用言（動詞・形容詞）として変換（おくりがなを続けて入力） |
| ↓ ↑ / PageDown PageUp | 候補をえらぶ |
| Tab / Shift+Tab | 変換する範囲をちぢめる / のばす |
| Enter | 確定 |
| Esc | 変換をとりやめて、よみにもどす |
| 右Shift | カーソルの前のひらがなをカタカナにする（Enterで辞書に登録） |

候補ウィンドウはマウスでもえらべます。タスクバーの「A/あ」表示を右クリックすると、入力モード、設定、ユーザー辞書の編集、ヘルプのメニューが出ます。

くわしい使い方は ibus-hiragana のヘルプ（インストール先の `docs\index.html`）をみてください。ふりがなパッドの使い方は `furiganapad-docs\index.html` です。

## 設定

`C:\Program Files\HiraganaIME\hiragana-setup.exe`（またはタスクバーの右クリックメニュー →「設定」）で変更します。

- 入力: ローマ字 / かな (JIS配列) / かな (ニュースティックニー配列)
- キーボードの種類: 自動 / 日本語(106/109) / 英語(101/104) / Dvorak
- キーの入れかえ: スペース・変換・無変換・カタカナ/ひらがな のうち 2 つを入れかえる（例: スペース ⇔ 変換 で、日本語キーボードでもスペースで変換）。英数モード(A)のときと、Ctrl・Alt といっしょに押したときは、もとのキーのままです
- 漢字辞書: 小学校１年生 〜 おとな（学年別漢字配当表にもとづく辞書）
- 個人用辞書（`%APPDATA%\HiraganaIME\my.dic`）、おくりがなの「許容」、入力履歴のクリア
- 算用数字を半角で入力、英数モードでの ^ ~ の結合、'nn' の扱い、はじめの入力モード
- 大規模言語モデル (LLM) で変換候補をえらぶ（下の「LLM による候補えらび」）

設定・個人用辞書・入力履歴は `%APPDATA%\HiraganaIME` に保存されます。設定を保存すると、ひらいているアプリにも自動で反映されます。

## ビルド

必要なもの: Visual Studio 2022（「C++ によるデスクトップ開発」）、Python 3（テスト用）

元のソース（`ibus-hiragana` と `furiganapad`）は git のサブモジュールです。辞書・キー配列・ヘルプの文書をそこから使います。

```powershell
git clone --recursive https://github.com/nkyossi/hiragana-ime-for-windows.git
cd hiragana-ime-for-windows
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Arch both -Test
```

ビルド結果は `%LOCALAPPDATA%\HiraganaIME-build\x64` と `x86` にできます（OneDrive の同期をさけるため）。おもなファイルは `hiragana-ime.dll`（TSF のテキストサービス）、`hiragana-setup.exe`（設定）、`furiganapad.exe`（ふりがなパッド）と、そのヘルプ `furiganapad-docs\` です。

### テスト

- `core_tests`: 辞書（`tests/test_dictionary.py` の移植）、学習の保存、複数プロセスでの履歴の統合など
- `golden_*`: 元の Python 版 `Dictionary` と C++ 版の変換候補を、辞書のすべての読みで比較
- `engine_golden`: 元の Python 版エンジン（IBus をまねた `tests/golden/fake_gi.py` 上で動かす）と C++ 版エンジンに同じキー操作を与え、文書・未確定文字・候補ウィンドウの状態を1キーごとに比較

実際のキー入力を確かめるテストは、開発用にIMEを一時的に登録してから実行します（設定と入力履歴は退避・復元されます）。

```powershell
powershell -ExecutionPolicy Bypass -File tools\run-e2e.ps1
```

### ソースからのインストール / アンインストール

ビルドしたものを、管理者の PowerShell でインストールします。

```powershell
powershell -ExecutionPolicy Bypass -File installer\install.ps1
powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1   # -RemoveUserData で設定と辞書も削除
```

配布用の zip は `tools\make-release.ps1` でつくります（`-Model` で LLM のモデルの zip も）。

インストールすると、現在のユーザーの「日本語」のキーボードに「ひらがなIME」が追加され、スタートメニューに「ふりがなパッド」と「Hiragana IME Settings」のショートカットができます。Win+スペースで切り替えてください。

## しくみ

```
src/core   … engine.py / dictionary.py / event.py を C++ に移植した変換エンジン（Windows に依存しない）
src/tsf    … TSF のテキストサービス（hiragana-ime.dll、64bit と 32bit）
src/setup  … 設定プログラム（hiragana-setup.exe）
src/furiganapad … ふりがなパッド（furiganapad.exe。core は UI に依存しない）
ibus-hiragana/ … 元のソース（辞書 dic/ とキー配列 layouts/ をそのまま使います）
furiganapad/   … ふりがなパッドの元のソース（ヘルプの文書をそのまま使います）
```

元のエンジンは IBus の surrounding text（カーソルの前の文字）を読み書きします。Windows 版では TSF の編集セッションで、変換中の文字をコンポジション（未確定文字）として表示し、カーソルの前の文字を読んで置きかえます。コマンドプロンプトなど、カーソルの前の文字を読めないアプリでは、元の版と同じように入力した文字をすべて未確定文字として持ち、Enter で確定します。

ふりがなパッドは、同じプロセス内でひらがなIMEと直通でつながります（`src/common/DirectClient.h`）。IME はふりがなパッドに、Linux 版の IBus と同じ操作を同じ順番で届けるので、元の版のふりがな付けの処理をそのまま移植できます。

### 元の版とのちがい

- 大規模言語モデル (LLM) は、IME を使うアプリのプロセスではなく、別のプロセス `hiragana-llm.exe` で動かします（下の「LLM による候補えらび」）。
- 学習した候補の順番がかならず保存されます（元の版では、辞書の浅いコピーのため、一部の学習が保存されないことがあります）。
- 複数のアプリが同じ入力履歴ファイルを使うので、保存するときにほかのアプリの学習とあわせます。
- フォーカスが移るときは、変換中の候補を確定します。
- 右Altキーは AltGr として扱います（設定で無効にできます）。
- ふりがなパッドでは、ルビの大きさを本文の 1/2.75 ではなく 1/2 にしています（等倍の画面では小さすぎたため）。

## LLM による候補えらび

元の版の `engine/llm.py` を C++ に移植したものです。BERT マスク言語モデル（`tohoku-nlp/bert-base-japanese-v3`）を ONNX Runtime で動かし、形態素解析に MeCab（unidic-lite の辞書）をつかいます。`yougen_token.dic`、`katuyou_token.dic`、`kango_token.json` は元の版のものをそのまま読みます。

### 使いかた

1. 設定の「大規模言語モデル (LLM)」で「必要なファイルを入手...」をおします。モデル（約 450 MB）と unidic-lite の辞書（約 250 MB）が `%LOCALAPPDATA%\HiraganaIME\llm` に入ります。管理者の権限はいりません。
2. 「変換候補の選択にLLMを利用する」をオンにします。

モデルは、このリポジトリの [Releases](https://github.com/nkyossi/hiragana-ime-for-windows/releases/tag/llm-model-1) にある `hiragana-llm-model-1.zip`（`tohoku-nlp/bert-base-japanese-v3` を ONNX に変換しただけのもの。Apache License 2.0）を入手します。unidic-lite は PyPI から入手します。どちらも SHA-256 を確かめます。`get-llm.ps1` に `-ModelDir`（`model.onnx` と `vocab.txt` のあるフォルダ）や `-Export`（Python で Hugging Face から変換）をつけると、ほかの方法でも用意できます。

### しくみ

モデルはメモリを約 1 GB つかうので、IME を使うアプリのプロセスごとに読みこむことはしません。64bit の `hiragana-llm.exe` がログオンセッションに 1 つだけ動いてモデルを読みこみ、各アプリの IME（64bit も 32bit も）が名前付きパイプで問い合わせます（`src/common/LlmProtocol.h`）。

- サーバーは、IME が最初に必要としたときに起動します。モデルの読みこみ（約 1 秒）が終わるまでは、LLM なしで変換します。
- 1 回の問い合わせは平均 90 ミリ秒ほどです。3 秒たっても答えがないときは、LLM なしで変換します。
- 設定で LLM をオフにするか、30 分つかわないと、サーバーは終了してメモリを返します。
- ストアアプリ（AppContainer）からも接続できますが、サーバーを起動することはできません。ほかのアプリで起動ずみのサーバーを使います。管理者として実行しているアプリからも起動しません。
- 「LLMの計算にCUDAを利用する」は、CUDA 版の ONNX Runtime を入れたときだけ有効です。CUDA が使えないときは CPU で計算します。

### テスト

- `llm_tokenizer`: NFKC、MeCab の分かち書き、WordPiece、`[UNK]` などの特殊トークンの扱い
- `llm_assist`: `assist()` と `assist_yougen()` の確率（300 + 200 ケース）
- `llm_remote`: 同じケースを `hiragana-llm.exe` と IME の接続部分（`RemoteModel`）をとおして確認

### ビルド

つぎのものを用意すると有効になり、`tools\build.ps1` が自動で見つけます。用意しないときは、この機能だけが無効になります（ほかは同じようにビルドできます）。

1. **ONNX Runtime の C++ 版**（MIT）: `powershell -ExecutionPolicy Bypass -File tools\fetch-onnxruntime.ps1`
2. **モデル**（テスト用）: `tools\export-llm-model.py` を実行して `%LOCALAPPDATA%\HiraganaIME-build\llm\model-single.onnx` をつくります（PyTorch と transformers が必要です）。
3. **unidic-lite の辞書**（テスト用）: `HIRAGANA_MECAB_DIC_DIR` に置き場所を指定します。Python の `unidic-lite` パッケージの `unidic_lite\dicdir` をそのまま使えます。

MeCab 本体はビルドずみのものを `third_party/mecab-win` に置いています（辞書は含みません）。ONNX Runtime には 32bit 版の配布物が無いので、`hiragana-llm.exe` は 64bit だけです。32bit のアプリの IME も、この 64bit のサーバーを使います。

## ライセンス

このリポジトリは、フォルダごとにライセンスが異なります。

| フォルダ | 内容 | ライセンス |
|:--|:--|:--|
| `src/core`、`src/tsf`、`src/setup`、`src/common`、`src/llm` | ひらがなIME本体（ibus-hiragana の移植） | Apache License 2.0 |
| `src/furiganapad` | ふりがなパッド（FuriganaPad の移植） | GNU LGPL v2.1 以降 |
| `ibus-hiragana` | 本家のソース（辞書 `dic/` とキー配列 `layouts/` を使用） | Apache License 2.0 |
| `furiganapad` | ふりがなパッドの元のソース（ヘルプの文書を使用） | GNU LGPL v2.1 以降 |
| `ibus-hiragana/dic` | 辞書ファイル | SKK-JISYO (GPLv2)、EDICT2 / katakana.dic (CC BY-SA 3.0) |
| `third_party/mecab-win` | MeCab 0.996（LLM の形態素解析。ビルド時のみ使用） | BSD（GPL / LGPL / BSD の三重ライセンスのうち BSD を選びます。同梱の `BSD` を参照） |

- プログラム本体（`src/core`、`src/tsf`、`src/setup`、`src/common`、`src/llm`）: Apache License 2.0（ibus-hiragana: Copyright 2017-2026 Esrille Inc.）
- ふりがなパッド（`src/furiganapad`）: GNU Lesser General Public License v2.1 以降（FuriganaPad: Copyright 2019-2025 Esrille Inc.、Windows 移植 2026）
- 辞書: SKK-JISYO (GPLv2)、EDICT2 / katakana.dic (CC BY-SA 3.0)

くわしくは `ibus-hiragana/LICENSE`、`ibus-hiragana/NOTICE`、`ibus-hiragana/COPYING`（GPLv2 の辞書）、`furiganapad/COPYING`（LGPL-2.1）、`furiganapad/NOTICE` をみてください。Esrille のロゴは使用していません。

LLM による候補えらびでは、さらにつぎのものを使います（いずれもこのリポジトリには含めていません。インストール先には ONNX Runtime と MeCab のライセンスをいっしょに置きます）。ONNX Runtime（MIT License、Microsoft）、MeCab（BSD。`third_party/mecab-win/BSD`）、unidic-lite の辞書（UniDic。同梱の COPYING を参照）、`tohoku-nlp/bert-base-japanese-v3`（東北大学。モデルカードのライセンスに従います）。
