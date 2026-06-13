# リリース手順 / Release Guide

配布物（macOS / Windows のビルド済みアプリ）は **公式サイト (utawave.com)** で公開します。
署名 (macOS: Developer ID + 公証 / Windows: SignPath) を付けるため、**配布ビルドは GitHub Actions
(手動発火) で行います**。署名済み zip は **GitHub Releases（アプリ repo `AsteroidApp-hub/Utawave`）へ
アップロード**し、**公式サイト (utawave.com) の `download.html` から、その Release アセットへ直リンク**する。
サイトリポジトリ (Utawave-Site) には**バイナリを置かない**（zip は GitHub が配信。容量上限や repo 履歴の
肥大を気にせず済む）。

> repo が public である限り誰でも匿名でダウンロードできる（**private に戻すとリンクが切れる**点に注意）。
> ランディングは自ドメイン (`download.html`)・ファイル実体だけ GitHub という形なので、被リンクは自ドメインに
> 溜まり SEO 上の不利も無い。

> **署名のセットアップ**は `Docs/MACOS_SIGNING_SETUP.md` (Apple) と `Docs/WINDOWS_SIGNING_SETUP.md`
> (SignPath) を参照。必要な GitHub Secrets / Variables が未登録のうちは、macOS は ad-hoc (未署名)
> ビルドのまま zip 化されます。
>
> **ASIO 対応の Windows 版**は再配布制限のある SDK を CI に置けないため、CI では作れません。必要な場合
> のみ従来どおりローカル手動ビルド (=未署名) で作ります (末尾「補足」参照)。

zip のファイル名にはバージョン + アーキテクチャを含めます（例: `Utawave-0.1.0-macOS-arm64.zip` /
`Utawave-0.1.0-Windows-x64.zip`）。

## 1. バージョン更新

`CMakeLists.txt` の `VERSION`（必要なら About 等）を更新してコミット・push する。

## 2. ワークフローを手動発火してビルド + 署名

1. GitHub の **Actions タブ → "Release Build (macOS + Windows)" → Run workflow**。
2. **version** に今回のバージョン（例 `0.1.0`）を入力して実行。
3. Windows は SignPath 署名で**承認待ちになる**ので、SignPath.io で署名要求を**承認**する。
4. 完了後、ワークフローの成果物 (Artifacts) から以下の 2 つの zip をダウンロード:
   - `Utawave-<version>-macOS-arm64`（署名 + 公証済み / 未設定時は ad-hoc）
   - `Utawave-<version>-Windows-x64`（SignPath 署名済み exe を zip 化済み）

> 両 zip ともライセンス類（LICENSE / THIRD_PARTY_LICENSES.txt / MANUAL.html / README.md / README.ja.md）を同梱済み。
> ローカルでの再パッケージは不要。

> **Windows の `Utawave.pdb` を必ず保管すること**: Release ビルドでは exe の隣に PDB が生成される。
> 配布 zip には入れず、**リリースごとに exe + PDB のペアを手元に保管**する。クラッシュレポートの
> スタックトレース（`Utawave.exe + 0x<RVA>` 形式）を関数・行番号へ解決するのに必要
> （手順は `Docs/CRASH_REPORT_SETUP.md`）。RVA はビルドごとに変わるため、該当バージョンの PDB が無いと解決できない。

## 3. ユニットテスト（任意・推奨）

ワークフローとは別に、ローカルで回す場合:

```sh
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --target UtawaveTests --config Release
# 生成された UtawaveTests を実行（全合格で終了コード 0）
```

## 4. GitHub Release を作成（配布物の本体 + タグ）

GitHub Releases が**配布物の実体の置き場**で、公式サイトの `download.html` はここへ直リンクする。
タグを打つことで AGPL の「対応ソースコード」も同じタグに紐づく。

```sh
gh release create v0.1.0 Utawave-0.1.0-macOS-arm64.zip Utawave-0.1.0-Windows-x64.zip \
  --title "Utawave v0.1.0" --notes "更新内容の要約"
```

タグ `v0.1.0` が未作成なら `gh` が作成して push する。Web から行う場合は
**「Releases」→「Draft a new release」**でタグを新規作成し、zip をドラッグ&ドロップして
**Publish release**。

## 5. 公式サイトのリンクを更新（download.html を Release へ向ける）

サイトに**バイナリは置かない**。手順 4 の Release アセット URL を `download.html` に差し込むだけ。
CI やトークンは不要で、push すると Cloudflare Pages が自動デプロイする。サイト repo は
`~/Dropbox/アプリ開発/Utawave-Site`。

1. **`download.html` のダウンロードリンクを Release アセット URL に差し替える**（表示バージョンも更新）。
   URL の形式（タグ `v<version>` とファイル名は手順 2/4 のもの）:

   ```
   https://github.com/AsteroidApp-hub/Utawave/releases/download/v0.1.0/Utawave-0.1.0-macOS-arm64.zip
   https://github.com/AsteroidApp-hub/Utawave/releases/download/v0.1.0/Utawave-0.1.0-Windows-x64.zip
   ```

2. **メタ情報を更新**:
   - `version.json`（**リポジトリ直下**）の `"version"` を今回の版に更新
     → 旧バージョンのアプリの起動画面に「アップデートがあります」が出るようになる（`UpdateChecker`）
   - `changelog.html` に新しいリリースブロックを追加

3. **コミット & push**:

   ```sh
   cd ~/Dropbox/アプリ開発/Utawave-Site
   git add version.json download.html changelog.html
   git commit -m "release: Utawave v0.1.0"
   git push
   ```

4. **反映確認**: https://utawave.com/download.html から macOS / Windows 両方ダウンロードできること、
   旧バージョンのアプリの起動画面に「アップデートがあります」が表示されること

> **毎回 download.html を編集したくない場合**: CI のアセット名から**バージョンを外し**（例
> `Utawave-macOS-arm64.zip`）、`https://github.com/AsteroidApp-hub/Utawave/releases/latest/download/Utawave-macOS-arm64.zip`
> を使うと**常に最新 Release へリダイレクト**されるので、`download.html` のリンクは固定にできる
> （リリース時に触るのは `version.json` / `changelog.html` だけになる。要 workflow のアセット名変更）。

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
cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release

$exe = Get-ChildItem -Path build-win -Recurse -Filter Utawave.exe | Select-Object -First 1
New-Item -ItemType Directory -Force -Path Utawave-Windows | Out-Null
Copy-Item $exe.FullName Utawave-Windows/
Copy-Item LICENSE,THIRD_PARTY_LICENSES.txt,Docs/MANUAL.html,README.md,README.ja.md Utawave-Windows/
Compress-Archive -Path Utawave-Windows -DestinationPath Utawave-0.1.0-win64-asio.zip -Force

# クラッシュレポート解決用に exe + PDB のペアを保管する (zip には入れない)
$pdb = Get-ChildItem -Path build-win -Recurse -Filter Utawave.pdb | Select-Object -First 1
New-Item -ItemType Directory -Force -Path Symbols-0.1.0 | Out-Null
Copy-Item $exe.FullName,$pdb.FullName Symbols-0.1.0/
```

> CMake configure 時に `ASIO SDK found ... — ASIO サポート有効` と表示されれば ASIO 付きです。
