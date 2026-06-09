// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include "../AppColours.h"

// メーター描画の共通ヘルパ。
// TrackHeaderView (横メーター) と MasterPanel (縦メーター) では UI の形状が異なるため
// 描画ロジック全体は共通化できないが、dB→正規化変換と色判定は集約しておく。
namespace MeterDraw
{
    // 標準的なメーター範囲 (-60 dB 〜 0 dB) で db を 0.0..1.0 に変換。
    inline float dbToNorm(float db, float minDb = -60.0f, float maxDb = 0.0f)
    {
        return juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }

    // メーターのゾーン境界 (dB)。赤は 0 dBFS 以上 = ピークオーバー (クリップ) を意味する。
    static constexpr float kRedDb    =  0.0f;   // これ以上は赤 (0 dBFS オーバー)
    static constexpr float kYellowDb = -6.0f;   // これ以上 0dB 未満は黄 (クリップ手前)

    // dB レベルに応じた標準色を返す:
    //    0 dBFS 以上 → 赤 (ピークオーバー)
    //   -6 〜 0 dB   → 黄 (クリップ手前)
    //    それ以下     → 緑
    inline juce::Colour colourForDb(float db)
    {
        if (db >= kRedDb)    return AppColours::meterRed;
        if (db >= kYellowDb) return AppColours::meterYellow;
        return AppColours::meterGreen;
    }

    // 縦メーター描画 (MasterPanel 等で使用)。
    // db: 現在値、holdDb: ピークホールド表示 (なし = minDb 未満)、vuRefLevel: 0 VU 基準線
    inline void drawVertical(juce::Graphics& g, juce::Rectangle<int> b,
                              float db, float holdDb,
                              float vuReferenceLevel = -18.0f,
                              float minDb = -60.0f, float maxDb = 0.0f)
    {
        g.setColour(AppColours::meterBg);
        g.fillRect(b);
        g.setColour(AppColours::separator);
        g.drawRect(b, 1);

        const float norm = dbToNorm(db, minDb, maxDb);
        const int   fillH = (int)(b.getHeight() * norm);
        if (fillH > 0)
        {
            // レベルごとにゾーン塗り分け。赤は 0 dBFS 以上 (ピークオーバー) の部分だけに出る。
            const int fillTopY = b.getBottom() - fillH;
            auto yForDb = [&](float d)
            {
                return b.getBottom() - (int)(b.getHeight() * dbToNorm(d, minDb, maxDb));
            };
            const int yellowY = yForDb(kYellowDb);   // -6 dB ライン
            const int redY    = yForDb(kRedDb);      //  0 dBFS ライン

            // 緑: bottom 〜 -6dB
            {
                const int top = juce::jmax(fillTopY, yellowY);
                if (top < b.getBottom())
                {
                    g.setColour(AppColours::meterGreen);
                    g.fillRect(b.getX(), top, b.getWidth(), b.getBottom() - top);
                }
            }
            // 黄: -6dB 〜 0dB
            if (fillTopY < yellowY)
            {
                const int top = juce::jmax(fillTopY, redY);
                if (top < yellowY)
                {
                    g.setColour(AppColours::meterYellow);
                    g.fillRect(b.getX(), top, b.getWidth(), yellowY - top);
                }
            }
            // 赤: 0dBFS 以上 (ピークオーバー)
            if (fillTopY < redY)
            {
                g.setColour(AppColours::meterRed);
                g.fillRect(b.getX(), fillTopY, b.getWidth(), redY - fillTopY);
            }
        }

        // 0 VU 基準線（破線風: 短い線分を 2 本）
        if (vuReferenceLevel > minDb && vuReferenceLevel < maxDb)
        {
            const float refNorm = dbToNorm(vuReferenceLevel, minDb, maxDb);
            const int   refY    = b.getBottom() - (int)(b.getHeight() * refNorm);
            g.setColour(AppColours::accent.withAlpha(0.75f));
            const float left  = (float) b.getX();
            const float right = (float) b.getRight();
            const float mid   = (left + right) * 0.5f;
            g.drawLine(left,        (float) refY, mid - 1.0f,    (float) refY, 1.0f);
            g.drawLine(mid + 1.0f,  (float) refY, right,         (float) refY, 1.0f);
        }

        if (holdDb > minDb)
        {
            const float hNorm = dbToNorm(holdDb, minDb, maxDb);
            const int   hy    = b.getBottom() - (int)(b.getHeight() * hNorm);
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.drawHorizontalLine(hy, (float) b.getX(), (float) b.getRight());
        }
    }
} // namespace MeterDraw
