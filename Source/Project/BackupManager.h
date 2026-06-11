// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>

// 世代バックアップ (日時付きファイル名) のファイル管理ロジック。
// MainComponent から切り出してオーディオ / GUI 非依存にし、単体テスト可能にしたもの
// (Tests/BackupManagerTests.cpp)。命名・列挙・間引き・復旧判定の単一の真実源。
//
// baseName: 保存済みプロジェクトのファイル名 (拡張子なし)。未保存 (Untitled) は空文字。
namespace BackupManager
{
    // 新規書き込み用プレフィックス: "<name>.autosave-" / 未保存 "autosave-"
    juce::String filePrefix(const juce::String& baseName);

    // 列挙結果の前方一致フィルタ: "<name>.autosave" / 未保存 "autosave"
    // 旧固定名 "<name>.autosave.utawave" と日時付き "<name>.autosave-....utawave" の両方に一致。
    // 名前を直接ワイルドカードに入れないので、名前に '*' '?' (macOS で合法) が含まれても
    // 過剰一致しない。
    juce::String matchPrefix(const juce::String& baseName);

    // backupDir 内の日時付きバックアップのパス (実体は作らない)
    juce::File datedFile(const juce::File& backupDir, const juce::String& baseName, juce::Time when);

    // backupDir 内の該当バックアップを更新時刻の新しい順 (最新が先頭) に返す
    juce::Array<juce::File> list(const juce::File& backupDir, const juce::String& baseName);

    // 最新のバックアップ ({}=無し)
    juce::File newest(const juce::File& backupDir, const juce::String& baseName);

    // 新しい順に keep 個残し、残りを削除 (keep は最低 1 にクランプ → 全消ししない)
    void prune(const juce::File& backupDir, const juce::String& baseName, int keep);

    // 最新バックアップが projectFile より「厳密に」新しければ true (= 復旧を提案すべき)。
    // 同時刻 (<=) は false: 通常の自動保存はバックアップ→本体の順で書くので本体が新しくなり、
    // 誤って復旧を促さない。本体保存の途中でクラッシュした時だけ最新バックアップが新しくなる。
    bool shouldOfferRecovery(const juce::File& backupDir, const juce::String& baseName,
                             const juce::File& projectFile);
}
