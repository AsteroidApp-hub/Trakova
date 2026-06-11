// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

// Utawave — LufsMeter (ITU-R BS.1770 簡易実装) のユニットテスト
//
// インポート時の自動ラウドネス正規化を駆動する測定器。絶対 LUFS は K特性フィルタ応答に
// 依存するので、**第一原理で厳密に導ける相対関係**を中心に検証する:
//   ・線形ゲイン -6dB → -6.02 LU (フィルタは線形なので厳密)
//   ・デュアルモノ → モノ比 +3.01 LU (BS.1770 チャンネル合算)
//   ・K特性の方向: 1kHz は 低域 (20Hz) より明確に大きい (RLB ハイパス)
//   ・ゲート: 無音 / 400ms 未満 → -infinity
// 1.5s 以上の信号を生成すること (400ms ブロック×複数が要る)。
// AudioFormatManager は runTest ローカル。expect は ASCII。

#include <JuceHeader.h>
#include <cmath>
#include <limits>
#include "../Source/Audio/LufsMeter.h"

namespace
{
class LufsMeterTests : public juce::UnitTest
{
public:
    LufsMeterTests() : juce::UnitTest("LufsMeter") {}

    juce::File dir;

    // 32bit float WAV に正弦波を書く
    juce::File writeSine(const juce::String& name, double sr, double secs,
                         double freq, double amp, int ch)
    {
        auto f = dir.getChildFile(name);
        f.deleteFile();
        std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
        if (os == nullptr) return {};
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wav.createWriterFor(os.get(), sr, (unsigned int) ch, 24, {}, 0));
        if (w == nullptr) return {};
        os.release();
        const int n = (int) (sr * secs);
        juce::AudioBuffer<float> buf(ch, n);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
                buf.setSample(c, i,
                    (float) (amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr)));
        w->writeFromAudioSampleBuffer(buf, 0, n);
        return f;
    }

    static bool isNegInf(double v) { return std::isinf(v) && v < 0.0; }

    void runTest() override
    {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("UtawaveLufsTests");
        dir.deleteRecursively(); dir.createDirectory();
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();

        testGainRelationship(fmt);
        testAmplitudeRelationship(fmt);
        testDualMonoVsMono(fmt);
        testKWeightingDirection(fmt);
        testGating(fmt);
        testAbsoluteSanity(fmt);

        dir.deleteRecursively();
    }

    // ── 線形ゲイン -6dB → -6.02 LU (同一ファイルに gain を掛けるので厳密) ──
    void testGainRelationship(juce::AudioFormatManager& fmt)
    {
        beginTest("measureFileSegment: gain 0.5 -> -6.02 LU (linear, exact)");
        auto f = writeSine("g.wav", 48000.0, 2.0, 1000.0, 0.5, 1);
        const double full = LufsMeter::measureFileSegment(f, fmt, 0.0, 0.0, 1.0f);
        const double half = LufsMeter::measureFileSegment(f, fmt, 0.0, 0.0, 0.5f);
        expect(std::isfinite(full) && std::isfinite(half), "both measurements finite");
        expect(std::abs((full - half) - 6.0206) < 0.05, "halving gain lowers LUFS by ~6.02 LU");
    }

    // ── 振幅 -6dB (別ファイル) でも -6.02 LU ──
    void testAmplitudeRelationship(juce::AudioFormatManager& fmt)
    {
        beginTest("measureFileSegment: half amplitude file -> -6.02 LU");
        auto loud  = writeSine("a1.wav", 48000.0, 2.0, 1000.0, 0.5,  1);
        auto quiet = writeSine("a2.wav", 48000.0, 2.0, 1000.0, 0.25, 1);
        const double lL = LufsMeter::measureFileSegment(loud,  fmt, 0.0, 0.0, 1.0f);
        const double lQ = LufsMeter::measureFileSegment(quiet, fmt, 0.0, 0.0, 1.0f);
        expect(std::abs((lL - lQ) - 6.0206) < 0.1, "half-amplitude file is ~6.02 LU quieter");
    }

    // ── デュアルモノ → モノ比 +3.01 LU (チャンネル合算) ──
    void testDualMonoVsMono(juce::AudioFormatManager& fmt)
    {
        beginTest("measureFileSegment: dual-mono is ~3.01 LU louder than mono (channel summation)");
        auto mono   = writeSine("m1.wav", 48000.0, 2.0, 1000.0, 0.5, 1);
        auto stereo = writeSine("m2.wav", 48000.0, 2.0, 1000.0, 0.5, 2);   // L = R = 同一正弦
        const double lM = LufsMeter::measureFileSegment(mono,   fmt, 0.0, 0.0, 1.0f);
        const double lS = LufsMeter::measureFileSegment(stereo, fmt, 0.0, 0.0, 1.0f);
        expect(std::abs((lS - lM) - 3.0103) < 0.15, "dual-mono ~3.01 LU louder");
    }

    // ── K特性の方向: 1kHz は低域 (20Hz) より明確に大きい (RLB ハイパス) ──
    void testKWeightingDirection(juce::AudioFormatManager& fmt)
    {
        beginTest("measureFileSegment: 1kHz louder than 20Hz at same amplitude (RLB high-pass)");
        auto hi = writeSine("k1.wav", 48000.0, 2.0, 1000.0, 0.5, 1);
        auto lo = writeSine("k2.wav", 48000.0, 2.0,   20.0, 0.5, 1);
        const double lHi = LufsMeter::measureFileSegment(hi, fmt, 0.0, 0.0, 1.0f);
        const double lLo = LufsMeter::measureFileSegment(lo, fmt, 0.0, 0.0, 1.0f);
        expect(std::isfinite(lHi) && std::isfinite(lLo), "both finite");
        expect(lHi > lLo + 3.0, "1kHz clearly louder than 20Hz (K-weighting attenuates lows)");
    }

    // ── ゲート: 無音 / 400ms 未満 → -infinity ──
    void testGating(juce::AudioFormatManager& fmt)
    {
        beginTest("measureFileSegment: silence and <400ms return -infinity");
        auto silent = writeSine("s.wav", 48000.0, 2.0, 1000.0, 0.0, 1);   // 無音
        expect(isNegInf(LufsMeter::measureFileSegment(silent, fmt, 0.0, 0.0, 1.0f)),
               "pure silence -> -infinity (absolute gate)");

        auto tooShort = writeSine("t.wav", 48000.0, 0.3, 1000.0, 0.5, 1);  // 0.3s < 4 chunks(400ms)
        expect(isNegInf(LufsMeter::measureFileSegment(tooShort, fmt, 0.0, 0.0, 1.0f)),
               "under 400ms -> -infinity (insufficient blocks)");

        // 存在しないファイル
        expect(isNegInf(LufsMeter::measureFileSegment(dir.getChildFile("nope.wav"), fmt, 0.0, 0.0, 1.0f)),
               "missing file -> -infinity");
    }

    // ── 絶対値の妥当性 (広めのサニティ) ──
    void testAbsoluteSanity(juce::AudioFormatManager& fmt)
    {
        beginTest("measureFileSegment: full-scale 1kHz sine in a plausible LUFS band");
        auto f = writeSine("abs.wav", 48000.0, 2.0, 1000.0, 1.0, 1);
        const double l = LufsMeter::measureFileSegment(f, fmt, 0.0, 0.0, 1.0f);
        expect(std::isfinite(l), "finite");
        expect(l > -6.0 && l < 2.0, "0 dBFS 1kHz sine measures roughly -1 LUFS (broad sanity band)");
    }
};

static LufsMeterTests lufsMeterTests;
}
