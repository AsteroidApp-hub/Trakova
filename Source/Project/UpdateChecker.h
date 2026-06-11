// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

// 公式サイトの version JSON から取り出した最新版情報。
// JSON 形式: { "version": "0.2.0", "url": "https://.../download" } (url は任意)
struct UpdateInfo
{
    juce::String version;   // version から数字部分を抽出したもの (例 "0.2.0")。バージョン比較用
    juce::String tag;       // JSON の version 生文字列 (例 "v0.2.0")。表示用
    juce::String pageUrl;   // クリックで開くダウンロードページ URL
};

// 公式サイトの version JSON を取得し、現在のアプリバージョンと比較して新しい版があるかを
// 判定する (広告フィード AdService と同じ「公式サイトの JSON を読む」作法)。
// ネットワーク I/O はすべてバックグラウンドスレッドで行い、結果はメッセージスレッドへ戻す。
// インスタンスは持たず static 関数で完結する (呼び出し元の寿命管理を不要にするため)。
class UpdateChecker
{
public:
    // 取得をキャンセルするための共有フラグ。呼び出し元 (StartupComponent) と worker スレッドの
    // 双方が所有 (shared_ptr) し、true になると worker は結果配信を打ち切る。呼び出し元が取得完了前に
    // 破棄される時に立てることで、メッセージスレッドへ無駄な callAsync を投げない。
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    // 非同期で versionUrl の JSON を取得する。完了時 (キャンセルされなかった時) に cb を
    // 「メッセージスレッド」で 1 回呼ぶ。
    //   info : 取得できた最新版情報 (失敗時は空)。JSON に url が無ければ fallbackPageUrl を入れる
    //   ok   : 取得・パースに成功したか (false = ネットワーク / パース失敗 / version 無し)
    // 引数はすべて値コピーされ worker が所有するので、呼び出し元が取得完了前に破棄されても安全
    // (cb 側で SafePointer 等により自身の生存を確認すること)。バージョン比較は呼び出し元で行う。
    static void check(juce::String versionUrl, juce::String fallbackPageUrl, CancelFlag cancel,
                      std::function<void(UpdateInfo, bool)> cb);

    // ── ネットワーク非依存のテスト可能な純関数 ──
    // version JSON から version / url を取り出す。version が欠ける場合もそのまま返すので、
    // 呼び出し側で空判定すること (url の fallback 適用は check() が行う)。
    static UpdateInfo parseVersionInfo(const juce::String& jsonText);

    // バージョン文字列から先頭の数字部分を抽出する (例 "v0.2.0" / "Utawave-v0.2.0-beta" → "0.2.0")。
    // 数字が無ければ空文字列。
    static juce::String normaliseVersion(const juce::String& raw);

    // latest が current より厳密に新しいセマンティックバージョンなら true。
    // ドット区切りを数値として比較する (0.10.0 > 0.9.0)。解析不能な latest / 同一 / 古い場合は
    // false (= 更新通知を出さない)。current が空なら latest を新しい扱い (true)。
    static bool isNewerVersion(const juce::String& latest, const juce::String& current);

    // 既定の version JSON URL。公式ビルドはコンパイル時 UTAWAVE_VERSION_URL で本番 URL を埋め込む
    // (無ければプレースホルダ。サーバー未稼働なら取得失敗 → 通知を出さない = 正しいフォールバック)。
    static juce::String defaultVersionUrl();
    // JSON に url が無い時に使うダウンロードページ既定値 (UTAWAVE_DOWNLOAD_PAGE_URL で差し替え可)。
    static juce::String defaultDownloadPageUrl();
    // 現在のアプリバージョン (ビルド時に埋め込まれた JUCE_APPLICATION_VERSION_STRING)。
    static juce::String currentAppVersion();
};
