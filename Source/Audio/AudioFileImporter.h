// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

class AudioFileImporter
{
public:
    AudioFileImporter(juce::AudioFormatManager& fmt) : formatManager(fmt) {}

    // 取り込み結果
    struct Result
    {
        bool        success    { false };
        juce::File  file;          // 実際に使用するファイル（リサンプル時はキャッシュ）
        double      durationSec { 0.0 };
        double      sampleRate { 0.0 };
        int         numChannels { 0 };
        bool        wasResampled { false };
        bool        cancelled   { false };  // onProgress が false を返して中断された
        juce::String errorMessage;
    };

    // 入力ファイルをプロジェクトSRと比較し、必要なら r8brain でリサンプルしたキャッシュを生成
    // 元ファイルは変更しない。SR一致時は元ファイルへの参照のみ返す。
    // outputBits: 32 = 32bit float（既定・ディザ不要）、24 = 24bit PCM + TPDFディザ
    // onProgress: リサンプル進捗 (0..1) を報告する任意コールバック。false を返すと中断する
    //             (success=false, cancelled=true で返る)。SR一致 (リサンプル無し) では呼ばれない。
    Result importFile(const juce::File& src, double projectSampleRate, int outputBits = 32,
                      std::function<bool(double)> onProgress = {});

    // プロジェクト連携用: 設定するとリサンプル出力先がここから取得される
    std::function<juce::File()> getCacheFolderCb;

    static juce::File getDefaultCacheFolder();
    juce::File getCacheFolder() const;

    // WAV ファイルを iXML / bext チャンクを取り除いて dst にコピーする。
    // (他 DAW で埋め込まれたテンポ情報・各種メタデータがプロジェクトに流入するのを防ぐ)
    // 戻り値 false = 失敗 (src が WAV でない場合や I/O エラー)。
    bool copyStrippingMetadata(const juce::File& src, const juce::File& dst,
                               juce::String& errorOut);

private:
    bool resampleToFile(const juce::File& src, const juce::File& dst,
                        double srcSr, double dstSr, int numChannels,
                        juce::AudioFormatReader& reader,
                        int outputBits,
                        juce::String& errorOut,
                        const std::function<bool(double)>& onProgress = {});

    juce::AudioFormatManager& formatManager;
};
