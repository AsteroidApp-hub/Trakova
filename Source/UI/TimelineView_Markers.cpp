// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// TimelineView のマーカー操作・名前編集 (TimelineView_Edit.cpp から分割)。

#include "TimelineView.h"
#include "../Localisation.h"
#include "../AppColours.h"
#include "../Tracks/MidiClip.h"
#include "../Edit/SilenceDetector.h"
#include "TextImageCache.h"
#include <set>
#include <map>

void TimelineView::addMarkerAtTime(double t, const juce::String& name)
{
    static const std::array<juce::Colour, 8> palette = {{
        juce::Colour(0xffffaa44), juce::Colour(0xffff5555),
        juce::Colour(0xff55cc55), juce::Colour(0xff5599ff),
        juce::Colour(0xffffdd44), juce::Colour(0xffaa66cc),
        juce::Colour(0xffff66aa), juce::Colour(0xff44cccc)
    }};
    static juce::Random rng;

    auto markersCopy = ruler.getMarkers();
    Marker m;
    m.time   = snapTime(t);
    m.name   = name;
    m.colour = palette[rng.nextInt((int)palette.size())];
    markersCopy.push_back(m);
    std::sort(markersCopy.begin(), markersCopy.end(),
              [](const Marker& a, const Marker& b){ return a.time < b.time; });
    ruler.setMarkers(markersCopy);

    int idx = -1;
    auto& list = ruler.getMarkers();
    for (size_t i = 0; i < list.size(); ++i)
        if (std::abs(list[i].time - m.time) < 1e-6) { idx = (int)i; break; }
    if (idx >= 0) beginMarkerNameEdit(idx);
}

bool TimelineView::jumpToNextMarker(double currentTime, double& outTime)
{
    auto& list = ruler.getMarkers();
    for (auto& m : list)
        if (m.time > currentTime + 0.01) { outTime = m.time; return true; }
    return false;
}

bool TimelineView::jumpToPrevMarker(double currentTime, double& outTime)
{
    auto& list = ruler.getMarkers();
    Marker* prev = nullptr;
    for (auto& m : const_cast<std::vector<Marker>&>(list))
    {
        if (m.time < currentTime - 0.05)
        {
            if (!prev || m.time > prev->time) prev = &m;
        }
    }
    if (prev) { outTime = prev->time; return true; }
    return false;
}

void TimelineView::beginMarkerNameEdit(int markerIdx)
{
    finishMarkerNameEdit(false);
    auto& list = ruler.getMarkers();
    if (markerIdx < 0 || markerIdx >= (int)list.size()) return;
    editingMarkerIdx = markerIdx;
    auto& m = list[markerIdx];

    const double bps = bpm / 60.0;
    int x = (int)(m.time * bps * pixelsPerBeat - scrollX);

    markerNameEditor = std::make_unique<juce::TextEditor>();
    markerNameEditor->setBounds(x + 16, 0, 200, 16);
    markerNameEditor->setText(m.name, juce::dontSendNotification);
    markerNameEditor->setFont(juce::FontOptions(11.0f));
    markerNameEditor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.85f));
    markerNameEditor->setColour(juce::TextEditor::textColourId,        juce::Colour(0xffffaa44));
    markerNameEditor->setColour(juce::TextEditor::highlightColourId,   juce::Colour(0xff5a8aaa));
    markerNameEditor->setColour(juce::TextEditor::outlineColourId,     juce::Colour(0xffffaa44));
    markerNameEditor->setBorder({1, 1, 1, 1});
    markerNameEditor->onReturnKey = [this] { finishMarkerNameEdit(true); };
    markerNameEditor->onEscapeKey = [this] { finishMarkerNameEdit(false); };
    markerNameEditor->onFocusLost = [this] { finishMarkerNameEdit(true); };
    addAndMakeVisible(markerNameEditor.get());
    markerNameEditor->grabKeyboardFocus();
    markerNameEditor->selectAll();
}

void TimelineView::finishMarkerNameEdit(bool commit)
{
    if (!markerNameEditor) return;
    if (commit && editingMarkerIdx >= 0)
    {
        auto markersCopy = ruler.getMarkers();
        if (editingMarkerIdx < (int)markersCopy.size()
            && markersCopy[editingMarkerIdx].name != markerNameEditor->getText())
        {
            // 名前変更を 1 つの Undo として記録 (ルーラーの 3 リストを before/after で捕捉)
            ruler.beginMusicEdit();
            markersCopy[editingMarkerIdx].name = markerNameEditor->getText();
            ruler.setMarkers(markersCopy);
            ruler.commitMusicEdit();
        }
    }
    removeChildComponent(markerNameEditor.get());
    markerNameEditor.reset();
    editingMarkerIdx = -1;
    repaint();
}
