// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Utawave — 録音まわりのユニットテスト
//
// オーディオデバイス不要・決定論的に検証する:
//   ・パンチインのクロスフェード Track::trimAndCrossfadeOnLane0 の全ケース
//       内包→消去 / またぎ→左右分割 (right の fileOffset 再計算・両端 30ms フェード) /
//       左端のみ重複→末尾トリム / 右端のみ重複→開始押し出し+fileOffset 前進 / 非重複→不変 /
//       新クリップに 30ms フェード / 残り ≤0.01s は消去
//   ・finishLiveRecording のテイク退避 ((file, fileOffset, duration) 3 つ組 dedup) と lane0 配置
//   ・LiveRecordingBuffer (遅延確保・ピーク蓄積・reset・尺算術)
//
// trimAndCrossfadeOnLane0 は位置/フェード/fileOffset を操作するだけでデコードしないため
// ダミー File で可。finishLiveRecording は refreshThumbnail を呼ぶので実 WAV を使う。
// AudioFormatManager は各テストのローカル (静的だと終了時 leak assertion)。expect は ASCII。

#include <JuceHeader.h>
#include <cmath>
#include "../Source/Tracks/TrackManager.h"
#include "../Source/Tracks/Track.h"
#include "../Source/Tracks/AudioClip.h"
#include "../Source/Recording/LiveRecordingBuffer.h"

namespace
{
static bool approxEq(double a, double b, double eps) { return std::abs(a - b) < eps; }

class RecordingTests : public juce::UnitTest
{
public:
    RecordingTests() : juce::UnitTest("Recording (punch-in / takes)") {}

    static constexpr double kXfade = 0.030;   // trimAndCrossfadeOnLane0 と同じ

    void runTest() override
    {
        testPunchContained();
        testPunchStraddle();
        testPunchLeftOverlap();
        testPunchRightOverlap();
        testPunchNonOverlap();
        testPunchTinyRemainderErased();
        testFinishLiveRecordingBackup();
        testFinishLiveRecordingBackupPreservesOffset();
        testFinishLiveRecordingThumbnailsMatch();
        testFinishLiveRecordingDedup();
        testLiveRecordingBuffer();
    }

    // lane0 に直接クリップを足す (overlaps チェック無しでそのまま lane0 に入る)
    AudioClip* addLane0(Track* t, const juce::File& f, double start, double dur, double fo = 0.0)
    {
        auto* c = t->getLane(0)->addClip(f, start, dur, t->getFormatManager(), t->getThumbnailCache());
        if (c) c->setFileOffset(fo);
        return c;
    }

    AudioClip* clipAtStart(Lane* lane, double start, double tol = 0.02)
    {
        for (auto& c : lane->clips)
            if (std::abs(c->getStartPosition() - start) < tol) return c.get();
        return nullptr;
    }

    // ── パンチイン: 内包 (既存が新規の内側) → 消去 ──
    void testPunchContained()
    {
        beginTest("trimAndCrossfadeOnLane0: existing fully inside punch -> erased");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        const juce::File dummy("/tmp/utawave_punch.wav");

        addLane0(t, dummy, 1.0, 1.0);          // existing [1,2]
        auto* nc = addLane0(t, dummy, 0.0, 4.0);   // new [0,4] (contains existing)
        t->trimAndCrossfadeOnLane0(nc, 0.0, 4.0);

        expect((int) t->getLane(0)->clips.size() == 1, "contained existing clip erased (only new remains)");
        expect(t->getLane(0)->clips[0].get() == nc, "remaining clip is the new recording");
    }

    // ── パンチイン: またぎ (既存が新規をまたぐ) → 左右分割 ──
    void testPunchStraddle()
    {
        beginTest("trimAndCrossfadeOnLane0: existing straddles punch -> split into left + right");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        const juce::File dummy("/tmp/utawave_punch.wav");

        addLane0(t, dummy, 0.0, 4.0, /*fo=*/0.0);   // existing [0,4]
        auto* nc = addLane0(t, dummy, 1.0, 2.0);    // new [1,3] (punch start=1 dur=2)
        t->trimAndCrossfadeOnLane0(nc, 1.0, 2.0);

        // left [0, ~1.03] fadeOut 0.03 / new [1,3] / right [~2.97, 4] fileOffset ~2.97 fadeIn 0.03
        expect((int) t->getLane(0)->clips.size() == 3, "straddle -> 3 clips (left, new, right)");

        auto* left = clipAtStart(t->getLane(0), 0.0);
        expect(left != nullptr, "left piece exists at start 0");
        if (left)
        {
            expect(approxEq(left->getDuration(), 1.0 + kXfade, 1e-3), "left trimmed to punchStart + 30ms");
            expect(approxEq(left->getFadeOutSecs(), kXfade, 1e-4), "left has 30ms fade-out");
        }

        auto* right = clipAtStart(t->getLane(0), 3.0 - kXfade);
        expect(right != nullptr, "right piece exists at punchEnd - 30ms");
        if (right)
        {
            expect(approxEq(right->getDuration(), 4.0 - (3.0 - kXfade), 1e-3), "right spans to original end");
            expect(approxEq(right->getFileOffset(), 3.0 - kXfade, 1e-3),
                   "right fileOffset recalculated to keep file continuity");
            expect(approxEq(right->getFadeInSecs(), kXfade, 1e-4), "right has 30ms fade-in");
        }

        expect(approxEq(nc->getFadeInSecs(), kXfade, 1e-4),  "new clip has 30ms fade-in");
        expect(approxEq(nc->getFadeOutSecs(), kXfade, 1e-4), "new clip has 30ms fade-out");
    }

    // ── パンチイン: 左端のみ重複 (既存が左から食い込む) → 末尾トリム ──
    void testPunchLeftOverlap()
    {
        beginTest("trimAndCrossfadeOnLane0: existing overlaps from the left -> trimmed end + fade-out");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        const juce::File dummy("/tmp/utawave_punch.wav");

        addLane0(t, dummy, 0.0, 2.0);            // existing [0,2]
        auto* nc = addLane0(t, dummy, 1.0, 2.0); // new [1,3]
        t->trimAndCrossfadeOnLane0(nc, 1.0, 2.0);

        auto* left = clipAtStart(t->getLane(0), 0.0);
        expect(left != nullptr, "existing kept (left part)");
        if (left)
        {
            expect(approxEq(left->getDuration(), 1.0 + kXfade, 1e-3), "trimmed to punchStart + 30ms");
            expect(approxEq(left->getFadeOutSecs(), kXfade, 1e-4), "30ms fade-out at the join");
        }
        expect((int) t->getLane(0)->clips.size() == 2, "no extra split piece for left-only overlap");
    }

    // ── パンチイン: 右端のみ重複 (既存が右へ食い込む) → 開始押し出し + fileOffset 前進 ──
    void testPunchRightOverlap()
    {
        beginTest("trimAndCrossfadeOnLane0: existing overlaps from the right -> start pushed + fileOffset advanced");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        const juce::File dummy("/tmp/utawave_punch.wav");

        addLane0(t, dummy, 2.0, 2.0, /*fo=*/0.0);   // existing [2,4] fileOffset 0
        auto* nc = addLane0(t, dummy, 1.0, 2.0);    // new [1,3]
        t->trimAndCrossfadeOnLane0(nc, 1.0, 2.0);

        // existing pushed to max(2, endPos-30ms) = max(2, 2.97) = 2.97
        auto* right = clipAtStart(t->getLane(0), 3.0 - kXfade);
        expect(right != nullptr, "existing start pushed to punchEnd - 30ms");
        if (right)
        {
            expect(approxEq(right->getStartPosition(), 3.0 - kXfade, 1e-3), "start pushed right");
            expect(approxEq(right->getFileOffset(), (3.0 - kXfade) - 2.0, 1e-3),
                   "fileOffset advanced by the trimmed amount");
            expect(approxEq(right->getDuration(), 4.0 - (3.0 - kXfade), 1e-3), "duration shortened from the left");
            expect(approxEq(right->getFadeInSecs(), kXfade, 1e-4), "30ms fade-in at the join");
        }
        expect((int) t->getLane(0)->clips.size() == 2, "no extra split piece for right-only overlap");
    }

    // ── パンチイン: 非重複 → 不変 ──
    void testPunchNonOverlap()
    {
        beginTest("trimAndCrossfadeOnLane0: non-overlapping existing clip is untouched");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        const juce::File dummy("/tmp/utawave_punch.wav");

        auto* other = addLane0(t, dummy, 5.0, 1.0);   // existing [5,6] (far away)
        const double offFade = other->getFadeOutSecs();
        auto* nc = addLane0(t, dummy, 1.0, 2.0);      // new [1,3]
        t->trimAndCrossfadeOnLane0(nc, 1.0, 2.0);

        expect(approxEq(other->getStartPosition(), 5.0, 1e-9), "non-overlapping clip start unchanged");
        expect(approxEq(other->getDuration(), 1.0, 1e-9), "non-overlapping clip duration unchanged");
        expect(approxEq(other->getFadeOutSecs(), offFade, 1e-9), "non-overlapping clip fade unchanged");
    }

    // ── パンチイン: トリム後の残りが ≤0.01s → 消去 ──
    void testPunchTinyRemainderErased()
    {
        beginTest("trimAndCrossfadeOnLane0: tiny trimmed remainder (<= 0.01s) is erased");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        const juce::File dummy("/tmp/utawave_punch.wav");

        // existing [2.999, 3.005] : 右端だけ punch [1,3] を僅かに超える → 開始押し出しで残り ~0.006s
        addLane0(t, dummy, 2.999, 0.006);
        auto* nc = addLane0(t, dummy, 1.0, 2.0);   // new [1,3]
        t->trimAndCrossfadeOnLane0(nc, 1.0, 2.0);

        expect((int) t->getLane(0)->clips.size() == 1, "tiny remainder erased (only new remains)");
        expect(t->getLane(0)->clips[0].get() == nc, "remaining clip is the new recording");
    }

    // 前半 loud / 後半 quiet の識別しやすい WAV (波形の取り違えを検出するため)
    juce::File writeWavHalves(const juce::File& dir, const juce::String& name, double sr, double secs)
    {
        auto f = dir.getChildFile(name);
        f.deleteFile();
        std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
        if (os == nullptr) return {};
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 1, 16, {}, 0));
        if (w == nullptr) return {};
        os.release();
        const int n = (int)(sr * secs);
        juce::AudioBuffer<float> buf(1, n);
        for (int i = 0; i < n; ++i)
        {
            const float amp = (i < n / 2) ? 0.5f : 0.04f;   // 前半 loud / 後半 quiet
            buf.setSample(0, i, amp * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr));
        }
        w->writeFromAudioSampleBuffer(buf, 0, n);
        return f;
    }

    // 通常録音で lane0 クリップとテイク退避が「同じ波形」を描くことの検証。
    // (両者は同一ファイル・fileOffset 0 のはずなのに描画が食い違う不具合の回帰テスト)
    void testFinishLiveRecordingThumbnailsMatch()
    {
        beginTest("finishLiveRecording: lane0 clip and take backup have identical thumbnails");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("UtawaveRecTests4");
        dir.createDirectory();
        auto f = writeWavHalves(dir, "rec.wav", 48000.0, 4.0);
        expect(f.existsAsFile(), "distinctive WAV written");

        t->startLiveRecording(1.0);
        t->finishLiveRecording(f, 1.0, 4.0);   // lane0 + take backup, both fileOffset 0

        AudioClip* lane0clip = nullptr;
        for (auto& c : t->getLane(0)->clips) if (c->getFile() == f) lane0clip = c.get();
        AudioClip* takeClip = nullptr;
        for (int li = 1; li < t->getLaneCount(); ++li)
            for (auto& c : t->getLane(li)->clips) if (c->getFile() == f) takeClip = c.get();
        expect(lane0clip != nullptr, "lane0 clip exists");
        expect(takeClip != nullptr, "take backup exists");
        if (lane0clip && takeClip)
        {
            expect(approxEq(lane0clip->getFileOffset(), 0.0, 1e-9), "lane0 fileOffset 0");
            expect(approxEq(takeClip->getFileOffset(), 0.0, 1e-9), "take fileOffset 0");

            for (int i = 0; i < 400; ++i)
            {
                if (lane0clip->getThumbnail().isFullyLoaded()
                    && takeClip->getThumbnail().isFullyLoaded()) break;
                juce::Thread::sleep(15);
            }
            float maxDiff = 0.0f;
            for (int k = 0; k < 40; ++k)
            {
                double t0 = (k / 40.0) * 4.0, t1 = ((k + 1) / 40.0) * 4.0;
                float a0 = 0, a1 = 0, b0 = 0, b1 = 0;
                lane0clip->getThumbnail().getApproximateMinMax(t0, t1, 0, a0, a1);
                takeClip ->getThumbnail().getApproximateMinMax(t0, t1, 0, b0, b1);
                maxDiff = juce::jmax(maxDiff, std::abs(a1 - b1), std::abs(a0 - b0));
            }
            logMessage("thumb maxDiff=" + juce::String(maxDiff, 4)
                       + " lane0loaded=" + juce::String((int)lane0clip->getThumbnail().isFullyLoaded())
                       + " takeloaded=" + juce::String((int)takeClip->getThumbnail().isFullyLoaded()));
            expect(maxDiff < 0.02f, "lane0 and take thumbnails match across the whole clip");
        }
        dir.deleteRecursively();
    }

    // ── 実 WAV を一時生成 (16bit mono/220Hz) ──
    juce::File writeWav(const juce::File& dir, const juce::String& name, double sr, double secs)
    {
        auto f = dir.getChildFile(name);
        f.deleteFile();
        std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
        if (os == nullptr) return {};
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wav.createWriterFor(os.get(), sr, 1, 16, {}, 0));
        if (w == nullptr) return {};
        os.release();   // writer がストリームを所有
        const int n = (int) (sr * secs);
        juce::AudioBuffer<float> buf(1, n);
        for (int i = 0; i < n; ++i)
            buf.setSample(0, i, 0.1f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr));
        w->writeFromAudioSampleBuffer(buf, 0, n);
        return f;   // writer のデストラクタで flush/close
    }

    int countBackups(Track* t, const juce::File& file, double fo, double dur)
    {
        int n = 0;
        for (int li = 1; li < t->getLaneCount(); ++li)
            for (auto& c : t->getLane(li)->clips)
                if (c->getFile() == file
                    && approxEq(c->getFileOffset(), fo, 1e-3)
                    && approxEq(c->getDuration(), dur, 1e-3))
                    ++n;
        return n;
    }

    // ── finishLiveRecording: 既存クリップをテイクレーンに退避し、lane0 に新録音を置く ──
    void testFinishLiveRecordingBackup()
    {
        beginTest("finishLiveRecording: displaced clip backed up to a take lane; new clip on lane 0");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("UtawaveRecTests");
        dir.createDirectory();
        auto wavOld = writeWav(dir, "old.wav", 48000.0, 4.0);
        auto wavNew = writeWav(dir, "new.wav", 48000.0, 2.0);
        expect(wavNew.existsAsFile(), "test WAV written");

        addLane0(t, wavOld, 0.0, 4.0);             // existing recording on lane 0
        t->startLiveRecording(1.0);
        auto* rec = t->finishLiveRecording(wavNew, 1.0, 2.0);   // punch [1,3]

        expect(rec != nullptr, "finishLiveRecording returns the new clip");
        bool newInLane0 = false;
        for (auto& c : t->getLane(0)->clips) if (c.get() == rec) newInLane0 = true;
        expect(newInLane0, "new recording lives on lane 0 (punch-in continuity)");

        expect(t->getLaneCount() >= 2, "a take lane was created for the backup");
        expect(countBackups(t, wavOld, 0.0, 4.0) >= 1, "displaced old clip backed up to a take lane");

        dir.deleteRecursively();
    }

    // 退避クリップが元の fileOffset / gain を引き継ぐ (テイクの波形が元と一致する) ことの回帰テスト。
    // 旧実装は fileOffset を捨てて 0 で退避していたため、分割/パンチイン由来で fileOffset>0 の
    // 元クリップを退避するとテイク側だけファイル先頭から描画/再生され波形が食い違っていた。
    void testFinishLiveRecordingBackupPreservesOffset()
    {
        beginTest("finishLiveRecording: backup preserves the source clip fileOffset and gain");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("UtawaveRecTests3");
        dir.createDirectory();
        auto wavOld = writeWav(dir, "old.wav", 48000.0, 8.0);
        auto wavNew = writeWav(dir, "new.wav", 48000.0, 2.0);

        // fileOffset>0 の既存クリップ (分割 / パンチイン由来を模す): [2,6] が file の 1.5s 目から
        auto* old = addLane0(t, wavOld, 2.0, 4.0, /*fo=*/1.5);
        old->setGain(0.5f);

        t->startLiveRecording(3.0);
        t->finishLiveRecording(wavNew, 3.0, 2.0);   // punch [3,5] (既存 [2,6] の内側)

        // 退避は (file, fileOffset=1.5, dur=4.0) で見つかる。旧バグでは offset 0 で退避されていた
        expect(countBackups(t, wavOld, 1.5, 4.0) >= 1,
               "backup keeps the source clip fileOffset (not reset to 0)");
        expect(countBackups(t, wavOld, 0.0, 4.0) == 0,
               "backup is NOT stored at fileOffset 0 (the old bug)");

        // gain も引き継ぐ (描画振幅・再生音量を元と一致させる)
        bool gainCopied = false;
        for (int li = 1; li < t->getLaneCount(); ++li)
            for (auto& c : t->getLane(li)->clips)
                if (c->getFile() == wavOld && approxEq(c->getFileOffset(), 1.5, 1e-3))
                    gainCopied = approxEq(c->getGain(), 0.5, 1e-4);
        expect(gainCopied, "backup copies the source clip gain");

        dir.deleteRecursively();
    }

    void testFinishLiveRecordingDedup()
    {
        beginTest("finishLiveRecording: identical (file,fileOffset,duration) backup is not duplicated");
        juce::AudioFormatManager fmt; fmt.registerBasicFormats();
        TrackManager tm(fmt);
        auto* t = tm.addTrack();
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("UtawaveRecTests2");
        dir.createDirectory();
        auto wavOld = writeWav(dir, "old.wav", 48000.0, 4.0);
        auto wavNew = writeWav(dir, "new.wav", 48000.0, 2.0);

        addLane0(t, wavOld, 0.0, 4.0);                 // existing on lane 0
        t->backupToTakeLane(wavOld, 0.0, 4.0);         // 同一 3 つ組のバックアップを先に take レーンへ
        const int before = countBackups(t, wavOld, 0.0, 4.0);
        expect(before == 1, "one pre-existing backup");

        t->startLiveRecording(1.0);
        t->finishLiveRecording(wavNew, 1.0, 2.0);

        const int after = countBackups(t, wavOld, 0.0, 4.0);
        expect(after == before, "duplicate 3-tuple backup is skipped (not duplicated)");

        dir.deleteRecursively();
    }

    // ── LiveRecordingBuffer ──
    void testLiveRecordingBuffer()
    {
        beginTest("LiveRecordingBuffer: deferred alloc, peak accumulation, abs, duration, reset");
        LiveRecordingBuffer buf;

        // reset 前: ピーク無し・pushSamples は no-op (peaksPtr == nullptr)
        expect(buf.getPeakCount() == 0, "no peaks before reset");
        std::vector<float> pre(1024, 0.5f);
        buf.pushSamples(pre.data(), (int) pre.size());
        expect(buf.getPeakCount() == 0, "pushSamples before reset is a no-op");

        buf.reset();
        expect(buf.getPeakCount() == 0, "0 peaks right after reset");

        // samplesPerPeak * 3 サンプル (0.5) → 3 ピーク、各 0.5
        const int spp = LiveRecordingBuffer::samplesPerPeak;
        std::vector<float> d((size_t) spp * 3, 0.5f);
        buf.pushSamples(d.data(), (int) d.size());
        expect(buf.getPeakCount() == 3, "3 peaks after 3 * samplesPerPeak");
        expect(approxEq(buf.getPeak(0), 0.5, 1e-6), "peak = abs max of block");

        // 負値 → abs で蓄積
        std::vector<float> dn((size_t) spp, -0.8f);
        buf.pushSamples(dn.data(), (int) dn.size());
        expect(buf.getPeakCount() == 4, "4th peak appended");
        expect(approxEq(buf.getPeak(3), 0.8, 1e-6), "peak uses absolute value");

        // 尺 = peaks * samplesPerPeak / sampleRate
        expect(approxEq(buf.getDurationSeconds(48000.0), 4.0 * spp / 48000.0, 1e-9),
               "duration = peakCount * samplesPerPeak / sampleRate");

        // reset でクリア
        buf.reset();
        expect(buf.getPeakCount() == 0, "reset clears peaks");
    }
};

static RecordingTests recordingTests;
}
