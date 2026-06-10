// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "AppPreferences.h"

bool AppPreferences::adsCompiledIn()
{
   #if defined(UTAWAVE_ADS_ENABLED) && (UTAWAVE_ADS_ENABLED)
    return true;
   #else
    return false;   // 公開ソース / 通常ビルドの既定
   #endif
}

juce::File AppPreferences::getStoreFile()
{
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Utawave");
    root.createDirectory();
    return root.getChildFile("app_prefs.xml");
}

AppPreferences AppPreferences::load()
{
    AppPreferences p;
    if (auto xml = juce::XmlDocument::parse(getStoreFile()))
    {
        p.showMidiExportMenu = xml->getBoolAttribute("showMidiExportMenu", p.showMidiExportMenu);
        p.showAds            = xml->getBoolAttribute("showAds", p.showAds);
    }
    return p;
}

void AppPreferences::save() const
{
    juce::XmlElement xml("Preferences");
    xml.setAttribute("showMidiExportMenu", showMidiExportMenu);
    xml.setAttribute("showAds", showAds);
    xml.writeTo(getStoreFile());
}
