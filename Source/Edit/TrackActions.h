// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>
#include "../Tracks/TrackManager.h"

// トラック追加 / 複製の Undo アクション。
// PluginActions.h と同じくヘッダオンリー (UtawaveTests がリンク追加なしで検証できる)。
//
// 設計:
//  - 「追加済み」のトラックに対して作る (呼び出し側が先に追加し、最初の perform() は no-op)。
//  - undo は Track を破棄せずアクションが unique_ptr で所有して延命する。redo で同一
//    インスタンス (プラグインチェーン / クリップ / レーン含む) がそのまま復帰するため、
//    履歴中の他アクションが持つ Track* / Lane* もダングリングしない。
//  - undo 時の位置解決は indexOf (ポインタ一致) で行う。トラックが既に消えている
//    (Undo 非対応のトラック削除など) 場合は false を返して安全に何もしない。
//  - willRemove はリストから外す直前に呼ばれる。呼び出し側はここでプラグインエディタを
//    閉じ、audioEngine.clearPlayback() (UAF バリア) を張る。
namespace EditActions
{

class TrackAddAction : public juce::UndoableAction
{
public:
    TrackAddAction(TrackManager& tmIn, Track* addedTrack,
                   std::function<void(Track*)> willRemoveCb,
                   std::function<void()> onChangeCb)
        : tm(tmIn), track(addedTrack),
          willRemove(std::move(willRemoveCb)),
          onChange(std::move(onChangeCb)) {}

    bool perform() override
    {
        if (firstPerform)
        {
            firstPerform = false;          // 追加自体は呼び出し側が実施済み
            return track != nullptr;
        }
        if (!stored) return false;         // 整合しない状態 (undo を経ていない)
        tm.insertTrack(insertIndex, std::move(stored));
        if (onChange) onChange();
        return true;
    }

    bool undo() override
    {
        const int idx = tm.indexOf(track);
        if (idx < 0) return false;         // 既に存在しない → 安全に no-op
        insertIndex = idx;                 // redo で同じ位置へ戻す
        if (willRemove) willRemove(track);
        stored = tm.extractTrack(idx);
        if (onChange) onChange();
        return stored != nullptr;
    }

private:
    TrackManager& tm;
    Track* track { nullptr };              // 同一性の解決用 (所有は stored / TrackManager)
    std::unique_ptr<Track> stored;         // undo 中の延命所有
    int  insertIndex { 0 };
    bool firstPerform { true };
    std::function<void(Track*)> willRemove;
    std::function<void()>       onChange;

    JUCE_DECLARE_NON_COPYABLE(TrackAddAction)
};

// トラック並べ替えの Undo アクション。
//
// 設計:
//  - 並べ替え自体は呼び出し側 (TrackHeaderPanel::performReorder) が実施済みなので、
//    最初の perform() は no-op。before/after の「トラック順 (Track* 列)」を保持し、
//    undo は before 順、redo は after 順へ `reorderTo` で並べ直す。
//  - Track の生成/破棄はしない (並べ替えはトラック集合を変えない) ので延命所有は不要。
//    保持する Track* は順序解決にのみ使い、参照外しはしない。
//  - reorderTo は「現在のトラック集合の並べ替えか」を検証し、Undo 非対応のトラック削除等で
//    トラックが消えていれば false を返す → アクションも安全に false で no-op。
class TrackReorderAction : public juce::UndoableAction
{
public:
    TrackReorderAction(TrackManager& tmIn,
                       std::vector<Track*> beforeOrder,
                       std::vector<Track*> afterOrder,
                       std::function<void()> onChangeCb)
        : tm(tmIn), before(std::move(beforeOrder)), after(std::move(afterOrder)),
          onChange(std::move(onChangeCb)) {}

    bool perform() override   // redo (初回は呼び出し側が並べ替え済みなので no-op)
    {
        if (firstPerform) { firstPerform = false; return true; }
        const bool ok = tm.reorderTo(after);
        if (ok && onChange) onChange();
        return ok;
    }

    bool undo() override
    {
        const bool ok = tm.reorderTo(before);
        if (ok && onChange) onChange();
        return ok;
    }

private:
    TrackManager& tm;
    std::vector<Track*> before, after;   // 順序解決用 (参照外ししない)
    bool firstPerform { true };
    std::function<void()> onChange;

    JUCE_DECLARE_NON_COPYABLE(TrackReorderAction)
};

} // namespace EditActions
