// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Studio Asteroid

#include "AudioClip.h"

AudioClip::AudioClip(const juce::File& f, double startPos, double dur,
                     juce::AudioFormatManager& fmtMgr,
                     juce::AudioThumbnailCache& cache)
    : formatManager(&fmtMgr),
      file(f), startPosition(startPos), duration(dur),
      name(f.getFileNameWithoutExtension()),
      // 64 サンプル/サムネイルサンプル (~1.3ms @ 48kHz)。512 の 8 倍精度で
      // クロスフェードや短い無音も視覚的に正確に描ける。
      thumbnail(64, fmtMgr, cache)
{
    thumbnail.setSource(new ContentHashedFileSource(file));
}
