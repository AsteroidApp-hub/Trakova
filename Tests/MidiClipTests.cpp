// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

// Utawave — MidiClip 編集 op のユニットテスト
//
// 純粋・ヘッダオンリー。ピアノロール/トランスポーズで毎回走る基礎演算:
//   transposeSemitones (note 番号 [0,127] クランプ・非ノートは不変)
//   scaleVelocity (note-on velocity [1,127] クランプ・0 にしない・note-off 不変)
//   getNoteRange (note-on の min/max・空 -> {60,72})
//   setChannel [0,15] クランプ・isDrumChannel (ch==9 = MIDI ch10) のみ true
// ExportEngineTests.cpp が main() を持つので静的インスタンスを置くだけ。expect は ASCII。

#include <JuceHeader.h>
#include "../Source/Tracks/MidiClip.h"

namespace
{
void addNote(MidiClip& clip, int note, int vel, double onSec = 0.0, double offSec = 0.5)
{
    auto& seq = clip.getSequence();
    seq.addEvent(juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), onSec);
    seq.addEvent(juce::MidiMessage::noteOff(1, note),                    offSec);
}

// 指定ノートの note-on velocity を返す (見つからなければ -1)
int velocityOf(MidiClip& clip, int note)
{
    auto& seq = clip.getSequence();
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer(i)->message;
        if (m.isNoteOn() && m.getNoteNumber() == note) return m.getVelocity();
    }
    return -1;
}

class MidiClipTests : public juce::UnitTest
{
public:
    MidiClipTests() : juce::UnitTest("MidiClip (edit ops)") {}

    void runTest() override
    {
        testTranspose();
        testTransposeClampAndNonNote();
        testScaleVelocity();
        testNoteRange();
        testChannel();
    }

    // ── 移調 ──
    void testTranspose()
    {
        beginTest("transposeSemitones: shifts all notes");
        MidiClip clip(0.0, 4.0);
        addNote(clip, 60, 100);
        addNote(clip, 64, 100);
        addNote(clip, 67, 100);
        clip.transposeSemitones(12);
        int lo, hi; clip.getNoteRange(lo, hi);
        expect(lo == 72 && hi == 79, "all notes shifted +12 (60->72, 67->79)");
        clip.transposeSemitones(-12);
        clip.getNoteRange(lo, hi);
        expect(lo == 60 && hi == 67, "shift back -12 restores original range");
    }

    void testTransposeClampAndNonNote()
    {
        beginTest("transposeSemitones: clamps to [0,127]; non-note events unchanged");
        MidiClip clip(0.0, 4.0);
        addNote(clip, 125, 100);   // +5 -> 130 clamp 127
        addNote(clip,   2, 100);   // -8 -> -6 clamp 0
        // 非ノート: コントローラ (CC74 = 64)
        clip.getSequence().addEvent(juce::MidiMessage::controllerEvent(1, 74, 64), 0.1);

        clip.transposeSemitones(5);
        int lo, hi; clip.getNoteRange(lo, hi);
        expect(hi == 127, "125 + 5 clamps to 127");

        // 低域クランプを単独検証 (別クリップで -8)
        MidiClip clip2(0.0, 4.0);
        addNote(clip2, 2, 100);
        clip2.transposeSemitones(-8);
        int lo2, hi2; clip2.getNoteRange(lo2, hi2);
        expect(lo2 == 0, "2 - 8 clamps to 0");

        // 非ノート (CC) が transpose で破壊されないこと
        MidiClip clip3(0.0, 4.0);
        addNote(clip3, 60, 100);
        clip3.getSequence().addEvent(juce::MidiMessage::controllerEvent(1, 74, 64), 0.1);
        clip3.transposeSemitones(3);
        bool ccIntact = false;
        auto& seq = clip3.getSequence();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer(i)->message;
            if (m.isController() && m.getControllerNumber() == 74)
                ccIntact = (m.getControllerValue() == 64);
        }
        expect(ccIntact, "controller event value unchanged by transpose");
    }

    // ── ベロシティ倍率 ──
    void testScaleVelocity()
    {
        beginTest("scaleVelocity: clamps to [1,127], never 0; note-off unchanged");
        {
            MidiClip c(0.0, 4.0); addNote(c, 60, 100);
            c.scaleVelocity(0.5f);
            expect(velocityOf(c, 60) == 50, "100 * 0.5 -> 50");
        }
        {
            MidiClip c(0.0, 4.0); addNote(c, 60, 100);
            c.scaleVelocity(2.0f);
            expect(velocityOf(c, 60) == 127, "100 * 2.0 -> clamp 127");
        }
        {
            MidiClip c(0.0, 4.0); addNote(c, 60, 100);
            c.scaleVelocity(0.0f);
            expect(velocityOf(c, 60) == 1, "100 * 0 -> clamp to 1 (not 0)");
        }
        {
            // note-off は変更されない (scaleVelocity は isNoteOn のみ)
            MidiClip c(0.0, 4.0); addNote(c, 60, 100);
            c.scaleVelocity(0.5f);
            auto& seq = c.getSequence();
            bool offOk = false;
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer(i)->message;
                if (m.isNoteOff() && m.getNoteNumber() == 60) offOk = true;
            }
            expect(offOk, "note-off still present (untouched by scaleVelocity)");
        }
    }

    // ── ノート範囲 ──
    void testNoteRange()
    {
        beginTest("getNoteRange: min/max of note-ons; empty -> {60,72}");
        MidiClip empty(0.0, 4.0);
        int lo, hi; empty.getNoteRange(lo, hi);
        expect(lo == 60 && hi == 72, "empty sequence -> {60,72}");

        MidiClip clip(0.0, 4.0);
        addNote(clip, 48, 100);
        addNote(clip, 72, 100);
        addNote(clip, 60, 100);
        clip.getNoteRange(lo, hi);
        expect(lo == 48 && hi == 72, "min=48, max=72");
    }

    // ── チャンネル ──
    void testChannel()
    {
        beginTest("setChannel: [0,15] clamp; isDrumChannel only ch9");
        MidiClip clip(0.0, 4.0);
        clip.setChannel(5);  expect(clip.getChannel() == 5, "channel set to 5");
        clip.setChannel(20); expect(clip.getChannel() == 15, "channel clamps to 15");
        clip.setChannel(-3); expect(clip.getChannel() == 0,  "channel clamps to 0");

        clip.setChannel(9);  expect(clip.isDrumChannel(), "ch 9 (MIDI ch10) is drum");
        clip.setChannel(0);  expect(! clip.isDrumChannel(), "ch 0 is not drum");
        clip.setChannel(10); expect(! clip.isDrumChannel(), "ch 10 (MIDI ch11) is not drum");
    }
};

static MidiClipTests midiClipTests;
}
