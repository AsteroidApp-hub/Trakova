// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Trakova — TrackManager のユニットテスト (色サイクル / 自動命名 / 複製 / フォルダ署名)
//
//   ・色サイクル: nextColourIndex が mod 8 で巡回し、9 本目が 1 本目と同色 (負数対応は paletteColour)
//   ・自動命名: 空名は既存 "Track N" の最大 + 1。明示名は尊重
//   ・hasClickTrack / addClickTrack (1 本のみ) / hasMidiTrack
//   ・duplicateTrack: 直後に挿入・基本プロパティ/クリップ/レーン solo/MIDI を深くコピー・
//                     recArm/solo は引き継がない・Click/範囲外は nullptr・名前は一意化
//   ・audioFolderSignature: 拡張子フィルタ・内容変化で署名変化・非ディレクトリは空・決定論的
// クリップはダミー File (デコードしない)。複製名は tr() を介すのでロケール非依存に検証する。
// AudioFormatManager は runTest ローカル (静的だと終了時 leak assertion)。expect は ASCII。

#include <JuceHeader.h>
#include "../Source/Tracks/TrackManager.h"
#include "../Source/Localisation.h"

namespace
{
class TrackManagerTests : public juce::UnitTest
{
public:
    TrackManagerTests() : juce::UnitTest("TrackManager") {}

    void runTest() override
    {
        testColourCycle();
        testAutoNaming();
        testClickAndMidiQueries();
        testDuplicateBasic();
        testDuplicateMidiDeepCopy();
        testDuplicateGuardsAndUniqueName();
        testAudioFolderSignature();
    }

    // ── 色サイクル: track i は paletteColour(i)、9 本目 (idx 8) は 1 本目と同色 ──
    void testColourCycle()
    {
        beginTest("addTrack assigns palette colours cyclically (mod 8)");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        for (int i = 0; i < 9; ++i) tm.addTrack();
        for (int i = 0; i < 9; ++i)
            expect(tm.getTrack(i)->getColour() == Track::paletteColour(i),
                   ("track " + juce::String(i) + " uses paletteColour(i)").toRawUTF8());
        expect(tm.getTrack(8)->getColour() == tm.getTrack(0)->getColour(),
               "9th track wraps to the 1st palette colour");
        // paletteColour は負数も巡回する
        expect(Track::paletteColour(-1) == Track::paletteColour(7), "paletteColour(-1) == (7)");
        expect(Track::paletteColour(-8) == Track::paletteColour(0), "paletteColour(-8) == (0)");
    }

    // ── 自動命名: 空名は max("Track N") + 1、明示名は尊重 ──
    void testAutoNaming()
    {
        beginTest("addTrack auto-numbers empty names as Track N (max + 1)");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        expect(tm.addTrack()->getName() == "Track 1", "first empty -> Track 1");
        expect(tm.addTrack()->getName() == "Track 2", "second empty -> Track 2");
        expect(tm.addTrack("Track 5")->getName() == "Track 5", "explicit name is respected");
        expect(tm.addTrack()->getName() == "Track 6", "next empty -> max(5) + 1 = Track 6");
        expect(tm.addTrack("Vocals")->getName() == "Vocals", "non-Track name is respected");
    }

    // ── hasClickTrack / addClickTrack (1 本のみ) / hasMidiTrack ──
    void testClickAndMidiQueries()
    {
        beginTest("click track is unique; hasMidiTrack tracks MIDI tracks");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        expect(!tm.hasClickTrack(), "no click track initially");
        expect(!tm.hasMidiTrack(),  "no MIDI track initially");

        auto* click = tm.addClickTrack();
        expect(click != nullptr && click->isClickTrack(), "addClickTrack returns a click track");
        expect(tm.hasClickTrack(), "hasClickTrack true after adding");
        expect(tm.addClickTrack() == nullptr, "second addClickTrack returns nullptr (unique)");

        auto* midi = tm.addTrack("Synth");
        expect(!tm.hasMidiTrack(), "audio track does not count as MIDI");
        midi->setMidiTrack(true);
        expect(tm.hasMidiTrack(), "hasMidiTrack true once a track is MIDI");
    }

    // ── duplicateTrack: 直後挿入・基本プロパティ/クリップ/レーン solo をコピー・recArm/solo 非継承 ──
    void testDuplicateBasic()
    {
        beginTest("duplicateTrack copies properties/clips, inserts after, drops recArm/solo");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* src = tm.addTrack("Vocals");
        src->setVolume(-6.0f); src->setPan(0.3f); src->setReverbSend(0.2f);
        src->setMuted(true); src->setRecArmed(true); src->setSoloed(true);
        src->setInputMonitor(true); src->setInputChannel(1); src->setStereo(true);
        src->setCustomHeight(140);

        juce::File dummy("/tmp/trakova_dummy_clip.wav");
        auto* clip = src->addClip(dummy, 1.0, 2.0);
        expect(clip != nullptr, "source clip created");
        clip->setFileOffset(0.5); clip->setGain(0.7f);
        clip->getGainPointsRW().push_back({ 0.5, -3.0f });
        src->getLane(0)->soloed = true;

        auto* dst = tm.duplicateTrack(0);
        expect(dst != nullptr, "duplicate returns a track");
        expect(tm.getTrackCount() == 2 && tm.getTrack(1) == dst, "inserted right after source");
        expect(dst->getName() == juce::String("Vocals") + tr(u8" (コピー)"),
               "name is source + copy suffix");

        expect(juce::approximatelyEqual(dst->getVolume(), -6.0f), "volume copied");
        expect(juce::approximatelyEqual(dst->getPan(), 0.3f),     "pan copied");
        expect(juce::approximatelyEqual(dst->getReverbSend(), 0.2f), "reverbSend copied");
        expect(dst->isMuted(), "muted copied");
        expect(dst->isInputMonitor(), "inputMonitor copied");
        expect(dst->getInputChannel() == 1, "inputChannel copied");
        expect(dst->isStereo(), "stereo copied");
        expect(dst->getCustomHeight() == 140, "customHeight copied");
        // 録音アーム・ソロは混乱回避のため引き継がない
        expect(!dst->isRecArmed(), "recArm is NOT inherited");
        expect(!dst->isSoloed(),   "track solo is NOT inherited");

        // クリップの深いコピー
        auto* dl = dst->getLane(0);
        expect(dl != nullptr && (int) dl->clips.size() == 1, "one clip copied to lane 0");
        if (dl && dl->clips.size() == 1)
        {
            auto* dc = dl->clips[0].get();
            expect(dc->getFile() == dummy, "clip file copied");
            expect(juce::approximatelyEqual(dc->getStartPosition(), 1.0), "clip start copied");
            expect(juce::approximatelyEqual(dc->getDuration(), 2.0),      "clip duration copied");
            expect(juce::approximatelyEqual(dc->getFileOffset(), 0.5),    "clip fileOffset copied");
            expect(juce::approximatelyEqual(dc->getGain(), 0.7f),         "clip gain copied");
            expect((int) dc->getGainPoints().size() == 1, "gain points deep-copied (count)");
        }
        expect(dst->getLane(0)->soloed.load(), "lane soloed copied");
    }

    // ── MIDI トラックの深いコピー: synth/移調・MIDI クリップ ch・シーケンス ──
    void testDuplicateMidiDeepCopy()
    {
        beginTest("duplicateTrack deep-copies MIDI track (synth/transpose/channel/sequence)");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* src = tm.addTrack("Harmony");
        src->setMidiTrack(true);
        src->setSynthWaveform(2); src->setOctaveShift(1); src->setSemitoneTranspose(3);
        auto* mc = src->addMidiClip(0.0, 2.0);
        mc->setChannel(9);  // ch10 ドラム
        mc->getSequence().addEvent(juce::MidiMessage::noteOn(10, 60, (juce::uint8) 100), 0.0);
        mc->getSequence().addEvent(juce::MidiMessage::noteOff(10, 60), 0.5);
        mc->getSequence().updateMatchedPairs();
        const int srcEvents = mc->getSequence().getNumEvents();

        auto* dst = tm.duplicateTrack(0);
        expect(dst != nullptr && dst->isMidiTrack(), "duplicate is a MIDI track");
        expect(dst->getSynthWaveform() == 2,      "synthWaveform copied");
        expect(dst->getOctaveShift() == 1,        "octaveShift copied");
        expect(dst->getSemitoneTranspose() == 3,  "semitoneTranspose copied");
        expect(dst->getMidiClipCount() == 1, "one MIDI clip copied");
        if (dst->getMidiClipCount() == 1)
        {
            auto* dmc = dst->getMidiClip(0);
            expect(dmc->getChannel() == 9, "MIDI channel (ch10 drum) preserved");
            expect(dmc->getSequence().getNumEvents() == srcEvents, "sequence events copied");
        }
    }

    // ── 範囲外 / Click は nullptr、連続複製で名前が一意化される ──
    void testDuplicateGuardsAndUniqueName()
    {
        beginTest("duplicateTrack guards (click/out-of-range) and unique naming");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        expect(tm.duplicateTrack(-1) == nullptr, "negative index -> nullptr");
        expect(tm.duplicateTrack(99) == nullptr, "out-of-range index -> nullptr");

        auto* click = tm.addClickTrack();
        juce::ignoreUnused(click);
        expect(tm.duplicateTrack(0) == nullptr, "click track cannot be duplicated");

        auto* v = tm.addTrack("Vocals");          // index 1
        juce::ignoreUnused(v);
        auto* d1 = tm.duplicateTrack(1);
        auto* d2 = tm.duplicateTrack(1);
        expect(d1 != nullptr && d2 != nullptr, "two duplicates created");
        expect(d1->getName() == juce::String("Vocals") + tr(u8" (コピー)"), "first copy name");
        expect(d2->getName() != d1->getName(), "second copy gets a distinct (numbered) name");
        expect(d2->getName().startsWith(d1->getName()), "second copy name extends the first");
    }

    // ── audioFolderSignature: 拡張子フィルタ・内容変化で変化・非ディレクトリは空・決定論的 ──
    void testAudioFolderSignature()
    {
        beginTest("audioFolderSignature: extension filter / content-change / non-dir / deterministic");
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("TrakovaTMSigTest");
        dir.deleteRecursively(); dir.createDirectory();

        expect(TrackManager::audioFolderSignature(dir.getChildFile("nope")) == juce::String(),
               "non-existent / non-directory -> empty signature");

        auto a = dir.getChildFile("a.wav");
        auto b = dir.getChildFile("b.wav");
        a.replaceWithText("AAAA");
        b.replaceWithText("BBBBBB");
        const auto sig1 = TrackManager::audioFolderSignature(dir);
        expect(sig1.isNotEmpty(), "signature of a folder with audio is non-empty");
        expect(TrackManager::audioFolderSignature(dir) == sig1, "signature is deterministic");

        // 非オーディオ拡張子は無視される (署名は変わらない)
        dir.getChildFile("notes.txt").replaceWithText("ignore me");
        expect(TrackManager::audioFolderSignature(dir) == sig1,
               "non-audio file does not affect the signature");

        // 内容 (サイズ) が変われば署名が変わる
        a.replaceWithText("AAAAAAAAAAAAAAAA");
        expect(TrackManager::audioFolderSignature(dir) != sig1,
               "changing an audio file's content changes the signature");

        // .wav 以外のオーディオ拡張子 (.aiff/.mp3/.aif) も算入される (.txt の除外と対照)
        const auto sigBeforeExt = TrackManager::audioFolderSignature(dir);
        dir.getChildFile("c.aiff").replaceWithText("CCCC");
        const auto sigAiff = TrackManager::audioFolderSignature(dir);
        expect(sigAiff != sigBeforeExt, ".aiff file IS counted in the signature");
        dir.getChildFile("d.mp3").replaceWithText("DDDD");
        const auto sigMp3 = TrackManager::audioFolderSignature(dir);
        expect(sigMp3 != sigAiff, ".mp3 file IS counted in the signature");
        dir.getChildFile("e.aif").replaceWithText("EEEE");
        expect(TrackManager::audioFolderSignature(dir) != sigMp3,
               ".aif file IS counted in the signature");

        dir.deleteRecursively();
    }
};

static TrackManagerTests trackManagerTests;
}
