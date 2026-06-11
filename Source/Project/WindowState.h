// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

// メインウィンドウのサイズを次回起動時に復元するためのグローバル設定。
// ~/Library/Application Support/Utawave/window.txt に保存される。
class WindowState
{
public:
    static constexpr int defaultWidth  = 1280;
    static constexpr int defaultHeight = 800;
    static constexpr int minWidth      = 1000;
    static constexpr int minHeight     = 600;

    int width  { defaultWidth };
    int height { defaultHeight };

    static juce::File getStoreFile();

    // 保存済みサイズを読み込む (無ければデフォルト)。最低サイズより小さい値はクランプする。
    static WindowState load();

    // 現在のサイズを保存する。
    void save() const;
};
