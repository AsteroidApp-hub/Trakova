// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Trakova — BpmDetector のユニットテスト (看板機能の回帰テスト)
//
// 合成クリック列 (各拍に短いバースト) を一時 WAV に書き、検出 BPM を検証する。
// CLAUDE.md 記載の「WAV 直読み検証」を正式な UnitTest 化し、HW 重み/kStepRatio/探索範囲を
// 調整しても documented ケースが黙って壊れないようにする。detect は整数 BPM を返す。
//   ・既知 BPM のクリック列を正答 (オクターブ誤検出しない)
//   ・ガード: 存在しないファイル / 2 秒未満 / 無音 / onProgress キャンセル → 0.0
// AudioFormatManager は runTest ローカル。expect は ASCII。

#include <JuceHeader.h>
#include <cmath>
#include "../Source/Audio/BpmDetector.h"

namespace
{
class BpmDetectorTests : public juce::UnitTest
{
public:
    BpmDetectorTests() : juce::UnitTest("BpmDetector") {}

    juce::File dir;

    std::unique_ptr<juce::AudioFormatWriter> makeWriter(const juce::File& f, double sr)
    {
        f.deleteFile();
        auto* os = f.createOutputStream().release();
        if (os == nullptr) return nullptr;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os, sr, 1, 24, {}, 0));
        if (w == nullptr) { delete os; return nullptr; }
        return w;
    }

    // 各拍に 25ms の減衰トーンバーストを置いたクリック列
    juce::File writeClicks(const juce::String& name, double sr, double secs, double bpm)
    {
        auto f = dir.getChildFile(name);
        auto w = makeWriter(f, sr);
        if (!w) return {};
        const int n = (int) (sr * secs);
        juce::AudioBuffer<float> buf(1, n);
        buf.clear();
        const double beatSec = 60.0 / bpm;
        const int burstLen = (int) (sr * 0.025);
        for (double t = 0.0; t < secs; t += beatSec)
        {
            const int start = (int) (t * sr);
            for (int j = 0; j < burstLen && start + j < n; ++j)
            {
                const double env = 1.0 - (double) j / (double) burstLen;   // 線形減衰
                buf.setSample(0, start + j,
                    (float) (0.6 * env * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * j / sr)));
            }
        }
        w->writeFromAudioSampleBuffer(buf, 0, n);
        return f;
    }

    // 前半 bpm1 / 後半 bpm2 の 2 テンポを 1 ファイルに並べる (windowing 検証用)
    juce::File writeTwoTempo(const juce::String& name, double sr,
                            double bpm1, double secs1, double bpm2, double secs2)
    {
        auto f = dir.getChildFile(name);
        auto w = makeWriter(f, sr);
        if (!w) return {};
        const double total = secs1 + secs2;
        const int n = (int) (sr * total);
        juce::AudioBuffer<float> buf(1, n);
        buf.clear();
        const int burstLen = (int) (sr * 0.025);
        auto place = [&](double startSec, double endSec, double bpm)
        {
            const double beatSec = 60.0 / bpm;
            for (double t = startSec; t < endSec; t += beatSec)
            {
                const int start = (int) (t * sr);
                for (int j = 0; j < burstLen && start + j < n; ++j)
                {
                    const double env = 1.0 - (double) j / (double) burstLen;
                    buf.setSample(0, start + j,
                        (float) (0.6 * env * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * j / sr)));
                }
            }
        };
        place(0.0,   secs1, bpm1);
        place(secs1, total, bpm2);
        w->writeFromAudioSampleBuffer(buf, 0, n);
        return f;
    }

    juce::File writeSilence(const juce::String& name, double sr, double secs)
    {
        auto f = dir.getChildFile(name);
        auto w = makeWriter(f, sr);
        if (!w) return {};
        const int n = (int) (sr * secs);
        juce::AudioBuffer<float> buf(1, n);
        buf.clear();
        w->writeFromAudioSampleBuffer(buf, 0, n);
        return f;
    }

    void runTest() override
    {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("TrakovaBpmTests");
        dir.deleteRecursively(); dir.createDirectory();
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();

        testGuards(fmt);
        testKnownTempos(fmt);
        testWindowedDetection(fmt);
        testWindowGuard(fmt);

        dir.deleteRecursively();
    }

    void testGuards(juce::AudioFormatManager& fmt)
    {
        beginTest("detect: guards return 0.0 (missing / <2s / silence / cancelled)");
        // 存在しないファイル
        expect(BpmDetector::detect(dir.getChildFile("nope.wav"), fmt) == 0.0, "missing file -> 0.0");

        // 2 秒未満 (nw5 < 400)
        auto shortClip = writeClicks("short.wav", 48000.0, 1.0, 120.0);
        expect(BpmDetector::detect(shortClip, fmt) == 0.0, "under 2s -> 0.0");

        // 無音
        auto silent = writeSilence("silent.wav", 48000.0, 4.0);
        expect(BpmDetector::detect(silent, fmt) == 0.0, "silence -> 0.0");

        // onProgress が false を返す (キャンセル)
        auto clip = writeClicks("cancel.wav", 48000.0, 4.0, 120.0);
        const double cancelled = BpmDetector::detect(clip, fmt, 0.0, 0.0,
                                                     [](double) { return false; });
        expect(cancelled == 0.0, "onProgress returning false -> 0.0 (cancelled)");
    }

    void testKnownTempos(juce::AudioFormatManager& fmt)
    {
        beginTest("detect: known click-train tempos (octave-resilient)");
        // 12 秒のクリック列で既知 BPM を正答すること
        const double bpms[] = { 100.0, 120.0, 90.0, 150.0 };
        for (double bpm : bpms)
        {
            auto f = writeClicks("clk_" + juce::String((int) bpm) + ".wav", 48000.0, 12.0, bpm);
            const double got = BpmDetector::detect(f, fmt);
            expect(std::abs(got - bpm) <= 1.0,
                   ("click train " + juce::String((int) bpm) + " BPM -> detected "
                    + juce::String(got)).toRawUTF8());
        }
    }

    // 本番の唯一の呼び出し元 (右クリック「テンポを検出」) は常にクリップの fileOffset/duration を
    // 渡す。windowing が正しく効くこと (前半 90 / 後半 150 の後半だけ解析) を検証する。
    void testWindowedDetection(juce::AudioFormatManager& fmt)
    {
        beginTest("detect: fileOffset/duration window analyzes only the sub-region (production path)");
        auto f = writeTwoTempo("two.wav", 48000.0, /*bpm1*/ 90.0, /*secs1*/ 6.0, /*bpm2*/ 150.0, /*secs2*/ 12.0);
        const double windowed = BpmDetector::detect(f, fmt, 6.0, 12.0);   // 後半 (150 BPM) だけ
        expect(std::abs(windowed - 150.0) <= 1.0,
               ("windowed [6s,18s) of a 90|150 file -> detected " + juce::String(windowed)).toRawUTF8());
    }

    void testWindowGuard(juce::AudioFormatManager& fmt)
    {
        beginTest("detect: window past EOF (endSample <= startSample) returns 0.0");
        auto f = writeClicks("guard.wav", 48000.0, 4.0, 120.0);
        // fileOffset がファイル末尾を超える -> startSample >= endSample -> 0.0
        expect(BpmDetector::detect(f, fmt, 100.0, 1.0) == 0.0, "fileOffset past EOF -> 0.0");
    }
};

static BpmDetectorTests bpmDetectorTests;
}
