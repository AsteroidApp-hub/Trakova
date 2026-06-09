// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

namespace AudioDeviceSettings
{
    juce::File getStateFile();
    std::unique_ptr<juce::XmlElement> loadState();
    void saveState(const juce::AudioDeviceManager& dm);

    // 共有状態でデバイスマネージャを初期化する。保存済み状態が無ければデフォルトで初期化。
    juce::String initialise(juce::AudioDeviceManager& dm, int numInputs, int numOutputs);

    // 現在のデバイス名（"出力 / 入力" 形式）。デバイス未設定時は空文字
    juce::String getDeviceSummary(const juce::AudioDeviceManager& dm);

    // 保有期間中、deviceManager の変更を監視して自動的に状態を保存する RAII ヘルパ
    class AutoSaver : public juce::ChangeListener
    {
    public:
        explicit AutoSaver(juce::AudioDeviceManager& d) : dm(d) { dm.addChangeListener(this); }
        ~AutoSaver() override { dm.removeChangeListener(this); }
        void changeListenerCallback(juce::ChangeBroadcaster*) override { saveState(dm); }
    private:
        juce::AudioDeviceManager& dm;
    };
}
