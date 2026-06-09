// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Trakova — Localisation (多言語化) のユニットテスト
//
// アプリ文言は日本語をキーに tr(u8"...") → juce::translate で表示する。English モードでは
// englishTranslations テーブルを引く。テーブルのキーは末尾スペース・改行・エスケープまで
// 完全一致が要るため、ここで代表キーを固定して回帰 (キー drift / 訳抜け) を捕捉する。
//   ・install(English) で代表キーが英訳される (末尾/先頭スペース・埋め込み改行を含む)
//   ・未訳キーは日本語キーのままフォールバックする
//   ・install(Japanese) ではアプリキーは素通り (元の日本語)
//   ・displayName
// install は process グローバルな setCurrentMappings を呼ぶため、終了時に Japanese へ戻す。
// 永続化 (language.txt) はユーザ設定ファイルを触るので本テストでは呼ばない。
// expect は ASCII (メッセージのみ。u8 リテラルのキーは日本語可)。

#include <JuceHeader.h>
#include "../Source/Localisation.h"

namespace
{
class LocalisationTests : public juce::UnitTest
{
public:
    LocalisationTests() : juce::UnitTest("Localisation") {}

    void runTest() override
    {
        testEnglishTable();
        testFallback();
        testJapanesePassThrough();
        testDisplayName();

        // グローバル mappings を既定 (Japanese) に戻して後続テストへ影響させない
        Localisation::install(Localisation::Language::Japanese);
    }

    // ── English テーブル: 代表キーが正しく英訳される (空白/改行/エスケープ込み) ──
    void testEnglishTable()
    {
        beginTest("install(English): representative keys translate exactly");
        Localisation::install(Localisation::Language::English);

        expect(tr(u8"保存") == "Save", "plain key");
        expect(tr(u8"プラグイン管理") == "Plugin Manager", "menu key");
        expect(tr(u8"クロスフェードを描く") == "Create Crossfade", "edit menu key");
        expect(tr(u8"トラックを複製") == "Duplicate Track", "track menu key");
        // 末尾スペースまで一致が要る連結断片
        expect(tr(u8"テンポ: ") == "Tempo: ", "key with a trailing space");
        // 先頭スペースの断片 (複製サフィックス)
        expect(tr(u8" (コピー)") == " (copy)", "key with a leading space");
        // 埋め込み改行 (テーブルは \\n、ソースは実 LF) が一致する
        expect(tr(u8"このプロジェクトには未保存の変更があります。\n保存しますか?")
                   == "This project has unsaved changes.\nDo you want to save?",
               "key with an embedded newline");
    }

    // ── 未訳キーは日本語キーのままフォールバック ──
    void testFallback()
    {
        beginTest("untranslated keys fall back to the Japanese key itself");
        Localisation::install(Localisation::Language::English);
        const char* missing = u8"このキーは翻訳テーブルに存在しない12345";
        expect(tr(missing) == juce::String::fromUTF8(missing),
               "missing key returns the key unchanged");
    }

    // ── Japanese モードではアプリキーは素通り (元の日本語) ──
    void testJapanesePassThrough()
    {
        beginTest("install(Japanese): app keys pass through unchanged");
        Localisation::install(Localisation::Language::Japanese);
        expect(tr(u8"保存") == juce::String::fromUTF8(u8"保存"),
               "Japanese mode returns the original Japanese key");
        expect(tr(u8"プラグイン管理") == juce::String::fromUTF8(u8"プラグイン管理"),
               "Japanese mode passes app key through");
    }

    // ── displayName ──
    void testDisplayName()
    {
        beginTest("displayName returns the language label");
        expect(Localisation::displayName(Localisation::Language::English) == "English",
               "English label");
        expect(Localisation::displayName(Localisation::Language::Japanese)
                   == juce::String::fromUTF8(u8"日本語"),
               "Japanese label");
    }
};

static LocalisationTests localisationTests;
}
