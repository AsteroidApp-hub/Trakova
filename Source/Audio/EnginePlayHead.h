// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>
#include <cmath>

/**
    ホスト (AudioEngine) の再生位置・テンポ・拍子・ループ範囲をプラグインへ伝える AudioPlayHead。

    プラグインは processBlock 内で getPlayHead()->getPosition() を呼び、再生位置やテンポを取得する。
    EQ 等のサンプル処理だけで完結するプラグインはこれを使わないが、Melodyne (Transfer による
    取り込み位置の同期) やテンポ同期ディレイ/LFO など **transport 情報に依存するプラグインは
    これが無いと正しく動作しない** (Melodyne の場合、ホストが再生中であることと再生位置を
    取得できず、取り込んだ音声をタイムラインへ同期できない)。

    update() / getPosition() はともにオーディオスレッドから同期的に呼ばれる (update はブロック
    先頭、getPosition はその直後のプラグイン processBlock 内)。同一スレッド・逐次アクセスなので
    アトミックは不要。1 インスタンスを全トラック/マスターのチェーンで共有する (同一ブロック内は
    全プラグインが同じ位置を見る)。
*/
class EnginePlayHead : public juce::AudioPlayHead
{
public:
    // ブロック先頭で現在の位置情報をセットする (秒・サンプル・テンポ・拍子・ループ)。
    // ppqPosition は四分音符単位の累積拍 (Utawave のモデルは 1 拍 = 四分音符)。
    void update (juce::int64 timeInSamples, double timeInSeconds,
                 double bpm, double ppqPosition, double ppqOfLastBarStart,
                 int tsNum, int tsDen, bool playing, bool recording,
                 bool looping, double loopStartPpq, double loopEndPpq)
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setTimeInSamples (timeInSamples);
        info.setTimeInSeconds (timeInSeconds);
        info.setBpm (bpm);
        info.setPpqPosition (ppqPosition);
        info.setPpqPositionOfLastBarStart (ppqOfLastBarStart);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { tsNum, tsDen });
        info.setIsPlaying (playing);
        info.setIsRecording (recording);
        info.setIsLooping (looping);
        if (looping)
            info.setLoopPoints (juce::AudioPlayHead::LoopPoints { loopStartPpq, loopEndPpq });
        current = info;
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        return current;
    }

private:
    juce::AudioPlayHead::PositionInfo current;
};
