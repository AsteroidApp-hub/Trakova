// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "UpdateChecker.h"

namespace
{
    // 接続タイムアウト。到達不能なサーバーへの接続でスレッドが長く居座らないよう抑えめに。
    constexpr int kConnectTimeoutMs = 5000;

    // GitHub API へ GET し、ボディ文字列を返す (失敗時は空)。User-Agent は GitHub API で必須。
    juce::String httpGet(const juce::String& url)
    {
        juce::URL u(url);
        const auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                              .withConnectionTimeoutMs(kConnectTimeoutMs)
                              .withExtraHeaders("User-Agent: Trakova\r\n"
                                                "Accept: application/vnd.github+json");
        if (auto in = u.createInputStream(opts))
            return in->readEntireStreamAsString();
        return {};
    }

    // バージョン文字列を数値コンポーネントの配列にする ("0.10.0" → {0,10,0})。
    std::vector<int> versionParts(const juce::String& v)
    {
        std::vector<int> parts;
        const auto core = UpdateChecker::normaliseVersion(v);
        if (core.isEmpty()) return parts;

        juce::StringArray toks;
        toks.addTokens(core, ".", "");
        for (auto& t : toks)
            parts.push_back(t.getIntValue());
        return parts;
    }
}

void UpdateChecker::check(juce::String apiBase, juce::String releasesPageUrl, CancelFlag cancel,
                          std::function<void(UpdateInfo, bool)> cb)
{
    // 引数はすべて値コピーされ、デタッチスレッドが所有する。
    // run 中に呼び出し元 (StartupComponent) のメンバへは一切触れない。
    juce::Thread::launch([apiBase, releasesPageUrl, cancel, cb = std::move(cb)]
    {
        const auto cancelled = [&cancel] { return cancel != nullptr && cancel->load(); };

        UpdateInfo info;
        bool ok = false;

        // 1) Releases を優先 (正式リリースならダウンロードページ html_url が得られる)。
        if (! cancelled())
        {
            info = parseLatestRelease(httpGet(apiBase + "/releases/latest"));
            ok   = info.tag.isNotEmpty() && info.pageUrl.isNotEmpty();
        }

        // 2) リリースが無ければ Tags にフォールバック (タグだけ打ってあれば検知)。
        if (! ok && ! cancelled())
        {
            const auto newest = pickNewestTag(parseTags(httpGet(apiBase + "/tags")));
            if (newest.isNotEmpty())
            {
                info.tag     = newest;
                info.version = normaliseVersion(newest);
                info.pageUrl = releasesPageUrl;   // タグにファイルは無いのでリリース一覧へ誘導
                ok           = info.pageUrl.isNotEmpty();
            }
        }

        if (cancelled())
            return;   // 呼び出し元が消えていれば結果は捨てる (callAsync すら投げない)

        // 結果はメッセージスレッドへ。cb は SafePointer で自身の生存を確認する想定。
        juce::MessageManager::callAsync([cb, info = std::move(info), ok]() mutable
        {
            if (cb)
                cb(std::move(info), ok);
        });
    });
}

UpdateInfo UpdateChecker::parseLatestRelease(const juce::String& jsonText)
{
    UpdateInfo info;

    const auto v = juce::JSON::parse(jsonText);
    if (auto* obj = v.getDynamicObject())
    {
        info.tag     = obj->getProperty("tag_name").toString().trim();
        info.pageUrl = obj->getProperty("html_url").toString().trim();
        info.version = normaliseVersion(info.tag);
    }
    return info;
}

std::vector<juce::String> UpdateChecker::parseTags(const juce::String& jsonText)
{
    std::vector<juce::String> names;

    const auto v = juce::JSON::parse(jsonText);
    if (auto* arr = v.getArray())
        for (auto& e : *arr)
            if (auto* o = e.getDynamicObject())
            {
                const auto n = o->getProperty("name").toString().trim();
                if (n.isNotEmpty())
                    names.push_back(n);
            }
    return names;
}

juce::String UpdateChecker::pickNewestTag(const std::vector<juce::String>& tags)
{
    juce::String best;
    for (auto& t : tags)
    {
        if (normaliseVersion(t).isEmpty())          // 数字を含まないタグは無視
            continue;
        if (best.isEmpty() || isNewerVersion(t, best))
            best = t;
    }
    return best;
}

juce::String UpdateChecker::normaliseVersion(const juce::String& raw)
{
    const auto s = raw.trim();

    // 先頭の数字位置を探す ("v0.2.0" / "Trakova-v0.2.0" のような接頭辞を飛ばす)。
    int start = -1;
    for (int i = 0; i < s.length(); ++i)
        if (juce::CharacterFunctions::isDigit(s[i])) { start = i; break; }
    if (start < 0) return {};

    // 数字とドットの連続を取り出す ("0.2.0-beta" → "0.2.0")。
    int end = start;
    while (end < s.length()
           && (juce::CharacterFunctions::isDigit(s[end]) || s[end] == '.'))
        ++end;

    auto core = s.substring(start, end);
    while (core.endsWithChar('.'))           // 末尾ドットを除去 ("1.2." → "1.2")
        core = core.dropLastCharacters(1);
    return core;
}

bool UpdateChecker::isNewerVersion(const juce::String& latest, const juce::String& current)
{
    const auto a = versionParts(latest);
    const auto b = versionParts(current);

    if (a.empty()) return false;   // 最新版が解析不能なら通知しない
    if (b.empty()) return true;    // 現在版が不明なら最新を新しい扱い

    const size_t n = juce::jmax(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
    {
        const int ai = i < a.size() ? a[i] : 0;
        const int bi = i < b.size() ? b[i] : 0;
        if (ai != bi) return ai > bi;
    }
    return false;   // 同一バージョン
}

juce::String UpdateChecker::defaultApiBase()
{
    return "https://api.github.com/repos/AsteroidApp-hub/Trakova";
}

juce::String UpdateChecker::defaultReleasesPageUrl()
{
    return "https://github.com/AsteroidApp-hub/Trakova/releases";
}

juce::String UpdateChecker::currentAppVersion()
{
    return JUCE_APPLICATION_VERSION_STRING;
}
