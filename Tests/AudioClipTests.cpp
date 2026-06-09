// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Trakova — AudioClip (波形まわり) のユニットテスト
//
// オーディオデバイス / GUI 不要・決定論的に、クリップの基礎演算を検証する:
//   ・フェード曲線 applyFadeCurve (4 種、端点/クランプ/中点/単調性)
//   ・ゲインエンベロープ getEnvelopeDBAt (空/単点/補間/外挿/ゼロ除算回避)
//   ・フェード長 / 尺 / fileOffset のクランプ (setFadeIn/OutSecs, setDuration, setFileOffset)
//   ・isSameContinuousAudio (分割片の判定 = クロスフェード抑止/許可の土台)
//
// AudioClip 構築は AudioFormatManager& + AudioThumbnailCache& が要るが、デコードは
// getThumbnail/getOrCreateReader/refreshThumbnail まで起きないのでダミー File で可。
// AudioFormatManager は各テストのローカルに持つ (静的にすると終了時 leak assertion)。
// ExportEngineTests.cpp が main() を持つので、ここは静的インスタンスを置くだけ。expect は ASCII。

#include <JuceHeader.h>
#include <cmath>
#include "../Source/Tracks/AudioClip.h"

namespace
{
static bool approxEq(double a, double b, double eps) { return std::abs(a - b) < eps; }

class AudioClipTests : public juce::UnitTest
{
public:
    AudioClipTests() : juce::UnitTest("AudioClip (waveform)") {}

    void runTest() override
    {
        testApplyFadeCurve();
        testEnvelope();
        testFadeAndDurationClamp();
        testFileOffsetClamp();
        testSameContinuousAudio();
    }

    // ── フェード曲線 ──
    void testApplyFadeCurve()
    {
        beginTest("applyFadeCurve: endpoints, clamping, midpoints, monotonic");
        const FadeCurve curves[] = { FadeCurve::Linear, FadeCurve::Logarithmic,
                                     FadeCurve::EqualPower, FadeCurve::SCurve };
        for (auto c : curves)
        {
            expect(approxEq(AudioClip::applyFadeCurve(0.0f, c), 0.0, 1e-5), "t=0 -> 0");
            expect(approxEq(AudioClip::applyFadeCurve(1.0f, c), 1.0, 1e-5), "t=1 -> 1");
            expect(approxEq(AudioClip::applyFadeCurve(-0.5f, c), 0.0, 1e-5), "t<0 clamps to 0");
            expect(approxEq(AudioClip::applyFadeCurve(1.5f, c), 1.0, 1e-5), "t>1 clamps to 1");

            // 単調非減少
            float prev = -1.0f;
            bool mono = true;
            for (int i = 0; i <= 20; ++i)
            {
                const float g = AudioClip::applyFadeCurve((float) i / 20.0f, c);
                if (g < prev - 1e-4f) mono = false;
                prev = g;
            }
            expect(mono, "monotonic non-decreasing");
        }

        // 中点の既知値
        expect(approxEq(AudioClip::applyFadeCurve(0.5f, FadeCurve::Linear), 0.5, 1e-4),
               "Linear midpoint = 0.5");
        expect(approxEq(AudioClip::applyFadeCurve(0.5f, FadeCurve::Logarithmic), std::sqrt(0.5), 1e-4),
               "Logarithmic midpoint ~0.707");
        expect(approxEq(AudioClip::applyFadeCurve(0.5f, FadeCurve::EqualPower),
                        std::sin(0.5 * juce::MathConstants<double>::halfPi), 1e-4),
               "EqualPower midpoint ~0.707");
        expect(approxEq(AudioClip::applyFadeCurve(0.5f, FadeCurve::SCurve), 0.5, 1e-4),
               "SCurve midpoint = 0.5");
    }

    // ── ゲインエンベロープ ──
    void testEnvelope()
    {
        beginTest("getEnvelopeDBAt: empty/single/interp/extrapolate/3-point/duplicate-time");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        juce::AudioThumbnailCache cache(8);
        AudioClip clip(juce::File(), 0.0, 4.0, fmt, cache);

        // 空 -> 0dB
        expect(approxEq(clip.getEnvelopeDBAt(1.0), 0.0, 1e-5), "empty envelope -> 0dB");

        // 単点 -> どこでもその値
        clip.getGainPointsRW().push_back({ 1.0, -6.0f });
        expect(approxEq(clip.getEnvelopeDBAt(0.0), -6.0, 1e-5), "single point (before) -> its value");
        expect(approxEq(clip.getEnvelopeDBAt(3.0), -6.0, 1e-5), "single point (after) -> its value");

        // 2 点 {(0,-12),(2,0)} の中点 t=1 -> -6
        clip.clearGainPoints();
        clip.getGainPointsRW().push_back({ 0.0, -12.0f });
        clip.getGainPointsRW().push_back({ 2.0,   0.0f });
        expect(approxEq(clip.getEnvelopeDBAt(1.0), -6.0, 1e-4), "2-point interp midpoint -6dB");
        expect(approxEq(clip.getEnvelopeDBAt(-1.0), -12.0, 1e-5), "before first -> first value");
        expect(approxEq(clip.getEnvelopeDBAt(5.0),   0.0, 1e-5), "after last -> last value");

        // 3 点 {(0,0),(1,-6),(3,-6)} の区間選択
        clip.clearGainPoints();
        clip.getGainPointsRW().push_back({ 0.0,  0.0f });
        clip.getGainPointsRW().push_back({ 1.0, -6.0f });
        clip.getGainPointsRW().push_back({ 3.0, -6.0f });
        expect(approxEq(clip.getEnvelopeDBAt(0.5), -3.0, 1e-4), "first segment interp -3dB");
        expect(approxEq(clip.getEnvelopeDBAt(2.0), -6.0, 1e-4), "second (flat) segment -6dB");

        // 同時刻の重複点でもクラッシュせず有限値を返す。t==front.time の早期 return 経路を通る
        // (内部の jmax(1e-6, b.time-a.time) ゼロ幅ガードは防御的で、通常クエリでは到達しない)。
        clip.clearGainPoints();
        clip.getGainPointsRW().push_back({ 1.0, -6.0f });
        clip.getGainPointsRW().push_back({ 1.0,  0.0f });
        const double v = clip.getEnvelopeDBAt(1.0);
        expect(std::isfinite(v), "duplicate-time points return a finite value");
    }

    // ── フェード / 尺のクランプ ──
    void testFadeAndDurationClamp()
    {
        beginTest("setFadeIn/OutSecs + setDuration re-clamp");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        juce::AudioThumbnailCache cache(8);
        AudioClip clip(juce::File(), 0.0, 4.0, fmt, cache);

        clip.setFadeInSecs(10.0);
        expect(approxEq(clip.getFadeInSecs(), 2.0, 1e-9), "fadeIn clamps to duration*0.5");
        clip.setFadeInSecs(-1.0);
        expect(approxEq(clip.getFadeInSecs(), 0.0, 1e-9), "fadeIn clamps to >= 0");
        clip.setFadeOutSecs(10.0);
        expect(approxEq(clip.getFadeOutSecs(), 2.0, 1e-9), "fadeOut clamps to duration*0.5");

        // 尺を縮めると既存フェードを再クランプ (fadeIn+fadeOut > duration を防ぐ)
        clip.setFadeInSecs(2.0);
        clip.setFadeOutSecs(2.0);
        clip.setDuration(1.0);   // duration*0.5 = 0.5
        expect(clip.getFadeInSecs()  <= 0.5 + 1e-9, "fadeIn re-clamped on shrink");
        expect(clip.getFadeOutSecs() <= 0.5 + 1e-9, "fadeOut re-clamped on shrink");
        expect(clip.getFadeInSecs() + clip.getFadeOutSecs() <= clip.getDuration() + 1e-9,
               "fadeIn + fadeOut never exceeds duration");
    }

    void testFileOffsetClamp()
    {
        beginTest("setFileOffset clamps to >= 0");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        juce::AudioThumbnailCache cache(8);
        AudioClip clip(juce::File(), 0.0, 4.0, fmt, cache);
        clip.setFileOffset(-5.0);
        expect(approxEq(clip.getFileOffset(), 0.0, 1e-9), "negative fileOffset -> 0");
        clip.setFileOffset(3.5);
        expect(approxEq(clip.getFileOffset(), 3.5, 1e-9), "positive fileOffset kept");
    }

    // ── 同一連続音声の判定 ──
    void testSameContinuousAudio()
    {
        beginTest("isSameContinuousAudio: split match / different file / different region / 10ms boundary");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        juce::AudioThumbnailCache cache(8);
        const auto tmp   = juce::File::getSpecialLocation(juce::File::tempDirectory);
        const auto fileA = tmp.getChildFile("trakova_xfade_A.wav");   // 実体は不要 (パス比較のみ)
        const auto fileB = tmp.getChildFile("trakova_xfade_B.wav");

        // Cmd+Click 分割: 同一ファイル・連続。anchor (fileOffset - start) が一致
        AudioClip a(fileA, 0.0, 2.0, fmt, cache);                 // anchor = 0 - 0 = 0
        AudioClip b(fileA, 2.0, 2.0, fmt, cache); b.setFileOffset(2.0);   // anchor = 2 - 2 = 0
        expect(AudioClip::isSameContinuousAudio(a, b), "split pieces -> same continuous audio");

        // 別ファイル
        AudioClip d(fileB, 2.0, 2.0, fmt, cache); d.setFileOffset(2.0);
        expect(! AudioClip::isSameContinuousAudio(a, d), "different file -> not same");

        // 同一ファイル・別リージョン (テイク): anchor が異なる
        AudioClip take(fileA, 2.0, 2.0, fmt, cache); take.setFileOffset(0.0);   // anchor = 0 - 2 = -2
        expect(! AudioClip::isSameContinuousAudio(a, take), "same file different region -> not same");

        // 10ms 境界: 9ms 差は同一、11ms 差は別
        AudioClip near9(fileA, 2.0, 2.0, fmt, cache); near9.setFileOffset(2.0 + 0.009);
        expect(AudioClip::isSameContinuousAudio(a, near9), "anchor diff 9ms (<10ms) -> same");
        AudioClip far11(fileA, 2.0, 2.0, fmt, cache); far11.setFileOffset(2.0 + 0.011);
        expect(! AudioClip::isSameContinuousAudio(a, far11), "anchor diff 11ms (>10ms) -> not same");
    }
};

static AudioClipTests audioClipTests;
}
