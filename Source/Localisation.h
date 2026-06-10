// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

namespace Localisation
{
    enum class Language { Japanese, English };

    // 指定言語の LocalisedStrings をインストールする。
    //  - Japanese: JUCE 標準 UI の英語文字列を日本語化 (アプリ文言は元の日本語のまま)
    //  - English : アプリの日本語キーを英語へ翻訳 (JUCE 標準文言は英語のまま)
    void install(Language lang);

    // 後方互換 (= install(Language::Japanese))
    void installJapanese();

    // アプリ全体の言語設定の永続化 (~/Library/Utawave/language.txt)
    Language getSavedLanguage();
    void     saveLanguage(Language lang);

    // 言語表示名 (設定 UI 用)
    juce::String displayName(Language lang);
}

// アプリ UI 文言の翻訳ヘルパ。日本語をキーにして juce::translate に通す。
//   例: tr(u8"保存")  → 日本語モードでは「保存」、英語モードでは "Save"
inline juce::String tr(const char* utf8Japanese)
{
    return juce::translate(juce::String::fromUTF8(utf8Japanese));
}
