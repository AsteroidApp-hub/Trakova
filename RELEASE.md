# リリース手順 / Release Guide

配布物（macOS / Windows のビルド済みアプリ）は **GitHub Releases** で公開します。
ビルドは CI ではなく **各自のマシンで手動**で行い、生成した zip を Release にアップロードします。
（ASIO 対応の Windows 版もこの手動ビルドで作れます）

## 1. バージョン更新

`CMakeLists.txt` の `VERSION`（必要なら About 等）を更新する。

## 2. macOS 版をビルド（Mac 上で）

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# .app をライセンス類と一緒に zip
APP=$(find build -type d -name "Utawave.app" | head -1)
mkdir -p Utawave-macOS && cp -R "$APP" Utawave-macOS/
cp LICENSE THIRD_PARTY_LICENSES.txt Docs/MANUAL.html README.md Utawave-macOS/
ditto -c -k --sequesterRsrc --keepParent Utawave-macOS Utawave-macOS.zip
```

## 3. Windows 版をビルド（Windows 上で・ASIO 対応可）

ASIO を有効にする場合は、先に ASIO SDK を `Source/ThirdParty/asiosdk/` に配置する
（CMake が自動検出して `JUCE_ASIO=1` を有効化）。

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# .exe をライセンス類と一緒に zip
$exe = Get-ChildItem -Path build -Recurse -Filter Utawave.exe | Select-Object -First 1
New-Item -ItemType Directory -Force -Path Utawave-Windows | Out-Null
Copy-Item $exe.FullName Utawave-Windows/
Copy-Item LICENSE,THIRD_PARTY_LICENSES.txt,Docs/MANUAL.html,README.md Utawave-Windows/
Compress-Archive -Path Utawave-Windows -DestinationPath Utawave-Windows.zip -Force
```

> CMake configure 時に `ASIO SDK found ... — ASIO サポート有効` と表示されれば ASIO 付きです。
> 表示されなければ WASAPI / DirectSound のみになります。

## 4. ユニットテスト（任意・推奨）

```sh
cmake --build build --target UtawaveTests --config Release
# 生成された UtawaveTests を実行（全合格で終了コード 0）
```

## 5. GitHub Release を作成してアップロード

- リポジトリの **「Releases」→「Draft a new release」**
- **タグ** `v0.1.0` を新規作成（target: main）
- タイトル・説明を記入し、**手順 2・3 で作った zip をドラッグ&ドロップ**
- **Publish release**

gh CLI を使う場合:

```sh
gh release create v0.1.0 Utawave-macOS.zip Utawave-Windows.zip \
  --title "Utawave v0.1.0" --notes "初回リリース"
```

> 利用者は **Releases ページ**からログイン不要でダウンロードできます。
> 本リリースは AGPL-3.0-or-later で配布されます。対応するソースコードは同じタグのリポジトリです
> （同梱ライブラリのライセンスは THIRD_PARTY_LICENSES.txt を参照）。
> macOS 版は ad-hoc 署名のため、初回は Finder で右クリック →「開く」で起動してください。
