// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>
#include <vector>

namespace SilenceDetector
{
    struct Region { double start; double end; }; // クリップ内秒数

    // 元クリップ範囲 [0, duration] の中で、振幅が threshold 未満の状態が
    // minSilenceSecs 以上続く区間を返す。block 単位 (~10ms) で peak を見る簡易方式。
    inline std::vector<Region> detect(const juce::File& file,
                                      double fileOffset,
                                      double duration,
                                      float thresholdLinear,
                                      double minSilenceSecs,
                                      juce::AudioFormatManager& fmt)
    {
        std::vector<Region> out;
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
        if (!reader || reader->sampleRate <= 0.0 || reader->numChannels <= 0) return out;

        const double sr = reader->sampleRate;
        const juce::int64 startSample = (juce::int64)(fileOffset * sr);
        const juce::int64 endSample   = juce::jmin(reader->lengthInSamples,
                                                    (juce::int64)((fileOffset + duration) * sr));
        if (endSample <= startSample) return out;

        const int blockSize = juce::jmax(64, (int)(sr * 0.010));  // 10ms ブロック
        juce::AudioBuffer<float> buf((int)reader->numChannels, blockSize);

        bool   inSilence    = false;
        double silenceStart = 0.0;
        juce::int64 cursor = startSample;

        while (cursor < endSample)
        {
            const int toRead = (int)juce::jmin((juce::int64)blockSize, endSample - cursor);
            buf.clear();
            reader->read(&buf, 0, toRead, cursor, true, true);

            float peak = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                peak = juce::jmax(peak, buf.getMagnitude(ch, 0, toRead));

            const double blockStartInClip = (double)(cursor - startSample) / sr;
            const double blockEndInClip   = blockStartInClip + (double)toRead / sr;

            if (peak < thresholdLinear)
            {
                if (!inSilence) { inSilence = true; silenceStart = blockStartInClip; }
            }
            else
            {
                if (inSilence)
                {
                    const double silenceEnd = blockStartInClip;
                    if (silenceEnd - silenceStart >= minSilenceSecs)
                        out.push_back({ silenceStart, silenceEnd });
                    inSilence = false;
                }
            }
            cursor += toRead;
        }
        if (inSilence)
        {
            const double silenceEnd = duration;
            if (silenceEnd - silenceStart >= minSilenceSecs)
                out.push_back({ silenceStart, silenceEnd });
        }
        return out;
    }

    // silence の補集合 = 残すセグメント。padding 適用後、隣接マージ。
    inline std::vector<Region> regionsToKeep(const std::vector<Region>& silence,
                                             double duration,
                                             double padBeforeSecs,
                                             double padAfterSecs)
    {
        std::vector<Region> keep;
        double cursor = 0.0;
        for (auto& s : silence)
        {
            if (s.start > cursor) keep.push_back({ cursor, s.start });
            cursor = s.end;
        }
        if (cursor < duration) keep.push_back({ cursor, duration });

        // パディング: 各セグメントの境界を外側に広げる
        for (auto& k : keep)
        {
            k.start = juce::jmax(0.0,      k.start - padBeforeSecs);
            k.end   = juce::jmin(duration, k.end   + padAfterSecs);
        }
        // 重なりをマージ
        std::vector<Region> merged;
        for (auto& k : keep)
        {
            if (!merged.empty() && k.start <= merged.back().end + 1e-6)
                merged.back().end = juce::jmax(merged.back().end, k.end);
            else
                merged.push_back(k);
        }
        return merged;
    }
}
