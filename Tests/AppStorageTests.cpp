// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Utawave — RecentProjects / WindowState (アプリ全体ストレージ) のユニットテスト
//
// どちらも ~/Library/Application Support/Utawave/ の実ファイル (recents.txt / window.txt) を
// 読み書きする (パス注入不可)。**テスト開始時に実ファイルを退避し、終了時に必ず復元**して
// 開発者の設定を壊さないようにする (juce::UnitTest は expect 失敗で中断しないので、runTest 末尾の
// 復元は通常どのケースでも実行される)。
//   RecentProjects: MRU 先頭挿入 / dedup / 上限 10 / 存在しないファイルは add 無視・load で除外 / remove / clear
//   WindowState   : 既定値 / save→load 往復 / save のクランプ / load の検証 (小さすぎ/巨大/不正は既定へ)
// expect は ASCII。

#include <JuceHeader.h>
#include "../Source/Project/RecentProjects.h"
#include "../Source/Project/WindowState.h"

namespace
{
// 実ストアファイルを退避/復元する RAII ヘルパ (存在しなかった場合は復元時に削除)。
struct StoreGuard
{
    juce::File file;
    bool       existed { false };
    juce::String content;

    explicit StoreGuard(juce::File f) : file(std::move(f))
    {
        existed = file.existsAsFile();
        if (existed) content = file.loadFileAsString();
    }
    ~StoreGuard()
    {
        if (existed) file.replaceWithText(content);
        else         file.deleteFile();
    }
};

//==============================================================================
class RecentProjectsTests : public juce::UnitTest
{
public:
    RecentProjectsTests() : juce::UnitTest("RecentProjects") {}

    juce::File tmpDir;

    juce::File makeProject(const juce::String& name)
    {
        auto f = tmpDir.getChildFile(name);
        f.replaceWithText("UtawaveProject");   // 実在さえすれば内容は不問
        return f;
    }

    bool listEquals(const juce::Array<juce::File>& got, const juce::Array<juce::File>& want)
    {
        if (got.size() != want.size()) return false;
        for (int i = 0; i < got.size(); ++i)
            if (got[i] != want[i]) return false;
        return true;
    }

    void runTest() override
    {
        StoreGuard guard(RecentProjects::getStoreFile());   // 実 recents.txt を退避→終了時復元

        tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("UtawaveRecentsTests");
        tmpDir.deleteRecursively(); tmpDir.createDirectory();

        testMruDedupAndOrder();
        testNonexistentIgnoredAndFiltered();
        testCapAtMaxEntries();
        testRemoveAndClear();

        tmpDir.deleteRecursively();
    }

    // ── MRU: 新しいものが先頭・同一は dedup して前へ ──
    void testMruDedupAndOrder()
    {
        beginTest("RecentProjects: newest first, duplicates move to front");
        RecentProjects::clear();
        auto a = makeProject("A.utawave");
        auto b = makeProject("B.utawave");
        auto c = makeProject("C.utawave");
        RecentProjects::add(a);
        RecentProjects::add(b);
        RecentProjects::add(c);
        expect(listEquals(RecentProjects::load(), { c, b, a }), "order is newest-first [C,B,A]");

        // 既存を再追加 → 先頭へ移動・重複なし
        RecentProjects::add(a);
        expect(listEquals(RecentProjects::load(), { a, c, b }), "re-adding A moves it to front, no dup");
    }

    // ── 存在しないファイルは add 無視 / load で除外 ──
    void testNonexistentIgnoredAndFiltered()
    {
        beginTest("RecentProjects: non-existent files are ignored on add and filtered on load");
        RecentProjects::clear();
        auto real = makeProject("real.utawave");
        RecentProjects::add(real);
        RecentProjects::add(tmpDir.getChildFile("ghost.utawave"));   // 存在しない → 無視
        expect(listEquals(RecentProjects::load(), { real }), "non-existent file is not added");

        // ストアに存在しないパスを直接書いても load は弾く
        auto ghost = tmpDir.getChildFile("ghost2.utawave");
        RecentProjects::getStoreFile().replaceWithText(
            ghost.getFullPathName() + "\n" + real.getFullPathName());
        expect(listEquals(RecentProjects::load(), { real }), "load filters out paths that no longer exist");

        // 削除されたファイルは次の load から消える
        real.deleteFile();
        expect(RecentProjects::load().isEmpty(), "a deleted project drops out of the list");
        makeProject("real.utawave");   // 後続テストのため作り直し (tmpDir は後で消す)

        // 空行・空白のみの行は load でスキップ (load の trim().isEmpty() 分岐)
        RecentProjects::getStoreFile().replaceWithText(
            "\n   \n" + real.getFullPathName() + "\n\n");
        expect(listEquals(RecentProjects::load(), { real }),
               "blank / whitespace-only lines are skipped on load");
    }

    // ── 上限 10: 12 個追加で最新 10 個・最古 2 個が落ちる ──
    void testCapAtMaxEntries()
    {
        beginTest("RecentProjects: capped at maxEntries (10), oldest dropped");
        RecentProjects::clear();
        juce::Array<juce::File> files;
        for (int i = 0; i < 12; ++i)
            files.add(makeProject("p" + juce::String(i) + ".utawave"));
        for (auto& f : files) RecentProjects::add(f);   // p0 .. p11 の順に追加

        auto got = RecentProjects::load();
        expect(got.size() == RecentProjects::maxEntries, "list size is capped at 10");
        expect(got.getFirst() == files[11], "most-recent (p11) is at the front");
        expect(!got.contains(files[0]) && !got.contains(files[1]),
               "the two oldest (p0, p1) were dropped");
        expect(got.getLast() == files[2], "p2 is the oldest surviving entry");
    }

    // ── remove / clear ──
    void testRemoveAndClear()
    {
        beginTest("RecentProjects: remove deletes one entry, clear empties the list");
        RecentProjects::clear();
        auto a = makeProject("ra.utawave");
        auto b = makeProject("rb.utawave");
        RecentProjects::add(a);
        RecentProjects::add(b);
        RecentProjects::remove(a);
        expect(listEquals(RecentProjects::load(), { b }), "remove(a) leaves only b");

        // リストに無いファイルの remove は no-op
        auto never = makeProject("never.utawave");
        RecentProjects::remove(never);
        expect(listEquals(RecentProjects::load(), { b }), "remove of a file not in the list is a no-op");

        RecentProjects::clear();
        expect(RecentProjects::load().isEmpty(), "clear empties the list");
        expect(!RecentProjects::getStoreFile().existsAsFile(), "clear deletes the store file");
    }
};

//==============================================================================
class WindowStateTests : public juce::UnitTest
{
public:
    WindowStateTests() : juce::UnitTest("WindowState") {}

    void runTest() override
    {
        StoreGuard guard(WindowState::getStoreFile());   // 実 window.txt を退避→終了時復元

        testDefaultWhenMissing();
        testRoundTrip();
        testSaveClampsBelowMinimum();
        testLoadRejectsInvalid();
    }

    // ── ストアが無ければ既定 (1280x800) ──
    void testDefaultWhenMissing()
    {
        beginTest("WindowState: missing store -> default size");
        WindowState::getStoreFile().deleteFile();
        auto s = WindowState::load();
        expect(s.width == WindowState::defaultWidth && s.height == WindowState::defaultHeight,
               "defaults to 1280x800 when no file");
    }

    // ── save → load 往復 ──
    void testRoundTrip()
    {
        beginTest("WindowState: save then load round-trips a valid size");
        WindowState s; s.width = 1600; s.height = 900;
        s.save();
        auto r = WindowState::load();
        expect(r.width == 1600 && r.height == 900, "1600x900 round-trips");
    }

    // ── save は最低サイズ未満を min へクランプ ──
    void testSaveClampsBelowMinimum()
    {
        beginTest("WindowState: save clamps values below the minimum");
        WindowState s; s.width = 500; s.height = 400;   // < min (1000x600)
        s.save();
        auto r = WindowState::load();
        expect(r.width == WindowState::minWidth && r.height == WindowState::minHeight,
               "save clamps to 1000x600 minimum");
    }

    // ── load は不正値 (小さすぎ/巨大/非数値/欠落) を弾いて既定へ ──
    void testLoadRejectsInvalid()
    {
        beginTest("WindowState: load rejects invalid stored values (falls back to default)");
        auto store = WindowState::getStoreFile();

        store.replaceWithText("500,400");   // 最低未満 (save 経由でないので load の検証を直接突く)
        auto r1 = WindowState::load();
        expect(r1.width == WindowState::defaultWidth && r1.height == WindowState::defaultHeight,
               "too-small stored values are rejected -> default");

        store.replaceWithText("20000,20000");   // 上限 16384 超
        auto r2 = WindowState::load();
        expect(r2.width == WindowState::defaultWidth && r2.height == WindowState::defaultHeight,
               "oversized stored values are rejected -> default");

        store.replaceWithText("garbage");   // 非数値・トークン不足
        auto r3 = WindowState::load();
        expect(r3.width == WindowState::defaultWidth && r3.height == WindowState::defaultHeight,
               "unparseable stored value -> default");
    }
};

static RecentProjectsTests recentProjectsTests;
static WindowStateTests    windowStateTests;
}
