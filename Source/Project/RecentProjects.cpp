// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#include "RecentProjects.h"

juce::File RecentProjects::getStoreFile()
{
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Utawave");
    root.createDirectory();
    return root.getChildFile("recents.txt");
}

juce::Array<juce::File> RecentProjects::load()
{
    juce::Array<juce::File> out;
    auto f = getStoreFile();
    if (!f.existsAsFile()) return out;

    juce::StringArray lines;
    f.readLines(lines);
    for (auto& line : lines)
    {
        auto t = line.trim();
        if (t.isEmpty()) continue;
        juce::File pf(t);
        if (pf.existsAsFile()) out.add(pf);
    }
    return out;
}

static void writeBack(const juce::Array<juce::File>& list)
{
    juce::StringArray lines;
    for (auto& f : list) lines.add(f.getFullPathName());
    auto txt = lines.joinIntoString("\n");
    RecentProjects::getStoreFile().replaceWithText(txt);
}

void RecentProjects::add(const juce::File& projectFile)
{
    if (!projectFile.existsAsFile()) return;
    auto list = load();
    list.removeAllInstancesOf(projectFile);
    list.insert(0, projectFile);
    while (list.size() > maxEntries) list.removeLast();
    writeBack(list);
}

void RecentProjects::remove(const juce::File& projectFile)
{
    auto list = load();
    list.removeAllInstancesOf(projectFile);
    writeBack(list);
}

void RecentProjects::clear()
{
    getStoreFile().deleteFile();
}
