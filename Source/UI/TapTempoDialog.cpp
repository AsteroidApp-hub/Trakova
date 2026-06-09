// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "TapTempoDialog.h"

TapTempoDialog::TapTempoDialog(double initialBpm)
{
    currentEstimate = initialBpm;
    setWantsKeyboardFocus(true);

    auto J = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };
    auto styleLbl = [this](juce::Label& l, juce::String txt, float sz,
                            juce::Colour col, juce::Justification just)
    {
        l.setText(txt, juce::dontSendNotification);
        l.setFont(juce::FontOptions(sz));
        l.setColour(juce::Label::textColourId, col);
        l.setJustificationType(just);
        addAndMakeVisible(l);
    };
    styleLbl(hintLbl,
             J(u8"TAP ボタンを拍に合わせて連打\n（やり直す場合は Reset）"),
             12.0f, juce::Colour(0xffaaaaaa), juce::Justification::centredTop);
    styleLbl(bpmLbl,   "--- BPM", 38.0f, juce::Colours::white, juce::Justification::centred);
    styleLbl(countLbl, J(u8"タップ: 0"), 11.0f,
             juce::Colour(0xff888888), juce::Justification::centred);

    tapBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff5a3a1a));
    tapBtn.setColour(juce::TextButton::textColourOffId,  juce::Colours::white);
    tapBtn.setWantsKeyboardFocus(false);
    tapBtn.onClick = [this] { registerTap(); };
    addAndMakeVisible(tapBtn);

    applyBtn.setWantsKeyboardFocus(false);
    applyBtn.onClick = [this] {
        if (taps.size() >= 2 && onApply) onApply(currentEstimate);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    cancelBtn.setWantsKeyboardFocus(false);
    cancelBtn.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    resetBtn.setWantsKeyboardFocus(false);
    resetBtn.onClick = [this] { taps.clear(); updateDisplay(); };
    addAndMakeVisible(applyBtn);
    addAndMakeVisible(cancelBtn);
    addAndMakeVisible(resetBtn);

    setSize(380, 240);
}

void TapTempoDialog::registerTap()
{
    const juce::int64 now = juce::Time::currentTimeMillis();
    taps.push_back(now);
    if (taps.size() > 8)
        taps.erase(taps.begin(), taps.begin() + ((int) taps.size() - 8));
    updateDisplay();
}

void TapTempoDialog::updateDisplay()
{
    auto J = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };
    countLbl.setText(J(u8"タップ: ") + juce::String((int) taps.size()),
                     juce::dontSendNotification);
    if (taps.size() < 2)
    {
        bpmLbl.setText("--- BPM", juce::dontSendNotification);
        return;
    }
    double sumMs = 0.0;
    for (size_t i = 1; i < taps.size(); ++i)
        sumMs += (double)(taps[i] - taps[i - 1]);
    const double avg = sumMs / (double)(taps.size() - 1);
    if (avg <= 0.0) return;
    double newBpm = std::round(juce::jlimit(20.0, 300.0, 60000.0 / avg));
    currentEstimate = newBpm;
    bpmLbl.setText(juce::String((int) newBpm) + " BPM", juce::dontSendNotification);
}

bool TapTempoDialog::keyPressed(const juce::KeyPress& k)
{
    // Space は再生トグルとぶつかるので採用しない (TAP ボタンのクリックのみ)
    if (k == juce::KeyPress::returnKey)
    {
        applyBtn.triggerClick();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        cancelBtn.triggerClick();
        return true;
    }
    return false;
}

void TapTempoDialog::resized()
{
    const int W = getWidth();
    hintLbl  .setBounds(10, 12, W - 20, 32);
    bpmLbl   .setBounds(10, 54, W - 20, 50);
    countLbl .setBounds(10, 106, W - 20, 16);
    tapBtn   .setBounds(W / 2 - 70, 130, 140, 44);

    const int btnW = 80, btnH = 26, gap = 8, margin = 10;
    const int y    = getHeight() - btnH - margin;
    resetBtn .setBounds(margin, y, 70, btnH);
    applyBtn .setBounds(W - btnW - margin, y, btnW, btnH);
    cancelBtn.setBounds(W - btnW - margin - gap - btnW, y, btnW, btnH);
}

void TapTempoDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
}
