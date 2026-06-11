// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

/**
    内蔵シンセ。波形（Sine/Saw/Square）+ ADSR エンベロープ。
    トラック単位で 1 インスタンス保持し、AudioEngine から processBlock で駆動する。
    juce::Synthesiser ではなくシンプルな自前実装にして依存を減らす。
*/
class InternalSynth
{
public:
    enum class Waveform { Sine = 0, Saw = 1, Square = 2 };

    InternalSynth();

    void prepareToPlay(double sampleRate, int maxBlockSize);
    void releaseResources();

    void setWaveform(int w)         { waveform = (Waveform)juce::jlimit(0, 2, w); }

    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    // Pitch Wheel: pw14 は 0..16383 (8192 = センター)。
    // Range ±2 半音 (一般的なデフォルト) で全ボイスの再生周波数を変化させる。
    void setPitchWheel(int pw14);

    // numChannels >= 1。chan 0/1 に同じ信号を加算する（モノ→出力先がステレオなら両 ch）
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

private:
    enum class Stage { Idle, Attack, Hold, Release };

    struct Voice
    {
        int    note     { -1 };
        Stage  stage    { Stage::Idle };
        float  envelope { 0.0f };
        float  velocity { 1.0f };
        double phase    { 0.0 };   // 0..1
        double basePhaseInc { 0.0 };  // ピッチベンド適用前の素の phaseInc
        double phaseInc { 0.0 };       // 実際に使う phaseInc (basePhaseInc * pitchBendRatio)
    };

    // ピッチベンド倍率 (1.0 = センター)。setPitchWheel で更新。
    double pitchBendRatio { 1.0 };
    static constexpr double kPitchBendRangeSemis = 2.0;  // ±2 半音 (一般的)

    static constexpr int kMaxVoices = 16;
    Voice voices[kMaxVoices];

    Waveform waveform { Waveform::Sine };
    // 固定: アタック 1ms（プチ音防止用）、リリース 50ms
    static constexpr float attackS  { 0.001f };
    static constexpr float releaseS { 0.050f };

    double sr { 48000.0 };
    // sr + 固定 attackS/releaseS から prepareToPlay で 1 度だけ算出し、processVoice では読むだけにする
    float aStep { 1.0f };
    float rStep { 1.0f };

    void processVoice(Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    // dt = 1サンプル当たりの位相増分 (PolyBLEP 用)
    float oscillate(Waveform w, double phase, double dt) const;
};
