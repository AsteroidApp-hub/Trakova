// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Utawave — MeterDraw (メーター描画の共通ヘルパ) のユニットテスト
//
// dbToNorm (dB→0..1 正規化) と colourForDb (緑/黄/赤の閾値判定) は純関数で、
// トラックヘッダ (横メーター) とマスターパネル (縦メーター) の両方が依存する。
// 閾値 (-6dB 黄 / 0dBFS 赤) や範囲クランプが回帰すると全メーターの見た目が崩れる。
// drawVertical は Graphics を要するため対象外 (描画は目視 QA)。
// expect は ASCII。

#include <JuceHeader.h>
#include "../Source/UI/Meter.h"

namespace
{
class MeterDrawTests : public juce::UnitTest
{
public:
    MeterDrawTests() : juce::UnitTest("MeterDraw") {}

    static bool approx(float a, float b) { return std::abs(a - b) < 1.0e-5f; }

    void runTest() override
    {
        testDbToNorm();
        testColourForDb();
    }

    // ── dbToNorm: -60..0 dB を 0..1 に線形写像し、範囲外はクランプ ──
    void testDbToNorm()
    {
        beginTest("dbToNorm: linear -60..0 dB -> 0..1 with clamping");
        expect(approx(MeterDraw::dbToNorm(-60.0f), 0.0f), "-60 dB -> 0.0 (floor)");
        expect(approx(MeterDraw::dbToNorm(0.0f),   1.0f), "0 dB -> 1.0 (ceil)");
        expect(approx(MeterDraw::dbToNorm(-30.0f), 0.5f), "-30 dB -> 0.5 (midpoint)");
        expect(approx(MeterDraw::dbToNorm(-72.0f), 0.0f), "below -60 dB clamps to 0.0");
        expect(approx(MeterDraw::dbToNorm(6.0f),   1.0f), "above 0 dB clamps to 1.0");
        // カスタム範囲 (-18..0)
        expect(approx(MeterDraw::dbToNorm(-9.0f, -18.0f, 0.0f), 0.5f),
               "custom range -18..0: -9 dB -> 0.5");
        expect(approx(MeterDraw::dbToNorm(-24.0f, -18.0f, 0.0f), 0.0f),
               "custom range: below floor clamps to 0.0");
    }

    // ── colourForDb: 0dBFS 以上=赤 / -6..0=黄 / それ以下=緑 ──
    void testColourForDb()
    {
        beginTest("colourForDb: red >= 0 dBFS, yellow [-6,0), green below -6");
        expect(MeterDraw::colourForDb(0.0f)    == AppColours::meterRed,
               "0 dBFS -> red (peak over)");
        expect(MeterDraw::colourForDb(3.0f)    == AppColours::meterRed,
               "above 0 dBFS -> red");
        expect(MeterDraw::colourForDb(-0.001f) == AppColours::meterYellow,
               "just below 0 dBFS -> yellow");
        expect(MeterDraw::colourForDb(-6.0f)   == AppColours::meterYellow,
               "-6 dB (boundary) -> yellow (inclusive)");
        expect(MeterDraw::colourForDb(-6.001f) == AppColours::meterGreen,
               "just below -6 dB -> green");
        expect(MeterDraw::colourForDb(-30.0f)  == AppColours::meterGreen,
               "well below -6 dB -> green");
    }
};

static MeterDrawTests meterDrawTests;
}
