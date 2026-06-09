// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "MidiExporter.h"
#include "../Tracks/TrackManager.h"
#include "../Tracks/Track.h"
#include "../Tracks/MidiClip.h"
#include "../AppSettings.h"
#include "../Localisation.h"

namespace
{
    constexpr int kPPQ = 960;  // ticks per quarter note (書き出し解像度)

    int microsPerQuarter(double bpm)
    {
        bpm = juce::jmax(1.0, bpm);
        return juce::roundToInt(60000000.0 / bpm);
    }

    // 絶対タイムライン秒 → ティック。AppSettings のテンポマップ (bpmChanges) を考慮した
    // 累積拍数を使うため、途中テンポ変化があっても正しい位置になる。
    double secsToTicks(const AppSettings& s, double absSec)
    {
        return s.beatsAtTime(absSec) * (double) kPPQ;
    }

    // 1-based 小節の開始ティック。アプリのモデルでは 1 拍 = 四分音符・1 小節 = numerator 拍
    // として扱う (拍子の分母は小節長計算に使わない)。ティックは拍数に比例するためテンポ非依存。
    double barStartTicks(const AppSettings& s, int bar1)
    {
        double beats = 0.0;
        for (int b = 1; b < bar1; ++b)
        {
            int n, d; s.getMeterAtBar(b, n, d);
            beats += juce::jmax(1, n);
        }
        return beats * (double) kPPQ;
    }
}

MidiExporter::Result MidiExporter::save(const juce::File& dest,
                                        const TrackManager& tracks,
                                        const AppSettings& settings)
{
    Result r;

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPPQ);

    // ── コンダクタートラック (テンポ / 拍子マップ) ──
    {
        juce::MidiMessageSequence conductor;
        conductor.addEvent(juce::MidiMessage::tempoMetaEvent(microsPerQuarter(settings.initialBpm)), 0.0);
        conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(settings.meterNumerator,
                                                                     settings.meterDenominator), 0.0);
        for (const auto& bc : settings.bpmChanges)
            conductor.addEvent(juce::MidiMessage::tempoMetaEvent(microsPerQuarter(bc.bpm)),
                               secsToTicks(settings, bc.timeSec));
        for (const auto& mc : settings.meterChanges)
            conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(mc.numerator, mc.denominator),
                               barStartTicks(settings, mc.barIndex + 1));
        conductor.sort();
        mf.addTrack(conductor);
    }

    // ── 各 MIDI トラック (ノートのあるものだけ) ──
    for (int ti = 0; ti < tracks.getTrackCount(); ++ti)
    {
        const Track* track = tracks.getTrack(ti);
        if (track == nullptr || !track->isMidiTrack()) continue;

        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::textMetaEvent(3, track->getName()), 0.0);  // type 3 = Track Name

        const int transpose = track->getTotalTransposeSemitones();
        int trackNotes = 0;

        for (int ci = 0; ci < track->getMidiClipCount(); ++ci)
        {
            const MidiClip* clip = track->getMidiClip(ci);
            if (clip == nullptr) continue;
            const double clipStart = clip->getStartPosition();
            const int    channel1  = juce::jlimit(1, 16, clip->getChannel() + 1);  // 0-based → 1-based
            const auto&  src       = clip->getSequence();

            for (int i = 0; i < src.getNumEvents(); ++i)
            {
                juce::MidiMessage m = src.getEventPointer(i)->message;  // コピーして加工
                const bool playable = m.isNoteOnOrOff() || m.isPitchWheel() || m.isController()
                                   || m.isProgramChange() || m.isAftertouch() || m.isChannelPressure()
                                   || m.isSustainPedalOn() || m.isSustainPedalOff();
                if (!playable) continue;

                if (transpose != 0 && m.isNoteOnOrOff())
                    m.setNoteNumber(juce::jlimit(0, 127, m.getNoteNumber() + transpose));
                m.setChannel(channel1);  // クリップのチャンネルを尊重 (ch10 ドラム保持)

                const double absSec = clipStart + m.getTimeStamp();
                m.setTimeStamp(secsToTicks(settings, absSec));
                seq.addEvent(m);

                if (m.isNoteOn()) ++trackNotes;
            }
        }

        if (trackNotes == 0) continue;  // ノートの無い空 MIDI トラックは出力しない

        seq.updateMatchedPairs();
        seq.sort();
        mf.addTrack(seq);
        ++r.trackCount;
        r.noteCount += trackNotes;
    }

    if (r.noteCount == 0)
    {
        r.error = tr(u8"書き出せる MIDI ノートがありません");
        return r;
    }

    // ── ファイルへ書き出し (上書き) ──
    if (dest.existsAsFile())
        dest.deleteFile();

    juce::FileOutputStream out(dest);
    if (!out.openedOk())
    {
        r.error = tr(u8"ファイルを開けません");
        return r;
    }
    if (!mf.writeTo(out, 1))   // Type 1 = マルチトラック
    {
        r.error = tr(u8"書き出しに失敗しました");
        return r;
    }
    out.flush();
    r.ok = true;
    return r;
}
