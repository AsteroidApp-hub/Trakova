// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

class TrackManager;
struct AppSettings;

// プロジェクト内の MIDI クリップを Standard MIDI File (.mid, Type 1) として書き出す。
// MidiImporter の逆変換: クリップ内の「クリップ先頭からの秒」タイムスタンプを
// プロジェクトのテンポマップでティックへ変換し、テンポ / 拍子メタイベントを伴って
// 1 つのファイルにまとめる。
class MidiExporter
{
public:
    struct Result
    {
        bool         ok { false };
        juce::String error;
        int          trackCount { 0 };   // 実際に書き出した MIDI トラック数
        int          noteCount  { 0 };   // 書き出したノート総数
    };

    static Result save(const juce::File& dest,
                       const TrackManager& tracks,
                       const AppSettings& settings);
};
