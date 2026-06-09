// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <functional>

// BPM 検出 (多倍音コム + メトリカル・ダブリング):
//  コムは「基音の倍音系列が揃うほど高い」ため、ピーク最大は常に真テンポ「以下」
//  (真テンポか、その半/サブ周期) に出る。そこで:
//   1) コム最大を起点 (base) に取り、
//   2) ×2 (なければ ×3) した方のコムが起点の α 倍以上なら、その速いテンポへ歩進する
//      (= 速い拍も同程度に裏付けられていれば、それが真の拍。裏付けが落ちれば起点が基音)。
//  これで「半テンポ/倍テンポ」誤検出を構造的に解消する。中心テンポへ寄せる事前分布は
//  使わない (真テンポが 60〜200 のどこでも信号に素直に従うため)。
//  最終整数は 5ms 包絡のコムで近傍を直接評価して確定 (off-by-one 解消)。
//
//  読み込みは 1 回: 5ms RMS エネルギー → 隣接 2 個を RMS 合成して 10ms も得る (2 解像度)。
//  オクターブ判定は 10ms (安定)、整数確定は 5ms (高分解能)。
namespace BpmDetector
{
    namespace detail
    {
        // 正のエネルギー差分 → 対数圧縮 → ±1s 局所デトレンド (ゼロ平均化)
        inline std::vector<float> buildOnsetZeroMean(const std::vector<float>& energy, double hopSecs)
        {
            const int n = (int) energy.size();
            std::vector<float> onset((size_t) n, 0.0f);
            for (int i = 1; i < n; ++i)
            {
                const float diff = energy[(size_t) i] - energy[(size_t) i - 1];
                onset[(size_t) i] = juce::jmax(0.0f, std::log1p(diff * 50.0f));
            }
            std::vector<float> oz((size_t) n, 0.0f);
            std::vector<double> pref((size_t) n + 1, 0.0);
            for (int i = 0; i < n; ++i)
                pref[(size_t) i + 1] = pref[(size_t) i] + (double) onset[(size_t) i];
            const int W = juce::jmax(1, (int) std::round(1.0 / hopSecs)); // ±1s 半窓
            for (int i = 0; i < n; ++i)
            {
                const int a = juce::jmax(0, i - W);
                const int b = juce::jmin(n, i + W + 1);
                const double lm = (pref[(size_t) b] - pref[(size_t) a]) / (double)(b - a);
                oz[(size_t) i] = onset[(size_t) i] - (float) lm;
            }
            return oz;
        }
    }

    // onProgress: 0..1 の進捗を渡す。false を返すとキャンセルとして 0.0 を返す (任意)。
    inline double detect(const juce::File& file,
                         juce::AudioFormatManager& fmt,
                         double fileOffset = 0.0,
                         double duration   = 0.0,
                         const std::function<bool(double)>& onProgress = {})
    {
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
        if (!reader || reader->sampleRate <= 0.0 || reader->numChannels <= 0) return 0.0;

        const double sr  = reader->sampleRate;
        const juce::int64 startSample = (juce::int64) juce::jmax(0.0, fileOffset * sr);
        const juce::int64 endByDur    = (duration > 0.0)
                                          ? (juce::int64)((fileOffset + duration) * sr)
                                          : reader->lengthInSamples;
        const juce::int64 endSample   = juce::jmin(reader->lengthInSamples, endByDur);
        if (endSample <= startSample) return 0.0;

        // 5ms ホップ。実サンプル数からの実ホップ秒で BPM 換算 (SR 依存の丸め誤差を排除)。
        const int    hop5     = juce::jmax(64, (int) std::round(sr * 0.005));
        const double hop5Secs = (double) hop5 / sr;
        const int    nw5      = (int)((endSample - startSample) / hop5);
        if (nw5 < 400) return 0.0;  // 2 秒未満は推定不能

        // 1) 5ms RMS エネルギー (ファイル読み込みは 1 回)
        std::vector<float> energy5((size_t) nw5, 0.0f);
        {
            juce::AudioBuffer<float> buf((int) reader->numChannels, hop5);
            for (int w = 0; w < nw5; ++w)
            {
                // ファイル読み込みが処理時間の大半。ここで進捗報告 / キャンセル判定する
                if (onProgress && (w & 1023) == 0 && ! onProgress((double) w / (double) nw5))
                    return 0.0;
                buf.clear();
                reader->read(&buf, 0, hop5, startSample + (juce::int64) w * hop5, true, true);
                float sum = 0.0f;
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                {
                    const float* d = buf.getReadPointer(ch);
                    for (int i = 0; i < hop5; ++i) sum += d[i] * d[i];
                }
                energy5[(size_t) w] = std::sqrt(sum / (float)(hop5 * buf.getNumChannels()));
            }
        }
        // 隣接 2 個を RMS 合成 → 10ms エネルギー (true 10ms 読みと数値的に一致)
        const int    nw10      = nw5 / 2;
        const double hop10Secs = hop5Secs * 2.0;
        std::vector<float> energy10((size_t) nw10, 0.0f);
        for (int w = 0; w < nw10; ++w)
        {
            const float e0 = energy5[(size_t)(2 * w)];
            const float e1 = energy5[(size_t)(2 * w + 1)];
            energy10[(size_t) w] = std::sqrt((e0 * e0 + e1 * e1) * 0.5f);
        }

        // 2) 各解像度のゼロ平均オンセット包絡
        const std::vector<float> oz10 = detail::buildOnsetZeroMean(energy10, hop10Secs);
        const std::vector<float> oz5  = detail::buildOnsetZeroMean(energy5,  hop5Secs);

        // 無音 / ビート無しガード
        double ozEnergy = 0.0;
        for (float v : oz5) ozEnergy += (double) v * v;
        if (ozEnergy <= 1.0e-9) return 0.0;

        // 多倍音コム: 指定包絡で BPM の周期 p に対し corrFrac(k*p) を重み付き加算
        static constexpr int   K = 4;
        static constexpr float HW[K] = { 1.00f, 0.75f, 0.50f, 0.25f };
        auto combOf = [&](const std::vector<float>& oz, double hopSecs, double bpm) -> float
        {
            const int nw = (int) oz.size();
            const double p = 60.0 / (bpm * hopSecs);
            float acc = 0.0f;
            for (int k = 1; k <= K; ++k)
            {
                const double lag = (double) k * p;
                if (lag < 5.0 || lag >= (double)(nw - 1)) continue;
                const int    ip = (int) lag;
                const double fr = lag - (double) ip;
                double s = 0.0; int cnt = 0;
                for (int i = 0; i + ip + 1 < nw; ++i)
                {
                    const double a = (double) oz[(size_t)(i + ip)]     * (1.0 - fr)
                                   + (double) oz[(size_t)(i + ip + 1)] * fr;
                    s += (double) oz[(size_t) i] * a; ++cnt;
                }
                acc += HW[k - 1] * (cnt > 0 ? (float)(s / cnt) : 0.0f);
            }
            return acc;
        };
        auto comb10 = [&](double bpm) { return combOf(oz10, hop10Secs, bpm); };
        auto comb5  = [&](double bpm) { return combOf(oz5,  hop5Secs,  bpm); };

        // 3) 起点 = 10ms コムの大域最大 (BPM 60〜200)
        double base = 60.0; float bestV = -1.0e30f;
        for (double b = 60.0; b <= 200.0; b += 0.2)
        {
            const float v = comb10(b);
            if (v > bestV) { bestV = v; base = b; }
        }

        // 4) メトリカル・ダブリング歩進: ×2 (無ければ ×3) のコムが起点の α 倍以上なら歩進。
        //    α は実曲 (72 と 184) と合成スイートで検証した安全域 (0.70 < α < 0.94)。
        constexpr double kStepRatio = 0.80;
        double b = base;
        for (int iter = 0; iter < 3; ++iter)
        {
            const float cur = comb10(b);
            bool stepped = false;
            for (double m : { 2.0, 3.0 })
            {
                const double nb = b * m;
                if (nb > 215.0) continue;  // 204 等の高速曲まで歩進可能に
                if (comb10(nb) >= kStepRatio * cur) { b = nb; stepped = true; break; }
            }
            if (!stepped) break;
        }

        // 5) 整数確定: 5ms コムで近傍 (±3) を直接評価して最大を採る (off-by-one 解消)
        const int lo = juce::jlimit(60, 240, (int) std::round(b) - 3);
        const int hi = juce::jlimit(60, 240, (int) std::round(b) + 3);
        int   bestInt = juce::jlimit(60, 240, (int) std::round(b));
        float bestComb = -1.0e30f;
        for (int bi = lo; bi <= hi; ++bi)
        {
            const float s = comb5((double) bi);
            if (s > bestComb) { bestComb = s; bestInt = bi; }
        }
        return (double) bestInt;
    }
}
