// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

// Utawave — クラッシュレポート (CrashReporter) のネットワーク非依存ロジックのテスト。
// install() のハンドラ登録・sendAsync() の通信・offerPendingReports() のダイアログは
// 対象外 (UI / ネットワーク経路)。pendingLogs / markHandled / buildReportJson /
// crashLogFileName の純関数・ファイル操作を一時ディレクトリで検証する。

#include <JuceHeader.h>
#include "../Source/Project/CrashReporter.h"

struct CrashReporterTests : public juce::UnitTest
{
    CrashReporterTests() : juce::UnitTest("CrashReporter") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("UtawaveCrashReporterTest");
        dir.deleteRecursively();
        dir.createDirectory();

        beginTest("crashLogFileName: structure is crash-<15 digits>.log");
        {
            const auto name = CrashReporter::crashLogFileName(juce::Time::getCurrentTime());
            expect(name.startsWith("crash-"), "prefix");
            expect(name.endsWith(".log"), "extension");
            // crash- + YYYYMMDD_HHMMSS (15 文字) + .log
            expectEquals(name.length(), 6 + 15 + 4, "total length");
            expect(name[6 + 8] == juce::juce_wchar('_'), "date/time separator");
        }

        beginTest("pendingLogs: lists crash-*.log newest first, ignores others");
        {
            auto f1 = dir.getChildFile("crash-20260101_000000.log");
            auto f2 = dir.getChildFile("crash-20260102_000000.log");
            auto noise1 = dir.getChildFile("crash-20260103_000000.handled");
            auto noise2 = dir.getChildFile("notes.txt");
            f1.replaceWithText("a"); f2.replaceWithText("b");
            noise1.replaceWithText("c"); noise2.replaceWithText("d");
            // mtime で順位を確定させる (10 秒以上離す。FS の時刻分解能対策)
            const auto now = juce::Time::getCurrentTime();
            f1.setLastModificationTime(now - juce::RelativeTime::seconds(60));
            f2.setLastModificationTime(now);

            const auto pending = CrashReporter::pendingLogs(dir);
            expectEquals((int)pending.size(), 2, "only .log files");
            expect(pending[0] == f2, "newest first");
            expect(pending[1] == f1, "oldest last");
        }

        beginTest("pendingLogs: missing directory yields empty");
        {
            const auto none = CrashReporter::pendingLogs(dir.getChildFile("nope"));
            expect(none.empty(), "empty for missing dir");
        }

        beginTest("markHandled: renames .log to .handled and keeps content");
        {
            auto f = dir.getChildFile("crash-20260105_000000.log");
            f.replaceWithText("trace");
            expect(CrashReporter::markHandled(f), "rename ok");
            expect(!f.existsAsFile(), "original gone");
            auto handled = dir.getChildFile("crash-20260105_000000.handled");
            expect(handled.existsAsFile(), "handled exists");
            expectEquals(handled.loadFileAsString(), juce::String("trace"), "content kept");
            expect(!CrashReporter::markHandled(f), "second call returns false");

            // 処理済みは pendingLogs に出ない
            for (const auto& p : CrashReporter::pendingLogs(dir))
                expect(p.getFileExtension() == ".log", "pending only lists .log");
        }

        beginTest("buildReportJson: round-trips through JSON parse");
        {
            const auto json = CrashReporter::buildReportJson(
                "0.2.0", "macOS 14 / MacBook", "crash-20260101_000000.log",
                "line1\nline2 with \"quotes\" and \\backslash");
            const auto v = juce::JSON::parse(json);
            auto* obj = v.getDynamicObject();
            expect(obj != nullptr, "parses as object");
            expectEquals(obj->getProperty("app").toString(), juce::String("Utawave"));
            expectEquals(obj->getProperty("version").toString(), juce::String("0.2.0"));
            expectEquals(obj->getProperty("os").toString(), juce::String("macOS 14 / MacBook"));
            expectEquals(obj->getProperty("file").toString(),
                         juce::String("crash-20260101_000000.log"));
            expectEquals(obj->getProperty("log").toString(),
                         juce::String("line1\nline2 with \"quotes\" and \\backslash"));
        }

        beginTest("reportingCompiledIn: default build has no endpoint");
        {
            // テストビルドは UTAWAVE_CRASH_REPORT_URL を定義しない
            expect(!CrashReporter::reportingCompiledIn(), "off by default");
            expect(CrashReporter::defaultReportUrl().isEmpty(), "empty URL");
        }

        dir.deleteRecursively();
    }
};

static CrashReporterTests crashReporterTests;
