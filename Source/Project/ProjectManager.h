// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>
#include "../Tracks/TrackManager.h"
#include "../AppSettings.h"
#include "../UI/TimelineView.h"  // Marker

class PluginManager;
class PluginChain;

class ProjectManager
{
public:
    struct State
    {
        TrackManager*               trackManager   { nullptr };
        AppSettings*                appSettings    { nullptr };
        std::vector<Marker>*        markers        { nullptr };
        // Transport
        double*                     bpm            { nullptr };
        double*                     loopStartSecs  { nullptr };
        double*                     loopEndSecs    { nullptr };
        bool*                       loopActive     { nullptr };
        double*                     playheadSecs   { nullptr };
        // プラグイン復元用（無くても XML 保存・読込は動くが、その場合プラグインは復元されない）
        PluginManager*              pluginManager  { nullptr };
        double                      pluginSampleRate { 48000.0 };
        int                         pluginBlockSize  { 512 };
        // マスターチェーン（オプション。指定があれば保存/復元される）
        PluginChain*                masterChain    { nullptr };
        // タイムライン横ズーム (px / beat)。プロジェクト保存時の値で次回開いた時に復元する
        double*                     pixelsPerBeat  { nullptr };
        // load 時に見つからなかった音声ファイルの相対パスを格納する出力。
        // 呼び出し側で警告ダイアログを出すために使用する (nullptr なら収集しない)。
        std::vector<juce::String>*  missingFiles   { nullptr };
    };

    static juce::String fileExtension() { return ".uta"; }

    // 旧名 (Trakova) 時代の拡張子。XML ルートタグ互換は load() に残すが、保存・「開く」は常に新拡張子
    static juce::String legacyFileExtension() { return ".trakova"; }
    // 「開く」ダイアログ用ワイルドカード (新拡張子のみ)
    static juce::String openWildcard() { return "*" + fileExtension(); }

    // .uta ファイルへ書き出し
    static bool save(const juce::File& projectFile, const State& s);
    // .uta ファイルから読み込み（呼び出し側で TrackManager 等の状態を復元）
    static bool load(const juce::File& projectFile, State& s);
};
