// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Utawave — AppSettings の音楽位置/テンポ計算 + グリッド量子化のユニットテスト
//
// 純粋演算 (依存ゼロ・ヘッダオンリー)。ルーラー/グリッドスナップ/書き出し小節範囲の土台で、
// 区分積分と 0 始まり barIndex ↔ 1 始まり bar の対応はバグの温床。
//   beatsAtTime / bpmAtTime / barAndBeatAtTime / getMeterAtBar / isDownbeatAtBeat
//   snapModeUnitSecs (1 グリッド単位の秒・BPM 依存)
// ExportEngineTests.cpp が main() を持つので静的インスタンスを置くだけ。expect は ASCII。

#include <JuceHeader.h>
#include <cmath>
#include "../Source/AppSettings.h"

namespace
{
static bool approxEq(double a, double b, double eps) { return std::abs(a - b) < eps; }

class AppSettingsTests : public juce::UnitTest
{
public:
    AppSettingsTests() : juce::UnitTest("AppSettings (tempo / grid)") {}

    void runTest() override
    {
        testBeatsConstantTempo();
        testTempoChanges();
        testMeterAtBar();
        testDownbeat();
        testBarAndBeat();
        testSnapModeUnitSecs();
    }

    // ── 一定テンポの累積ビート ──
    void testBeatsConstantTempo()
    {
        beginTest("beatsAtTime: constant 120 BPM -> t * 2");
        AppSettings s;                 // initialBpm = 120, no changes
        expect(approxEq(s.beatsAtTime(0.0), 0.0, 1e-9), "t=0 -> 0 beats");
        expect(approxEq(s.beatsAtTime(3.0), 6.0, 1e-9), "t=3s @120 -> 6 beats");
        expect(approxEq(s.bpmAtTime(10.0), 120.0, 1e-9), "bpm is constant 120");
    }

    // ── テンポ変化 (区分的) ──
    void testTempoChanges()
    {
        beginTest("bpmAtTime / beatsAtTime: piecewise across a tempo change");
        AppSettings s;
        s.initialBpm = 120.0;
        s.bpmChanges = { { 4.0, 140.0 } };   // 4 秒で 140 へ

        expect(approxEq(s.bpmAtTime(3.9), 120.0, 1e-9), "before change -> 120");
        expect(approxEq(s.bpmAtTime(4.0), 140.0, 1e-9), "at change -> 140");
        expect(approxEq(s.bpmAtTime(9.0), 140.0, 1e-9), "after change -> 140");

        // 0..4s は 120 (=8 拍)、以降 140
        expect(approxEq(s.beatsAtTime(3.9), 3.9 * 2.0, 1e-9), "before change: 120 BPM slope");
        expect(approxEq(s.beatsAtTime(4.0), 8.0, 1e-9), "exactly at change: 8 beats accumulated");
        expect(approxEq(s.beatsAtTime(5.0), 8.0 + 1.0 * (140.0 / 60.0), 1e-9),
               "after change: 140 BPM slope continues from 8 beats");
    }

    // ── 拍子 (0 始まり barIndex ↔ 1 始まり bar) ──
    void testMeterAtBar()
    {
        beginTest("getMeterAtBar: meterChange at barIndex 4 (0-based) affects bar 5 (1-based)");
        AppSettings s;
        s.meterNumerator = 4; s.meterDenominator = 4;
        s.meterChanges = { { /*barIndex=*/4, 3, 4 } };

        int n, d;
        s.getMeterAtBar(1, n, d); expect(n == 4 && d == 4, "bar 1 -> 4/4 (default)");
        s.getMeterAtBar(4, n, d); expect(n == 4 && d == 4, "bar 4 -> 4/4 (still before change)");
        s.getMeterAtBar(5, n, d); expect(n == 3 && d == 4, "bar 5 -> 3/4 (barIndex 4 = bar 5)");
        s.getMeterAtBar(9, n, d); expect(n == 3 && d == 4, "bar 9 -> 3/4 (stays changed)");
    }

    // ── 小節頭判定 ──
    void testDownbeat()
    {
        beginTest("isDownbeatAtBeat: 4/4 downbeats at 0,4,8");
        AppSettings s;   // 4/4
        expect(s.isDownbeatAtBeat(0), "beat 0 is a downbeat");
        expect(! s.isDownbeatAtBeat(1), "beat 1 is not a downbeat");
        expect(! s.isDownbeatAtBeat(3), "beat 3 is not a downbeat");
        expect(s.isDownbeatAtBeat(4), "beat 4 is a downbeat (bar 2)");
        expect(! s.isDownbeatAtBeat(5), "beat 5 is not a downbeat");
        expect(s.isDownbeatAtBeat(8), "beat 8 is a downbeat (bar 3)");

        // 拍子変化を跨ぐ: bar1=4/4 (beats 0..3), bar2 以降 3/4 (barIndex 1)
        AppSettings t;
        t.meterChanges = { { 1, 3, 4 } };
        expect(t.isDownbeatAtBeat(4), "after 4/4 bar, beat 4 starts bar 2");
        expect(t.isDownbeatAtBeat(7), "beat 7 starts bar 3 (4 + 3)");
        expect(! t.isDownbeatAtBeat(6), "beat 6 is mid-bar in 3/4 section");
    }

    // ── (bar, beat) 変換 ──
    void testBarAndBeat()
    {
        beginTest("barAndBeatAtTime: 120 BPM 4/4");
        AppSettings s;   // 120 / 4/4
        int bar, beat;
        s.barAndBeatAtTime(0.0, bar, beat); expect(bar == 1 && beat == 1, "t=0 -> bar1 beat1");
        s.barAndBeatAtTime(1.0, bar, beat); expect(bar == 1 && beat == 3, "t=1s (2 beats) -> bar1 beat3");
        s.barAndBeatAtTime(2.0, bar, beat); expect(bar == 2 && beat == 1, "t=2s (4 beats) -> bar2 beat1");
    }

    // ── グリッド 1 単位の秒 ──
    void testSnapModeUnitSecs()
    {
        beginTest("snapModeUnitSecs: grid units at 120 BPM + BPM scaling + Off");
        const double spb = 0.5;   // 120 BPM の 1 拍 = 0.5s
        expect(approxEq(snapModeUnitSecs(SnapMode::Bar, 120.0),          spb * 4.0,       1e-12), "1/1 = 4 beats");
        expect(approxEq(snapModeUnitSecs(SnapMode::Half, 120.0),         spb * 2.0,       1e-12), "1/2 = 2 beats");
        expect(approxEq(snapModeUnitSecs(SnapMode::Quarter, 120.0),      spb,             1e-12), "1/4 = 1 beat");
        expect(approxEq(snapModeUnitSecs(SnapMode::Eighth, 120.0),       spb * 0.5,       1e-12), "1/8");
        expect(approxEq(snapModeUnitSecs(SnapMode::Sixteenth, 120.0),    spb * 0.25,      1e-12), "1/16");
        expect(approxEq(snapModeUnitSecs(SnapMode::ThirtySecond, 120.0), spb * 0.125,     1e-12), "1/32");
        expect(approxEq(snapModeUnitSecs(SnapMode::QuarterT, 120.0),     spb * 2.0 / 3.0, 1e-12), "1/4 triplet");
        expect(approxEq(snapModeUnitSecs(SnapMode::EighthT, 120.0),      spb / 3.0,       1e-12), "1/8 triplet");
        expect(approxEq(snapModeUnitSecs(SnapMode::SixteenthT, 120.0),   spb / 6.0,       1e-12), "1/16 triplet");
        expect(approxEq(snapModeUnitSecs(SnapMode::Off, 120.0),          0.0,             1e-12), "Off = 0");

        // BPM スケーリング: 60 BPM では 1 拍 = 1.0s
        expect(approxEq(snapModeUnitSecs(SnapMode::Quarter, 60.0), 1.0, 1e-12), "1/4 at 60 BPM = 1.0s");
        // bpm <= 1 ガード (0 で除算しない): spb = 60 / 1
        expect(approxEq(snapModeUnitSecs(SnapMode::Quarter, 0.0), 60.0, 1e-9), "bpm<=1 guard avoids divide-by-zero");
    }
};

static AppSettingsTests appSettingsTests;
}
