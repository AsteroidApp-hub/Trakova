// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// MainComponent のプラグイン関連処理 (Track / Master の追加・削除・編集ウィンドウ管理)
// および PianoRollWindow / PluginEditorWindow の実装。

#include "MainComponent.h"
#include "Localisation.h"
#include "VST/PluginChain.h"
#include "Edit/PluginActions.h"   // プラグインチェーン操作の Undo アクション (テスト可能・共有)
#include "UI/PluginManagerDialog.h"
#include "UI/PianoRollEditor.h"

using EditActions::PluginSlotAction;
using EditActions::PluginBypassAction;
using EditActions::PluginSwapAction;
using EditActions::PluginMoveAction;

// MainComponent デストラクタの実装は、OwnedArray<PluginEditorWindow / PianoRollWindow>
// の解体に完全型が必要なため、これらのクラス定義が見える本ファイルに置く。
MainComponent::~MainComponent()
{
    // VBlank コールバックは message thread で来る。破棄前にここで確実に外し、
    // onVBlank が参照する各メンバ (timelineView / audioEngine 等) より先に切り離す。
    vblankAttachment = {};
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
   #endif
    // プラグインエディタは trackManager より先に閉じる必要がある（editor がプラグインを参照しているため）
    pluginEditorWindows.clear();
    stopTimer();
    if (autoSaveTimer) autoSaveTimer->stopTimer();
    removeKeyListener(this);
    if (isRecording) stopRecording();
    audioEngine.shutdown();
}

// ── PluginEditorWindow 実装 ──────────────────────────────────────────
// クラス宣言は MainComponent.h 内 (OwnedArray の解体に完全型が必要なため)。
MainComponent::PluginEditorWindow::PluginEditorWindow(
    juce::AudioPluginInstance& p,
    std::function<void(PluginEditorWindow*)> onCloseCb)
    : DocumentWindow(p.getName(), juce::Colour(0xff1a1a1a),
                     DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      plugin(p),
      onClose(std::move(onCloseCb))
{
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    setAlwaysOnTop(true);   // 閉じるまで常に最前面

    if (auto* ed = plugin.createEditorIfNeeded())
    {
        editor = ed;
        setContentNonOwned(editor, true);
    }
    else
    {
        auto* lbl = new juce::Label({}, tr(u8"このプラグインは独自エディタを持っていません"));
        lbl->setSize(360, 80);
        lbl->setJustificationType(juce::Justification::centred);
        lbl->setColour(juce::Label::textColourId, juce::Colours::white);
        setContentOwned(lbl, true);
    }

    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

MainComponent::PluginEditorWindow::~PluginEditorWindow()
{
    // エディタを先に detach → editorBeingDeleted → delete の順で破棄
    if (editor != nullptr)
    {
        setContentNonOwned(nullptr, false);
        plugin.editorBeingDeleted(editor);
        delete editor;
        editor = nullptr;
    }
}

void MainComponent::PluginEditorWindow::closeButtonPressed()
{
    if (onClose) onClose(this);   // owner にこのウィンドウを破棄してもらう
}

void MainComponent::closePluginEditorFor(juce::AudioPluginInstance* plugin)
{
    for (int i = pluginEditorWindows.size(); --i >= 0;)
        if (pluginEditorWindows[i] && pluginEditorWindows[i]->getPlugin() == plugin)
            pluginEditorWindows.remove(i);   // OwnedArray が delete してくれる
}

// ── PianoRollWindow 実装 ─────────────────────────────────────────────
MainComponent::PianoRollWindow::PianoRollWindow(
    MidiClip& mc, Track& tr, double bpm,
    double initialFocusTimeSec,
    std::function<void(PianoRollWindow*)> onCloseCb,
    std::function<void()> onChangedCb,
    std::function<void(int, float, bool)> onPreviewCb,
    std::function<void(double)> onSeekCb)
    : DocumentWindow("Piano Roll - " + (mc.getName().isNotEmpty() ? mc.getName() : tr.getName()),
                     juce::Colour(0xff1a1a1a),
                     DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      clipPtr(&mc),
      editor(std::make_unique<PianoRollEditor>(mc, bpm, initialFocusTimeSec)),
      onClose(std::move(onCloseCb))
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    editor->setSize(900, 520);
    editor->onChanged     = std::move(onChangedCb);
    editor->onPreviewNote = std::move(onPreviewCb);
    editor->onSeek        = std::move(onSeekCb);
    setContentNonOwned(editor.get(), true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

MainComponent::PianoRollWindow::~PianoRollWindow()
{
    setContentNonOwned(nullptr, false);
}

void MainComponent::PianoRollWindow::closeButtonPressed()
{
    if (onClose) onClose(this);
}

void MainComponent::propagatePlayheadToPianoRolls(double playheadSecs)
{
    for (auto* w : pianoRollWindows)
        if (w && w->getClip() && w->getEditor())
        {
            const double rel = playheadSecs - w->getClip()->getStartPosition();
            // setPlayheadPosition が再生バーの帯だけを部分 repaint する。
            // 全面 repaint は 60Hz 更新では無駄なので張らない。
            w->getEditor()->setPlayheadPosition(rel);
        }
}

void MainComponent::openPianoRollFor(MidiClip* clip, Track* track)
{
    if (!clip || !track) return;
    // 既に開いていればそれを前面に
    for (auto* w : pianoRollWindows)
        if (w && w->getClip() == clip)
        {
            w->toFront(true);
            return;
        }
    // ピアノロールを開いた時点での再生バー位置 (クリップ先頭からの相対秒)。
    // 再生バーがクリップ範囲外なら 0 (クリップ先頭) にフォーカスする
    // (クリップ外へスクロールして空白だけ見える状態を防ぐ)。
    double playheadInClip = playPosition - clip->getStartPosition();
    if (playheadInClip < 0.0 || playheadInClip > clip->getDuration())
        playheadInClip = 0.0;

    // プレビュー用にトラック index を取得 (内蔵シンセは track index ベース)
    int trackIdx = -1;
    for (int i = 0; i < trackManager.getTrackCount(); ++i)
        if (trackManager.getTrack(i) == track) { trackIdx = i; break; }

    auto* win = new PianoRollWindow(
        *clip, *track, bpm,
        playheadInClip,
        [this](PianoRollWindow* self)
        {
            for (int i = pianoRollWindows.size(); --i >= 0;)
                if (pianoRollWindows[i] == self) { pianoRollWindows.remove(i); break; }
        },
        [this]
        {
            markProjectDirty();
            timelineView.repaint();
        },
        [this, trackIdx](int note, float vel, bool isOn)
        {
            if (trackIdx < 0) return;
            audioEngine.previewMidiNote(trackIdx, note, vel, isOn);
        },
        [this, clip](double secsInClip)
        {
            // ピアノロールのルーラークリック → グローバルプレイヘッドを移動
            seekTo(clip->getStartPosition() + secsInClip);
        });
    // PianoRoll の Undo をメイン UndoManager と統合する (#36)
    // → Cmd+Z でメイン側の他の編集 (クリップ移動等) と一貫した履歴になる
    if (auto* ed = win->getEditor())
    {
        ed->setUndoManager(&undoManager);
        // undo/redo で MidiClip が書き換わったときに、現在開いている
        // (作成元と異なる可能性のある) Editor を見つけて再描画させる経路。
        ed->setExternalReloadCallback([this](MidiClip* changedClip)
        {
            for (auto* w : pianoRollWindows)
                if (w && w->getClip() == changedClip)
                    if (auto* curEd = w->getEditor())
                        curEd->reloadNotesFromClip();
            // タイムライン側の MIDI クリップ描画も更新する
            timelineView.repaint();
            markProjectDirty();
        });
    }
    pianoRollWindows.add(win);
}

// ── プラグイン Undo: チェーン解決 ────────────────────────────────────
PluginChain* MainComponent::resolveChainForUndo(Track* track)
{
    if (track == nullptr) return &audioEngine.getMasterChain();
    return trackStillExists(track) ? &track->getPluginChain() : nullptr;
}

std::function<PluginChain*()> MainComponent::makeChainResolver(Track* track)
{
    return [this, track]() -> PluginChain* { return resolveChainForUndo(track); };
}

void MainComponent::swapTrackPluginsUndoable(int trackIdx, int a, int b)
{
    auto* t = trackManager.getTrack(trackIdx);
    if (!t) return;
    undoManager.beginNewTransaction();
    undoManager.perform(new PluginSwapAction(
        makeChainResolver(t), a, b, [this] { markProjectDirty(); }));
}

void MainComponent::swapMasterPluginsUndoable(int a, int b)
{
    undoManager.beginNewTransaction();
    undoManager.perform(new PluginSwapAction(
        makeChainResolver(nullptr), a, b, [this] { markProjectDirty(); }));
}

void MainComponent::togglePluginBypassUndoable(int trackIdx, int slot)
{
    Track* t = (trackIdx < 0) ? nullptr : trackManager.getTrack(trackIdx);
    if (trackIdx >= 0 && t == nullptr) return;
    auto* c = resolveChainForUndo(t);
    if (c == nullptr) return;
    const bool before = c->isBypassed(slot);
    undoManager.beginNewTransaction();
    undoManager.perform(new PluginBypassAction(
        makeChainResolver(t), slot, before, !before, [this] { markProjectDirty(); }));
}

// ── Track プラグインチェーン ─────────────────────────────────────────
void MainComponent::removePluginFromTrack(int trackIdx, int slotIdx)
{
    auto* t = trackManager.getTrack(trackIdx);
    if (!t) return;
    if (!t->getPluginChain().getPlugin(slotIdx)) return;

    // 削除を Undo 対応で実行 (インスタンスはアクションが延命・破棄せず保持)。
    // willRemove でエディタを閉じる。インスタンスは破棄しないので callAsync は不要。
    undoManager.beginNewTransaction();
    undoManager.perform(new PluginSlotAction(
        makeChainResolver(t), slotIdx, /*afterInstance*/ nullptr,
        [this] { markProjectDirty(); },
        [this](juce::AudioPluginInstance* p) { closePluginEditorFor(p); }));
}

void MainComponent::handlePluginDropAcrossTracks(int srcTrack, int srcSlot,
                                                 int dstTrack, int dstSlot, bool copy)
{
    // trackIdx == -1 はマスターチェーン。Track* で解決 (resolver は apply 時に生存確認)。
    Track* srcT = (srcTrack < 0) ? nullptr : trackManager.getTrack(srcTrack);
    Track* dstT = (dstTrack < 0) ? nullptr : trackManager.getTrack(dstTrack);
    if ((srcTrack >= 0 && srcT == nullptr) || (dstTrack >= 0 && dstT == nullptr)) return;

    auto* srcChainPtr = resolveChainForUndo(srcT);
    auto* dstChainPtr = resolveChainForUndo(dstT);
    if (!srcChainPtr || !dstChainPtr) return;
    auto* srcPlugin = srcChainPtr->getPlugin(srcSlot);
    if (!srcPlugin) return;

    auto onChange  = [this] { markProjectDirty(); };
    auto willClose = [this](juce::AudioPluginInstance* p) { closePluginEditorFor(p); };

    if (copy)
    {
        // 元プラグインの description と state を取得して、同じ種類の新インスタンスを生成
        auto desc = srcPlugin->getPluginDescription();
        juce::MemoryBlock state;
        srcPlugin->getStateInformation(state);

        const double sr = audioEngine.getSampleRate() > 0 ? audioEngine.getSampleRate() : 48000.0;
        const int    bs = 512;
        auto& fmgr      = pluginManager.getFormatManager();

        juce::String err;
        std::unique_ptr<juce::AudioPluginInstance> instance(
            fmgr.createPluginInstance(desc, sr, bs, err));

        if (!instance)
        {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(tr(u8"プラグインのコピーに失敗"))
                .withMessage(err)
                .withButton("OK"), nullptr);
            return;
        }
        if (state.getSize() > 0)
            instance->setStateInformation(state.getData(), (int) state.getSize());

        // コピー = dst スロットへの追加 (dst が埋まっていれば退避して undo で戻す)
        undoManager.beginNewTransaction();
        undoManager.perform(new PluginSlotAction(
            makeChainResolver(dstT), dstSlot, std::move(instance), onChange, willClose));
        return;
    }

    // 移動: src から取り出して dst の dstSlot へ (エディタ閉じ・dst 退避はアクション内で処理)
    undoManager.beginNewTransaction();
    undoManager.perform(new PluginMoveAction(
        makeChainResolver(srcT), srcSlot, makeChainResolver(dstT), dstSlot, onChange, willClose));
}

// ── Master プラグインチェーン ────────────────────────────────────────
void MainComponent::addPluginToMaster(int slotIdx)
{
    auto& knownList = pluginManager.getKnownPluginListRW();
    auto& fmgr      = pluginManager.getFormatManager();

    juce::PopupMenu menu;
    juce::KnownPluginList::addToMenu(menu, knownList.getTypes(),
                                     juce::KnownPluginList::sortByManufacturer);

    if (menu.getNumItems() == 0)
    {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle(tr(u8"プラグインがありません"))
            .withMessage(tr(u8"「ファイル」→「プラグインを管理...」でスキャンを実行してください"))
            .withButton("OK"), nullptr);
        return;
    }

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, slotIdx, &knownList, &fmgr](int result)
        {
            if (result <= 0) return;
            const int typeIdx = juce::KnownPluginList::getIndexChosenByMenu(knownList.getTypes(), result);
            if (typeIdx < 0) return;
            auto desc = knownList.getTypes()[typeIdx];

            const double sr  = audioEngine.getSampleRate() > 0 ? audioEngine.getSampleRate() : 48000.0;
            const int    bs  = 512;

            juce::String err;
            std::unique_ptr<juce::AudioPluginInstance> instance(
                fmgr.createPluginInstance(desc, sr, bs, err));

            if (!instance)
            {
                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle(tr(u8"プラグインの読込に失敗"))
                    .withMessage(err)
                    .withButton("OK"), nullptr);
                return;
            }

            auto& mc = audioEngine.getMasterChain();
            const int targetSlot = (slotIdx >= 0) ? slotIdx : mc.getNumPlugins();
            undoManager.beginNewTransaction();
            undoManager.perform(new PluginSlotAction(
                makeChainResolver(nullptr), targetSlot, std::move(instance),
                [this] { markProjectDirty(); },
                [this](juce::AudioPluginInstance* p) { closePluginEditorFor(p); }));
            openMasterPluginEditor(targetSlot);
        });
}

void MainComponent::openMasterPluginEditor(int slotIdx)
{
    auto* plugin = audioEngine.getMasterChain().getPlugin(slotIdx);
    if (!plugin) return;

    for (auto* w : pluginEditorWindows)
        if (w && w->getPlugin() == plugin) { w->toFront(true); return; }

    auto* w = new PluginEditorWindow(*plugin,
        [this](PluginEditorWindow* self) { pluginEditorWindows.removeObject(self); });
    pluginEditorWindows.add(w);

    auto* mappings = commandManager.getKeyMappings();
    w->addKeyListener(mappings);
    std::function<void(juce::Component*)> attach = [&](juce::Component* c)
    {
        if (c == nullptr) return;
        c->addKeyListener(mappings);
        for (auto* child : c->getChildren()) attach(child);
    };
    if (auto* content = w->getContentComponent())
    {
        content->setWantsKeyboardFocus(true);
        attach(content);
    }
}

void MainComponent::removeMasterPlugin(int slotIdx)
{
    if (!audioEngine.getMasterChain().getPlugin(slotIdx)) return;
    undoManager.beginNewTransaction();
    undoManager.perform(new PluginSlotAction(
        makeChainResolver(nullptr), slotIdx, /*afterInstance*/ nullptr,
        [this] { markProjectDirty(); },
        [this](juce::AudioPluginInstance* p) { closePluginEditorFor(p); }));
}

void MainComponent::handlePluginDropFromTrackToMaster(int srcTrack, int srcSlot,
                                                     int dstSlot, bool copy)
{
    // dstTrack = -1 (マスター) として汎用ハンドラに委譲
    handlePluginDropAcrossTracks(srcTrack, srcSlot, /*dstTrack=*/-1, dstSlot, copy);
}

// ── Track プラグイン追加 / エディタオープン ──────────────────────────
void MainComponent::addPluginToTrack(int trackIdx, int slotIdx)
{
    auto* track = trackManager.getTrack(trackIdx);
    if (!track) return;

    auto& knownList = pluginManager.getKnownPluginListRW();
    auto& fmgr      = pluginManager.getFormatManager();

    juce::PopupMenu menu;
    juce::KnownPluginList::addToMenu(menu, knownList.getTypes(),
                                     juce::KnownPluginList::sortByManufacturer);

    if (menu.getNumItems() == 0)
    {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle(tr(u8"プラグインがありません"))
            .withMessage(tr(u8"「ファイル」→「プラグインを管理...」でスキャンを実行してください"))
            .withButton("OK"), nullptr);
        return;
    }

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, trackIdx, slotIdx, &knownList, &fmgr](int result)
        {
            if (result <= 0) return;
            const int typeIdx = juce::KnownPluginList::getIndexChosenByMenu(knownList.getTypes(), result);
            if (typeIdx < 0) return;
            auto desc = knownList.getTypes()[typeIdx];

            const double sr  = audioEngine.getSampleRate() > 0 ? audioEngine.getSampleRate() : 48000.0;
            const int    bs  = 512;

            juce::String err;
            std::unique_ptr<juce::AudioPluginInstance> instance(
                fmgr.createPluginInstance(desc, sr, bs, err));

            if (!instance)
            {
                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle(tr(u8"プラグインの読込に失敗"))
                    .withMessage(err)
                    .withButton("OK"), nullptr);
                return;
            }

            auto* track = trackManager.getTrack(trackIdx);
            if (!track) return;
            // slotIdx 指定があればその位置に、無ければ末尾に
            const int targetSlot = (slotIdx >= 0)
                                       ? slotIdx
                                       : track->getPluginChain().getNumPlugins();
            undoManager.beginNewTransaction();
            undoManager.perform(new PluginSlotAction(
                makeChainResolver(track), targetSlot, std::move(instance),
                [this] { markProjectDirty(); },
                [this](juce::AudioPluginInstance* p) { closePluginEditorFor(p); }));
            openPluginEditor(trackIdx, targetSlot);
        });
}

void MainComponent::openPluginEditor(int trackIdx, int slotIdx)
{
    auto* track = trackManager.getTrack(trackIdx);
    if (!track) return;
    auto* plugin = track->getPluginChain().getPlugin(slotIdx);
    if (!plugin) return;

    // すでに開いていればフォーカスを当てるだけ
    for (auto* w : pluginEditorWindows)
        if (w && w->getPlugin() == plugin) { w->toFront(true); return; }

    auto* w = new PluginEditorWindow(*plugin,
        [this](PluginEditorWindow* self) { pluginEditorWindows.removeObject(self); });
    pluginEditorWindows.add(w);

    // プラグイン GUI 内部の子コンポーネントにフォーカスが当たっていても
    // スペース/トランスポートキーが効くよう、エディタ階層に再帰的に KeyListener を仕込む
    auto* mappings = commandManager.getKeyMappings();
    w->addKeyListener(mappings);

    std::function<void(juce::Component*)> attach = [&](juce::Component* c)
    {
        if (c == nullptr) return;
        c->addKeyListener(mappings);
        for (auto* child : c->getChildren())
            attach(child);
    };
    if (auto* content = w->getContentComponent())
    {
        content->setWantsKeyboardFocus(true);
        attach(content);
    }
}

void MainComponent::showPluginManager()
{
    auto* dlg = new PluginManagerDialog(pluginManager);
    dlg->setSize(800, 540);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(dlg);
    opts.dialogTitle                  = tr(u8"プラグイン管理");
    opts.dialogBackgroundColour       = juce::Colour(0xff181a1e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = true;
    opts.launchAsync();
}
