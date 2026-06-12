# リリース手順 / Release Guide

配布物（macOS / Windows のビルド済みアプリ）は **公式サイト (utawave.com)** で公開します。
署名 (macOS: Developer ID + 公証 / Windows: SignPath) を付けるため、**配布ビルドは GitHub Actions
(手動発火) で行います**。生成された署名済み zip を**公式サイトリポジトリ (Utawave-Site) の `dl/` に
コミット**して Cloudflare Pages から配信します（配置ルールの詳細はサイト側 README の「配布ファイルの
置き方」を参照。1 ファイル 25MiB 以内・Git LFS 不使用・旧バージョンは並置）。

> **署名のセットアップ**は `Docs/MACOS_SIGNING_SETUP.md` (Apple) と `Docs/WINDOWS_SIGNING_SETUP.md`
> (SignPath) を参照。必要な GitHub Secrets / Variables が未登録のうちは、macOS は ad-hoc (未署名)
> ビルドのまま zip 化されます。
>
> **ASIO 対応の Windows 版**は再配布制限のある SDK を CI に置けないため、CI では作れません。必要な場合
> のみ従来どおりローカル手動ビルド (=未署名) で作ります (末尾「補足」参照)。

zip のファイル名にはバージョンを含めます（例: `Utawave-0.1.0-macOS.zip` /
`Utawave-0.1.0-win64.zip`）。

## 1. バージョン更新

`CMakeLists.txt` の `VERSION`（必要なら About 等）を更新してコミット・push する。

## 2. ワークフローを手動発火してビルド + 署名

1. GitHub の **Actions タブ → "Release Build (macOS + Windows)" → Run workflow**。
2. **version** に今回のバージョン（例 `0.1.0`）を入力して実行。
3. Windows は SignPath 署名で**承認待ちになる**ので、SignPath.io で署名要求を**承認**する。
4. 完了後、ワークフローの成果物 (Artifacts) から以下の 2 つの zip をダウンロード:
   - `Utawave-<version>-macOS`（署名 + 公証済み / 未設定時は ad-hoc）
   - `Utawave-<version>-win64`（SignPath 署名済み exe を zip 化済み）

> 両 zip ともライセンス類（LICENSE / THIRD_PARTY_LICENSES.txt / MANUAL.html / README.md / README.ja.md）を同梱済み。
> ローカルでの再パッケージは不要。

## 3. ユニットテスト（任意・推奨）

ワークフローとは別に、ローカルで回す場合:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target UtawaveTests --config Release
# 生成された UtawaveTests を実行（全合格で終了コード 0）
```

## 4. GitHub Release を作成（タグ + 配布物のバックアップ）

GitHub Releases にも**同じ zip を必ず置く**。役割は 3 つ:
公式サイト障害時のバックアップ / AGPL の「対応ソースコード」用**タグの打ち忘れ防止** /
GitHub でリポジトリを見た人のダウンロード先 (README からリンク)。

```sh
gh release create v0.1.0 Utawave-0.1.0-macOS.zip Utawave-0.1.0-win64.zip \
  --title "Utawave v0.1.0" --notes "更新内容の要約"
```

タグ `v0.1.0` が未作成なら `gh` が作成して push する。Web から行う場合は
**「Releases」→「Draft a new release」**でタグを新規作成し、zip をドラッグ&ドロップして
**Publish release**。

## 5. 公式サイトに公開（一般利用者向けの配布先）

1. **公式サイトリポジトリ (Utawave-Site) で**:
   - 手順 2 で取得した zip を `dl/` に追加（**1 ファイル 25MiB 以内**であることを確認）
   - `version.json` の `"version"` を更新（旧バージョンのアプリに更新通知が出るようになる）
   - `download.html` の表示バージョンとダウンロードリンクを更新
     （旧バージョンは「過去のバージョン」欄へリンクを移す）
   - `changelog.html` に新しいリリースブロックを追加
   - コミットして push → Cloudflare Pages が自動デプロイ

2. **反映確認**: https://utawave.com/download.html からダウンロードできること、
   旧バージョンのアプリの起動画面に「アップデートがあります」が表示されること

> 本リリースは AGPL-3.0-or-later で配布されます。対応するソースコードは同じタグのリポジトリです
> （同梱ライブラリのライセンスは THIRD_PARTY_LICENSES.txt を参照）。
> macOS 署名 + 公証が未設定 (ad-hoc) のうちは、初回は Finder で右クリック →「開く」で起動してください
> （公証が有効になればこの注記は不要）。

---

## 補足: ASIO 対応 Windows 版をローカルで作る（任意・未署名）

ASIO SDK は再配布制限のため CI に置けないので、ASIO 入り Windows 版が必要な場合のみローカル手動でビルドする
（この成果物は**未署名**になる）。先に ASIO SDK を `Source/ThirdParty/asiosdk/` に配置する
（CMake が自動検出して `JUCE_ASIO=1` を有効化）。

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

$exe = Get-ChildItem -Path build -Recurse -Filter Utawave.exe | Select-Object -First 1
New-Item -ItemType Directory -Force -Path Utawave-Windows | Out-Null
Copy-Item $exe.FullName Utawave-Windows/
Copy-Item LICENSE,THIRD_PARTY_LICENSES.txt,Docs/MANUAL.html,README.md,README.ja.md Utawave-Windows/
Compress-Archive -Path Utawave-Windows -DestinationPath Utawave-0.1.0-win64-asio.zip -Force
```

> CMake configure 時に `ASIO SDK found ... — ASIO サポート有効` と表示されれば ASIO 付きです。
