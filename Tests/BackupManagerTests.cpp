// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

// Utawave — 世代バックアップ管理 (BackupManager) のユニットテスト
//
// オーディオデバイス / GUI 不要・決定論的に、Backup/ ディレクトリ内の
// 日時付きバックアップの命名 / 列挙 / 並べ替え / 間引き / 復旧判定を検証する。
// ファイルの更新時刻は setLastModificationTime で明示的に与えて決定論的にする
// (壁時計に依存しない)。
//
// ExportEngineTests.cpp が main() を持ち UnitTestRunner で全テストを走らせる。
// juce::UnitTest は構築時に自身を登録するので、ここでは静的インスタンスを置くだけ。
// expect の文字列は ASCII (CLAUDE.md の定石: 非 ASCII を juce::String(const char*) に渡すと assert)。

#include <JuceHeader.h>
#include "../Source/Project/BackupManager.h"

namespace
{
class BackupManagerTests : public juce::UnitTest
{
public:
    BackupManagerTests() : juce::UnitTest("Backup Manager") {}

    juce::File dir;                 // テスト用 Backup ディレクトリ
    juce::File projFile;            // 復旧判定用のダミー本体ファイル

    // 任意の固定基準時刻 (壁時計非依存)。各ファイルに相対オフセットを与える
    static juce::Time base() { return juce::Time(1700000000000LL); }
    static juce::Time at(int sec) { return base() + juce::RelativeTime::seconds((double) sec); }

    void freshDir()
    {
        dir.deleteRecursively();
        dir.createDirectory();
    }

    // 指定名のファイルを dir 内に作り、更新時刻 t を設定して返す
    juce::File makeFile(const juce::String& name, juce::Time t)
    {
        auto f = dir.getChildFile(name);
        f.replaceWithText("x");
        f.setLastModificationTime(t);
        return f;
    }

    void runTest() override
    {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("UtawaveBackupMgrTests");
        projFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("UtawaveBackupMgrProj.utawave");

        testPrefixes();
        testDatedFileName();
        testListMatchesAndFilters();
        testListOrderingNewestFirst();
        testNewest();
        testPruneKeepsNewest();
        testPruneClampAndNoop();
        testWildcardMetacharSafety();
        testUntitledPrefix();
        testShouldOfferRecovery();

        dir.deleteRecursively();
        projFile.deleteFile();
    }

    // ── 1. プレフィックス (保存済み / 未保存) ──
    void testPrefixes()
    {
        beginTest("prefixes: named vs untitled");
        expect(BackupManager::filePrefix("Song")  == "Song.autosave-", "named file prefix");
        expect(BackupManager::filePrefix("")       == "autosave-",      "untitled file prefix");
        expect(BackupManager::matchPrefix("Song") == "Song.autosave",  "named match prefix");
        expect(BackupManager::matchPrefix("")      == "autosave",       "untitled match prefix");
    }

    // ── 2. 日時付きファイル名 = <prefix><YYYYMMDD_HHMMSS>.utawave ──
    void testDatedFileName()
    {
        beginTest("datedFile: structure prefix + 15-char stamp + .utawave (timezone-independent)");
        freshDir();
        auto f  = BackupManager::datedFile(dir, "Song", at(0));
        auto nm = f.getFileName();
        expect(f.getParentDirectory() == dir, "dated file is in backup dir");
        expect(nm.startsWith("Song.autosave-"), "starts with file prefix");
        expect(nm.endsWith(".utawave"), "ends with extension");

        auto stamp = nm.fromFirstOccurrenceOf("Song.autosave-", false, false)
                       .upToLastOccurrenceOf(".utawave", false, false);
        expect(stamp.length() == 15, "stamp is YYYYMMDD_HHMMSS (8+1+6 = 15 chars)");
        expect(stamp.length() == 15 && stamp[8] == '_', "stamp separator at index 8");
        bool digits = (stamp.length() == 15);
        for (int i = 0; i < stamp.length(); ++i)
            if (i != 8 && ! juce::CharacterFunctions::isDigit(stamp[i]))
                digits = false;
        expect(digits, "stamp is digits except the underscore");
    }

    // ── 3. 列挙: 日時付き + 旧固定名に一致 / 本体・別名・接頭辞境界を除外 ──
    void testListMatchesAndFilters()
    {
        beginTest("list: matches dated+legacy, excludes project file / other project / prefix boundary");
        freshDir();
        auto dated  = makeFile("Song.autosave-20230101_000000.utawave", at(10));
        auto legacy = makeFile("Song.autosave.utawave",                 at(5));
        makeFile("Song.utawave",                          at(20));   // 本体ファイル相当 → 除外
        makeFile("SongX.autosave-20230101_000000.utawave", at(20));  // 別プロジェクト → 除外
        makeFile("Songbook.autosave-20230101_000000.utawave", at(20)); // 接頭辞境界 → 除外

        auto got = BackupManager::list(dir, "Song");
        expect(got.size() == 2, "exactly the two Song backups are listed");
        expect(got.contains(dated),  "dated backup present");
        expect(got.contains(legacy), "legacy fixed-name backup present");
    }

    // ── 4. 並べ替え: 更新時刻の新しい順 (名前順ではなく mtime) ──
    void testListOrderingNewestFirst()
    {
        beginTest("list: sorted by modification time, newest first");
        freshDir();
        auto a = makeFile("Song.autosave-20230101_000001.utawave", at(10));
        auto b = makeFile("Song.autosave-20230101_000002.utawave", at(30));
        auto c = makeFile("Song.autosave-20230101_000003.utawave", at(20));

        auto got = BackupManager::list(dir, "Song");
        expect(got.size() == 3, "three backups");
        expect(got[0] == b, "newest first");
        expect(got[1] == c, "middle second");
        expect(got[2] == a, "oldest last");
    }

    // ── 5. newest ──
    void testNewest()
    {
        beginTest("newest: latest mtime, or empty when none");
        freshDir();
        expect(BackupManager::newest(dir, "Song") == juce::File(), "empty when no backups");
        makeFile("Song.autosave-20230101_000001.utawave", at(10));
        auto top = makeFile("Song.autosave-20230101_000002.utawave", at(50));
        makeFile("Song.autosave-20230101_000003.utawave", at(30));
        expect(BackupManager::newest(dir, "Song") == top, "newest is the latest mtime");
    }

    // ── 6. 間引き: 新しい順に keep 個残す ──
    void testPruneKeepsNewest()
    {
        beginTest("prune: keeps newest N, deletes the rest");
        freshDir();
        juce::Array<juce::File> made;
        for (int i = 0; i < 5; ++i)   // i が大きいほど新しい
            made.add(makeFile("Song.autosave-2023010" + juce::String(i + 1) + "_000000.utawave",
                              at(i * 100)));

        BackupManager::prune(dir, "Song", 3);

        expect(BackupManager::list(dir, "Song").size() == 3, "three kept");
        expect(! made[0].existsAsFile(), "oldest deleted");
        expect(! made[1].existsAsFile(), "2nd oldest deleted");
        expect(made[2].existsAsFile() && made[3].existsAsFile() && made[4].existsAsFile(),
               "three newest kept");
    }

    // ── 7. 間引きのクランプ / no-op ──
    void testPruneClampAndNoop()
    {
        beginTest("prune: no-op when keep>=count; clamps keep>=1 (never deletes everything)");
        freshDir();
        juce::Array<juce::File> made;
        for (int i = 0; i < 3; ++i)
            made.add(makeFile("Song.autosave-2023010" + juce::String(i + 1) + "_000000.utawave",
                              at(i * 100)));

        BackupManager::prune(dir, "Song", 10);   // keep >= count → 何も消さない
        expect(BackupManager::list(dir, "Song").size() == 3, "no-op when keep exceeds count");

        BackupManager::prune(dir, "Song", 0);    // keep<=0 は 1 にクランプ
        auto got = BackupManager::list(dir, "Song");
        expect(got.size() == 1, "clamped to keep>=1 (one survivor)");
        expect(got[0] == made[2], "the single survivor is the newest");
    }

    // ── 8. 名前にワイルドカード文字 ('*') があっても過剰一致しない (回帰防止) ──
    void testWildcardMetacharSafety()
    {
        beginTest("list: project name with '*' is matched literally (no wildcard over-match)");
        freshDir();
       #if JUCE_WINDOWS
        // Windows はファイル名に '*' を予約文字として許さないため、この回帰シナリオ
        // (名前に glob メタ文字を含むプロジェクト) は原理的に発生しない。リテラル前方
        // 一致が glob 化しないことは testListMatchesAndFilters の境界ケース (Songbook) で
        // 別途検証済み。ここでは前提が成立しないことだけを記録してスキップする。
        expect(! makeFile("My*Mix.autosave-20230101_000000.utawave", at(10)).existsAsFile(),
               "Windows rejects '*' in filenames; wildcard scenario is not applicable");
       #else
        auto starFile = makeFile("My*Mix.autosave-20230101_000000.utawave", at(10));
        // '*' を含むファイルが作れない環境ならこのテストは前提が崩れるので明示
        expect(starFile.existsAsFile(), "filesystem allows '*' in a file name");
        // 旧実装 (名前をワイルドカードへ連結) なら "My*Mix" の '*' が下を誤って拾っていた
        makeFile("MyXMix.autosave-20230101_000000.utawave", at(20));

        auto got = BackupManager::list(dir, "My*Mix");
        expect(got.size() == 1, "literal prefix match: '*' is not treated as a wildcard");
        expect(got.contains(starFile), "only the exact-name backup is listed");
       #endif
    }

    // ── 9. 未保存 (Untitled) の列挙 ──
    void testUntitledPrefix()
    {
        beginTest("list: untitled ('') matches autosave* only, not named backups");
        freshDir();
        auto dated  = makeFile("autosave-20230101_000000.utawave", at(10));
        auto legacy = makeFile("autosave.utawave",                 at(5));
        makeFile("Song.autosave-20230101_000000.utawave", at(20));   // 名前付き → 未保存では除外

        auto got = BackupManager::list(dir, "");
        expect(got.size() == 2, "only the two untitled backups");
        expect(got.contains(dated) && got.contains(legacy), "dated + legacy untitled present");
    }

    // ── 10. 復旧判定: 最新 > 本体 のときだけ true (同時刻は false) ──
    void testShouldOfferRecovery()
    {
        beginTest("shouldOfferRecovery: true only when newest backup is strictly newer than project");
        freshDir();
        projFile.replaceWithText("p");
        projFile.setLastModificationTime(at(100));

        // (a) バックアップ無し
        expect(! BackupManager::shouldOfferRecovery(dir, "Song", projFile),
               "no backups: do not offer");

        // (b) バックアップが本体より古い
        makeFile("Song.autosave-20230101_000000.utawave", at(50));
        expect(! BackupManager::shouldOfferRecovery(dir, "Song", projFile),
               "older backup: do not offer");

        // (c) 同時刻 (境界 <=)
        freshDir();
        auto eq = makeFile("Song.autosave-20230101_000001.utawave", at(100));
        eq.setLastModificationTime(projFile.getLastModificationTime());  // 厳密に一致させる
        expect(! BackupManager::shouldOfferRecovery(dir, "Song", projFile),
               "equal mtime: do not offer (<=)");

        // (d) バックアップが本体より新しい
        freshDir();
        makeFile("Song.autosave-20230101_000002.utawave", at(150));
        expect(BackupManager::shouldOfferRecovery(dir, "Song", projFile),
               "newer backup: offer recovery");
    }
};

static BackupManagerTests backupManagerTests;
}
