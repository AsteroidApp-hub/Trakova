// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "AudioDeviceSettings.h"

namespace AudioDeviceSettings
{

juce::File getStateFile()
{
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Trakova");
    root.createDirectory();
    return root.getChildFile("audio_device.xml");
}

std::unique_ptr<juce::XmlElement> loadState()
{
    auto f = getStateFile();
    if (!f.existsAsFile()) return nullptr;
    return juce::XmlDocument::parse(f);
}

void saveState(const juce::AudioDeviceManager& dm)
{
    if (auto xml = dm.createStateXml())
        xml->writeTo(getStateFile());
}

juce::String initialise(juce::AudioDeviceManager& dm, int numInputs, int numOutputs)
{
    if (auto xml = loadState())
        return dm.initialise(numInputs, numOutputs, xml.get(), true);
    return dm.initialiseWithDefaultDevices(numInputs, numOutputs);
}

juce::String getDeviceSummary(const juce::AudioDeviceManager& dm)
{
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        auto in  = dev->getName();
        auto type = dm.getCurrentAudioDeviceType();
        // 多くの環境で入力/出力同名のため デバイス名 + サンプリングレートを表記
        return type + " : " + in
            + " (" + juce::String((int)dev->getCurrentSampleRate()) + " Hz)";
    }
    return {};
}

} // namespace AudioDeviceSettings
