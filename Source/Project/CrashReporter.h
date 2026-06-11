// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

/**
    クラッシュレポート。

    - install() がプロセス全体のクラッシュハンドラを登録し、クラッシュ時にスタックトレース +
      アプリ版 + OS 情報をローカルのログファイルへ書き出す (この時点では送信しない)。
    - 次回起動時に offerPendingReports() が未処理ログを見つけると、**ユーザーの同意ダイアログ**を
      出してから開発者へ送信する (同意なしに送信しない)。「送信しない」を選んでもログは
      ローカルに残る (.handled にリネームされ再プロンプトはされない)。
    - 送信先はコンパイル時 UTAWAVE_CRASH_REPORT_URL (公式ビルドが差し替え)。未定義の公開ソース
      ビルドでは送信機能ごと無効で、「ログを確認しますか?」のローカル表示のみになる
      (AdService / UpdateChecker と同じコンパイル時ゲートの作法)。
    - ネットワークは AdService と同じデタッチスレッド + 値コピー + callAsync。純関数
      (pendingLogs / markHandled / buildReportJson / crashLogFileName) は CrashReporterTests が検証。
*/
class CrashReporter
{
public:
    // プロセス全体のクラッシュハンドラを登録する (起動直後に 1 回)
    static void install();

    // ログ置き場: macOS = ~/Library/Logs/Utawave (OS 標準のログ置き場) /
    // その他 = <userApplicationData>/Utawave/Logs。設定置き場 (~/Library/Utawave) とは分離する
    static juce::File crashLogDirectory();

    // 未処理 (*.log) のクラッシュログを新しい順に返す
    static std::vector<juce::File> pendingLogs(const juce::File& dir);

    // 処理済みにする (.log → .handled へリネーム。ローカルには残す)
    static bool markHandled(const juce::File& logFile);

    // 送信ペイロード (JSON) を組み立てる (純関数)
    static juce::String buildReportJson(const juce::String& appVersion,
                                        const juce::String& osDesc,
                                        const juce::String& logFileName,
                                        const juce::String& logContent);

    // ログファイル名 (純関数): crash-YYYYMMDD_HHMMSS.log
    static juce::String crashLogFileName(juce::Time when);

    // コンパイル時に送信先 URL が埋め込まれているか
    static bool reportingCompiledIn();
    static juce::String defaultReportUrl();

    // JSON をデタッチスレッドで POST し、結果 (成功/失敗) をメッセージスレッドへ返す
    static void sendAsync(const juce::String& url, const juce::String& json,
                          std::function<void(bool)> onDone);

    // 起動時フロー: 未処理ログがあれば同意ダイアログを出し、同意時のみ送信する
    static void offerPendingReports();
};
