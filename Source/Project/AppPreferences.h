// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

// アプリ全体の UI 設定 (プロジェクトに依存しないグローバル設定)。
// 言語 (Localisation) や ウィンドウサイズ (WindowState) と同じく、プロジェクトファイル
// (.utawave) ではなく ~/Library/Application Support/Utawave/app_prefs.xml に保存する。
class AppPreferences
{
public:
    // 「MIDI を書き出す」メニュー項目を表示するか (既定: 非表示)。
    // 普段はあまり使わない機能のため、設定で ON にした時だけメニューに出す。
    bool showMidiExportMenu { false };

    // 起動画面に広告枠を表示するか (既定: 表示)。公式ビルド内でのユーザー個別 ON/OFF。
    // ただし実効表示はコンパイル時マスタースイッチ (adsCompiledIn) との AND になる。
    bool showAds { true };

    // 広告機能のコンパイル時マスタースイッチ。公開ソースの既定は OFF (起動画面は 2 列・通信なし)。
    // 公式配布ビルドのみ CMake の UTAWAVE_ADS_ENABLED=1 で有効化する (詳細は CMakeLists / CLAUDE.md)。
    static bool adsCompiledIn();

    // 実効的に起動画面へ広告を出すか (= コンパイル時に有効 かつ ユーザーが showAds を ON)。
    bool adsEffective() const { return adsCompiledIn() && showAds; }

    static juce::File getStoreFile();
    static AppPreferences load();
    void save() const;
};
