// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include <rubberband/RubberBandStretcher.h>
#include <vector>
#include <cmath>

// オフライン ピッチシフター
// Rubber Band Library R3 (高品質エンジン) を使用。
// オフラインの 2-pass 処理 (study → process/retrieve) で長さは保ったままピッチを変更する。
namespace PitchShifter
{
    // semitones: ±12 程度まで実用品質、それ以上は劣化が目立つ
    // outputBits: 16 / 24 / 32 (32 = 32bit float)
    // onProgress: 0.0〜1.0 の進捗を通知 (study / process 両パスを連結した擬似的な値)
    inline bool process(const juce::File& inputFile,
                        const juce::File& outputFile,
                        juce::AudioFormatManager& fmt,
                        double semitones,
                        int outputBits = 32,
                        std::function<void(double)> onProgress = {})
    {
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(inputFile));
        if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) return false;

        const int numCh = (int) reader->numChannels;
        const juce::int64 totalSamples64 = reader->lengthInSamples;
        if (totalSamples64 > (juce::int64) std::numeric_limits<int>::max()) return false;
        const int totalSamples = (int) totalSamples64;
        const double sampleRate = reader->sampleRate;

        // 入力をメモリへ展開
        juce::AudioBuffer<float> inBuf(numCh, totalSamples);
        reader->read(&inBuf, 0, totalSamples, 0, true, true);

        // ── Rubber Band 設定 ──
        const double pitchScale = std::pow(2.0, semitones / 12.0);
        // R3 (Finer) + オフライン + 高品質トランジェント検出
        const RubberBand::RubberBandStretcher::Options options =
            RubberBand::RubberBandStretcher::OptionProcessOffline
          | RubberBand::RubberBandStretcher::OptionEngineFiner
          | RubberBand::RubberBandStretcher::OptionTransientsCrisp
          | RubberBand::RubberBandStretcher::OptionPhaseLaminar
          | RubberBand::RubberBandStretcher::OptionWindowStandard
          | RubberBand::RubberBandStretcher::OptionFormantShifted
          | RubberBand::RubberBandStretcher::OptionPitchHighQuality
          | RubberBand::RubberBandStretcher::OptionChannelsApart;

        RubberBand::RubberBandStretcher stretcher(
            (size_t) sampleRate,
            (size_t) numCh,
            options,
            /* initialTimeRatio  */ 1.0,
            /* initialPitchScale */ pitchScale);

        stretcher.setExpectedInputDuration((size_t) totalSamples);
        // 念のため明示
        stretcher.setPitchScale(pitchScale);

        constexpr int blockSize = 1024;

        // チャンネル別ポインタ配列を用意
        std::vector<const float*> inPtrs((size_t) numCh, nullptr);
        std::vector<float*>       outPtrs((size_t) numCh, nullptr);

        // ── Study パス (オフライン専用 / 全体を一度走査) ──
        // 全体進捗の前半 (0〜0.5) に割り当てる
        for (int pos = 0; pos < totalSamples; pos += blockSize)
        {
            const int n = juce::jmin(blockSize, totalSamples - pos);
            for (int ch = 0; ch < numCh; ++ch)
                inPtrs[(size_t) ch] = inBuf.getReadPointer(ch, pos);
            stretcher.study(inPtrs.data(), (size_t) n, pos + n >= totalSamples);
            if (onProgress) onProgress(0.5 * (double)(pos + n) / (double) totalSamples);
        }

        // ── Process / Retrieve パス ──
        // 出力長は基本的に入力と同じ (timeRatio = 1.0)。 余裕を持って同じサイズを用意。
        juce::AudioBuffer<float> outBuf(numCh, totalSamples + blockSize);
        outBuf.clear();
        int outWritePos = 0;

        auto retrieveAvailable = [&]()
        {
            while (true)
            {
                const int avail = stretcher.available();
                if (avail <= 0) break;
                const int toGet = juce::jmin(avail, blockSize);
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const int writeAt = juce::jmin(outWritePos,
                                                    outBuf.getNumSamples() - 1);
                    outPtrs[(size_t) ch] = outBuf.getWritePointer(ch, writeAt);
                }
                const size_t got = stretcher.retrieve(outPtrs.data(), (size_t) toGet);
                outWritePos += (int) got;
                if ((int) got < toGet) break;
                if (outWritePos >= outBuf.getNumSamples()) break;
            }
        };

        for (int pos = 0; pos < totalSamples; pos += blockSize)
        {
            const int n = juce::jmin(blockSize, totalSamples - pos);
            for (int ch = 0; ch < numCh; ++ch)
                inPtrs[(size_t) ch] = inBuf.getReadPointer(ch, pos);
            stretcher.process(inPtrs.data(), (size_t) n, pos + n >= totalSamples);
            retrieveAvailable();
            if (onProgress) onProgress(0.5 + 0.5 * (double)(pos + n) / (double) totalSamples);
        }
        // 最後の残りも取り出す
        retrieveAvailable();
        if (onProgress) onProgress(1.0);

        // 開始 latency 分を捨てる (オフライン R3 では既にスキップ済みのこともあるが念のため)
        const int delay = (int) stretcher.getStartDelay();
        const int finalLen = juce::jmax(0, juce::jmin(totalSamples, outWritePos - delay));
        juce::AudioBuffer<float> finalBuf(numCh, finalLen);
        for (int ch = 0; ch < numCh; ++ch)
            finalBuf.copyFrom(ch, 0, outBuf, ch, delay, finalLen);

        // ── WAV 書き出し ──
        auto stream = std::make_unique<juce::FileOutputStream>(outputFile);
        if (!stream->openedOk()) return false;
        juce::WavAudioFormat wav;
        const bool useFloat = (outputBits >= 32);
        auto opts = juce::AudioFormatWriterOptions{}
                        .withSampleRate(sampleRate)
                        .withNumChannels((juce::uint32) numCh)
                        .withBitsPerSample(outputBits)
                        .withSampleFormat(useFloat
                            ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                            : juce::AudioFormatWriterOptions::SampleFormat::integral);
        std::unique_ptr<juce::OutputStream> outStream = std::move(stream);
        auto writer = wav.createWriterFor(outStream, opts);
        if (writer == nullptr) return false;
        writer->writeFromAudioSampleBuffer(finalBuf, 0, finalBuf.getNumSamples());
        writer->flush();
        return true;
    }
}
