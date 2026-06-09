// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include <JuceHeader.h>
#include "MainComponent.h"
#include "UI/StartupComponent.h"
#include "Localisation.h"
#include "Project/WindowState.h"
#include "Project/AppPreferences.h"

class TrakovaApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return JUCE_APPLICATION_NAME_STRING;
    }

    const juce::String getApplicationVersion() override
    {
        return JUCE_APPLICATION_VERSION_STRING;
    }

    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        Localisation::install(Localisation::getSavedLanguage());
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        // Cmd+Q / メニュー「Quit」/ ✕ ボタン、いずれの終了経路でも未保存変更があれば
        // 確認ダイアログを通してから終了する（ウィンドウサイズ保存も終了直前に行う）。
        if (mainWindow != nullptr)
        {
            if (auto* mc = dynamic_cast<MainComponent*>(mainWindow->getContentComponent()))
            {
                mc->confirmCloseIfDirty([this]
                {
                    if (mainWindow) mainWindow->persistWindowSizeIfMain();
                    quit();
                });
                return;   // ダイアログ応答（保存/破棄）後に quit。キャンセルなら終了しない
            }
            mainWindow->persistWindowSizeIfMain();
        }
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Colour(0xff1a1a1a),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            showStartup();
            setVisible(true);
        }

        void showStartup()
        {
            // 起動画面に戻る前に、メインウィンドウのサイズを保存しておく
            // (次回プロジェクトを開いた時に同じサイズで開けるように)
            persistWindowSizeIfMain();

            // 広告枠はコンパイル時フラグ (TRAKOVA_ADS_ENABLED) が ON かつユーザー設定が ON の時だけ。
            // 公開ソースの既定はフラグ OFF なので従来どおり 2 列表示になる。
            const bool showAds = AppPreferences::load().adsEffective();
            auto* startup = new StartupComponent(showAds);
            startup->onProjectChosen = [this](const juce::File& f, double sr, int bits, bool isNew)
            {
                showMain(f, sr, bits, isNew);
            };
            setContentOwned(startup, true);
            setResizable(false, false);
            centreWithSize(showAds ? StartupComponent::kWidthWithAds
                                   : StartupComponent::kWidthNoAds,
                           StartupComponent::kHeight);
        }

        void showMain(const juce::File& projectFile, double sampleRate, int bitDepth, bool isNew)
        {
            auto* mc = new MainComponent();
            mc->onCloseProject = [this]
            {
                juce::MessageManager::callAsync([this] { showStartup(); });
            };
            mc->onNewProject = [this]
            {
                juce::MessageManager::callAsync([this] { showStartup(); });
            };
            setContentOwned(mc, true);
            setResizable(true, true);

            // 前回保存したウィンドウサイズを復元 (無ければデフォルト 1280x800)
            const auto ws = WindowState::load();
            centreWithSize(ws.width, ws.height);

            if (isNew)
                mc->createNewProject(projectFile, sampleRate, bitDepth);
            else
                mc->openExistingProject(projectFile);
        }

        void closeButtonPressed() override
        {
            // ✕ ボタンも Cmd+Q と同じ終了経路へ委譲する。
            // 未保存確認・ウィンドウサイズ保存は systemRequestedQuit() がまとめて行う。
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        // 現在のメインウィンドウサイズを WindowState に書き出す。
        // 起動画面 (StartupComponent) 表示中は固定サイズなので何もしない。
        // ※ resized() ベースで保存すると破棄シーケンス中の自動リサイズで
        //    既定値に上書きされてしまうため、確実な終了経路だけで明示的に呼ぶ。
        void persistWindowSizeIfMain()
        {
            if (dynamic_cast<MainComponent*>(getContentComponent()) == nullptr) return;
            WindowState ws;
            ws.width  = getWidth();
            ws.height = getHeight();
            ws.save();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(TrakovaApplication)
