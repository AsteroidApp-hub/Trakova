// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#include "AudioFileImporter.h"
#include "CDSPResampler.h"  // r8brain (header-only)

juce::File AudioFileImporter::getDefaultCacheFolder()
{
    auto f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Utawave").getChildFile("Cache");
    f.createDirectory();
    return f;
}

juce::File AudioFileImporter::getCacheFolder() const
{
    if (getCacheFolderCb)
    {
        auto f = getCacheFolderCb();
        f.createDirectory();
        return f;
    }
    return getDefaultCacheFolder();
}

AudioFileImporter::Result
AudioFileImporter::importFile(const juce::File& src, double projectSampleRate, int outputBits,
                              std::function<bool(double)> onProgress)
{
    // outputBits は 24 (PCM + TPDF ディザ) または 32 (float) のみサポート。
    // それ以外の値が来た場合は無音で 32 にフォールバックさせず、明示的に DBG ログを残す
    // (16/64 等の指定は呼び出し側の設定ミスである可能性が高い)。
    if (outputBits != 24 && outputBits != 32)
    {
        DBG("AudioFileImporter: unsupported outputBits=" << outputBits
             << ", falling back to 32 (float)");
        jassertfalse;  // デバッグビルドで気付けるよう assert
        outputBits = 32;
    }
    Result r;
    if (!src.existsAsFile()) { r.errorMessage = "File not found"; return r; }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(src));
    if (!reader) { r.errorMessage = "Unsupported format"; return r; }

    r.sampleRate  = reader->sampleRate;
    r.numChannels = (int)reader->numChannels;
    r.durationSec = (double)reader->lengthInSamples / juce::jmax(1.0, reader->sampleRate);

    // SR一致なら元ファイルをそのまま使う
    if (std::abs(reader->sampleRate - projectSampleRate) < 0.01)
    {
        r.success = true;
        r.file    = src;
        return r;
    }

    // リサンプル: キャッシュフォルダにユニーク名でWAV出力
    auto cache = getCacheFolder();
    auto stem  = src.getFileNameWithoutExtension()
                 + juce::String::formatted("_%dHz_%lld.wav",
                                           (int)projectSampleRate,
                                           (long long)juce::Time::currentTimeMillis());
    auto dst   = cache.getChildFile(juce::File::createLegalFileName(stem));

    juce::String err;
    if (!resampleToFile(src, dst, reader->sampleRate, projectSampleRate,
                        (int)reader->numChannels, *reader, outputBits, err, onProgress))
    {
        r.errorMessage = err;
        r.cancelled    = (err == "cancelled");
        dst.deleteFile();   // 中断/失敗時の部分ファイルを片付ける (破損キャッシュを残さない)
        return r;
    }

    r.success      = true;
    r.file         = dst;
    r.wasResampled = true;
    r.sampleRate   = projectSampleRate;
    return r;
}

bool AudioFileImporter::resampleToFile(const juce::File& src, const juce::File& dst,
                                       double srcSr, double dstSr, int numChannels,
                                       juce::AudioFormatReader& reader,
                                       int outputBits,
                                       juce::String& errorOut,
                                       const std::function<bool(double)>& onProgress)
{
    juce::ignoreUnused(src);
    if (numChannels < 1) { errorOut = "No channels"; return false; }

    const int blockSize = 4096;
    // チャンネルごとに r8brain リサンプラを用意
    std::vector<std::unique_ptr<r8b::CDSPResampler24>> resamps;
    resamps.reserve((size_t)numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        resamps.emplace_back(std::make_unique<r8b::CDSPResampler24>(
            srcSr, dstSr, blockSize));

    juce::WavAudioFormat wav;
    auto outStreamUP = std::make_unique<juce::FileOutputStream>(dst);
    if (!outStreamUP->openedOk()) { errorOut = "Cannot open cache file"; return false; }
    outStreamUP->setPosition(0);
    outStreamUP->truncate();

    using SF = juce::AudioFormatWriterOptions::SampleFormat;
    auto opts = juce::AudioFormatWriterOptions{}
                    .withSampleRate(dstSr)
                    .withNumChannels(numChannels)
                    .withBitsPerSample(outputBits)
                    .withSampleFormat(outputBits == 32 ? SF::floatingPoint : SF::integral);

    std::unique_ptr<juce::OutputStream> outStream = std::move(outStreamUP);
    auto writer = wav.createWriterFor(outStream, opts);
    if (!writer) { errorOut = "Cannot create WAV writer"; return false; }

    juce::AudioBuffer<float>  inBuf((int)numChannels, blockSize);
    std::vector<std::vector<double>> inDouble((size_t)numChannels);
    for (auto& v : inDouble) v.resize((size_t)blockSize);

    juce::int64 totalIn = reader.lengthInSamples;
    juce::int64 expectedOut = (juce::int64)((double)totalIn * dstSr / srcSr);
    juce::int64 readPos = 0;
    juce::int64 writtenOut = 0;

    while (writtenOut < expectedOut)
    {
        if (onProgress && expectedOut > 0
            && ! onProgress((double) writtenOut / (double) expectedOut))
        {
            errorOut = "cancelled";
            return false;
        }

        int toRead = (int)juce::jmin((juce::int64)blockSize, totalIn - readPos);
        bool flushing = (toRead <= 0);

        if (!flushing)
        {
            inBuf.clear();
            reader.read(&inBuf, 0, toRead, readPos, true, true);
            readPos += toRead;
        }
        else
        {
            // ファイル末尾後はゼロを送ってリサンプラの遅延分を取り出す
            toRead = blockSize;
            inBuf.clear();
        }

        // float → double（チャンネル別）
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* src = inBuf.getReadPointer(ch);
            for (int i = 0; i < toRead; ++i)
                inDouble[(size_t)ch][(size_t)i] = (double)src[i];
        }

        // チャンネル別にリサンプラ通過 → 出力ポインタを取得
        std::vector<double*> outPtrs((size_t)numChannels, nullptr);
        int writeCount = 0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            double* opp = nullptr;
            int wc = resamps[(size_t)ch]->process(
                inDouble[(size_t)ch].data(), toRead, opp);
            outPtrs[(size_t)ch] = opp;
            writeCount = wc;  // 全チャンネル同じ
        }

        if (writeCount <= 0)
        {
            if (flushing) break;  // これ以上出ない
            continue;
        }

        // ファイル尺を超えないようクリップ
        if (writtenOut + writeCount > expectedOut)
            writeCount = (int)(expectedOut - writtenOut);
        if (writeCount <= 0) break;

        // double → float に変換しつつ writer に渡す（インターリーブ無し: writer はチャンネル分離 API を取る）
        juce::AudioBuffer<float> outBuf((int)numChannels, writeCount);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dstP = outBuf.getWritePointer(ch);
            for (int i = 0; i < writeCount; ++i)
                dstP[i] = (float)outPtrs[(size_t)ch][i];
        }

        // 24bit 出力時のみ TPDF ディザを足す（量子化雑音の信号相関を除去）
        if (outputBits == 24)
        {
            static thread_local juce::Random rng;
            const float lsb = 1.0f / (float)(1 << 23);  // 24bit の 1LSB（[-1,1] スケール）
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* p = outBuf.getWritePointer(ch);
                for (int i = 0; i < writeCount; ++i)
                {
                    float n1 = rng.nextFloat() - 0.5f;
                    float n2 = rng.nextFloat() - 0.5f;
                    p[i] += (n1 + n2) * lsb;  // 三角分布（ピーク ±1 LSB）
                }
            }
        }

        if (!writer->writeFromAudioSampleBuffer(outBuf, 0, writeCount))
        {
            errorOut = "Failed to write samples (disk full?)";
            return false;
        }
        writtenOut += writeCount;

        if (flushing && writeCount == 0) break;
    }

    writer->flush();
    return true;
}

bool AudioFileImporter::copyStrippingMetadata(const juce::File& src, const juce::File& dst,
                                              juce::String& errorOut)
{
    if (!src.existsAsFile()) { errorOut = "Source not found"; return false; }
    // WAV のみ対応。他のフォーマットは iXML/bext を含まないため呼び出し側で plain copy を使う。
    if (!src.hasFileExtension("wav")) { errorOut = "Not a WAV file"; return false; }

    juce::WavAudioFormat wav;
    auto inputStream = src.createInputStream();
    if (inputStream == nullptr) { errorOut = "Cannot open source"; return false; }
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(inputStream.get(), false));
    if (!reader) { errorOut = "Unsupported WAV"; return false; }
    inputStream.release();  // reader が所有

    // インポート時は LIST-INFO (RIFF INFO) の説明用タグだけを残し、それ以外のチャンク由来
    // メタデータ (bext / iXML・ASWG / smpl ループ / acid / tracktion 等のテンポ・ループ・
    // 各種制作情報) はすべて除去する。これにより他 DAW の埋め込みメタデータがプロジェクトへ
    // 流入するのを防ぐ。
    //
    // 判定は許可リスト方式: LIST-INFO のキーは RIFF 仕様上つねに 4 文字の FourCC
    // (例: IART=Artist, ICMT=Comment, ICRD=DateCreated)。一方、除去対象のキーはいずれも
    // FourCC にならない — "bwav description" (空白入り) / "tempo" "timeSig" "inKey" (iXML の
    // 小文字タグ名) / "Loop0Start" "NumSampleLoops" "Manufacturer" (smpl, 5 文字以上) /
    // "IXML_VERSION" (下線入り) など。そこで「4 文字の英大文字/数字」のキーのみ残す。
    //
    // (旧実装は "aswg"/"bwav"/"loop"/"sample" プレフィックス一致で除去していたが、JUCE が
    //  返す実キーは iXML が "tempo" 等の生タグ名・smpl が "Loop0Start"/"NumSampleLoops" で、
    //  "aswg"/"loop"/"sample" プレフィックスを持たない。さらに非プレフィックスの smpl キー
    //  (NumSampleLoops 等) が残ると writer が smpl チャンクを再生成するため、結果として iXML
    //  テンポ・smpl ループが素通りしていた。許可リスト方式はこの取りこぼしを構造的に防ぐ)
    auto isHarmlessInfoTag = [](const juce::String& key)
    {
        if (key.length() != 4) return false;
        for (auto ch : key)
            if (! ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
                return false;
        return true;
    };

    std::unordered_map<juce::String, juce::String> cleanMeta;
    const auto& meta = reader->metadataValues;
    const auto keys   = meta.getAllKeys();
    const auto values = meta.getAllValues();
    for (int i = 0; i < keys.size(); ++i)
        if (isHarmlessInfoTag(keys[i]))
            cleanMeta[keys[i]] = values[i];

    auto outStreamUP = std::make_unique<juce::FileOutputStream>(dst);
    if (!outStreamUP->openedOk()) { errorOut = "Cannot open destination"; return false; }
    outStreamUP->setPosition(0);
    outStreamUP->truncate();

    using SF = juce::AudioFormatWriterOptions::SampleFormat;
    const int bits = (int) reader->bitsPerSample;
    auto opts = juce::AudioFormatWriterOptions{}
                    .withSampleRate(reader->sampleRate)
                    .withNumChannels((int) reader->numChannels)
                    .withBitsPerSample(bits > 0 ? bits : 24)
                    .withSampleFormat(reader->usesFloatingPointData
                                        ? SF::floatingPoint : SF::integral)
                    .withMetadataValues(cleanMeta);

    std::unique_ptr<juce::OutputStream> outStream = std::move(outStreamUP);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(outStream, opts));
    if (!writer) { errorOut = "Cannot create WAV writer"; return false; }

    if (!writer->writeFromAudioReader(*reader, 0, reader->lengthInSamples))
    {
        writer.reset();    // ストリームを閉じてから
        dst.deleteFile();  // 部分書き込みの破損ファイルを残さない
        errorOut = "Failed to copy samples";
        return false;
    }
    writer->flush();
    return true;
}
