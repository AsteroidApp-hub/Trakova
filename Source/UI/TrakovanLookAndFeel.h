// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include "../AppColours.h"

class TrakovanLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TrakovanLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, AppColours::panelBg);
        setColour(juce::AlertWindow::backgroundColourId,     AppColours::panelBg);
        setColour(juce::AlertWindow::textColourId,           AppColours::text);
        setColour(juce::TextButton::buttonColourId,          AppColours::buttonBg);
        setColour(juce::TextButton::textColourOffId,         AppColours::text);
        setColour(juce::ComboBox::backgroundColourId,        AppColours::buttonBg);
        setColour(juce::ComboBox::textColourId,              AppColours::text);
        setColour(juce::PopupMenu::backgroundColourId,       AppColours::panelBg);
        setColour(juce::PopupMenu::textColourId,             AppColours::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, AppColours::accent);
        setColour(juce::Label::textColourId,                 AppColours::text);
    }

    // DAW スタイルの垂直フェーダー
    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float /*minSliderPos*/, float /*maxSliderPos*/,
                          juce::Slider::SliderStyle style,
                          juce::Slider& /*slider*/) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                             sliderPos, 0, 0, style, *((juce::Slider*)nullptr));
            return;
        }

        // ── Track (groove) ──
        const int trackW = 4;
        const int trackX = x + width / 2 - trackW / 2;
        g.setColour(AppColours::meterBg);
        g.fillRoundedRectangle((float)trackX, (float)y,
                               (float)trackW, (float)height, 2.0f);
        g.setColour(AppColours::separator);
        g.drawRoundedRectangle((float)trackX, (float)y,
                               (float)trackW, (float)height, 2.0f, 1.0f);

        // ── Fader cap ──
        const int capW = width - 6;
        const int capH = 20;
        const int capX = x + 3;
        const int capY = (int)sliderPos - capH / 2;

        // shadow
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle((float)(capX + 1), (float)(capY + 2),
                               (float)capW, (float)capH, 3.0f);

        // cap body
        juce::ColourGradient grad(
            AppColours::buttonHover, (float)capX, (float)capY,
            AppColours::buttonBg,    (float)capX, (float)(capY + capH),
            false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle((float)capX, (float)capY,
                               (float)capW, (float)capH, 3.0f);

        // cap border
        g.setColour(AppColours::separatorLight);
        g.drawRoundedRectangle((float)capX, (float)capY,
                               (float)capW, (float)capH, 3.0f, 1.0f);

        // center notch line
        const int midY = capY + capH / 2;
        g.setColour(AppColours::accent);
        g.drawLine((float)(capX + 4), (float)midY,
                   (float)(capX + capW - 4), (float)midY, 1.5f);
    }

    int getSliderThumbRadius(juce::Slider&) override { return 0; }
};
