// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "RecordingManager.h"
#include "../Audio/AudioEngine.h"

RecordingManager::RecordingManager(AudioEngine& eng, TrackManager& tracks,
                                   juce::AudioFormatManager& fmt)
    : audioEngine(eng), trackManager(tracks), formatManager(fmt)
{
    backgroundThread.startThread();
    getRecordingsFolder().createDirectory();
}

RecordingManager::~RecordingManager()
{
    stopRecording(0.0);
    stopRetrospective(false, 0.0);
    backgroundThread.stopThread(2000);
}

juce::File RecordingManager::getRecordingsFolder() const
{
    if (getAudioFolder) return getAudioFolder();
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("Utawave").getChildFile("Recordings");
}

juce::File RecordingManager::createRecordingFile(const juce::String& trackName) const
{
    auto folder = getRecordingsFolder();
    folder.createDirectory();
    auto ts   = juce::Time::getCurrentTime();
    auto name = juce::File::createLegalFileName(
        trackName + "_" + ts.formatted("%Y%m%d_%H%M%S") + ".wav");
    return folder.getChildFile(name);
}

bool RecordingManager::startRecording(double recStartSec, double playFromSec,
                                       bool loopRecording,
                                       double loopStart, double loopEnd)
{
    if (recording) return false;

    // 遡及録音アクティブ + アーム中トラックがそれと同じなら、Punch From Retro モードへ
    // 既存の retro writer をそのまま使い続け、stop 時に1つのクリップ（offset付き）として配置
    if (retroActive && retroTrack != nullptr && retroTrack->isRecArmed()
        && !loopRecording)
    {
        punchFromRetro = true;
        punchInRecStart = recStartSec;
        recording = true;
        // R 押下時点からライブ波形オーバーレイを表示開始
        retroTrack->startLiveRecording(recStartSec);
        audioEngine.setRetrospectiveLiveBuffer(&retroTrack->getLiveBuffer());
        audioEngine.setRecordingActive(true, recStartSec);
        return true;
    }

    juce::ignoreUnused(playFromSec);

    const double sampleRate = audioEngine.getSampleRate();
    juce::WavAudioFormat wavFormat;
    const int bits = recordingBitDepth;
    const auto sampleFormat = (bits >= 32)
        ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
        : juce::AudioFormatWriterOptions::SampleFormat::integral;

    for (int i = 0; i < trackManager.getTrackCount(); ++i)
    {
        auto* track = trackManager.getTrack(i);
        if (!track->isRecArmed()) continue;

        auto file   = createRecordingFile(track->getName());
        auto stream = std::make_unique<juce::FileOutputStream>(file);
        if (!stream->openedOk()) continue;

        const int numCh = track->isStereo() ? 2 : 1;
        auto opts = juce::AudioFormatWriterOptions{}
                        .withSampleRate(sampleRate)
                        .withNumChannels((juce::uint32) numCh)
                        .withBitsPerSample(bits)
                        .withSampleFormat(sampleFormat);
        std::unique_ptr<juce::OutputStream> outStream = std::move(stream);
        auto writer = wavFormat.createWriterFor(outStream, opts);
        if (writer == nullptr) continue;

        track->startLiveRecording(recStartSec);

        auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer.release(), backgroundThread, 65536);
        // ループ録音時はラップ毎にクリップを作成・読み出すため定期 flush を有効化
        if (loopRecording)
            tw->setFlushInterval((int)(sampleRate * 0.1));  // 100ms 毎にディスクへ反映

        // 複数トラック同時録音: 1 トラック目は setRecordingTarget で「単一設定」、
        // 2 トラック目以降は addRecordingTarget で追加する。
        // (1 トラック目で clear が走ることで、前回の録音設定が残らないように)
        if (activeRecordings.empty())
            audioEngine.setRecordingTarget(tw.get(), &track->getLiveBuffer(),
                                           track->getInputChannel(),
                                           track->isStereo());
        else
            audioEngine.addRecordingTarget(tw.get(), &track->getLiveBuffer(),
                                            track->getInputChannel(),
                                            track->isStereo());

        ActiveRecording ar;
        ar.track         = track;
        ar.file          = file;
        ar.startPosition = recStartSec;
        ar.writer        = std::move(tw);
        ar.loopRec       = loopRecording;
        ar.loopStart     = loopStart;
        ar.loopEnd       = loopEnd;
        ar.wallStartMs   = juce::Time::currentTimeMillis()
                           + (juce::int64)((recStartSec - playFromSec) * 1000.0);
        ar.takeStartLaneIdx = juce::jmax(1, track->getLaneCount());
        ar.takesAddedRealtime = 0;
        activeRecordings.push_back(std::move(ar));
        // 全 Rec アーム済みトラックを順に登録 (break なし)
    }

    // パンチイン録音開始時刻を AudioEngine に通知
    if (!activeRecordings.empty())
        audioEngine.setRecordingActive(true, recStartSec);

    recording = !activeRecordings.empty();
    return recording;
}

void RecordingManager::stopRecording(double endPositionSeconds)
{
    if (!recording) return;

    audioEngine.setRecordingActive(false);

    // ── Punch From Retro: retro ライターをそのまま終了し、offset 付きクリップで配置 ──
    if (punchFromRetro)
    {
        audioEngine.setRetrospectiveTarget(nullptr);
        retroWriter.reset();  // フラッシュ・クローズ

        const double fileStart = retroPlayStart;
        const double recStart  = punchInRecStart;
        const double stopPos   = endPositionSeconds;

        if (retroTrack && retroFile.existsAsFile())
        {
            retroTrack->cancelLiveRecording();  // ライブ波形オーバーレイを消す
            const double dur = stopPos - recStart;
            if (dur > 0.05)
            {
                const double fileOffset = juce::jmax(0.0, recStart - fileStart);
                auto* lane = retroTrack->getLane(0);
                if (lane)
                {
                    auto* clip = lane->addClip(retroFile, recStart, dur,
                                                retroTrack->getFormatManager(),
                                                retroTrack->getThumbnailCache());
                    if (clip)
                    {
                        clip->setFileOffset(fileOffset);
                        // 録音直後のファイルはキャッシュが古い/未完なので必ず再読込
                        clip->refreshThumbnail();
                        // パンチイン境界に最小クロスフェード作成
                        retroTrack->trimAndCrossfadeOnLane0(clip, recStart, dur);
                    }
                }
                // Take レーンにもバックアップ (録音履歴を残す)
                if (auto* bk = retroTrack->backupToTakeLane(retroFile, recStart, dur, fileOffset))
                    bk->refreshThumbnail();
            }
        }

        retroTrack    = nullptr;
        retroFile     = juce::File();
        retroActive   = false;
        punchFromRetro = false;
        recording = false;
        return;
    }

    audioEngine.setRecordingTarget(nullptr, nullptr);

    for (auto& ar : activeRecordings)
    {
        ar.writer.reset(); // フラッシュ・クローズ（ファイルが完全に書き終わる）

        // リアルタイムに作成されたクリップのサムネイルを再生成
        // （録音中はファイル末尾が伸び続けていて全体が読めていないため）
        for (auto* clip : ar.realtimeClips)
            if (clip) clip->refreshThumbnail();

        // 通常録音は再生位置ベースで尺を出す（ループ無し前提）
        // ループ録音時は AudioEngine が書き込んだサンプル数で実書き出し時間を出す
        // (currentPosition はループで巻き戻るため使えない。wall clock は OS 時計補正・
        //  スリープでズレるためサンプル数ベースの方が確実)
        double dur = 0.0;
        if (ar.loopRec)
        {
            const double sr = audioEngine.getSampleRate() > 0.0
                                ? audioEngine.getSampleRate() : 48000.0;
            dur = (double) audioEngine.getRecordedSampleCount() / sr;
        }
        else
        {
            dur = endPositionSeconds - ar.startPosition;
        }

        if (!ar.loopRec)
        {
            if (dur > 0.01 && ar.file.existsAsFile())
                ar.track->finishLiveRecording(ar.file, ar.startPosition, dur);
            else
                ar.track->cancelLiveRecording();
            continue;
        }

        // ── ループ録音: ファイルをループ区間ごとにスライスして Take レーンへ ──
        ar.track->cancelLiveRecording();

        const double loopDur = ar.loopEnd - ar.loopStart;
        if (loopDur < 0.05 || dur < 0.05 || !ar.file.existsAsFile()) continue;

        // recStart < loopStart の場合 pre-loop 部分（warm-up）と最初のループ周回を
        // 1 つの Take 1 として統合配置。以降の周回は順次 Take 2, 3, ... へ
        const double preLoopDur = juce::jmax(0.0, ar.loopStart - ar.startPosition);
        const double afterPreDur = juce::jmax(0.0, dur - preLoopDur);

        // リアルタイム配置済みの take は再追加しない
        const int alreadyAdded = ar.takesAddedRealtime;

        if (afterPreDur < 0.05)
        {
            // ループに入る前に停止 → pre-loop だけを Take 1 として配置
            if (alreadyAdded == 0 && preLoopDur >= 0.05)
            {
                auto* lane = ar.track->ensureLane(ar.takeStartLaneIdx);
                if (lane)
                {
                    auto* clip = lane->addClip(ar.file, ar.startPosition, preLoopDur,
                                                ar.track->getFormatManager(),
                                                ar.track->getThumbnailCache());
                    if (clip)
                    {
                        clip->setFileOffset(0.0);
                        clip->refreshThumbnail();
                    }
                }
            }
            continue;
        }

        // Take 1（まだ配置されていなければ）= pre-loop + 1周目 の連続クリップ
        if (alreadyAdded == 0)
        {
            const double firstSlice = juce::jmin(loopDur, afterPreDur);
            const double take1Dur = preLoopDur + firstSlice;
            auto* lane = ar.track->ensureLane(ar.takeStartLaneIdx);
            if (lane)
            {
                auto* clip = lane->addClip(ar.file, ar.startPosition, take1Dur,
                                            ar.track->getFormatManager(),
                                            ar.track->getThumbnailCache());
                if (clip)
                {
                    clip->setFileOffset(0.0);
                    clip->refreshThumbnail();
                }
            }
        }

        // 2周目以降の周回（まだ配置されていないものだけ追加）
        const int loopIterations = (int)std::ceil(afterPreDur / loopDur);
        for (int it = juce::jmax(1, alreadyAdded); it < loopIterations; ++it)
        {
            const double inLoopOffset = it * loopDur;
            const double slice = juce::jmin(loopDur, afterPreDur - inLoopOffset);
            if (slice < 0.05) continue;

            auto* lane = ar.track->ensureLane(ar.takeStartLaneIdx + it);
            if (!lane) continue;
            auto* clip = lane->addClip(ar.file, ar.loopStart, slice,
                                       ar.track->getFormatManager(),
                                       ar.track->getThumbnailCache());
            if (clip)
            {
                clip->setFileOffset(preLoopDur + inLoopOffset);
                clip->refreshThumbnail();
            }
        }
    }

    activeRecordings.clear();
    recording = false;
}

void RecordingManager::onLoopWrap()
{
    if (!recording) return;
    for (auto& ar : activeRecordings)
    {
        if (!ar.loopRec || !ar.writer || !ar.track) continue;

        const double loopDur = ar.loopEnd - ar.loopStart;
        if (loopDur < 0.05) continue;

        const double preLoopDur = juce::jmax(0.0, ar.loopStart - ar.startPosition);

        const int it = ar.takesAddedRealtime;
        double pos, takeDur, offset;
        if (it == 0)
        {
            // Take 1 = pre-loop + iter 0 の連続クリップ
            pos     = ar.startPosition;
            takeDur = preLoopDur + loopDur;
            offset  = 0.0;
        }
        else
        {
            pos     = ar.loopStart;
            takeDur = loopDur;
            offset  = preLoopDur + it * loopDur;
        }

        int laneIdx = ar.takeStartLaneIdx + it;
        auto* lane  = ar.track->ensureLane(laneIdx);
        if (!lane) continue;
        auto* clip = lane->addClip(ar.file, pos, takeDur,
                                   ar.track->getFormatManager(),
                                   ar.track->getThumbnailCache());
        if (clip)
        {
            clip->setFileOffset(offset);
            ar.realtimeClips.push_back(clip);
        }

        ++ar.takesAddedRealtime;
    }
}

bool RecordingManager::startRetrospective(Track* targetTrack, double playStartSec)
{
    if (retroActive) return false;
    if (!targetTrack) return false;

    const double sampleRate = audioEngine.getSampleRate();
    juce::WavAudioFormat wavFormat;
    const int bits = recordingBitDepth;
    const auto sampleFormat = (bits >= 32)
        ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
        : juce::AudioFormatWriterOptions::SampleFormat::integral;

    auto file   = createRecordingFile(targetTrack->getName() + "_retro");
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (!stream->openedOk()) return false;

    const int numCh = targetTrack->isStereo() ? 2 : 1;
    auto opts = juce::AudioFormatWriterOptions{}
                    .withSampleRate(sampleRate)
                    .withNumChannels((juce::uint32) numCh)
                    .withBitsPerSample(bits)
                    .withSampleFormat(sampleFormat);
    std::unique_ptr<juce::OutputStream> outStream = std::move(stream);
    auto writer = wavFormat.createWriterFor(outStream, opts);
    if (writer == nullptr) return false;

    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), backgroundThread, 65536);

    // R 押下までは波形を表示しない（liveBuffer 無し）。書き出しのみ裏で行う。
    audioEngine.setRetrospectiveTarget(tw.get(),
                                       /*liveBuf*/ nullptr,
                                       targetTrack->getInputChannel(),
                                       targetTrack->isStereo());

    retroTrack     = targetTrack;
    retroFile      = file;
    retroPlayStart = playStartSec;
    retroStereo    = targetTrack->isStereo();
    retroWriter    = std::move(tw);
    retroActive    = true;
    return true;
}

void RecordingManager::stopRetrospective(bool commit, double playEndSec)
{
    if (!retroActive) return;

    audioEngine.setRetrospectiveTarget(nullptr);
    retroWriter.reset();  // フラッシュ・クローズ

    // ライブ波形オーバーレイをクリア
    if (retroTrack) retroTrack->cancelLiveRecording();

    if (commit && retroTrack && retroFile.existsAsFile())
    {
        const double dur = playEndSec - retroPlayStart;
        if (dur > 0.05)
        {
            auto* lane = retroTrack->getLane(0);
            if (lane)
            {
                auto* clip = lane->addClip(retroFile, retroPlayStart, dur,
                              retroTrack->getFormatManager(),
                              retroTrack->getThumbnailCache());
                // 録音直後のファイルはキャッシュが古い/未完なので必ず再読込
                if (clip) clip->refreshThumbnail();
            }
        }
    }
    else
    {
        if (retroFile.existsAsFile()) retroFile.deleteFile();
    }

    retroTrack    = nullptr;
    retroFile     = juce::File();
    retroActive   = false;
}
