// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// TimelineView のマウスイベント処理。
// mouseMove / mouseExit / mouseDoubleClick / mouseDown / mouseDrag / mouseUp / mouseWheelMove
// および getMidiClipAt / クリップ名インライン編集を含む。
// TimelineView.cpp が肥大化したため分割。

#include "TimelineView.h"
#include "../Localisation.h"
#include "../AppColours.h"
#include "../Tracks/MidiClip.h"
#include "../Edit/SilenceDetector.h"
#include "../Audio/BpmDetector.h"
#include "../Audio/LufsMeter.h"
#include "../Audio/PitchEngine.h"
#include "TextImageCache.h"
#include <set>
#include <map>

void TimelineView::mouseMove(const juce::MouseEvent& e)
{
    // フェードハンドルのホバー強調用: 現在マウスが乗っている AudioClip と、
    // カーソルが当たっているフェードハンドル (FadeIn / FadeOut) を記録。
    // 変化したときだけ repaint (クリップ境界 / ハンドル境界を跨いだ瞬間のみ)。
    {
        auto hr = getClipAt(e.x, e.y);
        AudioClip* nowHover  = hr.valid() ? hr.clip : nullptr;
        DragMode   nowHandle = DragMode::None;
        // Alt (カット / ペンシル) 中は別モードなのでハンドル判定しない
        if (hr.valid() && !e.mods.isAltDown())
        {
            auto dm = getDragMode(hr, e.x, e.y);
            if (dm == DragMode::FadeIn || dm == DragMode::FadeOut)
                nowHandle = dm;
        }
        if (nowHover != hoveredClip || nowHandle != hoveredHandle)
        {
            hoveredClip   = nowHover;
            hoveredHandle = nowHandle;
            repaint();
        }
    }

    // MIDI クリップ上のカーソル (端=リサイズ / タイトル=ハンド)。
    // Alt (カット/ペンシル) 中はここで早期 return せず、下の Alt ブロックで
    // 十字カーソル (カット可) を出す。
    if (auto mh = getMidiClipAt(e.x, e.y); mh.clip != nullptr && !e.mods.isAltDown())
    {
        if (mh.leftEdge || mh.rightEdge)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else if (mh.isHeader)
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
        showCutLine = false;
        return;
    }

    // MIDI トラックの空きエリア + Option → ペンシル (十字カーソル)
    if (e.mods.isAltDown())
    {
        int mti = -1;
        if (midiTrackAtY(e.y, mti) != nullptr)
        {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            showCutLine = false;
            return;
        }
    }

    auto ref = getClipAt(e.x, e.y);

    // Alt (Mac は Option) ホールド = ハサミ（カット）モード
    if (e.mods.isAltDown())
    {
        showCutLine = ref.valid();
        cutLineX    = e.x;
        setMouseCursor(ref.valid() ? juce::MouseCursor::CrosshairCursor
                                   : juce::MouseCursor::NormalCursor);
        return;
    }

    showCutLine = false;

    if (!ref.valid())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // クロスフェードハンドルのカーソル検出 (双方向: ref が左でも右でも検出)
    {
        const double bps_ = bpm / 60.0;
        auto* lane = ref.track->getLane(ref.laneIdx);
        if (lane)
        {
            for (auto& cPtr : lane->clips)
            {
                auto* nb = cPtr.get();
                if (nb == ref.clip) continue;
                const double rS = ref.clip->getStartPosition();
                const double rE = ref.clip->getEndPosition();
                const double nS = nb->getStartPosition();
                const double nE = nb->getEndPosition();
                const double overlap = juce::jmin(rE, nE) - juce::jmax(rS, nS);
                if (overlap <= 0.001) continue;

                AudioClip* clipA = (rS <= nS) ? ref.clip : nb;
                AudioClip* clipB = (rS <= nS) ? nb       : ref.clip;
                // 同一連続音声 (Alt+Click 分割) のクロスフェードも、描画 (drawCrossfadeOverlay)
                // と同じく扱う (#I2 撤去)。既定 Linear は gainA+gainB=1 で完全再構成されコームが
                // 出ないため、選択/移動カーソルも許可する (描画が X を出すのに掴めない不整合を防ぐ)。
                // 描画と同じ条件でのみクロスフェードとして扱う (#L3)。X が見えない
                // (autoCrossfade OFF かつ両フェードが小の単なる重なり) ときはカーソルを変えない。
                const bool xfadeDrawn = appSettings.autoCrossfade
                                        || clipA->getFadeOutSecs() >= kCrossfadeFadeMinSecs
                                        || clipB->getFadeInSecs()  >= kCrossfadeFadeMinSecs;
                if (!xfadeDrawn) continue;
                const int xL = (int)(clipB->getStartPosition() * bps_ * pixelsPerBeat - scrollX);
                const int xR = (int)(clipA->getEndPosition()   * bps_ * pixelsPerBeat - scrollX);

                if (std::abs(e.x - xL) <= 8 || std::abs(e.x - xR) <= 8)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
                if (e.x > xL + 8 && e.x < xR - 8)
                {
                    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                    return;
                }
            }
        }
    }

    switch (getDragMode(ref, e.x, e.y))
    {
        case DragMode::FadeIn:
        case DragMode::FadeOut:
        case DragMode::ResizeLeft:
        case DragMode::ResizeRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::Gain:
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor); break;
        case DragMode::Selection:
            setMouseCursor(juce::MouseCursor::IBeamCursor); break;
        default:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);   break;
    }
}

void TimelineView::mouseExit(const juce::MouseEvent&)
{
    showCutLine = false;
    if (hoveredClip != nullptr || hoveredHandle != DragMode::None)
    {
        hoveredClip   = nullptr;
        hoveredHandle = DragMode::None;
        repaint();
    }
}

void TimelineView::mouseDown(const juce::MouseEvent& e)
{
    // クリップ名編集中なら、別箇所のクリックで確定して閉じる
    if (nameEditor != nullptr && !nameEditor->getBounds().contains(e.x, e.y))
        finishNameEditing(true);

    // ── Alt+クリック (Mac は Option) on MIDI クリップ → 分割 ──
    if (e.mods.isLeftButtonDown() && e.mods.isAltDown())
    {
        auto mh = getMidiClipAt(e.x, e.y);
        if (mh.clip != nullptr)
        {
            splitMidiClip(mh.track, mh.clip, juce::jmax(0.0, xToPosition(e.x)));
            return;
        }
    }

    // ── MIDI トラック上の左ボタン操作 (選択 / リサイズ / 移動 / 作成) ──
    if (e.mods.isLeftButtonDown() && !e.mods.isCommandDown() && !e.mods.isShiftDown())
    {
        auto mh = getMidiClipAt(e.x, e.y);
        if (mh.clip != nullptr)
        {
            // クリックで選択状態にする (Audio クリップ選択はクリア)
            clearAllSelections();
            selectedMidiClip  = mh.clip;
            selectedMidiTrack = mh.track;
            repaint();

            // 左右端 → リサイズ開始
            if (mh.leftEdge || mh.rightEdge)
            {
                resizingMidiClip    = mh.clip;
                midiResizeLeft      = mh.leftEdge;
                midiResizeOrigStart = mh.clip->getStartPosition();
                midiResizeOrigEnd   = mh.clip->getEndPosition();
                return;
            }
            // ヘッダ → 移動ドラッグ開始
            if (mh.isHeader)
            {
                draggingMidiClip   = mh.clip;
                midiDragOrigStart  = mh.clip->getStartPosition();
                midiDragStartX     = e.x;
                return;
            }
            // ボディクリック: 選択のみ
            return;
        }
        else if (e.mods.isAltDown())
        {
            // Option + MIDI トラックの空きエリア → ペンシル: ドラッグでクリップ作成
            int mti = -1;
            if (auto* mt = midiTrackAtY(e.y, mti); mt != nullptr)
            {
                const double unit  = gridUnitSecs();
                const double t     = juce::jmax(0.0, xToPosition(e.x));
                const double start = snapTime(t);  // GRID 設定に従ってスナップ
                if (auto* clip = mt->addMidiClip(start, unit))  // 初期サイズ = 1 グリッド単位
                {
                    clip->setName(mt->getName());
                    creatingMidiClip = clip;
                    midiCreateAnchor = start;
                    clearAllSelections();
                    selectedMidiClip  = clip;
                    selectedMidiTrack = mt;
                    // 作成中は再描画のみ。確定 (rebuild) は mouseUp で 1 回。
                    repaint();
                }
                return;
            }
        }
    }

    // 右クリック on MIDI クリップ → コンテキストメニュー (削除 / ピアノロール)
    if (e.mods.isRightButtonDown())
    {
        auto mh = getMidiClipAt(e.x, e.y);
        if (mh.clip != nullptr)
        {
            grabKeyboardFocus();
            clearAllSelections();
            selectedMidiClip  = mh.clip;
            selectedMidiTrack = mh.track;
            repaint();

            auto JM = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };
            juce::PopupMenu m;
            m.addItem(1, JM(u8"ピアノロールを開く"));
            m.addSeparator();
            m.addItem(2, JM(u8"削除"));
            m.showMenuAsync(juce::PopupMenu::Options(),
                [this, clip = mh.clip, track = mh.track](int result)
                {
                    if (result == 1)
                    {
                        if (onMidiClipDoubleClicked) onMidiClipDoubleClicked(clip, track);
                    }
                    else if (result == 2)
                    {
                        // 念のため選択を当該クリップに合わせてから削除
                        selectedMidiClip  = clip;
                        selectedMidiTrack = track;
                        deleteSelectedMidiClip();
                    }
                });
            return;
        }
    }

    // 右クリックでクリップのコンテキストメニュー
    if (e.mods.isRightButtonDown())
    {
        auto rcRef = getClipAt(e.x, e.y);
        if (rcRef.valid())
        {
            grabKeyboardFocus();
            // 右クリックしたクリップを選択
            if (!isClipInSelection(rcRef.clip))
            {
                clearAllSelections();
                selectedClip = rcRef;
                repaint();
                notifySelectionChanged();  // 右クリック選択確定 → 採用ボタン活性を更新
            }

            juce::PopupMenu m;
            // J ラムダはこの関数の後段で定義されているため、ここでローカルに用意する
            auto JJ = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };

            // Take レーン (laneIdx > 0) のクリップ → 最上段に「このテイクを使う」
            const bool isTakeLane = (rcRef.laneIdx > 0);
            if (isTakeLane)
            {
                // 注意: 320 は「クロスフェードを描く」で使用済み。混同すると採用ではなく
                // クロスフェードが走ってしまうため、別 ID (321) を使う。
                m.addItem(321, JJ(u8"このテイクを使う"));
                m.addSeparator();
            }

            // 色変更サブメニュー
            juce::PopupMenu colourMenu;
            const std::array<std::pair<const char*, juce::Colour>, 8> palette = {{
                {"Blue",   juce::Colour(0xff3a6ea5)},
                {"Green",  juce::Colour(0xff5aa55a)},
                {"Red",    juce::Colour(0xffa55a5a)},
                {"Orange", juce::Colour(0xffa5925a)},
                {"Purple", juce::Colour(0xff7a5aa5)},
                {"Cyan",   juce::Colour(0xff5a9ea5)},
                {"Pink",   juce::Colour(0xffa55a92)},
                {"Steel",  juce::Colour(0xff5a7aa5)}
            }};
            for (size_t i = 0; i < palette.size(); ++i)
            {
                juce::PopupMenu::Item item;
                item.itemID = 100 + (int)i;
                item.text = palette[i].first;
                item.colour = palette[i].second;
                colourMenu.addItem(item);
            }
            m.addSubMenu("Clip Colour", colourMenu);

            // フェードカーブは初心者向けに既定 (リニア = 綺麗な直線の X) に固定し、
            // 種別選択 UI (リニア/対数/等パワー/S字) は出さない (細かい設定を隠す)。
            auto J = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };

            m.addSeparator();
            int extraCount = (int)extraSelections.size() + (selectedClip.valid() ? 1 : 0);
            m.addItem(300, J(u8"クリップ結合 (Consolidate)"),
                      extraCount >= 2);
            // クロスフェードを描く: 「範囲選択あり」かつ「その範囲内で 2 つのクリップが
            // 重なり/接触している (= クロスフェード可能な箇所がある)」時だけメニューに出す。
            // (クリップを選んだだけ・範囲選択なしのときは出さない)
            if (hasSelectionRange())
            {
                const double selS = loopStartTV;
                const double selE = loopEndTV;
                const double tol  = 0.005;
                bool junction = false;
                for (int ti = 0; ti < trackManager.getTrackCount() && !junction; ++ti)
                {
                    auto* track = trackManager.getTrack(ti);
                    if (!track) continue;
                    for (int li = 0; li < track->getLaneCount() && !junction; ++li)
                    {
                        auto* lane = track->getLane(li);
                        if (!lane) continue;
                        for (size_t a = 0; a < lane->clips.size() && !junction; ++a)
                        {
                            auto* ca = lane->clips[a].get();
                            for (size_t b = 0; b < lane->clips.size(); ++b)
                            {
                                if (b == a) continue;
                                auto* cb = lane->clips[b].get();
                                if (cb->getStartPosition() < ca->getStartPosition()) continue; // ca を左に
                                // 接触/重なり (ca.end + tol >= cb.start)
                                if (ca->getEndPosition() + tol < cb->getStartPosition()) continue;
                                // 境界/重なり区間が選択範囲と交差するか
                                const double jS = juce::jmin(ca->getEndPosition(), cb->getStartPosition());
                                const double jE = juce::jmax(ca->getEndPosition(), cb->getStartPosition());
                                if (juce::jmax(selS, jS) <= juce::jmin(selE, jE) + tol)
                                    { junction = true; break; }
                            }
                        }
                    }
                }
                if (junction)
                    m.addItem(320, J(u8"クロスフェードを描く"));
            }
            m.addItem(310, J(u8"無音区間をカット..."));
            m.addItem(330, J(u8"テンポを検出"));
            m.addItem(340, J(u8"ラウドネスを ") + juce::String(appSettings.loudnessTargetLufs, 1)
                              + J(u8" LUFS に合わせる"));

            // ── キー変更 (-6 〜 +6 半音) サブメニュー ──
            // 現在のキー値はチェックマークで明示。 ★ は処理済みキャッシュあり。
            juce::PopupMenu pitchMenu;
            auto srcFileForMenu = rcRef.clip ? rcRef.clip->getFile() : juce::File();
            auto curStem        = srcFileForMenu.getFileNameWithoutExtension();
            int  currentSemis   = 0;
            juce::String srcStem = curStem;
            if (curStem.contains("_pitch"))
            {
                srcStem = curStem.upToLastOccurrenceOf("_pitch", false, false);
                currentSemis = curStem.fromLastOccurrenceOf("_pitch", false, false).getIntValue();
            }
            for (int s = -6; s <= 6; ++s)
            {
                juce::String label;
                if (s == 0) label = J(u8"0 (元の音程)");
                else        label = (s > 0 ? juce::String("+") : juce::String()) + juce::String(s) + J(u8" 半音");

                // キャッシュ判定
                if (s != 0 && srcFileForMenu.existsAsFile())
                {
                    const juce::String suffix = (s > 0 ? juce::String("+") : juce::String()) + juce::String(s);
                    auto cached = srcFileForMenu.getParentDirectory().getChildFile(srcStem + "_pitch" + suffix + ".wav");
                    if (cached.existsAsFile())
                        label += "  \xe2\x98\x85";  // ★ (UTF-8)
                }

                juce::PopupMenu::Item it;
                it.itemID  = 500 + 6 + s;
                it.text    = label;
                it.isTicked = (s == currentSemis);
                pitchMenu.addItem(it);
                if (s == 0) pitchMenu.addSeparator();
            }
            m.addSubMenu(J(u8"キーを変更..."), pitchMenu);
            m.addSeparator();
            m.addItem(400, J(u8"削除"));

            m.showMenuAsync(juce::PopupMenu::Options(),
                [this, rcRef](int result) {
                    if (result <= 0) return;
                    if (result >= 100 && result <= 107)
                    {
                        // 色変更
                        const std::array<juce::Colour, 8> palette2 = {
                            juce::Colour(0xff3a6ea5), juce::Colour(0xff5aa55a),
                            juce::Colour(0xffa55a5a), juce::Colour(0xffa5925a),
                            juce::Colour(0xff7a5aa5), juce::Colour(0xff5a9ea5),
                            juce::Colour(0xffa55a92), juce::Colour(0xff5a7aa5)
                        };
                        juce::Colour newCol = palette2[result - 100];
                        // 選択中のクリップ全てに適用 (Undo 対応)
                        std::vector<ClipRef> all;
                        if (selectedClip.valid()) all.push_back(selectedClip);
                        for (auto& r : extraSelections) all.push_back(r);
                        if (all.empty()) all.push_back(rcRef);
                        std::vector<EditActions::ClipState> oldS, newS;
                        for (auto& r : all)
                        {
                            EditActions::ClipState s; s.capture(r.clip); oldS.push_back(s);
                            r.clip->setColour(newCol);
                            EditActions::ClipState ns; ns.capture(r.clip); newS.push_back(ns);
                        }
                        if (undoManager)
                        {
                            undoManager->beginNewTransaction();
                            undoManager->perform(new EditActions::ClipsPropertyAction(
                                std::move(oldS), std::move(newS), editChangeCb));
                        }
                        repaint();
                        return;
                    }
                    if (result == 300)
                    {
                        consolidateSelectedClips();
                        return;
                    }
                    if (result == 320)
                    {
                        applyCrossfadeToSelection(FadeOpMode::CrossfadeOnly);
                        return;
                    }
                    if (result == 321)
                    {
                        // 「このテイクを使う」: 右クリックしたテイクレーンから Lane 0 へ採用。
                        // 選択範囲があればその範囲、無ければ右クリックしたクリップ全体を当てる
                        // (= Shift+↑ / ↑ ボタンと同じ promoteTakeLane)。範囲が無いときは
                        // 右クリッククリップを選択状態にして promoteTakeLane の「選択クリップ」
                        // 経路を満たす。promoteRangeToLane0 が editChangeCb
                        // (markProjectDirty / refresh / invalidatePlayback) を呼ぶので追加処理は不要。
                        if (!hasSelectionRange())
                        {
                            clearAllSelections();
                            selectedClip = rcRef;
                            notifySelectionChanged();
                        }
                        promoteTakeLane(rcRef.trackIdx, rcRef.laneIdx);
                        return;
                    }
                    if (result == 310)
                    {
                        showStripSilenceDialog(rcRef);
                        return;
                    }
                    if (result == 330)
                    {
                        // テンポを検出
                        if (rcRef.valid() && rcRef.track && rcRef.clip)
                        {
                            auto J = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };

                            // 検出は重い (全ファイル走査) ので進捗ウィンドウ付きで別スレッド実行。
                            // モーダル実行中はクリップが破棄されないため参照キャプチャで安全。
                            struct DetectJob : juce::ThreadWithProgressWindow
                            {
                                juce::File file; juce::AudioFormatManager& fmt;
                                double off, dur; double result = 0.0;
                                DetectJob (juce::File f, juce::AudioFormatManager& fm, double o, double d,
                                           const juce::String& title, const juce::String& status)
                                    : juce::ThreadWithProgressWindow (title, true, true),
                                      file (f), fmt (fm), off (o), dur (d)
                                { setStatusMessage (status); }
                                void run() override
                                {
                                    result = BpmDetector::detect (file, fmt, off, dur,
                                        [this] (double frac) { setProgress (frac); return ! threadShouldExit(); });
                                }
                            };
                            DetectJob job (rcRef.clip->getFile(),
                                           rcRef.track->getFormatManager(),
                                           rcRef.clip->getFileOffset(),
                                           rcRef.clip->getDuration(),
                                           J(u8"テンポ検出"), J(u8"テンポを検出中…"));
                            if (! job.runThread()) return;   // ユーザーがキャンセル
                            const double bpm = job.result;
                            if (bpm <= 0.0)
                            {
                                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                                    .withTitle(J(u8"テンポ検出"))
                                    .withMessage(J(u8"テンポを検出できませんでした。\nクリップが短すぎるか、明確なビートが見つかりません。"))
                                    .withButton("OK"), nullptr);
                            }
                            else
                            {
                                const juce::String msg = J(u8"検出されたテンポ: ")
                                    + juce::String((int) std::round(bpm)) + " BPM\n\n"
                                    + J(u8"プロジェクトに適用しますか?");
                                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                    .withTitle(J(u8"テンポ検出"))
                                    .withMessage(msg)
                                    .withButton(J(u8"適用"))
                                    .withButton(J(u8"キャンセル")),
                                    [this, bpm](int r) {
                                        if (r == 1 && onApplyDetectedBpm)
                                            onApplyDetectedBpm(bpm);
                                    });
                            }
                        }
                        return;
                    }
                    if (result == 340)
                    {
                        // ラウドネスを -18 LUFS に合わせる (クリップゲインを調整)
                        if (rcRef.valid() && rcRef.track && rcRef.clip)
                        {
                            const float  oldGain     = rcRef.clip->getGain();
                            const double oldGainDb   = 20.0 * std::log10(juce::jmax(1.0e-12f, oldGain));
                            const double measuredLufs = LufsMeter::measureFileSegment(
                                rcRef.clip->getFile(),
                                rcRef.track->getFormatManager(),
                                rcRef.clip->getFileOffset(),
                                rcRef.clip->getDuration(),
                                oldGain);
                            auto J = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };
                            if (!std::isfinite(measuredLufs))
                            {
                                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                                    .withTitle(J(u8"ラウドネス測定"))
                                    .withMessage(J(u8"ラウドネスを測定できませんでした。\nクリップが短すぎるか、無音が多すぎます。"))
                                    .withButton("OK"), nullptr);
                                return;
                            }

                            const double targetLufs   = (double) appSettings.loudnessTargetLufs;
                            const double adjustmentDb = targetLufs - measuredLufs;
                            const float  newGain      = oldGain * (float) std::pow(10.0, adjustmentDb / 20.0);
                            const double newGainDb    = oldGainDb + adjustmentDb;

                            auto fmt2 = [](double v) {
                                return (v >= 0.0 ? juce::String("+") : juce::String())
                                       + juce::String(v, 1);
                            };
                            const juce::String msg = J(u8"現在のラウドネス: ")
                                + juce::String(measuredLufs, 1) + " LUFS\n"
                                + J(u8"ターゲット: ") + juce::String(targetLufs, 1) + " LUFS\n"
                                + J(u8"クリップゲイン: ") + fmt2(oldGainDb) + " dB → "
                                + fmt2(newGainDb) + " dB ("
                                + fmt2(adjustmentDb) + " dB)\n\n"
                                + J(u8"適用しますか?");

                            juce::Component::SafePointer<TimelineView> safe(this);
                            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                .withTitle(J(u8"ラウドネス調整"))
                                .withMessage(msg)
                                .withButton(J(u8"適用"))
                                .withButton(J(u8"キャンセル")),
                                [safe, rcRef, newGain](int r)
                                {
                                    if (r != 1) return;
                                    auto* tv = safe.getComponent();
                                    if (!tv || !rcRef.valid() || rcRef.clip == nullptr) return;
                                    EditActions::ClipState oldS, newS;
                                    oldS.capture(rcRef.clip);
                                    rcRef.clip->setGain(newGain);
                                    newS.capture(rcRef.clip);
                                    if (tv->undoManager)
                                    {
                                        tv->undoManager->beginNewTransaction();
                                        tv->undoManager->perform(new EditActions::ClipsPropertyAction(
                                            { oldS }, { newS }, tv->editChangeCb));
                                    }
                                    tv->repaint();
                                });
                        }
                        return;
                    }
                    // キー変更: ID 500..512 (= -6..+6 半音, 506 = 0)
                    if (result >= 500 && result <= 512)
                    {
                        const int semis = result - 500 - 6;
                        if (!rcRef.valid() || !rcRef.track || !rcRef.clip) return;
                        auto* clip = rcRef.clip;
                        auto srcFile = clip->getFile();
                        auto J2 = [](const char* s) { return juce::translate(juce::String::fromUTF8(s)); };

                        // 元ファイル (拡張子付き) を解決
                        // - 現在が xxx_pitch±N.<ext> なら xxx.<ext> に戻して探す
                        // - 拡張子は .wav / .aif / .aiff / .mp3 / .m4a / .flac / .ogg を順に試す
                        juce::File origFile = srcFile;
                        {
                            auto stem = srcFile.getFileNameWithoutExtension();
                            if (stem.contains("_pitch"))
                            {
                                stem = stem.upToLastOccurrenceOf("_pitch", false, false);
                                auto dir = srcFile.getParentDirectory();
                                static const char* exts[] = { ".wav", ".aif", ".aiff", ".mp3", ".m4a", ".flac", ".ogg" };
                                juce::File found;
                                for (auto* e : exts)
                                {
                                    auto f = dir.getChildFile(stem + e);
                                    if (f.existsAsFile()) { found = f; break; }
                                }
                                if (found != juce::File()) origFile = found;
                            }
                        }

                        // semis == 0 (元の音程に戻す) の場合は元ファイルへ差し替えるだけ
                        if (semis == 0)
                        {
                            if (origFile == clip->getFile()) return;  // 既に元の状態
                            if (!origFile.existsAsFile())
                            {
                                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                                    .withTitle(J2(u8"キー変更"))
                                    .withMessage(J2(u8"元の音程のファイルが見つかりませんでした。"))
                                    .withButton("OK"), nullptr);
                                return;
                            }
                            clip->setFile(origFile);
                            clip->invalidateWaveformCache();
                            clip->refreshThumbnail();
                            if (editChangeCb) editChangeCb();
                            if (onWaveformRefreshNeeded) onWaveformRefreshNeeded();
                            repaint();
                            return;
                        }

                        // 変換目標ファイル名 = <元ステム>_pitch<符号><量>.wav
                        srcFile = origFile;
                        const juce::String suffix = (semis > 0 ? juce::String("+") : juce::String()) + juce::String(semis);
                        auto outFile = srcFile.getParentDirectory().getChildFile(
                            srcFile.getFileNameWithoutExtension() + "_pitch" + suffix + ".wav");

                        // 既存キャッシュがあれば即時切り替え
                        if (outFile.existsAsFile())
                        {
                            clip->setFile(outFile);
                            clip->invalidateWaveformCache();
                            clip->refreshThumbnail();
                            if (editChangeCb) editChangeCb();
                            if (onWaveformRefreshNeeded) onWaveformRefreshNeeded();
                            repaint();
                            return;
                        }

                        // ── プログレスウィンドウ付きで変換 ──
                        class PitchJob : public juce::ThreadWithProgressWindow
                        {
                        public:
                            PitchJob(const juce::File& in, const juce::File& out,
                                     juce::AudioFormatManager& fmt, double sem)
                                : juce::ThreadWithProgressWindow(
                                    /*title*/ makeTitle(sem),
                                    /*hasProgressBar*/ true,
                                    /*hasCancelButton*/ false),
                                  inFile(in), outFile(out), formatManager(fmt), semitones(sem) {}

                            static juce::String makeTitle(double sem)
                            {
                                const juce::String semStr = (sem > 0 ? juce::String("+") : juce::String())
                                                          + juce::String((int) sem);
                                return tr(u8"キーを ")
                                     + semStr
                                     + tr(u8" へ変換中...");
                            }

                            void run() override
                            {
                                ok = PitchEngine::processFile(inFile, outFile, formatManager,
                                                              semitones, 32,
                                    [this](double p) { setProgress(p); });
                            }
                            bool ok { false };
                        private:
                            juce::File inFile, outFile;
                            juce::AudioFormatManager& formatManager;
                            double semitones;
                        };

                        auto job = std::make_unique<PitchJob>(
                            srcFile, outFile, rcRef.track->getFormatManager(), (double) semis);
                        const bool finished = job->runThread();  // モーダル
                        if (!finished || !job->ok)
                        {
                            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::WarningIcon)
                                .withTitle(J2(u8"キー変更"))
                                .withMessage(J2(u8"ピッチシフト処理に失敗しました。"))
                                .withButton("OK"), nullptr);
                            return;
                        }
                        clip->setFile(outFile);
                        clip->invalidateWaveformCache();
                        clip->refreshThumbnail();
                        if (editChangeCb) editChangeCb();
                        // ロード完了までポーリングして波形を再描画 (MainComponent 側に委譲)
                        if (onWaveformRefreshNeeded) onWaveformRefreshNeeded();
                        repaint();
                        return;
                    }
                    // (「このテイクを使う」は ID 321 で上の方で処理済み。以前ここに ID 320 の
                    //  ハンドラがあったが、320 は「クロスフェードを描く」と衝突して到達不能だった)
                    if (result == 400)
                    {
                        deleteSelectedClips();
                        return;
                    }
                });
            return;
        }
    }

    grabKeyboardFocus();
    selectedCrossfade.clear();

    auto ref = getClipAt(e.x, e.y);
    const double bps = bpm / 60.0;
    dragStartSecs = (e.x + scrollX) / (bps * pixelsPerBeat);

    // 空白部クリック (Alt = 分割モードなので空白では何もしない)
    if (!ref.valid() && !e.mods.isCommandDown() && !e.mods.isShiftDown() && !e.mods.isAltDown())
    {
        // 範囲ツール (Selection or Both) なら時間範囲選択を開始
        if (appSettings.toolMode == ToolMode::Selection
            || appSettings.toolMode == ToolMode::Both)
        {
            // クリップ未選択でも DragMode::Selection を発動
            clearAllSelections();
            dragMode = DragMode::Selection;
            // 空エリアの場合はフォーカスレーンを y 位置から推定
            auto area = getContentArea();
            int relY = e.y - area.getY() + scrollY;
            int trackIdx = trackManager.trackAtY(relY);
            int laneIdx  = 0;
            if (trackIdx >= 0)
            {
                auto* tr = trackManager.getTrack(trackIdx);
                int trackTopRel = trackManager.getTrackY(trackIdx);
                int yInTrack = relY - trackTopRel;
                if (!tr->isLanesCollapsed() && tr->getLaneCount() > 1)
                {
                    int mainH = tr->getMainHeight();
                    if (yInTrack < mainH) laneIdx = 0;
                    else laneIdx = 1 + (yInTrack - mainH) / tr->getLaneHeight();
                    if (laneIdx >= tr->getLaneCount()) laneIdx = tr->getLaneCount() - 1;
                }
            }
            selectionFocusTrackIdx = trackIdx;
            selectionFocusLaneIdx  = laneIdx;
            // 既存範囲はクリア（mouseDrag で新規設定される）
            if (loopEndTV > loopStartTV + 0.001 && onSetSelectionRange)
                onSetSelectionRange(0.0, 0.0);
            // 再生バー追従
            if (appSettings.playheadFollowsSelection && onSeek)
                onSeek(snapTime(dragStartSecs));
            repaint();
            return;
        }

        // それ以外 → ラバーバンド（クリップ複数選択）
        clearAllSelections();
        rubberBandActive = true;
        rubberBandStart = { e.x, e.y };
        rubberBandEnd   = { e.x, e.y };
        repaint();
        return;
    }

    // Shift+Click → 追加選択（プライマリ単一選択を維持しつつ、複数選択に追加）
    if (ref.valid() && e.mods.isShiftDown() && !e.mods.isCommandDown() && !e.mods.isAltDown())
    {
        if (!selectedClip.valid())
        {
            selectedClip = ref;
        }
        else if (selectedClip.clip == ref.clip)
        {
            // 既に primary → 何もしない
        }
        else
        {
            // 既に extra にあるなら除去（トグル）、なければ追加
            bool found = false;
            for (auto it = extraSelections.begin(); it != extraSelections.end(); ++it)
                if (it->clip == ref.clip) { extraSelections.erase(it); found = true; break; }
            if (!found) extraSelections.push_back(ref);
        }
        dragMode = DragMode::None;
        notifySelectionChanged();  // Shift+クリックで primary が変わる場合があるため
        repaint();
        return;
    }

    // ── Phase 1: クロスフェード中央クリック → クロスフェード選択（最優先）──
    // (Alt = 分割モードのときはクロスフェード選択せず分割へ抜ける)
    if (ref.valid() && !e.mods.isCommandDown() && !e.mods.isAltDown())
    {
        const double bps_ = bpm / 60.0;
        auto* lane = ref.track->getLane(ref.laneIdx);
        if (lane)
        {
            for (auto& cPtr : lane->clips)
            {
                auto* nb = cPtr.get();
                if (nb == ref.clip) continue;

                // ref と nb の overlap を計算 (双方向対応)
                const double rS = ref.clip->getStartPosition();
                const double rE = ref.clip->getEndPosition();
                const double nS = nb->getStartPosition();
                const double nE = nb->getEndPosition();
                const double overlap = juce::jmin(rE, nE) - juce::jmax(rS, nS);
                if (overlap <= 0.001) continue;

                // 左右を確定 (smaller start = clipA = 左, larger start = clipB = 右)
                AudioClip* clipA = (rS <= nS) ? ref.clip : nb;
                AudioClip* clipB = (rS <= nS) ? nb       : ref.clip;
                const double overlapStart = clipB->getStartPosition();
                const double overlapEnd   = clipA->getEndPosition();

                // 同一連続音声 (Alt+Click 分割) のクロスフェードも、描画 (drawCrossfadeOverlay)
                // と同じく選択/移動を許可する (#I2 撤去)。これをしないと「X は描かれるのに掴むと
                // クリップが選ばれてしまう」不整合になる (分割片同士のクロスフェードで発生していた)。
                // 描画と同じ条件でのみクロスフェード選択を許可する (#L3)。X が見えない
                // 重なりを誤って「クロスフェード」として掴めてしまうのを防ぐ。
                const bool xfadeDrawn = appSettings.autoCrossfade
                                        || clipA->getFadeOutSecs() >= kCrossfadeFadeMinSecs
                                        || clipB->getFadeInSecs()  >= kCrossfadeFadeMinSecs;
                if (!xfadeDrawn) continue;

                int xL = (int)(overlapStart * bps_ * pixelsPerBeat - scrollX);
                int xR = (int)(overlapEnd   * bps_ * pixelsPerBeat - scrollX);

                if (std::abs(e.x - xR) <= 8)
                {
                    // 右端ハンドル → clipA.end をドラッグ
                    selectedCrossfade.clipA = clipA;
                    selectedCrossfade.clipB = clipB;
                    selectedClip.clear();
                    crossfadeNeighbor = clipA;
                    clipOrigStart     = clipB->getStartPosition();
                    clipOrigEnd       = clipA->getEndPosition();
                    clipBOrigEnd      = clipB->getEndPosition();
                    origFileOffset    = clipB->getFileOffset();
                    dragMode          = DragMode::CrossfadeRight;
                    preDragStates.clear();
                    EditActions::ClipState sA; sA.capture(clipA);
                    EditActions::ClipState sB; sB.capture(clipB);
                    preDragStates.push_back(sA); preDragStates.push_back(sB);
                    repaint();
                    return;
                }
                else if (std::abs(e.x - xL) <= 8)
                {
                    // 左端ハンドル → clipB.start をドラッグ
                    selectedCrossfade.clipA = clipA;
                    selectedCrossfade.clipB = clipB;
                    selectedClip.clear();
                    crossfadeNeighbor = clipA;
                    clipOrigStart     = clipB->getStartPosition();
                    clipOrigEnd       = clipA->getEndPosition();
                    clipBOrigEnd      = clipB->getEndPosition();
                    origFileOffset    = clipB->getFileOffset();
                    dragMode          = DragMode::CrossfadeLeft;
                    preDragStates.clear();
                    // clipA(=crossfadeNeighbor) も fadeOut を変更するため両方捕捉する
                    // (clipB のみだと Undo で clipA の fadeOut が戻らず左右非対称に壊れる)
                    EditActions::ClipState sA; sA.capture(clipA);
                    EditActions::ClipState sB; sB.capture(clipB);
                    preDragStates.push_back(sA); preDragStates.push_back(sB);
                    repaint();
                    return;
                }
                else if (e.x > xL + 8 && e.x < xR - 8)
                {
                    // 中央 → クロスフェード選択
                    selectedCrossfade.clipA = clipA;
                    selectedCrossfade.clipB = clipB;
                    selectedClip.clear();
                    crossfadeNeighbor = clipA;
                    clipOrigEnd       = clipA->getEndPosition();
                    clipOrigStart     = clipB->getStartPosition();
                    clipBOrigEnd      = clipB->getEndPosition();
                    origFileOffset    = clipB->getFileOffset();
                    dragMode          = DragMode::CrossfadeCenter;
                    preDragStates.clear();
                    EditActions::ClipState sA; sA.capture(clipA);
                    EditActions::ClipState sB; sB.capture(clipB);
                    preDragStates.push_back(sA); preDragStates.push_back(sB);
                    repaint();
                    return;
                }
            }
        }
    }

    // ── Phase 2: 通常のクリップ選択 ──
    // 既に複数選択中のクリップをドラッグした場合は selection を維持
    bool clickedClipInSelection = ref.valid() && isClipInSelection(ref.clip);
    if (!clickedClipInSelection)
    {
        // 別のクリップ（または空白）をクリック → selection をリセット
        extraSelections.clear();
        selectedClip = ref;
    }

    // クリップのクリックで選択範囲（loop range）もクリア。
    // clickedClipInSelection (= 範囲が乗っている選択中クリップの別位置を再クリック) の場合も
    // 解除する。これをしないと「同じ波形の他の場所をクリックしても範囲が消えない」不具合になる。
    // 上半分の Selection ドラッグなら直後の mouseDrag で新しい範囲が即座に再設定される。
    if (ref.valid() && (loopEndTV > loopStartTV + 0.001))
    {
        if (onSetSelectionRange) onSetSelectionRange(0.0, 0.0);
        selectionFocusTrackIdx = -1;
        selectionFocusLaneIdx  = -1;
    }
    notifySelectionChanged();  // 選択クリップ確定 → ヘッダの採用ボタン活性を更新
    if (!ref.valid()) { repaint(); return; }

    crossfadeNeighbor = nullptr;
    draggedGainPointIdx = -1;
    dragMode          = getDragMode(ref, e.x, e.y);

    // 範囲選択ドラッグはクリックしたクリップの (track, lane) をフォーカス対象に
    if (dragMode == DragMode::Selection)
    {
        selectionFocusTrackIdx = ref.trackIdx;
        selectionFocusLaneIdx  = ref.laneIdx;
        // 再生バー追従: ドラッグ開始位置にシーク
        if (appSettings.playheadFollowsSelection && onSeek)
            onSeek(snapTime(dragStartSecs));
    }
    // クリップを単純にクリック（Move/Selection以外の通常選択）した場合も、
    // 設定が ON なら再生バーをクリップ先頭に移動
    else if (appSettings.playheadFollowsSelection && onSeek
             && ref.valid() && dragMode == DragMode::Move)
    {
        onSeek(ref.clip->getStartPosition());
    }
    clipOrigStart     = ref.clip->getStartPosition();
    clipOrigEnd       = ref.clip->getEndPosition();
    origFadeIn        = ref.clip->getFadeInSecs();
    origFadeOut       = ref.clip->getFadeOutSecs();
    origFileOffset    = ref.clip->getFileOffset();
    origClipGain      = ref.clip->getGain();

    // Move / Resize ドラッグ開始時、対象クリップを lane の末尾へ移動して描画順を上に。
    // (ドラッグ中も他クリップの上に表示される)
    if ((dragMode == DragMode::Move
         || dragMode == DragMode::ResizeLeft
         || dragMode == DragMode::ResizeRight)
        && ref.lane && ref.clip)
    {
        auto moveClipToBack = [](Lane* lane, AudioClip* clip)
        {
            if (lane == nullptr || clip == nullptr) return;
            auto& clips = lane->clips;
            for (auto it = clips.begin(); it != clips.end(); ++it)
            {
                if (it->get() == clip)
                {
                    auto ptr = std::move(*it);
                    clips.erase(it);
                    clips.push_back(std::move(ptr));
                    return;
                }
            }
        };
        moveClipToBack(ref.lane, ref.clip);
        for (auto& r : extraSelections) moveClipToBack(r.lane, r.clip);
    }

    // GainPoint モード: 既存ポイントクリック or ライン上の新規追加
    if (dragMode == DragMode::GainPoint)
    {
        const double bps_ = bpm / 60.0;
        auto& pts = ref.clip->getGainPointsRW();

        // クリップ座標
        auto area2     = getContentArea();
        int trackTop2  = area2.getY() + trackManager.getTrackY(ref.trackIdx) - scrollY;
        int lTop2      = trackTop2 + (ref.laneIdx == 0 ? 0
                          : ref.track->getMainHeight() + (ref.laneIdx - 1) * ref.track->getLaneHeight());
        int lH2        = ref.laneIdx == 0
                          ? juce::jmin(ref.track->getMainHeight(), ref.track->getTotalHeight())
                          : ref.track->getLaneHeight();
        int clipTopY2  = lTop2 + 1;
        int clipBotY2  = lTop2 + lH2 - 2;
        int clipMidY2  = (clipTopY2 + clipBotY2) / 2;
        int halfH2     = (clipBotY2 - clipTopY2) / 2;
        int cx2        = (int)(ref.clip->getStartPosition() * bps_ * pixelsPerBeat - scrollX);

        // 既存ポイントヒット
        auto dbToNormY = [](float dB) -> float {
            if (dB >= 0.0f) return juce::jlimit(0.0f, 1.0f, dB / 12.0f);
            return juce::jlimit(-1.0f, 0.0f, dB / 60.0f);
        };
        int hitIndex = -1;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            int px = cx2 + (int)(pts[i].time * bps_ * pixelsPerBeat);
            int py = clipMidY2 - (int)(dbToNormY(pts[i].dB) * halfH2);
            if (std::abs(e.x - px) <= 6 && std::abs(e.y - py) <= 6) { hitIndex = (int)i; break; }
        }

        // Option+クリック = 削除
        if (hitIndex >= 0 && e.mods.isAltDown())
        {
            // Undo 用に状態保存
            preDragStates.clear();
            EditActions::ClipState s; s.capture(ref.clip);
            preDragStates.push_back(s);
            pts.erase(pts.begin() + hitIndex);
            // すぐに Undo アクション作成
            if (undoManager)
            {
                std::vector<EditActions::ClipState> newStates;
                EditActions::ClipState ns; ns.capture(ref.clip);
                newStates.push_back(ns);
                undoManager->beginNewTransaction();
                undoManager->perform(new EditActions::ClipsPropertyAction(
                    std::move(preDragStates), std::move(newStates), editChangeCb));
            }
            preDragStates.clear();
            dragMode = DragMode::None;
            repaint();
            return;
        }

        if (hitIndex >= 0)
        {
            // 既存ポイントをドラッグ
            draggedGainPointIdx = hitIndex;
        }
        else
        {
            // ライン上クリック → 新規ポイント追加（既存エンベロープのdBを維持）
            double timeInClip = juce::jlimit(0.0, ref.clip->getDuration(),
                                              dragStartSecs - ref.clip->getStartPosition());
            // 現在のラインの位置にあわせて dB を決定（クリックYは無視）
            float dB = pts.empty()
                       ? juce::Decibels::gainToDecibels(ref.clip->getGain(), -60.0f)
                       : ref.clip->getEnvelopeDBAt(timeInClip);
            dB = juce::jlimit(-12.0f, 12.0f, dB);
            size_t insertAt = 0;
            while (insertAt < pts.size() && pts[insertAt].time < timeInClip) ++insertAt;
            pts.insert(pts.begin() + insertAt, GainPoint { timeInClip, dB });
            // ドラッグせず、追加だけ。再クリックで掴んで調整する流れ
            draggedGainPointIdx = -1;
            dragMode            = DragMode::None;
            // GAIN 編集中のポイント追加はここで完結させる。後段の分割 (Alt+クリック) へ
            // 抜けると「ポイント追加と同時にクリップ分割」が起きてしまうため return する。
            repaint();
            return;
        }
    }

    // Undo 用に編集対象クリップの状態を記録（Selection は記録不要）
    preDragStates.clear();
    extraOrigStarts.clear();
    if (dragMode != DragMode::None && dragMode != DragMode::CrossfadeRight
        && dragMode != DragMode::Selection)
    {
        EditActions::ClipState s;
        s.capture(ref.clip);
        preDragStates.push_back(s);

        // Move モードで複数選択がある場合、追加クリップの状態も記録
        if (dragMode == DragMode::Move)
        {
            for (auto& er : extraSelections)
            {
                EditActions::ClipState es; es.capture(er.clip);
                preDragStates.push_back(es);
                extraOrigStarts.push_back(er.clip->getStartPosition());
            }
        }
    }

    // Alt+Click (Mac は Option) = クリップ分割
    if (e.mods.isAltDown() && ref.valid())
    {
        double splitSecs = dragStartSecs;
        double cs = ref.clip->getStartPosition();
        double ce = ref.clip->getEndPosition();

        if (splitSecs > cs + 0.01 && splitSecs < ce - 0.01)
        {
            auto& fmt   = ref.track->getFormatManager();
            auto& cache = ref.track->getThumbnailCache();

            if (undoManager)
            {
                undoManager->beginNewTransaction();
                // editBeforeChangeCb (= audioEngine.clearPlayback) を渡す。これを渡さないと
                // 再生中に Alt+Click 分割を Undo した際、left/right 破棄と invalidatePlayback の
                // 間にオーディオスレッドが破棄済み AudioClip を読む UAF の窓が開く。
                undoManager->perform(new EditActions::ClipSplitAction(
                    ref.lane, ref.clip, splitSecs, fmt, cache, editChangeCb, editBeforeChangeCb));
            }
            else
            {
                auto* left = ref.lane->addClip(ref.clip->getFile(), cs,
                                                splitSecs - cs, fmt, cache);
                if (left) left->setFileOffset(ref.clip->getFileOffset());
                auto* right = ref.lane->addClip(ref.clip->getFile(), splitSecs,
                                                 ce - splitSecs, fmt, cache);
                if (right) right->setFileOffset(ref.clip->getFileOffset() + (splitSecs - cs));
                auto& clips = ref.lane->clips;
                clips.erase(std::remove_if(clips.begin(), clips.end(),
                    [&](const auto& c){ return c.get() == ref.clip; }), clips.end());
            }

            selectedClip.clear();
        }
        // Alt はカット専用モード。分割できない位置 (クリップ端付近など) でも、getDragMode が
        // 返した Resize/Move が dragMode に残っていると後続の mouseDrag が誤ってリサイズ/移動して
        // しまうため、分割の成否に関わらず必ず dragMode を無効化してから抜ける。
        preDragStates.clear();
        dragMode = DragMode::None;
        repaint();
        return;
    }

    repaint();
}

void TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    // ── MIDI クリップ 左右リサイズ (GRID 単位) ──
    if (resizingMidiClip != nullptr)
    {
        const double unit = gridUnitSecs();
        const double t    = juce::jmax(0.0, xToPosition(e.x));
        if (midiResizeLeft)
        {
            // 左端: GRID にスナップ。最低 1 グリッド単位は残す。
            double newStart = snapTime(t);
            newStart = juce::jlimit(0.0, midiResizeOrigEnd - unit, newStart);
            resizingMidiClip->setStartPosition(newStart);
            resizingMidiClip->setDuration(midiResizeOrigEnd - newStart);
        }
        else
        {
            // 右端: GRID にスナップ。最低 1 グリッド単位。
            double newEnd = snapTime(t);
            newEnd = juce::jmax(midiResizeOrigStart + unit, newEnd);
            resizingMidiClip->setDuration(newEnd - midiResizeOrigStart);
        }
        // ドラッグ中は再描画のみ (rebuild は mouseUp で 1 回)
        repaint();
        return;
    }

    // ── MIDI クリップ 新規作成ドラッグ (Option) ──
    if (creatingMidiClip != nullptr)
    {
        const double unit = gridUnitSecs();
        const double t    = juce::jmax(0.0, xToPosition(e.x));
        double end = snapTime(t);  // GRID 設定に従ってスナップ
        end = juce::jmax(midiCreateAnchor + unit, end);  // 最低 1 グリッド単位
        creatingMidiClip->setDuration(end - midiCreateAnchor);
        // ドラッグ中は再描画のみ (rebuild は mouseUp で 1 回)
        repaint();
        return;
    }

    // ── MIDI クリップ タイトル ドラッグ移動 ──
    if (draggingMidiClip != nullptr)
    {
        const double bps = bpm / 60.0;
        const double dt  = (double)(e.x - midiDragStartX) / juce::jmax(1e-9, pixelsPerBeat * bps);
        double ns = juce::jmax(0.0, midiDragOrigStart + dt);
        ns = snapTime(ns);  // GRID 設定に従ってスナップ (Off なら自由移動)
        draggingMidiClip->setStartPosition(ns);
        // ドラッグ中は再描画のみ (再生キャッシュ rebuild は mouseUp で 1 回 → カクつき防止)
        repaint();
        return;
    }

    // ラバーバンド範囲選択
    if (rubberBandActive)
    {
        rubberBandEnd = { e.x, e.y };
        // 範囲内のクリップを選択
        clearAllSelections();
        const double bps_ = bpm / 60.0;
        int x1 = juce::jmin(rubberBandStart.x, rubberBandEnd.x);
        int x2 = juce::jmax(rubberBandStart.x, rubberBandEnd.x);
        int y1 = juce::jmin(rubberBandStart.y, rubberBandEnd.y);
        int y2 = juce::jmax(rubberBandStart.y, rubberBandEnd.y);

        bool primarySet = false;
        auto area2 = getContentArea();
        for (int ti = 0; ti < trackManager.getTrackCount(); ++ti)
        {
            auto* track = trackManager.getTrack(ti);
            int trackTop2 = area2.getY() + trackManager.getTrackY(ti) - scrollY;
            int trackH    = track->getTotalHeight();
            if (trackTop2 + trackH < y1 || trackTop2 > y2) continue;

            int laneCount = track->getLaneCount();
            int visible   = track->isLanesCollapsed() ? 1 : laneCount;
            for (int li = 0; li < visible; ++li)
            {
                int lTop2 = trackTop2 + (li == 0 ? 0
                            : track->getMainHeight() + (li - 1) * track->getLaneHeight());
                int lH2 = track->isLanesCollapsed() ? trackH
                         : (li == 0 ? juce::jmin(track->getMainHeight(), trackH)
                                    : track->getLaneHeight());
                if (lTop2 + lH2 < y1 || lTop2 > y2) continue;
                auto* lane = track->getLane(li);
                if (!lane) continue;
                for (auto& cPtr : lane->clips)
                {
                    auto* c = cPtr.get();
                    int cx_ = (int)(c->getStartPosition() * bps_ * pixelsPerBeat - scrollX);
                    int cw_ = juce::jmax(4, (int)(c->getDuration() * bps_ * pixelsPerBeat));
                    if (cx_ + cw_ < x1 || cx_ > x2) continue;
                    ClipRef ref;
                    ref.track = track; ref.lane = lane; ref.clip = c;
                    ref.trackIdx = ti; ref.laneIdx = li;
                    if (!primarySet) { selectedClip = ref; primarySet = true; }
                    else extraSelections.push_back(ref);
                }
            }
        }
        repaint();
        notifySelectionChanged();  // ラバーバンド選択確定 → 採用ボタン活性を更新
        return;
    }

    if (dragMode == DragMode::None) return;
    // Selection ドラッグはクリップ未選択でも空エリアから行えるよう除外
    if (dragMode != DragMode::Selection
        && !selectedClip.valid() && !selectedCrossfade.valid() && !crossfadeNeighbor)
        return;

    const double bps    = bpm / 60.0;
    const double curSec = (e.x + scrollX) / (bps * pixelsPerBeat);
    const double delta  = curSec - dragStartSecs;

    switch (dragMode)
    {
        case DragMode::Move:
        {
            // プライマリの新しい位置（snap適用）
            double newStart = juce::jmax(0.0, clipOrigStart + delta);
            newStart = snapTime(newStart);
            double effectiveDelta = newStart - clipOrigStart;
            selectedClip.clip->setStartPosition(newStart);
            for (size_t i = 0; i < extraSelections.size() && i < extraOrigStarts.size(); ++i)
            {
                double newPos = juce::jmax(0.0, extraOrigStarts[i] + effectiveDelta);
                extraSelections[i].clip->setStartPosition(newPos);
            }

            // 縦方向のホバー先トラック/レーンを判定
            auto area2 = getContentArea();
            dragHoverTrackIdx = -1;
            dragHoverLaneIdx  = 0;
            for (int ti = 0; ti < trackManager.getTrackCount(); ++ti)
            {
                auto* track = trackManager.getTrack(ti);
                int trackTop2 = area2.getY() + trackManager.getTrackY(ti) - scrollY;
                int trackH    = track->getTotalHeight();
                if (e.y >= trackTop2 && e.y < trackTop2 + trackH)
                {
                    dragHoverTrackIdx = ti;
                    int laneCount = track->getLaneCount();
                    bool collapsed = track->isLanesCollapsed();
                    int visible = collapsed ? 1 : laneCount;
                    for (int li = 0; li < visible; ++li)
                    {
                        int lTop2 = trackTop2 + (li == 0 ? 0
                                    : track->getMainHeight() + (li - 1) * track->getLaneHeight());
                        int lH2 = collapsed ? trackH
                                  : (li == 0 ? juce::jmin(track->getMainHeight(), trackH)
                                             : track->getLaneHeight());
                        if (e.y >= lTop2 && e.y < lTop2 + lH2)
                        { dragHoverLaneIdx = li; break; }
                    }
                    break;
                }
            }
            break;
        }
        case DragMode::FadeIn:
        {
            double fadeLen = juce::jmax(0.0, curSec - selectedClip.clip->getStartPosition());
            selectedClip.clip->setFadeInSecs(fadeLen);
            break;
        }
        case DragMode::FadeOut:
        {
            double fadeLen = juce::jmax(0.0, selectedClip.clip->getEndPosition() - curSec);
            selectedClip.clip->setFadeOutSecs(fadeLen);
            break;
        }
        case DragMode::ResizeLeft:
        {
            // 左端ドラッグ：開始位置を動かし右端は固定
            // fileOffset が 0 未満にならないよう、引き伸ばし上限を制限
            double minStart = clipOrigStart - origFileOffset;
            double newStart = juce::jlimit(minStart, clipOrigEnd - 0.05, clipOrigStart + delta);
            double newFO    = origFileOffset + (newStart - clipOrigStart);
            double newDur   = clipOrigEnd - newStart;
            selectedClip.clip->setStartPosition(newStart);
            selectedClip.clip->setFileOffset(juce::jmax(0.0, newFO));
            selectedClip.clip->setDuration(newDur);
            break;
        }
        case DragMode::ResizeRight:
        {
            double fileLen = selectedClip.clip->getThumbnail().getTotalLength();
            double maxDur  = (fileLen > 0.0)
                             ? fileLen - selectedClip.clip->getFileOffset()
                             : clipOrigEnd - clipOrigStart;
            double newDur  = juce::jlimit(0.05, maxDur, clipOrigEnd + delta - clipOrigStart);
            selectedClip.clip->setDuration(newDur);
            break;
        }
        case DragMode::CrossfadeRight:
        {
            if (crossfadeNeighbor)
            {
                AudioClip* cb = selectedCrossfade.valid() ? selectedCrossfade.clipB
                                : (selectedClip.valid() ? selectedClip.clip : nullptr);
                if (!cb) break;
                double newEnd = juce::jlimit(
                    crossfadeNeighbor->getStartPosition() + 0.05,
                    cb->getEndPosition() - 0.01,
                    clipOrigEnd + delta);
                crossfadeNeighbor->setDuration(newEnd - crossfadeNeighbor->getStartPosition());
                const double newOverlap = newEnd - cb->getStartPosition();
                if (newOverlap > 0.001)
                {
                    // 両クリップ長の半分でクランプしてから同値を入れる (#M1)。個別 setter の
                    // duration*0.5 クランプで左右非対称になり、描画 (対称な X) と実音 (レベル
                    // ディップ) がずれるのを防ぐ。
                    const double xf = juce::jmin(newOverlap,
                                                 crossfadeNeighbor->getDuration() * 0.5,
                                                 cb->getDuration() * 0.5);
                    crossfadeNeighbor->setFadeOutSecs(xf);
                    cb->setFadeInSecs(xf);
                }
                else
                {
                    // overlap が無くなった → クロスフェードを完全に解除
                    crossfadeNeighbor->setFadeOutSecs(0.0);
                    cb->setFadeInSecs(0.0);
                }
            }
            break;
        }
        case DragMode::CrossfadeLeft:
        {
            if (selectedCrossfade.valid())
            {
                auto* clipB = selectedCrossfade.clipB;
                if (clipB)
                {
                    const double minNewStart = juce::jmax(0.0, clipOrigStart - origFileOffset);
                    const double maxNewStart = clipBOrigEnd - 0.05;
                    const double newStart    = juce::jlimit(minNewStart, maxNewStart,
                                                            clipOrigStart + delta);
                    const double shift       = newStart - clipOrigStart;
                    clipB->setStartPosition(newStart);
                    clipB->setFileOffset(origFileOffset + shift);
                    clipB->setDuration(clipBOrigEnd - newStart);
                    const double newOverlap = clipOrigEnd - newStart;
                    if (newOverlap > 0.001)
                    {
                        // 両クリップ長の半分でクランプしてから同値を入れる (#M1、左右対称)
                        const double halfA = crossfadeNeighbor ? crossfadeNeighbor->getDuration() * 0.5
                                                               : newOverlap;
                        const double xf = juce::jmin(newOverlap, clipB->getDuration() * 0.5, halfA);
                        clipB->setFadeInSecs(xf);
                        if (crossfadeNeighbor)
                            crossfadeNeighbor->setFadeOutSecs(xf);
                    }
                    else
                    {
                        clipB->setFadeInSecs(0.0);
                        if (crossfadeNeighbor)
                            crossfadeNeighbor->setFadeOutSecs(0.0);
                    }
                }
            }
            break;
        }
        case DragMode::Gain:
        {
            // 縦ドラッグでクリップゲインを調整（4px = 1dB、上=大）
            double deltaY  = e.getDistanceFromDragStartY();
            float  origDB  = juce::Decibels::gainToDecibels(origClipGain, -60.0f);
            float  newDB   = juce::jlimit(-60.0f, 12.0f, origDB - (float)(deltaY * 0.25));
            float  newGain = juce::Decibels::decibelsToGain(newDB, -60.0f);
            selectedClip.clip->setGain(newGain);
            break;
        }
        case DragMode::Selection:
        {
            // クリップ上半分のドラッグで選択範囲（ループ範囲）を更新
            double t1 = dragStartSecs;
            double t2 = curSec;
            if (t1 > t2) std::swap(t1, t2);
            t1 = snapTime(t1);
            t2 = snapTime(t2);
            if (onSetSelectionRange) onSetSelectionRange(t1, t2);
            break;
        }
        case DragMode::GainPoint:
        {
            if (!selectedClip.valid() || draggedGainPointIdx < 0) break;
            auto& pts = selectedClip.clip->getGainPointsRW();
            if (draggedGainPointIdx >= (int)pts.size()) break;

            // 現在マウス位置 → クリップ内時刻 / dB
            double timeInClip = juce::jlimit(0.0, selectedClip.clip->getDuration(),
                                              curSec - selectedClip.clip->getStartPosition());
            // Y → dB
            auto area2     = getContentArea();
            int trackTop2  = area2.getY() + trackManager.getTrackY(selectedClip.trackIdx) - scrollY;
            int lTop2      = trackTop2 + (selectedClip.laneIdx == 0 ? 0
                              : selectedClip.track->getMainHeight()
                              + (selectedClip.laneIdx - 1) * selectedClip.track->getLaneHeight());
            int lH2        = selectedClip.laneIdx == 0
                              ? juce::jmin(selectedClip.track->getMainHeight(),
                                            selectedClip.track->getTotalHeight())
                              : selectedClip.track->getLaneHeight();
            int clipMidY2  = lTop2 + lH2 / 2;
            int halfH2     = (lH2 - 4) / 2;
            // 縦マッピング: 上=norm+1(+12dB)、中央=0(0dB)、下=norm-1(-60dB)
            float norm = juce::jlimit(-1.0f, 1.0f,
                                       -(float)(e.y - clipMidY2) / juce::jmax(1, halfH2));
            float dB = (norm >= 0.0f) ? (norm * 12.0f) : (norm * 60.0f);

            pts[draggedGainPointIdx].time = timeInClip;
            pts[draggedGainPointIdx].dB   = dB;

            // 時間順を維持: 隣接ポイントとの順序が崩れたら入れ替え
            while (draggedGainPointIdx > 0
                   && pts[draggedGainPointIdx].time < pts[draggedGainPointIdx - 1].time)
            {
                std::swap(pts[draggedGainPointIdx], pts[draggedGainPointIdx - 1]);
                --draggedGainPointIdx;
            }
            while (draggedGainPointIdx < (int)pts.size() - 1
                   && pts[draggedGainPointIdx].time > pts[draggedGainPointIdx + 1].time)
            {
                std::swap(pts[draggedGainPointIdx], pts[draggedGainPointIdx + 1]);
                ++draggedGainPointIdx;
            }
            break;
        }
        case DragMode::CrossfadeCenter:
        {
            if (!crossfadeNeighbor) break;

            AudioClip* clipB = selectedCrossfade.valid()
                               ? selectedCrossfade.clipB
                               : (selectedClip.valid() ? selectedClip.clip : nullptr);

            if (selectedCrossfade.valid() && clipB)
            {
                // ── 選択状態: オーバーラップ幅を保ったまま両クリップを同量シフト ──
                // clip B の右端は固定したまま start と fileOffset と duration を調整

                // clip A のファイル実尺から右移動上限
                double fileLen    = crossfadeNeighbor->getThumbnail().getTotalLength();
                double maxByFileA = (fileLen > 0.0)
                                    ? crossfadeNeighbor->getStartPosition()
                                      + fileLen - crossfadeNeighbor->getFileOffset()
                                    : 1e9;
                // clip B の右端を固定するため、newBStart < clipBOrigEnd - 0.05
                double maxByClipB = clipBOrigEnd - 0.05 - clipOrigStart;

                // 左移動下限: clip B の fileOffset / start が負にならない
                double minDelta = juce::jmax(-origFileOffset, -clipOrigStart);
                double maxDelta = juce::jmin(maxByFileA - clipOrigEnd, maxByClipB);

                double clampedDelta = juce::jlimit(minDelta, maxDelta, delta);

                // clip A の終端を移動
                crossfadeNeighbor->setDuration(
                    clipOrigEnd + clampedDelta - crossfadeNeighbor->getStartPosition());

                // clip B: start + fileOffset + duration を更新して右端を維持
                double newBStart    = clipOrigStart + clampedDelta;
                double newFO        = origFileOffset + clampedDelta;
                double newBDuration = clipBOrigEnd - newBStart;
                if (newBStart >= 0.0 && newFO >= 0.0 && newBDuration > 0.05)
                {
                    clipB->setStartPosition(newBStart);
                    clipB->setFileOffset(newFO);
                    clipB->setDuration(newBDuration);  // 右端固定
                }
            }
            else
            {
                // ── 非選択（ハンドルドラッグ）: clip A の終端のみ調整 ──
                double fileLen   = crossfadeNeighbor->getThumbnail().getTotalLength();
                double maxByFile = (fileLen > 0.0)
                                   ? crossfadeNeighbor->getStartPosition()
                                     + fileLen - crossfadeNeighbor->getFileOffset()
                                   : 1e9;
                double maxByClipB = clipB ? clipB->getEndPosition() - 0.01 : 1e9;
                double newAEnd = juce::jlimit(
                    crossfadeNeighbor->getStartPosition() + 0.05,
                    juce::jmin(maxByFile, maxByClipB),
                    clipOrigEnd + delta);
                crossfadeNeighbor->setDuration(newAEnd - crossfadeNeighbor->getStartPosition());
            }
            break;
        }
        default: break;
    }
    repaint();
}

void TimelineView::mouseUp(const juce::MouseEvent&)
{
    // MIDI クリップ リサイズ終了 (start/duration の変化を Undo 対応で記録)
    if (resizingMidiClip != nullptr)
    {
        auto* clip = resizingMidiClip;
        resizingMidiClip = nullptr;
        const double oldStart = midiResizeOrigStart;
        const double oldDur   = midiResizeOrigEnd - midiResizeOrigStart;
        const double newStart = clip->getStartPosition();
        const double newDur   = clip->getDuration();
        if (undoManager && (newStart != oldStart || newDur != oldDur))
        {
            undoManager->beginNewTransaction();
            undoManager->perform(new EditActions::MidiClipPropertyAction(
                clip, oldStart, oldDur, newStart, newDur,
                [this] { if (editChangeCb) editChangeCb(); repaint(); }));
        }
        else if (editChangeCb) editChangeCb();
        return;
    }

    // MIDI クリップ 新規作成終了 (ライブ作成したクリップを Undo 対応で作り直す)
    if (creatingMidiClip != nullptr)
    {
        auto* live  = creatingMidiClip;
        auto* track = selectedMidiTrack;   // 作成時に設定済み
        creatingMidiClip = nullptr;
        if (track != nullptr)
        {
            EditActions::MidiClipReplaceAction::NewMidiClip np;
            np.startPos = live->getStartPosition();
            np.duration = live->getDuration();
            np.name     = live->getName();
            np.colour   = live->getColour();
            np.channel  = live->getChannel();
            np.sequence = live->getSequence();
            selectedMidiClip = nullptr;        // live を指す選択を先に外す
            track->extractMidiClip(live);      // ライブ作成分を取り除き
            pushMidiReplaceAction(track, {}, { std::move(np) });  // Undo 対応で作り直す
        }
        else if (editChangeCb) editChangeCb();
        return;
    }

    // MIDI クリップ移動終了 (start の変化を Undo 対応で記録)
    if (draggingMidiClip != nullptr)
    {
        auto* clip = draggingMidiClip;
        draggingMidiClip = nullptr;
        const double oldStart = midiDragOrigStart;
        const double newStart = clip->getStartPosition();
        const double dur      = clip->getDuration();   // 移動では尺は不変
        if (undoManager && newStart != oldStart)
        {
            undoManager->beginNewTransaction();
            undoManager->perform(new EditActions::MidiClipPropertyAction(
                clip, oldStart, dur, newStart, dur,
                [this] { if (editChangeCb) editChangeCb(); repaint(); }));
        }
        else if (editChangeCb) editChangeCb();
        return;
    }

    // ラバーバンド終了
    if (rubberBandActive)
    {
        rubberBandActive = false;
        repaint();
        return;
    }

    // Move ドラッグ完了時: マウスのY位置から別トラック/レーンへ移動するか判定
    if (dragMode == DragMode::Move && selectedClip.valid())
    {
        auto area2 = getContentArea();
        int mouseY = getMouseXYRelative().getY();
        Track* destTrack = nullptr;
        Lane*  destLane  = nullptr;
        int    destLaneIdx = 0, destTrackIdx = -1;

        for (int ti = 0; ti < trackManager.getTrackCount(); ++ti)
        {
            auto* track = trackManager.getTrack(ti);
            int trackTop2 = area2.getY() + trackManager.getTrackY(ti) - scrollY;
            int trackH    = track->getTotalHeight();
            if (mouseY >= trackTop2 && mouseY < trackTop2 + trackH)
            {
                destTrack    = track;
                destTrackIdx = ti;
                int laneCount = track->getLaneCount();
                bool collapsed = track->isLanesCollapsed();
                int visible = collapsed ? 1 : laneCount;
                for (int li = 0; li < visible; ++li)
                {
                    int lTop2 = trackTop2 + (li == 0 ? 0
                                : track->getMainHeight() + (li - 1) * track->getLaneHeight());
                    int lH2 = collapsed ? trackH
                              : (li == 0 ? juce::jmin(track->getMainHeight(), trackH)
                                         : track->getLaneHeight());
                    if (mouseY >= lTop2 && mouseY < lTop2 + lH2)
                    {
                        destLane = track->getLane(li);
                        destLaneIdx = li;
                        break;
                    }
                }
                break;
            }
        }

        // 別レーン/トラックへ移動
        if (destTrack && destLane && destLane != selectedClip.lane)
        {
            // 元から削除して移動先に新規作成（Undo は元に戻す ClipDeleteAction + ClipAddAction の組み合わせ）
            if (undoManager) undoManager->beginNewTransaction("Move to Track");

            // 現在のクリップのパラメータを取得
            EditActions::ClipParams p;
            p.file       = selectedClip.clip->getFile();
            p.startPos   = selectedClip.clip->getStartPosition();
            p.duration   = selectedClip.clip->getDuration();
            p.fileOffset = selectedClip.clip->getFileOffset();
            p.fadeIn     = selectedClip.clip->getFadeInSecs();
            p.fadeOut    = selectedClip.clip->getFadeOutSecs();
            p.gain       = selectedClip.clip->getGain();
            p.name       = selectedClip.clip->getName();
            p.colour     = selectedClip.clip->getColour();

            if (undoManager)
            {
                undoManager->perform(new EditActions::ClipDeleteAction(
                    selectedClip.lane, selectedClip.clip, editChangeCb));
                undoManager->perform(new EditActions::ClipAddAction(
                    destLane, p,
                    destTrack->getFormatManager(),
                    destTrack->getThumbnailCache(),
                    editChangeCb));
            }
            preDragStates.clear();
            extraOrigStarts.clear();
            selectedClip.clear();
            extraSelections.clear();
            dragMode = DragMode::None;
            repaint();
            return;
        }
    }

    // ドラッグ完了時にクリップ状態が変わっていれば Undo アクションを記録。
    // また preDrag 時の位置情報を後段のカット判定で利用するため map に控える。
    bool didMove = false;
    std::map<AudioClip*, std::pair<double, double>> preDragBounds;
    if (!preDragStates.empty())
    {
        std::vector<EditActions::ClipState> newStates;
        bool changed = false;
        for (auto& oldS : preDragStates)
        {
            preDragBounds[oldS.clip] = { oldS.startPos, oldS.startPos + oldS.duration };
            EditActions::ClipState newS;
            newS.capture(oldS.clip);
            newStates.push_back(newS);
            if (oldS.differsFrom(newS)) changed = true;
        }
        if (changed)
        {
            didMove = true;
            if (undoManager)
            {
                undoManager->beginNewTransaction();
                undoManager->perform(new EditActions::ClipsPropertyAction(
                    std::move(preDragStates), std::move(newStates), editChangeCb));
            }
        }
    }
    preDragStates.clear();

    // Move / CrossfadeCenter 完了時: クロスフェードペアのフェード長を overlap に同期
    // (X 字交差を綺麗に保つため)
    const bool isMoveDrag = (dragMode == DragMode::Move
                              || dragMode == DragMode::CrossfadeCenter);
    if (didMove && isMoveDrag)
    {
        auto syncLaneCrossfades = [](Lane* lane)
        {
            if (!lane) return;
            for (size_t i = 0; i < lane->clips.size(); ++i)
            {
                for (size_t j = i + 1; j < lane->clips.size(); ++j)
                {
                    auto* a = lane->clips[i].get();
                    auto* b = lane->clips[j].get();
                    AudioClip* cA = (a->getStartPosition() <= b->getStartPosition()) ? a : b;
                    AudioClip* cB = (a->getStartPosition() <= b->getStartPosition()) ? b : a;
                    // cB が cA に内包される (cB.end <= cA.end) のは正規のクロスフェード境界では
                    // ない (cA のフェードアウトが cB の無い位置に来る)。同期しない。
                    if (cB->getEndPosition() <= cA->getEndPosition() + 0.001) continue;
                    const double rawOverlap = cA->getEndPosition() - cB->getStartPosition();
                    // どちらかにフェードがあれば、overlap に同期。ただし両クリップ長の半分で
                    // クランプしてから両側に同値を入れる (#M1)。個別 setter の duration*0.5
                    // クランプで左右非対称になり、描画 (対称な X) と実音がずれるのを防ぐ。
                    if (rawOverlap > 0.001
                        && (cA->getFadeOutSecs() > 0.0 || cB->getFadeInSecs() > 0.0))
                    {
                        const double xf = juce::jmin(rawOverlap,
                                                     cA->getDuration() * 0.5,
                                                     cB->getDuration() * 0.5);
                        cA->setFadeOutSecs(xf);
                        cB->setFadeInSecs(xf);
                    }
                }
            }
        };
        if (selectedClip.valid())
            syncLaneCrossfades(selectedClip.lane);
        else if (selectedCrossfade.valid())
        {
            // CrossfadeCenter の場合 selectedClip は空。track 全 lane を走査
            for (int ti = 0; ti < trackManager.getTrackCount(); ++ti)
            {
                auto* tr = trackManager.getTrack(ti);
                if (!tr) continue;
                for (int li = 0; li < tr->getLaneCount(); ++li)
                    syncLaneCrossfades(tr->getLane(li));
            }
        }
    }

    // リサイズで動かした側に該当するクロスフェードのみ解除
    //   ResizeLeft  → d の左側 (左隣クリップとの overlap) のフェードを解除
    //   ResizeRight → d の右側 (右隣クリップとの overlap) のフェードを解除
    const bool isResizeDrag = (dragMode == DragMode::ResizeLeft
                                || dragMode == DragMode::ResizeRight);
    if (didMove && isResizeDrag && selectedClip.valid() && selectedClip.lane)
    {
        auto* d = selectedClip.clip;
        const double dS = d->getStartPosition();
        const double dE = d->getEndPosition();
        for (auto& cPtr : selectedClip.lane->clips)
        {
            auto* c = cPtr.get();
            if (c == d) continue;
            const double cS = c->getStartPosition();
            const double cE = c->getEndPosition();
            const double overlap = juce::jmin(dE, cE) - juce::jmax(dS, cS);
            if (overlap <= -0.001) continue;  // 完全に離れていればスキップ

            if (cS < dS && dragMode == DragMode::ResizeLeft)
            {
                // 左隣のクロスフェード: d の左端をリサイズ → クリア
                c->setFadeOutSecs(0.0);
                d->setFadeInSecs(0.0);
            }
            else if (cS >= dS && dragMode == DragMode::ResizeRight)
            {
                // 右隣のクロスフェード: d の右端をリサイズ → クリア
                c->setFadeInSecs(0.0);
                d->setFadeOutSecs(0.0);
            }
        }
    }

    // 移動 / リサイズ完了時、波形が重なった場合の処理:
    //   ・対象クリップを lane の末尾へ移して描画順で上に
    //   ・重なった下側クリップは上側クリップとの重なり領域をカット
    const bool isReshapingDrag = (dragMode == DragMode::Move
                                  || dragMode == DragMode::ResizeLeft
                                  || dragMode == DragMode::ResizeRight);
    if (didMove && isReshapingDrag)
    {
        auto moveClipToBack = [](Lane* lane, AudioClip* clip)
        {
            if (lane == nullptr || clip == nullptr) return;
            auto& clips = lane->clips;
            for (auto it = clips.begin(); it != clips.end(); ++it)
            {
                if (it->get() == clip)
                {
                    auto ptr = std::move(*it);
                    clips.erase(it);
                    clips.push_back(std::move(ptr));
                    return;
                }
            }
        };
        // 重なった「下側クリップ」を上側との重なり分だけカット:
        //   ・C 全体が D に覆われる → C を削除
        //   ・C が D より左で右側が重なる → C の end を dS まで詰める
        //   ・C が D より右で左側が重なる → C の start を dE まで詰める (fileOffset 補正)
        // ただし D が ドラッグ前から C と重なっていた場合は、既存のクロスフェードを
        // 維持するためカットしない (= クロスフェード調整用の Move とみなす)
        auto cutOverlappedClips = [&preDragBounds](Lane* lane, AudioClip* dragged)
        {
            if (lane == nullptr || dragged == nullptr) return;
            const double dS = dragged->getStartPosition();
            const double dE = dragged->getEndPosition();

            // ドラッグ前の D の位置範囲
            double preDS = dS, preDE = dE;
            auto it = preDragBounds.find(dragged);
            if (it != preDragBounds.end()) { preDS = it->second.first; preDE = it->second.second; }

            auto& clips = lane->clips;
            for (auto it2 = clips.begin(); it2 != clips.end(); )
            {
                auto* c = it2->get();
                if (c == dragged) { ++it2; continue; }

                const double cS = c->getStartPosition();
                const double cE = c->getEndPosition();

                if (cE <= dS || cS >= dE) { ++it2; continue; }

                // 既にドラッグ前から C と重なっていた → カットしない (既存クロスフェード保持)
                const double preOverlap = juce::jmin(preDE, cE) - juce::jmax(preDS, cS);
                if (preOverlap > 0.001) { ++it2; continue; }

                if (cS >= dS && cE <= dE)
                {
                    it2 = clips.erase(it2);
                    continue;
                }

                if (cS < dS && cE > dS && cE <= dE)
                {
                    const double newDur = juce::jmax(0.01, dS - cS);
                    c->setDuration(newDur);
                    if (c->getFadeOutSecs() > newDur * 0.5)
                        c->setFadeOutSecs(newDur * 0.5);
                }
                else if (cS >= dS && cS < dE && cE > dE)
                {
                    const double shift = dE - cS;
                    c->setStartPosition(dE);
                    c->setFileOffset(c->getFileOffset() + shift);
                    const double newDur = juce::jmax(0.01, cE - dE);
                    c->setDuration(newDur);
                    if (c->getFadeInSecs() > newDur * 0.5)
                        c->setFadeInSecs(newDur * 0.5);
                }
                else if (cS < dS && cE > dE)
                {
                    const double newDur = juce::jmax(0.01, dS - cS);
                    c->setDuration(newDur);
                    if (c->getFadeOutSecs() > newDur * 0.5)
                        c->setFadeOutSecs(newDur * 0.5);
                }
                ++it2;
            }
        };
        if (selectedClip.valid())
        {
            cutOverlappedClips(selectedClip.lane, selectedClip.clip);
            moveClipToBack(selectedClip.lane, selectedClip.clip);
        }
        for (auto& r : extraSelections)
        {
            cutOverlappedClips(r.lane, r.clip);
            moveClipToBack(r.lane, r.clip);
        }
    }

    dragMode          = DragMode::None;
    showCutLine       = false;
    dragHoverTrackIdx = -1;
    dragHoverLaneIdx  = -1;
    repaint();
}
void TimelineView::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& w)
{
    if (e.mods.isShiftDown() && e.mods.isAltDown())
    {
        // Shift+Option+スクロール = 波形振幅の縦ズーム
        // deltaX/deltaY 両軸を見て、感度を高めに
        double delta = (std::abs(w.deltaY) > std::abs(w.deltaX)) ? w.deltaY : w.deltaX;
        // 倍率変化（指数的）にする方が体感的に分かりやすい
        waveformZoom = juce::jlimit(0.1, 6.0, waveformZoom * std::pow(1.5, delta));
        repaint();
        return;
    }
    else if (e.mods.isCommandDown())
    {
        // ズーム範囲: 1 px/beat（極端な縮小=全体ビュー）〜 200000 px/beat（サンプル単位）。
        // 高ズーム域では加速度的に拡大できるよう、現在値に比例した刻みを採用
        const double bps      = bpm / 60.0;
        const double contentW = (double)getContentArea().getWidth();

        // ズーム支点 (秒) の決定:
        //  - appSettings.zoomToMousePosition == true:  マウスカーソル位置の時刻
        //  - false (デフォルト): 再生バーの時刻
        double anchorTime;
        double anchorX;  // コンテンツ領域内 x (px)
        if (appSettings.zoomToMousePosition)
        {
            const auto contentArea = getContentArea();
            const int mouseX = juce::jlimit(0, juce::jmax(0, contentArea.getWidth() - 1),
                                              e.x - contentArea.getX());
            anchorTime = (mouseX + scrollX) / juce::jmax(1e-9, pixelsPerBeat * bps);
            anchorX    = (double) mouseX;
        }
        else
        {
            anchorTime = playheadSecs;
            anchorX    = contentW * 0.5;  // 画面中央
        }

        const double step = juce::jmax(40.0, pixelsPerBeat * 0.25);
        pixelsPerBeat = juce::jlimit(1.0, 200000.0, pixelsPerBeat + w.deltaY * step);

        // anchorTime が anchorX に来るよう scrollX を再計算
        scrollX = juce::jmax(0.0, anchorTime * bps * pixelsPerBeat - anchorX);

        // ルーラーのプレイヘッド絶対座標も新しい pixelsPerBeat で再計算
        ruler.setPlayheadX(playheadSecs * bps * pixelsPerBeat);
        ruler.setPixelsPerBeat(pixelsPerBeat);
        ruler.setScrollX(scrollX);
        hScrollBar.setCurrentRange(scrollX, hScrollBar.getCurrentRangeSize());
        resized();
    }
    else if (e.mods.isShiftDown() || std::abs(w.deltaX) > std::abs(w.deltaY))
    {
        // Shift+スクロール または トラックパッドで横スワイプ
        // macOSはShift押下時にdeltaYがdeltaXに切り替わることがあるため両軸を見る
        double delta = (std::abs(w.deltaX) > std::abs(w.deltaY)) ? w.deltaX : w.deltaY;
        scrollX = juce::jmax(0.0, scrollX - delta * 200.0);
        ruler.setScrollX(scrollX);
        hScrollBar.setCurrentRange(scrollX, hScrollBar.getCurrentRangeSize());
    }
    else
    {
        scrollY = juce::jmax(0, (int)(scrollY - w.deltaY * 60.0));
        vScrollBar.setCurrentRange(scrollY, vScrollBar.getCurrentRangeSize());
        if (onVerticalScroll) onVerticalScroll(scrollY);
    }
    repaint();
}
