// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include "../Localisation.h"
#include "MidiImporter.h"

/**
    MIDI 読み込み時のトラック選択ダイアログ。
    各トラックをチェックボックスで選び、配置オプションも指定する。
*/
class MidiImportDialog : public juce::Component
{
public:
    enum class Placement { AtStart, AtPlayhead };

    struct Result
    {
        bool      accepted { false };
        std::vector<int> selectedTrackIndices;
        Placement placement { Placement::AtStart };
        bool      importMarkers { true };
        bool      importTempoMeter { true };
    };

    MidiImportDialog(const MidiImporter::ImportResult& importedFile);

    void resized() override;
    void paint(juce::Graphics&) override;

    Result getResult() const;
    std::function<void(Result)> onClose;

private:
    void buildList();

    const MidiImporter::ImportResult& imported;

    juce::Label titleLabel;
    juce::Label infoLabel;

    // トラックリスト（スクロール領域）
    class TrackRow : public juce::Component
    {
    public:
        TrackRow(int idx, const MidiImporter::ImportedTrack& tr, bool drumDefault);
        void resized() override;

        juce::ToggleButton checkBtn;
        juce::Label        nameLabel;
        juce::Label        infoLabel;
    };
    juce::OwnedArray<TrackRow> rows;
    juce::Viewport listViewport;
    juce::Component listContent;

    juce::ToggleButton placeStartBtn { tr(u8"先頭に配置") };
    juce::ToggleButton placePlayheadBtn { tr(u8"現在の再生位置に配置") };
    juce::ToggleButton markersBtn { tr(u8"マーカーも取り込む") };
    juce::ToggleButton tempoMeterBtn { tr(u8"テンポ・拍子も取り込む") };

    juce::TextButton okBtn     { tr(u8"読み込み") };
    juce::TextButton cancelBtn { tr(u8"キャンセル") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiImportDialog)
};
