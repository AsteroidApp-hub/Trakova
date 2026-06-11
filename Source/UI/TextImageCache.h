// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

/**
    描画済みのテキスト Image を (text, font, colour, size, justify) でキーに持つキャッシュ。
    JUCE 8 の drawText は内部で HarfBuzz シェーピング + フォントキャッシュ参照を毎回行うため、
    固定ラベル（バー番号、Peak、VU、Tempo、トラック名、クリップ名等）が頻繁に再描画されると
    支配的なコストになる。一度 Image に焼いて blit すれば次回からほぼゼロコスト。

    ⚠ 将来の JUCE バージョンアップ時 (9 / 10 等) には、HarfBuzz 統合の改善で
    この workaround 自体が不要になっている可能性があります。アップグレード時には
    本キャッシュを通す/通さないでフレームレート計測を行い、効果を再検証してください。
    効果が確認できない場合は本キャッシュを撤去して直接 drawText に戻すのが望ましい。
*/
class TextImageCache
{
public:
    static TextImageCache& getInstance()
    {
        static TextImageCache instance;
        return instance;
    }

    /// 指定領域にテキストを描画。最初の呼び出しでレンダリングして Image に焼き、以降は blit。
    void drawText(juce::Graphics& g,
                  const juce::String& text,
                  juce::Rectangle<int> bounds,
                  const juce::Font& font,
                  juce::Colour colour,
                  juce::Justification justify)
    {
        if (text.isEmpty() || bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
            return;

        Key k;
        k.text       = text;
        k.fontHeight = font.getHeight();
        k.fontStyle  = font.getStyleFlags();
        k.colourArgb = colour.getARGB();
        k.w          = bounds.getWidth();
        k.h          = bounds.getHeight();
        k.justify    = justify.getFlags();

        juce::Image img;
        {
            const juce::ScopedLock sl(lock);
            auto it = cache.find(k);
            if (it != cache.end())
                img = it->second;
        }

        if (!img.isValid())
        {
            img = juce::Image(juce::Image::ARGB, k.w, k.h, true);
            juce::Graphics ig(img);
            ig.setFont(font);
            ig.setColour(colour);
            ig.drawText(text, 0, 0, k.w, k.h, justify, true);

            const juce::ScopedLock sl(lock);
            // 単純な上限管理: 上限を超えたら先頭から削る
            if (cache.size() >= maxEntries)
                cache.erase(cache.begin());
            cache[k] = img;
        }

        g.drawImageAt(img, bounds.getX(), bounds.getY());
    }

    /// 上記オーバーロード (x,y,w,h 直接指定)
    void drawText(juce::Graphics& g,
                  const juce::String& text,
                  int x, int y, int w, int h,
                  const juce::Font& font,
                  juce::Colour colour,
                  juce::Justification justify)
    {
        drawText(g, text, { x, y, w, h }, font, colour, justify);
    }

    void clear()
    {
        const juce::ScopedLock sl(lock);
        cache.clear();
    }

private:
    struct Key
    {
        juce::String text;
        float        fontHeight   { 0 };
        int          fontStyle    { 0 };
        juce::uint32 colourArgb   { 0 };
        int          w            { 0 };
        int          h            { 0 };
        int          justify      { 0 };
        bool operator<(const Key& o) const
        {
            if (text       != o.text)       return text       < o.text;
            if (fontHeight != o.fontHeight) return fontHeight < o.fontHeight;
            if (fontStyle  != o.fontStyle)  return fontStyle  < o.fontStyle;
            if (colourArgb != o.colourArgb) return colourArgb < o.colourArgb;
            if (w          != o.w)          return w          < o.w;
            if (h          != o.h)          return h          < o.h;
            return justify < o.justify;
        }
    };

    std::map<Key, juce::Image> cache;
    juce::CriticalSection      lock;
    static constexpr size_t    maxEntries { 2048 };
};
