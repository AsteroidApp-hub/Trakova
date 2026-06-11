// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

/**
    Standard MIDI File (.mid) を読み込んでトラック単位のシーケンスに分解する。
    JUCE の juce::MidiFile を利用し、テンポマップを使ってティック→秒に変換する。
*/
class MidiImporter
{
public:
    struct ImportedTrack
    {
        juce::String              name;            // トラック名（Meta Track Name または "Track N"）
        juce::MidiMessageSequence sequence;        // タイムスタンプは秒
        int                       primaryChannel { 0 };  // 主に使われる MIDI ch (0-based)
        int                       numNoteOnEvents { 0 }; // ノート数（チェックUI 用）
        bool                      isDrum { false };       // ch10 (0-based 9) なら true
    };

    struct ImportResult
    {
        bool ok { false };
        juce::String error;
        std::vector<ImportedTrack> tracks;
        double endTimeSecs { 0.0 };  // ファイル全体の長さ（秒）
        // テンポ・拍子（先頭値）
        double initialBpm  { 120.0 };
        int    meterNumerator   { 4 };
        int    meterDenominator { 4 };
        // 途中変化を含むテンポマップ / 拍子マップ
        // (initialBpm 以外の変化点を 1 件目以降に格納する)
        std::vector<std::pair<double /*timeSec*/, double /*bpm*/>>      tempoChanges;
        std::vector<std::pair<int    /*barIndex*/, std::pair<int,int>>> meterChanges;
        // マーカー（時刻秒 -> 名前）
        std::vector<std::pair<double, juce::String>> markers;
    };

    static ImportResult load(const juce::File& smfFile);
};
