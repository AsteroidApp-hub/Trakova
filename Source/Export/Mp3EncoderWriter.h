// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

/**
    AudioBuffer<float> を直接 MP3 ファイルへエンコードする最小限のヘルパ。
    内蔵 libmp3lame (Source/ThirdParty/lame) を使用するため、外部依存はない。
*/
class Mp3EncoderWriter
{
public:
    /** バッファを MP3 として書き出す。
        @param buffer       1ch または 2ch の浮動小数バッファ（[-1, 1] スケール）
        @param sampleRate   入力サンプリングレート
        @param bitrateKbps  目標ビットレート (32〜320)
        @param outFile      出力先 (.mp3)
        @param errorOut     失敗時のエラーメッセージ
        @param shouldCancel true を返すとエンコードを中断し false を返す (errorOut は空のまま)
        @returns 成功なら true
    */
    static bool encodeBuffer(const juce::AudioBuffer<float>& buffer,
                             double sampleRate,
                             int bitrateKbps,
                             const juce::File& outFile,
                             juce::String* errorOut = nullptr,
                             std::function<bool()> shouldCancel = {});
};
