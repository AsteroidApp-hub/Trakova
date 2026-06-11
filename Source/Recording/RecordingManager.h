// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include "../Tracks/TrackManager.h"

class AudioEngine;

class RecordingManager
{
public:
    RecordingManager(AudioEngine& engine, TrackManager& tracks,
                     juce::AudioFormatManager& fmt);
    ~RecordingManager();

    // recStartSec: 実際に録音を開始するタイムライン位置
    // playFromSec: 再生（カウントイン/プレロール込み）を開始する位置（recStart 以下）
    // loopRecording: ループ録音モード（停止時にループ区間ごとに Take レーンへスライス配置）
    // loopStart/loopEnd: ループ範囲（loopRecording=true 時のみ使用）
    bool startRecording(double recStartSec, double playFromSec = 0.0,
                        bool loopRecording = false,
                        double loopStart = 0.0, double loopEnd = 0.0);
    void stopRecording(double endPositionSeconds);
    bool isRecording() const { return recording; }

    // 直前の startRecording でファイル/writer 作成に失敗したトラック名
    // (空でなければ呼び出し側がユーザーへ通知する。silent failure 防止)
    const juce::StringArray& getLastStartFailures() const { return lastStartFailures; }

    // ループラップ通知（録音中に呼ぶ。完了したループ周回をリアルタイムで Take レーンへ配置）
    void onLoopWrap();

    // 遡及録音
    bool startRetrospective(Track* targetTrack, double playStartSec);
    void stopRetrospective(bool commit, double playEndSec);
    bool hasRetrospective() const { return retroActive; }

    // Recordings folder（コールバック未設定時のフォールバック）
    juce::File getRecordingsFolder() const;
    // プロジェクト連携用: 設定すると録音先がここから取得される
    std::function<juce::File()> getAudioFolder;

    // 録音 WAV のビット深度 (16/24 = PCM, 32 = float)
    void setBitDepth(int bits)
    {
        recordingBitDepth = (bits == 16 || bits == 24 || bits == 32) ? bits : 24;
    }
    int  getBitDepth() const { return recordingBitDepth; }

private:
    juce::File createRecordingFile(const juce::String& trackName) const;

    AudioEngine&                      audioEngine;
    TrackManager&                     trackManager;
    juce::AudioFormatManager&         formatManager;

    juce::TimeSliceThread             backgroundThread { "RecordingThread" };

    struct ActiveRecording
    {
        Track* track { nullptr };
        juce::File file;
        double startPosition { 0.0 };  // recStart (count-in/preroll を含まない実書き出し開始)
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        // ループ録音
        bool loopRec { false };
        double loopStart { 0.0 };
        double loopEnd   { 0.0 };
        juce::int64 wallStartMs { 0 };
        // リアルタイムに作成済みのテイク数（ループラップ毎に増える）
        int  takesAddedRealtime { 0 };
        // 採番開始のレーン index
        int  takeStartLaneIdx   { 1 };
        // リアルタイムに追加された AudioClip 群（停止時にサムネイルを再読込する）
        std::vector<class AudioClip*> realtimeClips;
    };

    std::vector<ActiveRecording> activeRecordings;
    juce::StringArray lastStartFailures;
    bool recording { false };
    int  recordingBitDepth { 24 };

    // 遡及録音
    Track*     retroTrack    { nullptr };
    juce::File retroFile;
    double     retroPlayStart { 0.0 };
    bool       retroActive   { false };
    bool       retroStereo   { false };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> retroWriter;

    // 遡及録音 → 通常録音 (Punch from Retro)
    bool       punchFromRetro { false };
    double     punchInRecStart { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingManager)
};
