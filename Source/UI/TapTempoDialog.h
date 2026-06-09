// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

// タップでテンポを推定するモーダルダイアログ。
// TAP ボタンを連打すると平均間隔から BPM を算出し、OK で適用する。
class TapTempoDialog : public juce::Component
{
public:
    explicit TapTempoDialog(double initialBpm);

    // OK 押下時に推定 BPM を受け取るコールバック。
    std::function<void(double)> onApply;

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& k) override;

private:
    void registerTap();
    void updateDisplay();

    std::vector<juce::int64> taps;
    double                   currentEstimate { 120.0 };
    juce::TextButton         tapBtn   { "TAP" };
    juce::TextButton         applyBtn { "OK" };
    juce::TextButton         cancelBtn{ "Cancel" };
    juce::TextButton         resetBtn { "Reset" };
    juce::Label              bpmLbl, countLbl, hintLbl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapTempoDialog)
};
