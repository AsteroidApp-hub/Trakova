// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#include "PluginScannerProcess.h"

#include <mutex>
#include <condition_variable>
#include <queue>

// ─────────────────────────────────────────────────────────
// 子プロセス側 (スキャナ専用モードで起動された Utawave 本体)
//
// 親から (formatName, fileOrIdentifier) を受け取り、findAllTypesForFile の結果を
// PluginDescription の XML にして送り返す。プラグインがクラッシュすればこのプロセスごと
// 落ちるが、親は接続断で検知してブラックリスト化し継続する (これが目的の隔離)。
// メッセージスレッドが必要なフォーマット向けに AsyncUpdater 経由で message thread でも
// 走査できるようにする (JUCE AudioPluginHost の PluginScannerSubprocess と同構成)。
// ─────────────────────────────────────────────────────────
namespace
{
class PluginScannerSubprocess final : public juce::ChildProcessWorker,
                                      private juce::AsyncUpdater
{
public:
    PluginScannerSubprocess()
    {
        juce::addDefaultFormatsToManager(formatManager);
    }

private:
    void handleMessageFromCoordinator(const juce::MemoryBlock& mb) override
    {
        if (mb.isEmpty()) return;

        const std::lock_guard<std::mutex> lock(mutex);

        // メッセージスレッド以外でも走査できるフォーマットならその場で処理し、
        // 必要なら message thread (handleAsyncUpdate) へ回す
        if (const auto results = doScan(mb); !results.isEmpty())
            sendResults(results);
        else
        {
            pendingBlocks.emplace(mb);
            triggerAsyncUpdate();
        }
    }

    void handleConnectionLost() override
    {
        // 親が終了/キャンセルした → 自分も終了
        juce::JUCEApplicationBase::quit();
    }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (pendingBlocks.empty()) return;
            sendResults(doScan(pendingBlocks.front()));
            pendingBlocks.pop();
        }
    }

    juce::OwnedArray<juce::PluginDescription> doScan(const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream stream { block, false };
        const auto formatName = stream.readString();
        const auto identifier = stream.readString();

        juce::PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        pd.uniqueId = pd.deprecatedUid = 0;

        const auto matchingFormat = [&]() -> juce::AudioPluginFormat*
        {
            for (auto* format : formatManager.getFormats())
                if (format->getName() == formatName)
                    return format;
            return nullptr;
        }();

        juce::OwnedArray<juce::PluginDescription> results;

        if (matchingFormat != nullptr
            && (juce::MessageManager::getInstance()->isThisTheMessageThread()
                || matchingFormat->requiresUnblockedMessageThreadDuringCreation(pd)))
        {
            matchingFormat->findAllTypesForFile(results, identifier);
        }

        return results;
    }

    void sendResults(const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement xml("LIST");
        for (const auto& desc : results)
            xml.addChildElement(desc->createXml().release());

        const auto str = xml.toString();
        sendMessageToCoordinator({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
    }

    std::mutex mutex;
    std::queue<juce::MemoryBlock> pendingBlocks;
    juce::AudioPluginFormatManager formatManager;
};
} // namespace

namespace PluginScannerProcess
{
    juce::String processUID() { return "utawavePluginScanner"; }

    std::unique_ptr<juce::ChildProcessWorker> createWorkerIfInvoked(const juce::String& commandLine)
    {
        if (!commandLine.contains(processUID()))
            return nullptr;

        auto worker = std::make_unique<PluginScannerSubprocess>();
        if (!worker->initialiseFromCommandLine(commandLine, processUID()))
            return nullptr;

       #if JUCE_MAC
        juce::Process::setDockIconVisible(false);   // スキャナは Dock に出さない
       #endif
        return worker;
    }
}

// ─────────────────────────────────────────────────────────
// 親側: 子プロセスの起動・メッセージ往復 (ChildProcessCoordinator ラッパ)
// ─────────────────────────────────────────────────────────
class OutOfProcessPluginScanner::Subprocess final : private juce::ChildProcessCoordinator
{
public:
    Subprocess()
    {
        launched = launchWorkerProcess(
            juce::File::getSpecialLocation(juce::File::currentExecutableFile),
            PluginScannerProcess::processUID(),
            /*timeoutMs*/ 0, /*streamFlags*/ 0);
    }

    bool isLaunched() const { return launched; }

    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    // 50ms ごとに起きて呼び出し側がキャンセル/タイムアウトを判定できるようにする
    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (!condvar.wait_for(lock, std::chrono::milliseconds { 50 },
                              [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult = false;
        return { state, std::move(resultXml) };
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker(const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        resultXml = juce::parseXML(mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<juce::XmlElement> resultXml;
    bool connectionLost { false };
    bool gotResult { false };
    bool launched { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Subprocess)
};

// ─────────────────────────────────────────────────────────
// OutOfProcessPluginScanner
// ─────────────────────────────────────────────────────────
OutOfProcessPluginScanner::OutOfProcessPluginScanner() = default;
OutOfProcessPluginScanner::~OutOfProcessPluginScanner() = default;

bool OutOfProcessPluginScanner::findPluginTypesFor(juce::AudioPluginFormat& format,
                                                   juce::OwnedArray<juce::PluginDescription>& result,
                                                   const juce::String& fileOrIdentifier)
{
    // キャンセル済み: 空結果の「成功」で即抜ける (false はブラックリスト化されるため不可)
    if (aborted.load()) return true;

    if (subprocess == nullptr)
        subprocess = std::make_unique<Subprocess>();

    if (!subprocess->isLaunched())
    {
        // 子プロセスを起動できない環境 (テストバイナリ / 特殊なパッケージング) では
        // インプロセスで走査するフォールバック。隔離は効かないが機能は維持される
        subprocess.reset();
        format.findAllTypesForFile(result, fileOrIdentifier);
        return true;
    }

    juce::MemoryBlock block;
    juce::MemoryOutputStream stream { block, true };
    stream.writeString(format.getName());
    stream.writeString(fileOrIdentifier);

    if (!subprocess->sendMessageToWorker(block))
    {
        subprocess.reset();   // 送信失敗 = 子が既に死んでいる → クラッシュ扱い
        return false;
    }

    int elapsedMs = 0;
    for (;;)
    {
        if (aborted.load()) return true;   // キャンセル: 結果は捨てる (子は scanFinished で破棄)

        const auto response = subprocess->getResponse();

        if (response.state == Subprocess::State::timeout)
        {
            elapsedMs += 50;
            if (elapsedMs >= kFindTimeoutMs)
            {
                // 子が生きたままハング → 殺してブラックリスト化 (次のプラグインで再起動)
                subprocess.reset();
                return false;
            }
            continue;
        }

        if (response.xml != nullptr)
            for (const auto* item : response.xml->getChildIterator())
            {
                auto desc = std::make_unique<juce::PluginDescription>();
                if (desc->loadFromXml(*item))
                    result.add(desc.release());
            }

        if (response.state == Subprocess::State::gotResult)
            return true;

        // connectionLost = 子がクラッシュ → false でブラックリスト化。次の呼び出しで再起動
        subprocess.reset();
        return false;
    }
}

void OutOfProcessPluginScanner::scanFinished()
{
    subprocess.reset();
}
