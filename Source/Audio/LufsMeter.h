// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <limits>

// ITU-R BS.1770-4 / EBU R128 簡易実装
// K-weighting (Pre-filter + RLB) → 400ms ブロック (75% オーバーラップ)
// → 絶対ゲート (-70 LUFS) → 相対ゲート (-10 LU) → Integrated LUFS
namespace LufsMeter
{
    struct Biquad
    {
        double b0 { 0 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };
        double z1 { 0 }, z2 { 0 };

        void reset() { z1 = z2 = 0; }

        double process(double x)
        {
            // Transposed Direct Form II
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        // RBJ Audio EQ Cookbook: high-shelf
        static Biquad makeHighShelf(double fc, double sr, double Q, double gainDb)
        {
            Biquad bq;
            const double A     = std::pow(10.0, gainDb / 40.0);
            const double w     = 2.0 * juce::MathConstants<double>::pi * fc / sr;
            const double cw    = std::cos(w);
            const double sw    = std::sin(w);
            const double alpha = sw / (2.0 * Q);
            const double sA    = std::sqrt(A);
            const double a0    = (A + 1.0) - (A - 1.0) * cw + 2.0 * sA * alpha;
            bq.b0 = A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * sA * alpha) / a0;
            bq.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
            bq.b2 = A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * sA * alpha) / a0;
            bq.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
            bq.a2 = ((A + 1.0) - (A - 1.0) * cw - 2.0 * sA * alpha) / a0;
            return bq;
        }

        // RBJ Audio EQ Cookbook: high-pass
        static Biquad makeHighPass(double fc, double sr, double Q)
        {
            Biquad bq;
            const double w     = 2.0 * juce::MathConstants<double>::pi * fc / sr;
            const double cw    = std::cos(w);
            const double sw    = std::sin(w);
            const double alpha = sw / (2.0 * Q);
            const double a0    = 1.0 + alpha;
            bq.b0 =  (1.0 + cw) * 0.5 / a0;
            bq.b1 = -(1.0 + cw)       / a0;
            bq.b2 =  (1.0 + cw) * 0.5 / a0;
            bq.a1 = -2.0 * cw         / a0;
            bq.a2 =  (1.0 - alpha)    / a0;
            return bq;
        }
    };

    class Measurer
    {
    public:
        Measurer(double sr, int numCh)
            : numChannels(juce::jmax(1, numCh)),
              chunkSamples(juce::jmax(1, (int)(sr * 0.1)))   // 100ms チャンク
        {
            stage1.resize((size_t) numChannels);
            stage2.resize((size_t) numChannels);
            // ITU-R BS.1770-4 K-weighting パラメータ
            for (int ch = 0; ch < numChannels; ++ch)
            {
                stage1[(size_t) ch] = Biquad::makeHighShelf(1681.974450, sr, 0.70710753, 3.99984385);
                stage2[(size_t) ch] = Biquad::makeHighPass (  38.135470, sr, 0.50032705);
            }
            curChunkSum.assign((size_t) numChannels, 0.0);
        }

        // クリップ間でフィルタ状態をリセット（無音区間を挟む場合は推奨）
        void resetFilters()
        {
            for (auto& bq : stage1) bq.reset();
            for (auto& bq : stage2) bq.reset();
        }

        // 線形ゲイン (clip gain 等) を掛けながら処理
        void processBuffer(const juce::AudioBuffer<float>& buf, float gain = 1.0f)
        {
            const int nIn = juce::jmin(buf.getNumChannels(), numChannels);
            const int n   = buf.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < nIn; ++ch)
                {
                    double x = (double) buf.getReadPointer(ch)[i] * (double) gain;
                    x = stage1[(size_t) ch].process(x);
                    x = stage2[(size_t) ch].process(x);
                    curChunkSum[(size_t) ch] += x * x;
                }
                ++samplesInChunk;
                if (samplesInChunk >= chunkSamples)
                {
                    chunks.push_back(curChunkSum);
                    curChunkSum.assign((size_t) numChannels, 0.0);
                    samplesInChunk = 0;
                }
            }
        }

        // データ不足 / 全部ゲートで弾かれた場合は -infinity
        double getIntegratedLufs() const
        {
            const int chunksPerBlock = 4;  // 400ms = 4 × 100ms
            if ((int) chunks.size() < chunksPerBlock)
                return -std::numeric_limits<double>::infinity();

            // 各 400ms ブロックの mean-square (全チャンネル合計, 重み=1.0)
            const int numBlocks = (int) chunks.size() - chunksPerBlock + 1;
            std::vector<double> blockMSq((size_t) numBlocks, 0.0);
            const double blockSamples = (double)(chunksPerBlock * chunkSamples);
            for (int b = 0; b < numBlocks; ++b)
            {
                double sumSq = 0.0;
                for (int k = 0; k < chunksPerBlock; ++k)
                    for (double v : chunks[(size_t)(b + k)])
                        sumSq += v;
                blockMSq[(size_t) b] = sumSq / blockSamples;
            }

            // 絶対ゲート: L_block > -70 LUFS  ⇔  meanSquare > 10^((-70+0.691)/10)
            const double absMSqTh = std::pow(10.0, (-70.0 + 0.691) / 10.0);
            std::vector<double> gated;
            gated.reserve(blockMSq.size());
            for (double m : blockMSq)
                if (m > absMSqTh) gated.push_back(m);
            if (gated.empty()) return -std::numeric_limits<double>::infinity();

            // 相対ゲート: L_block > L_ungated - 10 LU  ⇔  m > meanGated * 0.1
            double meanGated = 0.0;
            for (double m : gated) meanGated += m;
            meanGated /= (double) gated.size();
            const double relMSqTh = meanGated * 0.1;

            double sum = 0.0; int count = 0;
            for (double m : gated)
                if (m > relMSqTh) { sum += m; ++count; }
            if (count == 0) return -std::numeric_limits<double>::infinity();

            const double finalMSq = sum / (double) count;
            return -0.691 + 10.0 * std::log10(finalMSq);
        }

    private:
        int    numChannels;
        int    chunkSamples;

        std::vector<Biquad> stage1;
        std::vector<Biquad> stage2;

        std::vector<double>               curChunkSum;
        int                               samplesInChunk { 0 };
        std::vector<std::vector<double>>  chunks;
    };

    // 単一ファイルセグメントを測定 (gain = 線形, デフォルト 1.0)
    inline double measureFileSegment(const juce::File& file,
                                       juce::AudioFormatManager& fmt,
                                       double fileOffset, double duration,
                                       float gain = 1.0f)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
        if (!reader || reader->sampleRate <= 0.0 || reader->numChannels <= 0)
            return -std::numeric_limits<double>::infinity();

        const double sr      = reader->sampleRate;
        const int    numCh   = (int) reader->numChannels;
        const juce::int64 startS  = (juce::int64) juce::jmax(0.0, fileOffset * sr);
        const juce::int64 endByD  = (duration > 0.0)
                                     ? (juce::int64)((fileOffset + duration) * sr)
                                     : reader->lengthInSamples;
        const juce::int64 endS    = juce::jmin(reader->lengthInSamples, endByD);
        if (endS <= startS) return -std::numeric_limits<double>::infinity();

        Measurer m(sr, numCh);

        const int chunk = juce::jmax(1024, (int)(sr * 0.05));   // 50ms 読み出し
        juce::AudioBuffer<float> buf(numCh, chunk);
        juce::int64 pos = startS;
        while (pos < endS)
        {
            const int n = (int) juce::jmin((juce::int64) chunk, endS - pos);
            buf.clear();
            reader->read(&buf, 0, n, pos, true, true);
            juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), numCh, 0, n);
            m.processBuffer(view, gain);
            pos += n;
        }

        return m.getIntegratedLufs();
    }
}
