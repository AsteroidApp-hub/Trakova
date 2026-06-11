// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

// Utawave — キー変更 CLI ツール (開発用コンソールアプリ)
//
// 自社製ピッチシフトエンジン (pitchcore) で単発変換する。品質チューニング時の
// 聴き比べ素材の生成と、ライブラリ単体配布時の使用例を兼ねる:
//   UtawavePitchTool <audiofile> <semitones> [outDir]
//     → <stem>_pitch<±N>.wav (32bit float)
// 処理時間も表示する。コンソール出力は ASCII のみ。

#include <JuceHeader.h>
#include "../Source/Audio/PitchEngine.h"
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: UtawavePitchTool <audiofile> <semitones> [outDir]\n"
                     "Writes <stem>_pitch<+/-N>.wav next to the input (or into outDir).\n";
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File inFile = cwd.getChildFile(juce::String(argv[1]));
    if (! inFile.existsAsFile())
    {
        std::cout << "Input file not found: "
                  << inFile.getFullPathName().toRawUTF8() << "\n";
        return 2;
    }

    const double semis = juce::String(argv[2]).getDoubleValue();
    juce::File outDir = (argc >= 4) ? cwd.getChildFile(juce::String(argv[3]))
                                    : inFile.getParentDirectory();
    outDir.createDirectory();

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    const juce::String suffix = (semis > 0 ? juce::String("+") : juce::String())
                              + juce::String((int) semis);
    const auto outFile = outDir.getChildFile(
        inFile.getFileNameWithoutExtension() + "_pitch" + suffix + ".wav");

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = PitchEngine::processFile(inFile, outFile, fmt, semis, 32);
    const double t1 = juce::Time::getMillisecondCounterHiRes();

    std::cout << (ok ? "OK   " : "FAIL ")
              << outFile.getFullPathName().toRawUTF8()
              << "  (" << (int) (t1 - t0) << " ms)\n";
    return ok ? 0 : 1;
}
