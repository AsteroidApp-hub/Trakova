// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

// GitHub の最新リリース / タグ情報。releases/latest または tags API の JSON から取り出す。
struct UpdateInfo
{
    juce::String version;   // tag から数字部分を抽出したもの (例 "0.2.0")。バージョン比較用
    juce::String tag;       // 生のタグ名 (例 "v0.2.0")。表示用
    juce::String pageUrl;   // クリックで開くダウンロード先 (リリースページ / リリース一覧)
};

// GitHub の最新版を取得し、現在のアプリバージョンと比較して新しい版があるかを判定する。
// Releases を優先し、正式リリースが無ければ Tags で判定する (タグだけ打ってあれば検知できる)。
// ネットワーク I/O はすべてバックグラウンドスレッドで行い、結果はメッセージスレッドへ戻す。
// インスタンスは持たず static 関数で完結する (呼び出し元の寿命管理を不要にするため・AdService と同じ作法)。
class UpdateChecker
{
public:
    // 取得をキャンセルするための共有フラグ。呼び出し元 (StartupComponent) と worker スレッドの
    // 双方が所有 (shared_ptr) し、true になると worker は結果配信を打ち切る。呼び出し元が取得完了前に
    // 破棄される時に立てることで、メッセージスレッドへ無駄な callAsync を投げない。
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    // 非同期で最新版を取得する。完了時 (キャンセルされなかった時) に cb を「メッセージスレッド」で
    // 1 回呼ぶ。まず apiBase + "/releases/latest" を見て、無ければ apiBase + "/tags" にフォールバックする。
    //   info : 取得できた最新版情報 (失敗時は空)。tags 経由の時は pageUrl に releasesPageUrl を入れる
    //   ok   : 取得・パースに成功したか (false = ネットワーク / パース失敗 / リリース・タグ無し)
    // 引数はすべて値コピーされ worker が所有するので、呼び出し元が取得完了前に破棄されても安全
    // (cb 側で SafePointer 等により自身の生存を確認すること)。バージョン比較は呼び出し元で行う。
    static void check(juce::String apiBase, juce::String releasesPageUrl, CancelFlag cancel,
                      std::function<void(UpdateInfo, bool)> cb);

    // ── ネットワーク非依存のテスト可能な純関数 ──
    // GitHub releases/latest API の JSON から tag_name / html_url を取り出す。
    // tag_name か html_url が欠ける場合もそのまま返すので、呼び出し側で空判定すること。
    static UpdateInfo parseLatestRelease(const juce::String& jsonText);

    // GitHub tags API (配列) の JSON から各タグ名 ("name") を API の順序で取り出す。
    static std::vector<juce::String> parseTags(const juce::String& jsonText);

    // タグ名の集合から最も新しいセマンティックバージョンのタグを返す (数字を含まないタグは無視)。
    // 候補が無ければ空文字列。
    static juce::String pickNewestTag(const std::vector<juce::String>& tags);

    // バージョン文字列から先頭の数字部分を抽出する (例 "v0.2.0" / "Trakova-v0.2.0-beta" → "0.2.0")。
    // 数字が無ければ空文字列。
    static juce::String normaliseVersion(const juce::String& raw);

    // latest が current より厳密に新しいセマンティックバージョンなら true。
    // ドット区切りを数値として比較する (0.10.0 > 0.9.0)。解析不能な latest / 同一 / 古い場合は
    // false (= 更新通知を出さない)。current が空なら latest を新しい扱い (true)。
    static bool isNewerVersion(const juce::String& latest, const juce::String& current);

    // 既定の API ベース URL (AsteroidApp-hub/Trakova)。"/releases/latest" / "/tags" を付けて使う。
    static juce::String defaultApiBase();
    // 既定のリリース一覧ページ (tags 経由フォールバック時のクリック先)。
    static juce::String defaultReleasesPageUrl();
    // 現在のアプリバージョン (ビルド時に埋め込まれた JUCE_APPLICATION_VERSION_STRING)。
    static juce::String currentAppVersion();
};
