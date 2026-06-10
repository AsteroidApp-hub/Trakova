// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "BackupManager.h"
#include <algorithm>

namespace BackupManager
{
    // ※ ProjectManager::fileExtension() (".utawave") と一致させること。
    //    BackupManager を GUI/オーディオ非依存に保つため (ProjectManager.h は TimelineView.h を
    //    引き込む) ここでは定数を持つ。
    static const juce::String kExt = ".utawave";

    juce::String filePrefix(const juce::String& baseName)
    {
        return baseName.isNotEmpty() ? baseName + ".autosave-" : juce::String("autosave-");
    }

    juce::String matchPrefix(const juce::String& baseName)
    {
        return baseName.isNotEmpty() ? baseName + ".autosave" : juce::String("autosave");
    }

    juce::File datedFile(const juce::File& backupDir, const juce::String& baseName, juce::Time when)
    {
        return backupDir.getChildFile(filePrefix(baseName)
                                      + when.formatted("%Y%m%d_%H%M%S") + kExt);
    }

    juce::Array<juce::File> list(const juce::File& backupDir, const juce::String& baseName)
    {
        juce::Array<juce::File> files;
        if (backupDir == juce::File() || ! backupDir.isDirectory()) return files;
        // ワイルドカードは固定の "*.utawave" のみ (ユーザー名は入れない) → 結果を前方一致で絞る
        const auto prefix = matchPrefix(baseName);
        for (auto& f : backupDir.findChildFiles(juce::File::findFiles, false, "*" + kExt))
            if (f.getFileName().startsWith(prefix))
                files.add(f);
        // 更新時刻の新しい順 (最新が先頭)。日時付き名なので名前順とも一致するが、
        // 旧固定名を正しく順位付けするため更新時刻で比較する。
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });
        return files;
    }

    juce::File newest(const juce::File& backupDir, const juce::String& baseName)
    {
        auto files = list(backupDir, baseName);
        return files.isEmpty() ? juce::File() : files.getFirst();
    }

    void prune(const juce::File& backupDir, const juce::String& baseName, int keep)
    {
        keep = juce::jmax(1, keep);
        auto files = list(backupDir, baseName);   // 新しい順
        for (int i = keep; i < files.size(); ++i)
            files.getReference(i).deleteFile();
    }

    bool shouldOfferRecovery(const juce::File& backupDir, const juce::String& baseName,
                             const juce::File& projectFile)
    {
        auto n = newest(backupDir, baseName);
        if (n == juce::File() || ! n.existsAsFile()) return false;
        return n.getLastModificationTime() > projectFile.getLastModificationTime();
    }
}
