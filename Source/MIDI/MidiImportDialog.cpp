// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "MidiImportDialog.h"
#include "../Localisation.h"
#include "../AppColours.h"

MidiImportDialog::TrackRow::TrackRow(int idx,
                                     const MidiImporter::ImportedTrack& trk,
                                     bool drumDefault)
{

    checkBtn.setToggleState(!drumDefault, juce::dontSendNotification);
    checkBtn.setColour(juce::ToggleButton::tickColourId, AppColours::accent);
    checkBtn.setColour(juce::ToggleButton::textColourId, AppColours::text);
    addAndMakeVisible(checkBtn);

    nameLabel.setText(juce::String(idx + 1) + ". " + trk.name, juce::dontSendNotification);
    nameLabel.setColour(juce::Label::textColourId, AppColours::textBright);
    nameLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(nameLabel);

    juce::String info;
    info << tr(u8"ch ") << juce::String(trk.primaryChannel + 1);
    if (trk.isDrum) info << tr(u8"  (Drum)");
    info << tr(u8"   ノート:") << juce::String(trk.numNoteOnEvents);
    infoLabel.setText(info, juce::dontSendNotification);
    infoLabel.setColour(juce::Label::textColourId, AppColours::textDim);
    infoLabel.setFont(juce::FontOptions(11.0f));
    infoLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(infoLabel);
}

void MidiImportDialog::TrackRow::resized()
{
    auto b = getLocalBounds().reduced(4, 2);
    checkBtn.setBounds(b.removeFromLeft(24));
    auto infoArea = b.removeFromRight(180);
    infoLabel.setBounds(infoArea);
    nameLabel.setBounds(b);
}

MidiImportDialog::MidiImportDialog(const MidiImporter::ImportResult& imp)
    : imported(imp)
{

    titleLabel.setText(tr(u8"MIDI を読み込む"), juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    titleLabel.setColour(juce::Label::textColourId, AppColours::textBright);
    addAndMakeVisible(titleLabel);

    juce::String info;
    info << tr(u8"トラック数: ") << juce::String((int)imported.tracks.size())
         << tr(u8"    長さ: ") << juce::String(imported.endTimeSecs, 2) << tr(u8" 秒")
         << tr(u8"    テンポ: ") << juce::String(imported.initialBpm, 1) << " BPM";
    infoLabel.setText(info, juce::dontSendNotification);
    infoLabel.setFont(juce::FontOptions(11.0f));
    infoLabel.setColour(juce::Label::textColourId, AppColours::textDim);
    addAndMakeVisible(infoLabel);

    buildList();
    addAndMakeVisible(listViewport);
    listViewport.setViewedComponent(&listContent, false);
    listViewport.setScrollBarsShown(true, false);

    // 配置オプション（排他トグル）
    placeStartBtn.setRadioGroupId(901);
    placePlayheadBtn.setRadioGroupId(901);
    placeStartBtn.setToggleState(true, juce::dontSendNotification);
    for (auto* b : { &placeStartBtn, &placePlayheadBtn })
    {
        b->setColour(juce::ToggleButton::tickColourId, AppColours::accent);
        b->setColour(juce::ToggleButton::textColourId, AppColours::text);
        addAndMakeVisible(*b);
    }

    markersBtn.setToggleState(true, juce::dontSendNotification);
    markersBtn.setColour(juce::ToggleButton::tickColourId, AppColours::accent);
    markersBtn.setColour(juce::ToggleButton::textColourId, AppColours::text);
    addAndMakeVisible(markersBtn);

    tempoMeterBtn.setToggleState(true, juce::dontSendNotification);
    tempoMeterBtn.setColour(juce::ToggleButton::tickColourId, AppColours::accent);
    tempoMeterBtn.setColour(juce::ToggleButton::textColourId, AppColours::text);
    addAndMakeVisible(tempoMeterBtn);

    okBtn.setColour(juce::TextButton::buttonColourId, AppColours::accent);
    okBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    okBtn.onClick = [this] {
        if (onClose) onClose(getResult());
    };
    addAndMakeVisible(okBtn);

    cancelBtn.setColour(juce::TextButton::buttonColourId, AppColours::buttonBg);
    cancelBtn.setColour(juce::TextButton::textColourOffId, AppColours::textDim);
    cancelBtn.onClick = [this] {
        Result r; r.accepted = false;
        if (onClose) onClose(r);
    };
    addAndMakeVisible(cancelBtn);
}

void MidiImportDialog::buildList()
{
    rows.clear();
    listContent.removeAllChildren();
    for (size_t i = 0; i < imported.tracks.size(); ++i)
    {
        auto* row = new TrackRow((int)i, imported.tracks[i], imported.tracks[i].isDrum);
        rows.add(row);
        listContent.addAndMakeVisible(row);
    }
}

void MidiImportDialog::paint(juce::Graphics& g)
{
    g.fillAll(AppColours::panelBg);
}

void MidiImportDialog::resized()
{
    auto b = getLocalBounds().reduced(12);
    titleLabel.setBounds(b.removeFromTop(22));
    infoLabel.setBounds(b.removeFromTop(18));
    b.removeFromTop(8);

    auto bottom = b.removeFromBottom(70);
    // オプション
    auto optsArea = bottom.removeFromTop(38);
    auto leftCol  = optsArea.removeFromLeft(optsArea.getWidth() / 2);
    placeStartBtn.setBounds(leftCol.removeFromTop(18));
    placePlayheadBtn.setBounds(leftCol.removeFromTop(18));
    markersBtn.setBounds(optsArea.removeFromTop(18));
    tempoMeterBtn.setBounds(optsArea.removeFromTop(18));

    // OK / Cancel
    auto btnRow = bottom.removeFromTop(28);
    okBtn.setBounds(btnRow.removeFromRight(110));
    btnRow.removeFromRight(8);
    cancelBtn.setBounds(btnRow.removeFromRight(110));

    listViewport.setBounds(b);
    const int rowH = 26;
    listContent.setSize(b.getWidth() - 16, (int)rows.size() * rowH);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds(0, i * rowH, listContent.getWidth(), rowH);
}

MidiImportDialog::Result MidiImportDialog::getResult() const
{
    Result r;
    r.accepted = true;
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i]->checkBtn.getToggleState())
            r.selectedTrackIndices.push_back(i);
    r.placement     = placeStartBtn.getToggleState() ? Placement::AtStart : Placement::AtPlayhead;
    r.importMarkers = markersBtn.getToggleState();
    r.importTempoMeter = tempoMeterBtn.getToggleState();
    return r;
}
