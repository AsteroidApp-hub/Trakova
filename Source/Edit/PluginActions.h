// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#pragma once
#include <JuceHeader.h>
#include "../VST/PluginChain.h"

// プラグインチェーン操作の Undo アクション群。
// チェーンは ChainResolver (apply 時に評価) で解決する。トラック削除済みなら nullptr を返し
// no-op になる (ダングリング回避)。プラグインインスタンスはアクションが所有して延命するため、
// undo/redo を跨いでも同一インスタンスが保たれる。チェーン変更は onChainChanged で UI が
// 自動更新されるので、onChanged は markProjectDirty 等の付随処理のみ担う。
namespace EditActions
{

using ChainResolver = std::function<PluginChain*()>;
using WillRemoveFn  = std::function<void(juce::AudioPluginInstance*)>;

// 1 スロットの「インスタンス差し替え」を表す統一アクション。
// 追加 (空→新規) / 削除 (既存→空) / 置換 (既存→別) を before/after の所有で表現する。
class PluginSlotAction : public juce::UndoableAction
{
public:
    PluginSlotAction(ChainResolver chain, int slot,
                     std::unique_ptr<juce::AudioPluginInstance> afterInstanceIn,
                     std::function<void()> onChangeIn, WillRemoveFn willRemoveIn)
        : resolve(std::move(chain)), slotIdx(slot),
          afterInstance(std::move(afterInstanceIn)),
          onChanged(std::move(onChangeIn)), willRemove(std::move(willRemoveIn)) {}

    bool perform() override { return apply(true); }
    bool undo()    override { return apply(false); }

private:
    bool apply(bool toAfter)
    {
        auto* c = resolve ? resolve() : nullptr;
        if (c == nullptr) return false;   // トラック削除済み等 → no-op

        // 現在スロットにあるインスタンスを退避 (反対側の状態として保存)
        std::unique_ptr<juce::AudioPluginInstance> displaced;
        if (auto* cur = c->getPlugin(slotIdx))
        {
            if (willRemove) willRemove(cur);
            displaced = c->extractPlugin(slotIdx);
        }
        // 置くべきインスタンスを挿入
        auto& toPlace = toAfter ? afterInstance : beforeInstance;
        if (toPlace) c->insertPluginAt(slotIdx, std::move(toPlace));
        // 退避した現在のインスタンスを反対側へ保存
        (toAfter ? beforeInstance : afterInstance) = std::move(displaced);

        if (onChanged) onChanged();
        return true;
    }

    ChainResolver resolve;
    int slotIdx;
    std::unique_ptr<juce::AudioPluginInstance> beforeInstance;  // perform 前にスロットにあったもの
    std::unique_ptr<juce::AudioPluginInstance> afterInstance;   // perform 後にスロットへ置くもの
    std::function<void()> onChanged;
    WillRemoveFn          willRemove;
};

// バイパスのトグル (setBypassed は onChainChanged を呼ばないので手動で UI 更新を促す)
class PluginBypassAction : public juce::UndoableAction
{
public:
    PluginBypassAction(ChainResolver chain, int slot, bool beforeB, bool afterB,
                       std::function<void()> onChangeIn)
        : resolve(std::move(chain)), slotIdx(slot),
          beforeBypass(beforeB), afterBypass(afterB), onChanged(std::move(onChangeIn)) {}

    bool perform() override { return apply(afterBypass); }
    bool undo()    override { return apply(beforeBypass); }

private:
    bool apply(bool b)
    {
        auto* c = resolve ? resolve() : nullptr;
        if (c == nullptr) return false;
        c->setBypassed(slotIdx, b);
        if (c->onChainChanged) c->onChainChanged();   // チップの見た目を更新
        if (onChanged) onChanged();
        return true;
    }
    ChainResolver resolve;
    int  slotIdx;
    bool beforeBypass, afterBypass;
    std::function<void()> onChanged;
};

// 同一チェーン内のスロット入れ替え (並べ替え)。swapSlots は対合なので perform/undo 同一。
class PluginSwapAction : public juce::UndoableAction
{
public:
    PluginSwapAction(ChainResolver chain, int slotA, int slotB, std::function<void()> onChangeIn)
        : resolve(std::move(chain)), a(slotA), b(slotB), onChanged(std::move(onChangeIn)) {}

    bool perform() override { return apply(); }
    bool undo()    override { return apply(); }

private:
    bool apply()
    {
        auto* c = resolve ? resolve() : nullptr;
        if (c == nullptr) return false;
        c->swapSlots(a, b);
        if (onChanged) onChanged();
        return true;
    }
    ChainResolver resolve;
    int a, b;
    std::function<void()> onChanged;
};

// トラック間 (またはマスターへ) のプラグイン移動。src から取り出し dst へ挿入する。
// dst が埋まっていたインスタンスは退避して undo で戻す。
class PluginMoveAction : public juce::UndoableAction
{
public:
    PluginMoveAction(ChainResolver srcChain, int srcSlot, ChainResolver dstChain, int dstSlot,
                     std::function<void()> onChangeIn, WillRemoveFn willRemoveIn)
        : srcResolve(std::move(srcChain)), srcIdx(srcSlot),
          dstResolve(std::move(dstChain)), dstIdx(dstSlot),
          onChanged(std::move(onChangeIn)), willRemove(std::move(willRemoveIn)) {}

    bool perform() override
    {
        auto* s = srcResolve ? srcResolve() : nullptr;
        auto* d = dstResolve ? dstResolve() : nullptr;
        if (s == nullptr || d == nullptr) return false;
        if (auto* sp = s->getPlugin(srcIdx)) if (willRemove) willRemove(sp);  // 移動元エディタを閉じる
        auto moved = s->extractPlugin(srcIdx);
        if (!moved) return false;
        if (auto* existing = d->getPlugin(dstIdx))
        {
            if (willRemove) willRemove(existing);
            dstDisplaced = d->extractPlugin(dstIdx);
        }
        d->insertPluginAt(dstIdx, std::move(moved));
        if (onChanged) onChanged();
        return true;
    }

    bool undo() override
    {
        auto* s = srcResolve ? srcResolve() : nullptr;
        auto* d = dstResolve ? dstResolve() : nullptr;
        if (s == nullptr || d == nullptr) return false;
        if (auto* mp = d->getPlugin(dstIdx)) if (willRemove) willRemove(mp);  // 移動先エディタを閉じる
        auto moved = d->extractPlugin(dstIdx);
        if (dstDisplaced) d->insertPluginAt(dstIdx, std::move(dstDisplaced));
        if (moved) s->insertPluginAt(srcIdx, std::move(moved));
        if (onChanged) onChanged();
        return true;
    }

private:
    ChainResolver srcResolve, dstResolve;
    int srcIdx, dstIdx;
    std::unique_ptr<juce::AudioPluginInstance> dstDisplaced;
    std::function<void()> onChanged;
    WillRemoveFn          willRemove;
};

}  // namespace EditActions
