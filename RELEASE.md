# リリース手順 / Release Guide

Trakova の配布物は **GitHub Releases** で公開し、利用者はそこから手動でダウンロードします。
ビルドは GitHub Actions（`.github/workflows/build.yml`）が自動で行います。

## 1. 通常リリース（自動・ASIO 無し）

1. バージョンを更新（`CMakeLists.txt` の `VERSION` など）。
2. タグを打って push:
   ```sh
   git tag v0.1.0
   git push origin v0.1.0
   ```
3. GitHub Actions が **macOS（arm64）** と **Windows（WASAPI/DirectSound・ASIO 無し）** を
   ビルド＆テストし、`Trakova-macOS.zip` / `Trakova-Windows.zip` を添付した
   **Release を自動作成**します。
4. 利用者は **リポジトリの「Releases」ページ**から、ログイン不要でダウンロードできます。

## 2. ASIO 対応 Windows 版（手動・plan C・任意）

ASIO SDK は再配布不可のためリポジトリ・CI には含めません。ASIO 版が必要なときだけ、
ローカルの Windows 環境（ASIO SDK を `Source/ThirdParty/asiosdk/` に配置済み）で手動ビルドします。

```powershell
# 1) リリースタグをチェックアウト
git fetch --tags
git checkout v0.1.0

# 2) ビルド（JUCE は自動取得。asiosdk があれば JUCE_ASIO=1 が自動で有効）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 3) パッケージ化（.exe + ライセンス / マニュアル）
$exe = Get-ChildItem -Path build -Recurse -Filter Trakova.exe | Select-Object -First 1
New-Item -ItemType Directory -Force -Path Trakova-Windows-ASIO | Out-Null
Copy-Item $exe.FullName Trakova-Windows-ASIO/
Copy-Item LICENSE,THIRD_PARTY_LICENSES.txt,Docs/MANUAL.html,README.md Trakova-Windows-ASIO/
Compress-Archive -Path Trakova-Windows-ASIO -DestinationPath Trakova-Windows-ASIO.zip -Force

# 4) 同じ Release に添付（gh CLI、または Releases 画面にドラッグ&ドロップ）
gh release upload v0.1.0 Trakova-Windows-ASIO.zip
```

> CMake configure 時に `ASIO SDK found ... — ASIO サポート有効` と表示されれば ASIO 付きでビルドされています。
> 表示されない場合は WASAPI/DirectSound のみ（＝通常 CI と同じ）です。

## 3. タグを打たずにビルドだけ取得したい場合

GitHub Actions の **「Run workflow」（workflow_dispatch）** から任意に実行でき、生成物は実行ページの
**「Artifacts」**からダウンロードできます（GitHub ログインが必要・既定 90 日で失効）。
一般配布には Release（上記 1）を使ってください。
