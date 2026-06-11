// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#include "PitchEngine.h"
#include "pitchcore/PitchShiftEngine.h"
#include <vector>
#include <cstring>

namespace PitchEngine
{
    bool shiftBuffer(const juce::AudioBuffer<float>& input,
                     double sampleRate,
                     double semitones,
                     juce::AudioBuffer<float>& output,
                     const std::function<bool(double)>& onProgress)
    {
        const int numCh = input.getNumChannels();
        const int inLen = input.getNumSamples();
        if (numCh <= 0 || inLen <= 0 || sampleRate <= 0.0) return false;

        std::vector<const float*> ptrs((size_t) numCh, nullptr);
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[(size_t) ch] = input.getReadPointer(ch);

        std::vector<std::vector<float>> result;
        if (! pitchcore::shiftPitch(ptrs.data(), numCh, inLen, sampleRate,
                                    semitones, result, onProgress))
            return false;

        output.setSize(numCh, inLen);
        for (int ch = 0; ch < numCh; ++ch)
            std::memcpy(output.getWritePointer(ch), result[(size_t) ch].data(),
                        (size_t) inLen * sizeof(float));
        return true;
    }

    bool processFile(const juce::File& inputFile,
                     const juce::File& outputFile,
                     juce::AudioFormatManager& fmt,
                     double semitones,
                     int outputBits,
                     std::function<void(double)> onProgress)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(inputFile));
        if (! reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return false;

        const int numCh = (int) reader->numChannels;
        if (reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max())
            return false;
        const int totalSamples = (int) reader->lengthInSamples;
        const double sampleRate = reader->sampleRate;

        juce::AudioBuffer<float> inBuf(numCh, totalSamples);
        reader->read(&inBuf, 0, totalSamples, 0, true, true);

        juce::AudioBuffer<float> outBuf;
        if (! shiftBuffer(inBuf, sampleRate, semitones, outBuf,
                          [&onProgress](double p)
                          { if (onProgress) onProgress(p); return true; }))
            return false;

        auto stream = std::make_unique<juce::FileOutputStream>(outputFile);
        if (! stream->openedOk()) return false;
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat wav;
        const bool useFloat = (outputBits >= 32);
        auto opts = juce::AudioFormatWriterOptions{}
                        .withSampleRate(sampleRate)
                        .withNumChannels((juce::uint32) numCh)
                        .withBitsPerSample(outputBits)
                        .withSampleFormat(useFloat
                            ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                            : juce::AudioFormatWriterOptions::SampleFormat::integral);
        std::unique_ptr<juce::OutputStream> outStream = std::move(stream);
        auto writer = wav.createWriterFor(outStream, opts);
        if (writer == nullptr) return false;
        writer->writeFromAudioSampleBuffer(outBuf, 0, outBuf.getNumSamples());
        writer->flush();
        return true;
    }
}
