// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

// プラグインスキャンの別プロセス化。
//
// スキャン中にクラッシュするプラグインが 1 つあるだけでアプリ本体が巻き込まれて落ちるのを
// 防ぐため、実際のプラグイン読み込み (findAllTypesForFile) は「自分自身のバイナリを
// スキャナ専用の子プロセスとして起動」して行う (JUCE AudioPluginHost と同じ構成:
// ChildProcessCoordinator/Worker + KnownPluginList::CustomScanner)。
//
// - 子がクラッシュ → findPluginTypesFor が false を返し、KnownPluginList が自動で
//   ブラックリスト化 (plugin_list.xml に永続化・以後のスキャンでスキップ)。本体は落ちない。
//   次のプラグインからは子プロセスを再起動してスキャンを続行する
// - 子が応答しないままハング → kFindTimeoutMs で打ち切り、同様にブラックリスト化
// - 子プロセスを起動できない環境 (UtawaveTests バイナリ等) では従来どおりインプロセスで
//   走査するフォールバック (false を返すと誤ブラックリストになるため必ず自前走査する)
namespace PluginScannerProcess
{
    // 子プロセス起動判定用の一意トークン (コマンドラインに含まれていればスキャナとして動く)
    juce::String processUID();

    // commandLine がスキャナ子プロセスとしての起動なら worker を生成して返す
    // (呼び出し側 = UtawaveApplication が保持する)。通常起動なら nullptr。
    std::unique_ptr<juce::ChildProcessWorker> createWorkerIfInvoked(const juce::String& commandLine);
}

// 親側: KnownPluginList::setCustomScanner にセットする別プロセススキャナ。
// findPluginTypesFor はスキャンスレッド (PluginManager::ScanThread) から呼ばれる。
class OutOfProcessPluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    // ctor/dtor は .cpp 側 (= default)。Subprocess が不完全型のためヘッダで生成できない
    OutOfProcessPluginScanner();
    ~OutOfProcessPluginScanner() override;

    bool findPluginTypesFor(juce::AudioPluginFormat& format,
                            juce::OwnedArray<juce::PluginDescription>& result,
                            const juce::String& fileOrIdentifier) override;

    // スキャン完了 (PluginDirectoryScanner 破棄時に呼ばれる) → 子プロセスを終了
    void scanFinished() override;

    // スキャン中止 (PluginManager::cancelScan、message thread から)。
    // ブロック中の応答待ちを 50ms 以内に起こして「空結果の成功」で抜けさせる
    // (false で抜けるとキャンセルしただけのプラグインがブラックリスト化されるため)。
    void abortScan()      { aborted.store(true); }
    void resetAbortFlag() { aborted.store(false); }

    // 1 プラグインあたりの応答待ち上限。子が生きたままハングした場合の打ち切り
    // (打ち切り後はクラッシュと同じ扱い = ブラックリスト化して次へ進む)
    static constexpr int kFindTimeoutMs = 120 * 1000;

private:
    class Subprocess;   // ChildProcessCoordinator ラッパ (実装は .cpp)
    std::unique_ptr<Subprocess> subprocess;
    std::atomic<bool> aborted { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutOfProcessPluginScanner)
};
