// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "WindowState.h"

juce::File WindowState::getStoreFile()
{
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Utawave");
    root.createDirectory();
    return root.getChildFile("window.txt");
}

WindowState WindowState::load()
{
    WindowState s;
    auto f = getStoreFile();
    if (!f.existsAsFile()) return s;

    const auto txt   = f.loadFileAsString();
    const auto parts = juce::StringArray::fromTokens(txt, ",", "");
    if (parts.size() >= 2)
    {
        const int w = parts[0].getIntValue();
        const int h = parts[1].getIntValue();
        if (w >= minWidth && h >= minHeight && w < 16384 && h < 16384)
        {
            s.width  = w;
            s.height = h;
        }
    }
    return s;
}

void WindowState::save() const
{
    const int w = juce::jmax(minWidth,  width);
    const int h = juce::jmax(minHeight, height);
    const auto txt = juce::String(w) + "," + juce::String(h);
    getStoreFile().replaceWithText(txt);
}
