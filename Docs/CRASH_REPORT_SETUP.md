# クラッシュレポートの配布側設定

公式ビルドでクラッシュレポート送信を有効にする手順。
クライアント側実装は `Source/Project/CrashReporter.h/cpp`（仕組みの詳細はヘッダのコメント参照）。

## 動作概要

1. アプリ起動時に `CrashReporter::install()` がクラッシュハンドラを登録
2. クラッシュ時、スタックトレース + アプリ版 + OS を
   `~/Library/Logs/Utawave/crash-YYYYMMDD_HHMMSS.log` に保存（送信はしない）
3. **次回起動時**に未処理ログを検出すると同意ダイアログを表示
   - 「送信する」→ JSON を POST（成功・失敗ともログはローカルに残る）
   - 「送信しない」→ 送信せず `.handled` にリネーム（再プロンプトしない）
   - 「ログを表示」→ Finder/Explorer でログを表示（処理は確定せず次回また確認）
4. **同意なしに送信されることはない**

## 有効化（公式ビルドのみ）

```sh
cmake -DUTAWAVE_CRASH_REPORT_URL="https://utawave.com/crash/report" ...
```

未指定（公開ソースの既定）では送信機能ごと無効になり、ダイアログは
「ログを確認しますか？」のローカル表示のみになる。

## サーバー側 (POST エンドポイント) の仕様

- メソッド: `POST` / `Content-Type: application/json`
- 成功は HTTP 2xx を返す（クライアントは 2xx 以外を失敗扱い）
- ボディ:

```json
{
  "app":     "Utawave",
  "version": "0.2.0",
  "os":      "macOS 14 / MacBook Pro",
  "file":    "crash-20260611_093000.log",
  "log":     "Utawave crash log\nversion: ...\n--- stack backtrace ---\n..."
}
```

- ログ本文はスタックトレース等の技術情報のみ。音声データ・個人情報は含まれない
  （ダイアログ文言でもそのように案内しているので、ペイロードに項目を足す時は文言と整合させること）

## 動作確認

1. `~/Library/Logs/Utawave/` に手動で `crash-20990101_000000.log` を置いて起動 → ダイアログが出る
2. 「送信する」→ サーバーに JSON が届く / 2xx でダイアログが静かに閉じる
3. 失敗時（サーバー停止）→「送信できませんでした」が出て、ログが `.handled` で残る
