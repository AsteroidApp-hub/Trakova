// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "CrashReporter.h"
#include "../Localisation.h"

namespace
{
    constexpr int kConnectTimeoutMs = 5000;

    juce::String currentAppVersion()
    {
       #ifdef JUCE_APPLICATION_VERSION_STRING
        return JUCE_APPLICATION_VERSION_STRING;
       #else
        return "unknown";
       #endif
    }

    juce::String osDescription()
    {
        return juce::SystemStats::getOperatingSystemName()
               + " / " + juce::SystemStats::getDeviceDescription();
    }

    // クラッシュ時に呼ばれる。シグナルハンドラ内での確保は厳密には async-signal-safe では
    // ないが、ここはプロセスがどのみち落ちる直前の best-effort (JUCE 標準の作法)。
    void onCrash(void*)
    {
        const auto dir = CrashReporter::crashLogDirectory();
        dir.createDirectory();
        const auto file = dir.getChildFile(
            CrashReporter::crashLogFileName(juce::Time::getCurrentTime()));

        juce::String log;
        log << "Utawave crash log\n"
            << "version: " << currentAppVersion() << "\n"
            << "os: "      << osDescription() << "\n"
            << "time: "    << juce::Time::getCurrentTime().toISO8601(true) << "\n"
            << "\n--- stack backtrace ---\n"
            << juce::SystemStats::getStackBacktrace();

        file.replaceWithText(log);
    }
} // namespace

void CrashReporter::install()
{
    juce::SystemStats::setApplicationCrashHandler(onCrash);
}

juce::File CrashReporter::crashLogDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Utawave").getChildFile("CrashLogs");
}

juce::String CrashReporter::crashLogFileName(juce::Time when)
{
    return "crash-" + when.formatted("%Y%m%d_%H%M%S") + ".log";
}

std::vector<juce::File> CrashReporter::pendingLogs(const juce::File& dir)
{
    std::vector<juce::File> out;
    if (!dir.isDirectory()) return out;
    for (const auto& e : dir.findChildFiles(juce::File::findFiles, false, "crash-*.log"))
        out.push_back(e);
    std::sort(out.begin(), out.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return out;
}

bool CrashReporter::markHandled(const juce::File& logFile)
{
    if (!logFile.existsAsFile()) return false;
    return logFile.moveFileTo(logFile.withFileExtension("handled"));
}

juce::String CrashReporter::buildReportJson(const juce::String& appVersion,
                                            const juce::String& osDesc,
                                            const juce::String& logFileName,
                                            const juce::String& logContent)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("app",     "Utawave");
    obj->setProperty("version", appVersion);
    obj->setProperty("os",      osDesc);
    obj->setProperty("file",    logFileName);
    obj->setProperty("log",     logContent);
    return juce::JSON::toString(juce::var(obj));
}

bool CrashReporter::reportingCompiledIn()
{
   #ifdef UTAWAVE_CRASH_REPORT_URL
    return true;
   #else
    return false;
   #endif
}

juce::String CrashReporter::defaultReportUrl()
{
   #ifdef UTAWAVE_CRASH_REPORT_URL
    return UTAWAVE_CRASH_REPORT_URL;
   #else
    return {};
   #endif
}

void CrashReporter::sendAsync(const juce::String& url, const juce::String& json,
                              std::function<void(bool)> onDone)
{
    // AdService / UpdateChecker と同じ作法: 値コピーのデタッチスレッドで通信し、
    // 結果だけを callAsync でメッセージスレッドへ返す (呼び出し元の生存に依存しない)。
    juce::Thread::launch([url, json, onDone = std::move(onDone)]
    {
        bool ok = false;
        {
            int statusCode = 0;
            const auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                  .withConnectionTimeoutMs(kConnectTimeoutMs)
                                  .withExtraHeaders("Content-Type: application/json\r\n"
                                                    "User-Agent: Utawave")
                                  .withStatusCode(&statusCode);
            juce::URL u = juce::URL(url).withPOSTData(json);
            if (auto in = u.createInputStream(opts))
            {
                // 応答ボディは読み捨て (サイズ/時間上限つき)
                char buf[1024];
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
                while (in->read(buf, (int)sizeof(buf)) > 0
                       && juce::Time::getMillisecondCounterHiRes() < deadline) {}
                ok = (statusCode >= 200 && statusCode < 300);
            }
        }
        if (onDone)
            juce::MessageManager::callAsync([onDone, ok] { onDone(ok); });
    });
}

void CrashReporter::offerPendingReports()
{
    const auto pending = pendingLogs(crashLogDirectory());
    if (pending.empty()) return;

    // 複数たまっていても送るのは最新 1 件 (残りもまとめて処理済みにして再ナグを防ぐ)
    const juce::File newest = pending.front();
    auto markAllHandled = [pending]
    {
        for (const auto& f : pending) markHandled(f);
    };

    if (!reportingCompiledIn())
    {
        // 送信先が無いビルド (公開ソースの既定): ローカル確認のみ提示
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(tr(u8"前回終了時に問題が発生しました"))
                .withMessage(tr(u8"クラッシュログが保存されています。内容を確認しますか？"))
                .withButton(tr(u8"ログを表示"))
                .withButton(tr(u8"閉じる")),
            [newest, markAllHandled](int result)
            {
                if (result == 1) newest.revealToUser();
                markAllHandled();
            });
        return;
    }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle(tr(u8"前回終了時に問題が発生しました"))
            .withMessage(tr(u8"アプリの改善のため、エラーログを開発者に送信してもよろしいですか？\n"
                            u8"ログにはクラッシュ時の技術情報 (スタックトレース・アプリのバージョン・OS) のみが含まれ、\n"
                            u8"音声データや個人情報は含まれません。"))
            .withButton(tr(u8"送信する"))
            .withButton(tr(u8"送信しない"))
            .withButton(tr(u8"ログを表示")),
        [newest, markAllHandled](int result)
        {
            if (result == 3)   // ログを表示 (処理はまだ確定しない → 次回また聞く)
            {
                newest.revealToUser();
                return;
            }
            if (result == 1)   // 送信する
            {
                const auto json = buildReportJson(currentAppVersion(), osDescription(),
                                                  newest.getFileName(),
                                                  newest.loadFileAsString());
                sendAsync(defaultReportUrl(), json, [](bool ok)
                {
                    if (ok) return;   // 成功時は静かに完了
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                            .withTitle(tr(u8"送信できませんでした"))
                            .withMessage(tr(u8"エラーログを送信できませんでした。\n"
                                            u8"インターネット接続を確認してください (ログはローカルに残っています)。"))
                            .withButton("OK"),
                        nullptr);
                });
            }
            // 「送信する」(送信試行後) /「送信しない」とも処理済みにして再ナグしない
            markAllHandled();
        });
}
