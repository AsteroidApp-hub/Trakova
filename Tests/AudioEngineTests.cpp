// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

// Utawave — AudioEngine 実時間パス (audioDeviceIOCallbackWithContext) のユニットテスト
//
// オーディオデバイスの代わりにスタブ (FakeAudioIODevice) で audioDeviceAboutToStart を
// 通し、デバイスコールバックをテストスレッドから直接駆動する。これにより従来
// renderOfflineRange 経由でしか検証できなかった「再生ブランチ」(スナップショット grab /
// renderClip / Mute・Solo / 停止時無音 / clearPlayback バリア / 遅延破棄 + 再構築) を
// デバイス無し・決定論的に検証する。
//
// 定石:
// - コールバックは単一スレッド (テストスレッド) から逐次呼ぶ。audio thread と UI thread の
//   並行性そのものは対象外 (lock-free 構造のレースは実機 QA / TSan の領分)
// - 内容検証は ExportEngineTests と同じく const 値 WAV + fade 0 + vol 0dB / pan 0 で行う
//   (pan はリニアバランス則・center 減衰なし、mono は L/R 複製)

#include <JuceHeader.h>
#include <cmath>

#include "../Source/Audio/AudioEngine.h"
#include "../Source/Tracks/TrackManager.h"
#include "../Source/Tracks/Track.h"
#include "../Source/Tracks/AudioClip.h"

namespace
{
constexpr double kSR    = 48000.0;
constexpr int    kBlock = 512;

// audioDeviceAboutToStart に渡す最小スタブ。SR / buffer size / チャンネル構成だけを返す。
struct FakeAudioIODevice : public juce::AudioIODevice
{
    FakeAudioIODevice() : juce::AudioIODevice("FakeDevice", "FakeType") {}

    juce::StringArray getOutputChannelNames() override        { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override         { return { "In 1", "In 2" }; }
    juce::Array<double> getAvailableSampleRates() override    { return { kSR }; }
    juce::Array<int> getAvailableBufferSizes() override       { return { kBlock }; }
    int getDefaultBufferSize() override                       { return kBlock; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&,
                      double, int) override                   { return {}; }
    void close() override                                     {}
    bool isOpen() override                                    { return true; }
    void start(juce::AudioIODeviceCallback*) override         {}
    void stop() override                                      {}
    bool isPlaying() override                                 { return false; }
    juce::String getLastError() override                      { return {}; }
    int getCurrentBufferSizeSamples() override                { return kBlock; }
    double getCurrentSampleRate() override                    { return kSR; }
    int getCurrentBitDepth() override                         { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { juce::BigInteger b; b.setRange(0, 2, true); return b; }
    juce::BigInteger getActiveInputChannels() const override  { juce::BigInteger b; b.setRange(0, 2, true); return b; }
    int getOutputLatencyInSamples() override                  { return 0; }
    int getInputLatencyInSamples() override                   { return 0; }
};

bool writeMonoConstWav(const juce::File& f, int numSamples, float value)
{
    juce::AudioBuffer<float> b(1, numSamples);
    juce::FloatVectorOperations::fill(b.getWritePointer(0), value, numSamples);
    juce::WavAudioFormat waf;
    using SF = juce::AudioFormatWriterOptions::SampleFormat;
    auto wopts = juce::AudioFormatWriterOptions{}
                     .withSampleRate(kSR).withNumChannels(1)
                     .withBitsPerSample(32).withSampleFormat(SF::floatingPoint);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    auto fos = std::make_unique<juce::FileOutputStream>(f);
    if (!fos->openedOk()) return false;
    std::unique_ptr<juce::OutputStream> os = std::move(fos);
    std::unique_ptr<juce::AudioFormatWriter> w(waf.createWriterFor(os, wopts));
    return w != nullptr && w->writeFromAudioSampleBuffer(b, 0, numSamples);
}

// numBlocks 分コールバックを駆動し、全ブロックの L/R 絶対値ピークを返す
juce::Range<float> runBlocks(AudioEngine& engine, int numBlocks,
                             float* outPeakL = nullptr, float* outPeakR = nullptr)
{
    juce::AudioBuffer<float> out(2, kBlock);
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < numBlocks; ++i)
    {
        out.clear();
        float* chans[2] = { out.getWritePointer(0), out.getWritePointer(1) };
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, chans, 2, kBlock, {});
        peakL = juce::jmax(peakL, out.getMagnitude(0, 0, kBlock));
        peakR = juce::jmax(peakR, out.getMagnitude(1, 0, kBlock));
    }
    if (outPeakL) *outPeakL = peakL;
    if (outPeakR) *outPeakR = peakR;
    return { juce::jmin(peakL, peakR), juce::jmax(peakL, peakR) };
}
} // namespace

struct AudioEngineRealtimeTests : public juce::UnitTest
{
    AudioEngineRealtimeTests() : juce::UnitTest("AudioEngine realtime callback") {}

    juce::File tempDir;

    void runTest() override
    {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("UtawaveAudioEngineTests");
        tempDir.deleteRecursively();
        tempDir.createDirectory();

        testPlaybackRendersClip();
        testNotPlayingIsSilent();
        testMuteAndSolo();
        testClearPlaybackBarrier();
        testDeferredDestructionRebuild();

        tempDir.deleteRecursively();
    }

    // シーン構築 + デバイス開始の共通部
    struct Scene
    {
        juce::AudioFormatManager fmt;
        std::unique_ptr<TrackManager> tm;
        FakeAudioIODevice device;
        AudioEngine engine;
        Scene()
        {
            fmt.registerBasicFormats();
            tm = std::make_unique<TrackManager>(fmt);
        }
        Track* addConstTrack(const juce::File& wav, double dur)
        {
            auto* t = tm->addTrack({}, false);
            auto* c = t->addClip(wav, 0.0, dur);
            c->setFadeInSecs(0.0); c->setFadeOutSecs(0.0);
            t->setVolume(0.0f); t->setPan(0.0f);
            return t;
        }
        void start()
        {
            engine.audioDeviceAboutToStart(&device);
            engine.preparePlayback(*tm);
            engine.setPosition(0.0);
        }
    };

    void testPlaybackRendersClip()
    {
        beginTest("playing: clip content reaches both output channels");
        auto wav = tempDir.getChildFile("c05.wav");
        expect(writeMonoConstWav(wav, (int)kSR, 0.5f), "source write");

        Scene s;
        s.addConstTrack(wav, 1.0);
        s.start();
        s.engine.play();

        float pl = 0, pr = 0;
        runBlocks(s.engine, 10, &pl, &pr);
        // mono クリップは L/R 複製・pan center は減衰なし・vol 0dB → ピークはほぼ 0.5
        expect(std::abs(pl - 0.5f) < 0.01f, "L peak ~0.5");
        expect(std::abs(pr - 0.5f) < 0.01f, "R peak ~0.5");

        // 再生位置がブロック数ぶん前進している (10 * 512 / 48000)
        const double expected = 10.0 * kBlock / kSR;
        expect(std::abs(s.engine.getCurrentPositionSeconds() - expected) < (kBlock / kSR) + 1e-6,
               "position advances by rendered blocks");
    }

    void testNotPlayingIsSilent()
    {
        beginTest("stopped: callback outputs silence and position holds");
        auto wav = tempDir.getChildFile("c05b.wav");
        expect(writeMonoConstWav(wav, (int)kSR, 0.5f), "source write");

        Scene s;
        s.addConstTrack(wav, 1.0);
        s.start();
        // play() しない
        auto peaks = runBlocks(s.engine, 4);
        expectEquals(peaks.getEnd(), 0.0f, "silent while stopped");
        expectEquals(s.engine.getCurrentPositionSeconds(), 0.0, "position unchanged");

        // 再生 → 停止後も無音に戻る
        s.engine.play();
        runBlocks(s.engine, 2);
        s.engine.stop();
        peaks = runBlocks(s.engine, 2);
        expectEquals(peaks.getEnd(), 0.0f, "silent after stop");
    }

    void testMuteAndSolo()
    {
        beginTest("mute / solo are honoured by the realtime mix");
        auto wavA = tempDir.getChildFile("a04.wav");
        auto wavB = tempDir.getChildFile("b02.wav");
        expect(writeMonoConstWav(wavA, (int)kSR, 0.4f), "source A write");
        expect(writeMonoConstWav(wavB, (int)kSR, 0.2f), "source B write");

        Scene s;
        auto* ta = s.addConstTrack(wavA, 1.0);
        auto* tb = s.addConstTrack(wavB, 1.0);
        s.start();
        s.engine.play();

        // 両方有効 → 0.4 + 0.2 = 0.6
        float pl = 0, pr = 0;
        runBlocks(s.engine, 3, &pl, &pr);
        expect(std::abs(pl - 0.6f) < 0.01f, "both tracks mix to ~0.6");

        // A を Mute → 0.2 のみ (Track::muted は atomic、再構築不要で即反映)
        ta->setMuted(true);
        runBlocks(s.engine, 3, &pl, &pr);
        expect(std::abs(pl - 0.2f) < 0.01f, "muted track drops out (~0.2)");
        ta->setMuted(false);

        // B を Solo → 0.2 のみ
        tb->setSoloed(true);
        runBlocks(s.engine, 3, &pl, &pr);
        expect(std::abs(pl - 0.2f) < 0.01f, "solo silences the other track (~0.2)");
    }

    void testClearPlaybackBarrier()
    {
        beginTest("clearPlayback: returns promptly and callback stays safe");
        auto wav = tempDir.getChildFile("c05c.wav");
        expect(writeMonoConstWav(wav, (int)kSR, 0.5f), "source write");

        Scene s;
        s.addConstTrack(wav, 1.0);
        s.start();
        s.engine.play();
        runBlocks(s.engine, 2);

        // audio thread が居ないので旧スナップショットは即時 drain される (テストが
        // ハングせず完走すること自体が 500ms バリアの検証)
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        s.engine.clearPlayback();
        expect(juce::Time::getMillisecondCounterHiRes() - t0 < 400.0,
               "clearPlayback returns without waiting for the timeout");

        // スナップショット切替直後は旧音からのデクリック・クロスフェードが入る (仕様)。
        // フェードを流し切ってから無音を確認する
        runBlocks(s.engine, 10);
        auto peaks = runBlocks(s.engine, 3);
        expectEquals(peaks.getEnd(), 0.0f, "silent after clearPlayback (post declick)");
    }

    void testDeferredDestructionRebuild()
    {
        beginTest("deferClipDestruction + invalidatePlayback while playing");
        auto wav = tempDir.getChildFile("c05d.wav");
        expect(writeMonoConstWav(wav, (int)kSR, 0.5f), "source write");

        Scene s;
        auto* t = s.addConstTrack(wav, 1.0);
        s.start();
        s.engine.play();
        runBlocks(s.engine, 2);

        // 破棄系編集の経路を再現: クリップを lane から外し、所有権を遅延破棄へ渡してから
        // invalidatePlayback (再生中なので即 rebuild)。コールバックは無音になり、クラッシュしない
        auto* lane = t->getLane(0);
        expect(lane != nullptr && !lane->clips.empty(), "lane has the clip");
        std::vector<std::unique_ptr<AudioClip>> removed;
        removed.push_back(std::move(lane->clips[0]));
        lane->clips.erase(lane->clips.begin());
        s.engine.deferClipDestruction(std::move(removed));
        s.engine.invalidatePlayback();

        // 切替デクリックを流し切ってから判定 (clearPlayback と同様)
        runBlocks(s.engine, 10);
        auto peaks = runBlocks(s.engine, 3);
        expectEquals(peaks.getEnd(), 0.0f, "silent after the clip was removed (post declick)");
    }
};

static AudioEngineRealtimeTests audioEngineRealtimeTests;
