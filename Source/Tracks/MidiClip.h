// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

/**
    MIDI クリップ。タイムライン上の時刻範囲と juce::MidiMessageSequence を保持する。
    シーケンス内のイベント時刻はクリップ先頭からの秒数で管理する。
*/
class MidiClip
{
public:
    MidiClip(double startPos, double durationSecs)
        : startPosition(startPos), duration(juce::jmax(0.0, durationSecs)) {}

    double getStartPosition() const { return startPosition; }
    double getDuration()      const { return duration; }
    double getEndPosition()   const { return startPosition + duration; }
    const juce::String& getName() const { return name; }
    juce::Colour getColour()  const { return colour; }
    int  getChannel()         const { return channel; }
    bool isDrumChannel()      const { return channel == 9; }   // MIDI ch10 (0-based 9)

    void setStartPosition(double pos) { startPosition = pos; }
    void setDuration(double d)        { duration = juce::jmax(0.0, d); }
    void setName(const juce::String& n) { name = n; }
    void setColour(juce::Colour c)    { colour = c; }
    void setChannel(int ch)           { channel = juce::jlimit(0, 15, ch); }

    juce::MidiMessageSequence&       getSequence()       { return sequence; }
    const juce::MidiMessageSequence& getSequence() const { return sequence; }

    // ── 編集機能 ──
    // 全ノートを半音単位で移調（範囲外は -127..127 にクランプ）
    void transposeSemitones(int semitones)
    {
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto* ev = sequence.getEventPointer(i);
            auto& m = ev->message;
            if (m.isNoteOnOrOff())
            {
                const int newNote = juce::jlimit(0, 127, m.getNoteNumber() + semitones);
                m.setNoteNumber(newNote);
            }
        }
    }
    // 全ノートのベロシティに倍率を掛ける（0.0〜2.0 程度を想定）
    void scaleVelocity(float factor)
    {
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto* ev = sequence.getEventPointer(i);
            auto& m = ev->message;
            if (m.isNoteOn())
            {
                int v = juce::jlimit(1, 127, juce::roundToInt(m.getVelocity() * factor));
                m.setVelocity((float) v / 127.0f);
            }
        }
    }

    // ノート範囲（描画時のスケーリングに使う）。空シーケンスでは {60, 72} を返す
    void getNoteRange(int& minNote, int& maxNote) const
    {
        minNote = 127; maxNote = 0;
        bool any = false;
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            const auto& m = sequence.getEventPointer(i)->message;
            if (m.isNoteOn())
            {
                minNote = juce::jmin(minNote, m.getNoteNumber());
                maxNote = juce::jmax(maxNote, m.getNoteNumber());
                any = true;
            }
        }
        if (!any) { minNote = 60; maxNote = 72; }
    }

private:
    double startPosition { 0.0 };
    double duration      { 0.0 };
    juce::String name;
    juce::Colour colour { juce::Colour(0xff7a5aa5) };   // MIDI は紫系で区別
    int channel { 0 };
    juce::MidiMessageSequence sequence;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiClip)
};
