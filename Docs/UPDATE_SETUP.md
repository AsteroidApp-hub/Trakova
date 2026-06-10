# Utawave アップデート通知 セットアップガイド

起動画面の右上に出る「アップデートがあります」リンクの仕組みと、配布側 (公式サイト) の設定手順。
広告枠 (`ADS_SETUP.md`) と同じく「公式サイトに置いた JSON をアプリが読む」方式。

## 1. 概要

- アプリは起動画面の表示時に **公式サイトの version JSON** を 1 回取得する
  (バックグラウンド・接続タイムアウト 5 秒・失敗時は何も表示しない)
- JSON の `version` が現在のアプリ版より**新しい時だけ**リンクを表示し、
  クリックで `url` (ダウンロードページ) を既定ブラウザで開く
- サーバーが未稼働 / オフラインの場合は黙って何も出ない (正常なフォールバック)
- リポジトリの公開/非公開とは無関係に動く (GitHub API には依存しない)

## 2. version JSON の仕様

公式サイトに静的ファイルとして置くだけでよい (例: `https://<公式サイト>/version.json`)。

```json
{
  "version": "0.2.0",
  "url": "https://<公式サイト>/download"
}
```

| フィールド | 必須 | 説明 |
|---|---|---|
| `version` | ✔ | 最新バージョン。`v0.2.0` のような接頭辞付きでも可 (数字部分で比較)。表示にはこの生文字列が使われる |
| `url` | 任意 | クリックで開くダウンロードページ。省略時はビルド時に埋め込んだ既定ページ (`UTAWAVE_DOWNLOAD_PAGE_URL`) |

- 比較は **数値のセマンティックバージョン比較** (`0.10.0 > 0.9.0`)。同一・古い・解析不能なら通知しない
- 未知のフィールドは無視されるので、将来 `notes` 等を足しても古いアプリは壊れない
- **リリース手順**: 新版のバイナリをサイトに置いたら、最後に `version.json` の `version` を上げる
  (この順なら通知を見たユーザーが必ず新版をダウンロードできる)

## 3. ビルド時に渡すフラグ

公開ソースの既定値はプレースホルダ (`https://utawave.com/version.json`)。
公式配布ビルドでは本番 URL を埋め込む:

```sh
cmake -S . -B build \
  -DUTAWAVE_VERSION_URL="https://<公式サイト>/version.json" \
  -DUTAWAVE_DOWNLOAD_PAGE_URL="https://<公式サイト>/download" \
  ...
```

- `UTAWAVE_VERSION_URL` — version JSON の取得先
- `UTAWAVE_DOWNLOAD_PAGE_URL` — JSON に `url` が無い時のフォールバック先 (任意)

広告フィード (`-DUTAWAVE_AD_FEED_URL=...`、`ADS_SETUP.md` 参照) と同時に指定する想定。

## 4. 動作確認

1. 任意の場所に `version.json` を置く (ローカル HTTP サーバーでも可)
2. `UTAWAVE_VERSION_URL` をそこへ向けてビルド
3. `version` を現在のアプリ版 (`CMakeLists.txt` の `VERSION`) より大きくして起動
   → 起動画面右上に赤いリンクが出る。クリックで `url` が開く
4. `version` を同じ/小さくすると出ない (新しい時だけ表示、が仕様)

## 5. 実装メモ

- 実装: `Source/Project/UpdateChecker.{h,cpp}` (デタッチスレッド / CancelFlag / SafePointer /
  5 秒タイムアウト。純関数 `parseVersionInfo` / `normaliseVersion` / `isNewerVersion` は
  `Tests/UpdateCheckerTests.cpp` が検証)
- 表示側: `Source/UI/StartupComponent.cpp` (`startUpdateCheck` / `showUpdateBanner`)
