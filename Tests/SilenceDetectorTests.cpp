// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Trakova — SilenceDetector::regionsToKeep のユニットテスト
//
// Strip Silence (破壊編集) の心臓部。区間演算は off-by-one で必要な音を落とす / ゼロ長クリップを
// 生む事故源。regionsToKeep は純関数 (vector のみ) なので最も安く網羅できる:
//   無音の補集合 / 先頭・末尾無音の扱い / パディングの境界拡張 + [0,dur] クランプ / 隣接マージ
// (detect() は実 WAV と閾値調整が要るため別途。ここは純関数のみ)
// ExportEngineTests.cpp が main() を持つので静的インスタンスを置くだけ。expect は ASCII。

#include <JuceHeader.h>
#include "../Source/Edit/SilenceDetector.h"

namespace
{
using SilenceDetector::Region;

static bool approxEq(double a, double b, double eps) { return std::abs(a - b) < eps; }
static bool regionEq(const Region& r, double s, double e, double eps = 1e-6)
{
    return approxEq(r.start, s, eps) && approxEq(r.end, e, eps);
}

class SilenceDetectorTests : public juce::UnitTest
{
public:
    SilenceDetectorTests() : juce::UnitTest("SilenceDetector (regionsToKeep)") {}

    void runTest() override
    {
        testEmptySilence();
        testWholeClipSilence();
        testCentralSilence();
        testLeadingAndTrailingSilence();
        testMultipleSilences();
        testPaddingMergeAndClamp();
        testPaddingNoMerge();
        testAsymmetricPadding();
    }

    void testEmptySilence()
    {
        beginTest("regionsToKeep: no silence -> single [0, duration]");
        auto keep = SilenceDetector::regionsToKeep({}, 5.0, 0.0, 0.0);
        expect(keep.size() == 1, "one keep region");
        if (! keep.empty()) expect(regionEq(keep[0], 0.0, 5.0), "[0,5]");
    }

    void testWholeClipSilence()
    {
        beginTest("regionsToKeep: whole-clip silence -> empty keep (everything cut)");
        auto keep = SilenceDetector::regionsToKeep({ { 0.0, 5.0 } }, 5.0, 0.0, 0.0);
        expect(keep.empty(), "fully-silent clip leaves no keep region");
        // パディングがあっても、保持区間が生まれていないので空のまま
        auto padded = SilenceDetector::regionsToKeep({ { 0.0, 5.0 } }, 5.0, 0.5, 0.5);
        expect(padded.empty(), "padding does not resurrect a keep region from nothing");
    }

    void testCentralSilence()
    {
        beginTest("regionsToKeep: central silence -> complement (two segments)");
        auto keep = SilenceDetector::regionsToKeep({ { 2.0, 3.0 } }, 5.0, 0.0, 0.0);
        expect(keep.size() == 2, "two keep regions");
        if (keep.size() == 2)
        {
            expect(regionEq(keep[0], 0.0, 2.0), "first [0,2]");
            expect(regionEq(keep[1], 3.0, 5.0), "second [3,5]");
        }
    }

    void testLeadingAndTrailingSilence()
    {
        beginTest("regionsToKeep: leading / trailing silence dropped");
        auto lead = SilenceDetector::regionsToKeep({ { 0.0, 2.0 } }, 5.0, 0.0, 0.0);
        expect(lead.size() == 1 && regionEq(lead[0], 2.0, 5.0), "leading silence -> [2,5]");

        auto trail = SilenceDetector::regionsToKeep({ { 3.0, 5.0 } }, 5.0, 0.0, 0.0);
        expect(trail.size() == 1 && regionEq(trail[0], 0.0, 3.0), "trailing silence -> [0,3]");
    }

    void testMultipleSilences()
    {
        beginTest("regionsToKeep: two silences -> three keep segments");
        auto keep = SilenceDetector::regionsToKeep({ { 1.0, 2.0 }, { 3.0, 4.0 } }, 5.0, 0.0, 0.0);
        expect(keep.size() == 3, "three keep regions");
        if (keep.size() == 3)
        {
            expect(regionEq(keep[0], 0.0, 1.0), "[0,1]");
            expect(regionEq(keep[1], 2.0, 3.0), "[2,3]");
            expect(regionEq(keep[2], 4.0, 5.0), "[4,5]");
        }
    }

    void testPaddingMergeAndClamp()
    {
        beginTest("regionsToKeep: padding extends boundaries, clamps to [0,dur], merges on touch");
        // 中央無音 [2,3]、前後 0.5s パディング -> [0,2.5] と [2.5,5] が接触してマージ -> [0,5]
        auto merged = SilenceDetector::regionsToKeep({ { 2.0, 3.0 } }, 5.0, 0.5, 0.5);
        expect(merged.size() == 1, "padded segments touching at 2.5 merge into one");
        if (! merged.empty()) expect(regionEq(merged[0], 0.0, 5.0), "merged [0,5]");

        // 過剰な padBefore は [0,dur] にクランプ (負へ突き抜けない)
        auto clamped = SilenceDetector::regionsToKeep({ { 2.0, 3.0 } }, 5.0, 10.0, 0.0);
        expect(clamped.size() == 1, "huge padBefore merges to one (clamped at 0)");
        if (! clamped.empty()) expect(regionEq(clamped[0], 0.0, 5.0), "clamped/merged [0,5]");
    }

    void testPaddingNoMerge()
    {
        beginTest("regionsToKeep: insufficient padding keeps two separate segments");
        // [2,3] 無音、前後 0.4s -> [0,2.4] と [2.6,5] は接触しない (2.6 > 2.4 + 1e-6)
        auto keep = SilenceDetector::regionsToKeep({ { 2.0, 3.0 } }, 5.0, 0.4, 0.4);
        expect(keep.size() == 2, "0.4s padding leaves a gap -> two segments");
        if (keep.size() == 2)
        {
            expect(regionEq(keep[0], 0.0, 2.4), "[0,2.4]");
            expect(regionEq(keep[1], 2.6, 5.0), "[2.6,5]");
        }
    }

    void testAsymmetricPadding()
    {
        beginTest("regionsToKeep: padBefore / padAfter applied independently");
        // [2,3] 無音、padBefore=0.5・padAfter=0.0 -> 後半の start だけ 2.5 へ前進、end は伸びない
        auto keep = SilenceDetector::regionsToKeep({ { 2.0, 3.0 } }, 5.0, 0.5, 0.0);
        expect(keep.size() == 2, "padBefore only -> still two segments");
        if (keep.size() == 2)
        {
            expect(regionEq(keep[0], 0.0, 2.0), "first end not extended (padAfter=0)");
            expect(regionEq(keep[1], 2.5, 5.0), "second start moved back by padBefore");
        }
    }
};

static SilenceDetectorTests silenceDetectorTests;
}
