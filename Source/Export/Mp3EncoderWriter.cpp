// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "Mp3EncoderWriter.h"
#include "../Localisation.h"

extern "C" {
#include <lame.h>
}

namespace
{
    // RAII: lame_init / lame_close を必ず対にする。
    // 途中で return / 例外が発生してもハンドルがリークしない。
    struct LameHandle
    {
        lame_global_flags* p { nullptr };
        LameHandle() : p(lame_init()) {}
        ~LameHandle() { if (p) lame_close(p); }
        LameHandle(const LameHandle&) = delete;
        LameHandle& operator=(const LameHandle&) = delete;
        operator lame_global_flags*() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };
}

bool Mp3EncoderWriter::encodeBuffer(const juce::AudioBuffer<float>& buffer,
                                    double sampleRate,
                                    int bitrateKbps,
                                    const juce::File& outFile,
                                    juce::String* errorOut)
{
    auto setErr = [errorOut](const juce::String& s) { if (errorOut) *errorOut = s; };

    const int channels = juce::jlimit(1, 2, buffer.getNumChannels());
    const int nFrames  = buffer.getNumSamples();
    if (nFrames <= 0) { setErr(tr(u8"空のバッファ")); return false; }

    LameHandle lame;
    if (!lame) { setErr("lame_init failed"); return false; }

    lame_set_in_samplerate(lame, (int)sampleRate);
    lame_set_num_channels  (lame, channels);
    lame_set_brate         (lame, bitrateKbps);
    lame_set_mode          (lame, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_quality       (lame, 2);                 // 0=最高品質 / 9=最低
    lame_set_VBR           (lame, vbr_off);           // CBR
    // CBR でも INFO タグを書くことで一部プレーヤーで尺/シーク精度が改善する
    lame_set_bWriteVbrTag  (lame, 1);
    if (lame_init_params(lame) < 0)
    {
        setErr("lame_init_params failed");
        return false;
    }

    juce::FileOutputStream out(outFile);
    if (!out.openedOk())
    {
        setErr(tr(u8"出力ファイルを開けません"));
        return false;
    }
    out.setPosition(0);
    out.truncate();

    // ブロック単位でエンコード
    const int blockSamples = 1152 * 4;
    // LAME 推奨の出力バッファサイズ: 1.25 * numSamples + 7200
    std::vector<unsigned char> mp3Buf((size_t)(1.25 * blockSamples + 7200));

    auto* const* readPtrs = buffer.getArrayOfReadPointers();

    int pos = 0;
    while (pos < nFrames)
    {
        const int n = juce::jmin(blockSamples, nFrames - pos);

        int written = 0;
        if (channels == 1)
        {
            written = lame_encode_buffer_ieee_float(
                lame,
                readPtrs[0] + pos,    // L
                readPtrs[0] + pos,    // R (mono は L を流用)
                n,
                mp3Buf.data(),
                (int)mp3Buf.size());
        }
        else
        {
            written = lame_encode_buffer_ieee_float(
                lame,
                readPtrs[0] + pos,
                readPtrs[1] + pos,
                n,
                mp3Buf.data(),
                (int)mp3Buf.size());
        }

        if (written < 0)
        {
            setErr("lame_encode_buffer_ieee_float failed: " + juce::String(written));
            return false;
        }
        if (written > 0)
            out.write(mp3Buf.data(), (size_t)written);

        pos += n;
    }

    // 末尾フラッシュ
    int flushed = lame_encode_flush(lame, mp3Buf.data(), (int)mp3Buf.size());
    if (flushed > 0)
        out.write(mp3Buf.data(), (size_t)flushed);

    // INFO/Xing タグをファイル先頭に書き戻す。
    // LAME はエンコード開始時にプレースホルダフレームを書いており、
    // ここで実際のタグ (尺・トータルフレーム数等) で上書きする。
    std::vector<unsigned char> infoBuf(16384);
    size_t infoSize = lame_get_lametag_frame(lame, infoBuf.data(), infoBuf.size());
    if (infoSize > 0 && infoSize <= infoBuf.size())
    {
        out.setPosition(0);
        out.write(infoBuf.data(), infoSize);
    }
    out.flush();
    return true;
}
