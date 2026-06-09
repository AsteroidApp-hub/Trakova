// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>

class RecentProjects
{
public:
    static juce::File getStoreFile();

    static juce::Array<juce::File> load();
    static void add(const juce::File& projectFile);
    static void remove(const juce::File& projectFile);
    static void clear();

    static constexpr int maxEntries = 10;
};
