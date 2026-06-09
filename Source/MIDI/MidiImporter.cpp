// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "MidiImporter.h"
#include "../Localisation.h"
#include <algorithm>
#include <cmath>
#include <vector>

MidiImporter::ImportResult MidiImporter::load(const juce::File& smfFile)
{
    ImportResult result;
    if (!smfFile.existsAsFile())
    {
        result.error = tr(u8"ファイルが見つかりません");
        return result;
    }

    juce::FileInputStream stream(smfFile);
    if (!stream.openedOk())
    {
        result.error = tr(u8"ファイルを開けません");
        return result;
    }

    juce::MidiFile mf;
    if (!mf.readFrom(stream))
    {
        result.error = tr(u8"SMF を解析できません");
        return result;
    }

    // ── 拍子マップは「ティック」のまま収集する（秒へ変換する前）。
    //    拍子変化の小節番号 (bar index) は譜面構造そのものでテンポに依存しない。
    //    秒へ変換してから initialBpm で拍数換算すると、途中にテンポ変化があると小節番号が
    //    ずれてしまう (旧実装のバグ)。ティックから直接求めればテンポ非依存で正確になる。 ──
    const short  timeFormat = mf.getTimeFormat();
    const double ppq = (timeFormat > 0) ? (double) timeFormat : 0.0;  // 正なら PPQ、負は SMPTE
    {
        struct MeterTick { double tick; int num; int den; };
        std::vector<MeterTick> meterTicks;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            if (auto* seq = mf.getTrack(t))
                for (int i = 0; i < seq->getNumEvents(); ++i)
                {
                    const auto& m = seq->getEventPointer(i)->message;
                    if (m.isTimeSignatureMetaEvent())
                    {
                        int n = 4, d = 4;
                        m.getTimeSignatureInfo(n, d);
                        meterTicks.push_back({ m.getTimeStamp(),
                                               juce::jmax(1, n), juce::jmax(1, d) });
                    }
                }
        std::sort(meterTicks.begin(), meterTicks.end(),
                  [](const MeterTick& a, const MeterTick& b) { return a.tick < b.tick; });

        // 同一ティックの重複 (複数トラックに同じ拍子が入る等) は後勝ちで 1 件に統合
        std::vector<MeterTick> uniq;
        for (const auto& mt : meterTicks)
        {
            if (! uniq.empty() && std::abs(uniq.back().tick - mt.tick) < 1.0e-6)
                uniq.back() = mt;
            else
                uniq.push_back(mt);
        }

        // 先頭の拍子イベント (通常ティック 0) を曲頭の拍子とする
        size_t startIdx = 0;
        double prevTick = 0.0;
        int    prevNum  = result.meterNumerator;   // 既定 4 (拍子イベントが無い場合)
        if (! uniq.empty() && uniq[0].tick <= 1.0e-6)
        {
            result.meterNumerator   = uniq[0].num;
            result.meterDenominator = uniq[0].den;
            prevNum  = uniq[0].num;
            prevTick = uniq[0].tick;
            startIdx = 1;
        }

        // 残りを meterChanges (0-based barIndex) へ。アプリのモデルは
        // 「1 拍 = 四分音符・1 小節 = numerator 拍」(AppSettings::getMeterAtBar / beatsAtTime と一致)
        // なので 1 小節 = numerator × PPQ ティック。これは MidiExporter::barStartTicks の逆変換で、
        // 書き出し → 読み込みの往復が一致する。
        if (ppq > 0.0)
        {
            double cumBars = 0.0;
            for (size_t i = startIdx; i < uniq.size(); ++i)
            {
                const double ticksPerBar = ppq * (double) juce::jmax(1, prevNum);
                cumBars += (uniq[i].tick - prevTick) / ticksPerBar;
                const int barIndex = juce::jmax(0, juce::roundToInt(cumBars));
                result.meterChanges.push_back({ barIndex, { uniq[i].num, uniq[i].den } });
                prevTick = uniq[i].tick;
                prevNum  = uniq[i].num;
            }
        }
    }

    // ティック→秒変換（テンポマップを反映）
    mf.convertTimestampTicksToSeconds();

    // ── テンポ / マーカーを秒で収集（変換後）──
    // 最初のテンポを initialBpm に、それ以降を tempoChanges に積む。
    // (SMF Type 1 ではテンポマップは通常トラック 0 にあるが、念のため全トラック走査)
    {
        bool firstTempoSeen = false;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            if (auto* seq = mf.getTrack(t))
                for (int i = 0; i < seq->getNumEvents(); ++i)
                {
                    const auto& m = seq->getEventPointer(i)->message;
                    if (m.isTempoMetaEvent())
                    {
                        const double secsPerQuarter = m.getTempoSecondsPerQuarterNote();
                        if (secsPerQuarter > 0.0)
                        {
                            const double bpm = 60.0 / secsPerQuarter;
                            if (! firstTempoSeen) { result.initialBpm = bpm; firstTempoSeen = true; }
                            else                   result.tempoChanges.push_back({ m.getTimeStamp(), bpm });
                        }
                    }
                    else if (m.isTextMetaEvent() && m.getMetaEventType() == 0x06)  // Marker
                    {
                        result.markers.push_back({ m.getTimeStamp(), m.getTextFromTextMetaEvent() });
                    }
                }
    }

    double maxEnd = 0.0;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        auto* seq = mf.getTrack(t);
        if (seq == nullptr) continue;

        ImportedTrack out;
        out.name = "Track " + juce::String(t + 1);

        // チャンネルカウントとノート数を集計しつつ、ノート関連 + ピッチベンド/コントローラを保存
        std::array<int, 16> chCount{};
        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            auto* ev = seq->getEventPointer(i);
            const auto& m = ev->message;
            // トラック名 Meta
            if (m.isTrackNameEvent() || (m.isTextMetaEvent() && m.getMetaEventType() == 0x03))
            {
                auto n = m.getTextFromTextMetaEvent().trim();
                if (n.isNotEmpty()) out.name = n;
                continue;
            }
            if (m.isMidiChannelMetaEvent())
            {
                out.primaryChannel = m.getMidiChannelMetaEventChannel() - 1;
            }
            // 演奏イベントだけを取り込む
            if (m.isNoteOnOrOff() || m.isPitchWheel() || m.isController()
                || m.isProgramChange() || m.isAftertouch() || m.isChannelPressure()
                || m.isSustainPedalOn() || m.isSustainPedalOff())
            {
                out.sequence.addEvent(m);  // タイムスタンプは秒（mf.convertTimestampTicksToSeconds 適用済み）
                if (m.isNoteOn())
                {
                    ++out.numNoteOnEvents;
                    const int ch = m.getChannel() - 1;
                    if (ch >= 0 && ch < 16) ++chCount[(size_t)ch];
                }
                maxEnd = juce::jmax(maxEnd, m.getTimeStamp());
            }
        }

        // Note On/Off の対応付け
        out.sequence.updateMatchedPairs();

        // 主なチャンネル
        int bestCh = -1, bestN = 0;
        for (int ch = 0; ch < 16; ++ch)
            if (chCount[(size_t)ch] > bestN) { bestN = chCount[(size_t)ch]; bestCh = ch; }
        if (bestCh >= 0) out.primaryChannel = bestCh;
        out.isDrum = (out.primaryChannel == 9);

        // ノートが 0 のトラックは取り込まない（テンポ専用トラック等）
        if (out.numNoteOnEvents > 0)
            result.tracks.push_back(std::move(out));
    }

    result.endTimeSecs = maxEnd;
    result.ok = true;
    return result;
}
