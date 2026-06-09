// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#pragma once
#include <JuceHeader.h>
#include "../Localisation.h"
#include "../Audio/AudioDeviceSettings.h"
#include "AdPanel.h"

class StartupComponent : public juce::Component,
                         public juce::ListBoxModel,
                         private juce::ChangeListener
{
public:
    // showAds: 右端に広告枠を表示するか (アプリ全体設定 AppPreferences::showAds に対応)。
    explicit StartupComponent(bool showAds = true);
    ~StartupComponent() override;

    // 起動ウィンドウの推奨サイズ。広告枠の有無で横幅が変わる。
    static constexpr int kWidthWithAds = 1080;
    static constexpr int kWidthNoAds   = 820;
    static constexpr int kHeight       = 520;

    // file: 新規時は <location>/<name>/<name>.trakova、既存時は選択された .trakova
    // sr / bits: 新規時のみ意味を持つ。既存読み込み時はプロジェクトファイル側の値を使用
    std::function<void(const juce::File& file, double sampleRate, int bitDepth, bool isNew)> onProjectChosen;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g,
                          int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

private:
    void chooseLocation();
    void browseExisting();
    void createNewProject();
    void refreshRecents();
    void showDeviceDialog();
    void refreshDeviceLabel();

    // 3 カラム (左:新規 / 中央:最近 / 右:広告) の枠 (角丸カード) を算出する。
    // 広告無効時は ad が空で、左右 2 カラムになる。paint / resized で共用。
    struct Columns { juce::Rectangle<int> newCol, recentsCol, adCol; };
    Columns computeColumns() const;

    const bool adsEnabled;

    // 左カラム（新規）
    juce::Label    titleLabel;
    juce::Label    subtitleLabel;
    juce::Label    newSectionLabel;
    juce::Label    nameLabel, locationLabel, sampleRateLabel, bitDepthLabel, deviceLabel;
    juce::TextEditor nameEditor;
    juce::TextEditor locationEditor;
    juce::TextButton browseLocBtn  { tr(u8"参照...") };
    juce::ComboBox   sampleRateBox;
    juce::ComboBox   bitDepthBox;
    juce::Label      deviceSummaryLabel;
    juce::TextButton deviceChangeBtn { tr(u8"変更...") };
    juce::TextButton createBtn       { tr(u8"作成") };

    // 起動画面用の AudioDeviceManager（保存済み状態を共有）
    juce::AudioDeviceManager startupDeviceManager;

    // ChangeListener
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // 中央カラム（最近 / 開く）
    juce::Label      recentsLabel;
    juce::ListBox    recentsList   { "Recents", this };
    juce::TextButton openBtn       { tr(u8"別の場所から開く...") };

    // 右カラム（広告）。adsEnabled の時だけ生成する。
    juce::Label                adsLabel;
    std::unique_ptr<AdPanel>   adPanel;

    juce::Array<juce::File> recents;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StartupComponent)
};
