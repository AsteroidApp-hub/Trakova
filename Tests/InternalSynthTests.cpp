// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

// Utawave — InternalSynth のユニットテスト (ADSR / アイドル早期return / ピッチホイール)
//
// オフラインで processBlock / noteOn / setPitchWheel を直接駆動して検証する。デバイス不要。
//   ・アイドル/消音ボイスは無音: 空 MIDI + 全ボイス Idle なら buffer は 1 サンプルも書かれない
//     ※ processBlock の早期 return は CPU 最適化のみで出力差はない (非最適化経路でも idle voice は
//        processVoice 冒頭で return し無音)。よって観測できるのは「idle voice が無音」という性質まで
//   ・発音中ボイスがあれば空 MIDI でも処理継続する
//   ・ADSR: アタック後にフル振幅 (~0.25 * velocity)、ノートオフ → リリースで Idle 復帰
//   ・velocity スケーリング (線形) / velocity 0 で無音 / velocity クランプ
//   ・ピッチホイール: ±2半音の周波数比、鳴動中ボイスへ即時反映、pw14 のクランプ
//   ・SR を変えても 440Hz ノートは ~440Hz (basePhaseInc = freq/sr)
//   ・MIDI イベント経路 (noteOn/noteOff/pitchWheel/allNotesOff) のディスパッチ
// 周波数は正方向ゼロ交差を数えて推定する (Sine は位相に依らず 1 周期 1 交差)。
// expect は ASCII。MIDI/buffer はテストローカル。

#include <JuceHeader.h>
#include <cmath>
#include "../Source/MIDI/InternalSynth.h"

namespace
{
class InternalSynthTests : public juce::UnitTest
{
public:
    InternalSynthTests() : juce::UnitTest("InternalSynth") {}

    // 発音中ボイスを空 MIDI で secs 秒処理し、正方向ゼロ交差から周波数 (Hz) を推定する。
    static double measureHz(InternalSynth& s, double sr, double secs)
    {
        const int n = (int) (sr * secs);
        juce::AudioBuffer<float> buf(1, n);
        buf.clear();
        juce::MidiBuffer empty;
        s.processBlock(buf, empty);
        const float* d = buf.getReadPointer(0);
        int crossings = 0;
        for (int i = 1; i < n; ++i)
            if (d[i - 1] < 0.0f && d[i] >= 0.0f) ++crossings;
        return (double) crossings / secs;
    }

    // 発音中ボイスを空 MIDI で secs 秒処理し、ピーク絶対振幅を返す。
    static float measurePeak(InternalSynth& s, double sr, double secs)
    {
        const int n = (int) (sr * secs);
        juce::AudioBuffer<float> buf(1, n);
        buf.clear();
        juce::MidiBuffer empty;
        s.processBlock(buf, empty);
        return buf.getMagnitude(0, 0, n);
    }

    void runTest() override
    {
        testIdleVoicesEmitNoOutput();
        testActiveVoiceProcessesOnEmptyMidi();
        testAttackReachesFullLevel();
        testVelocityScaling();
        testNoteOffReleaseReturnsToIdle();
        testPitchWheelFrequency();
        testPitchWheelClamp();
        testSampleRateIndependence();
        testMidiEventPath();
        testWaveformClamp();
    }

    // ── 空 MIDI + 全ボイス Idle → idle voice は 1 サンプルも書かない (buffer 不変) ──
    // (この性質は早期 return の有無に関わらず成り立つ。早期 return は CPU 最適化のみで出力差なし)
    void testIdleVoicesEmitNoOutput()
    {
        beginTest("processBlock: idle voices write no samples to the buffer");
        InternalSynth s;
        s.prepareToPlay(48000.0, 512);

        juce::AudioBuffer<float> buf(1, 1024);
        for (int i = 0; i < 1024; ++i) buf.setSample(0, i, 0.5f);   // sentinel
        juce::MidiBuffer empty;
        s.processBlock(buf, empty);

        bool untouched = true;
        for (int i = 0; i < 1024; ++i)
            if (buf.getSample(0, i) != 0.5f) { untouched = false; break; }
        expect(untouched, "idle synth with empty MIDI does not modify the buffer");
    }

    // ── 発音中ボイスがあれば空 MIDI でも処理する (リリース継続のため) ──
    void testActiveVoiceProcessesOnEmptyMidi()
    {
        beginTest("processBlock: active voice still processed when MIDI is empty");
        InternalSynth s;
        s.prepareToPlay(48000.0, 512);
        s.noteOn(69, 1.0f);
        const float peak = measurePeak(s, 48000.0, 0.05);
        expect(peak > 0.01f, "an active voice produces output even with empty MIDI");
    }

    // ── アタック後にフル振幅へ到達 (sine, velocity 1.0 → ~0.25) ──
    void testAttackReachesFullLevel()
    {
        beginTest("ADSR: after attack the level approaches 0.25 (sine, velocity 1.0)");
        InternalSynth s;
        s.prepareToPlay(48000.0, 512);
        s.setWaveform((int) InternalSynth::Waveform::Sine);
        s.noteOn(69, 1.0f);
        const float peak = measurePeak(s, 48000.0, 0.1);   // attack=1ms << 0.1s
        expect(std::abs(peak - 0.25f) < 0.01f, "full-envelope sine peaks near 0.25");
    }

    // ── velocity スケーリング: 線形 / 0 で無音 / >1 はクランプ ──
    void testVelocityScaling()
    {
        beginTest("velocity scales output linearly; 0 -> silent; >1 clamps");
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.noteOn(69, 0.5f);
            expect(std::abs(measurePeak(s, 48000.0, 0.1) - 0.125f) < 0.01f,
                   "velocity 0.5 peaks near 0.125 (half of full)");
        }
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.noteOn(69, 0.0f);
            expect(measurePeak(s, 48000.0, 0.1) < 1.0e-6f,
                   "velocity 0 produces silence");
        }
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.noteOn(69, 2.0f);   // clamps to 1.0
            expect(std::abs(measurePeak(s, 48000.0, 0.1) - 0.25f) < 0.01f,
                   "velocity > 1 clamps to full level (~0.25)");
        }
    }

    // ── ノートオフ → リリース減衰 → Idle 復帰 (次の空 MIDI で早期 return する) ──
    void testNoteOffReleaseReturnsToIdle()
    {
        beginTest("ADSR: noteOff triggers release, voice returns to idle (release=50ms)");
        InternalSynth s;
        s.prepareToPlay(48000.0, 512);
        s.noteOn(69, 1.0f);
        measurePeak(s, 48000.0, 0.05);   // 定常まで進める
        s.noteOff(69);
        // リリース 50ms (=2400 サンプル) を十分超える 0.1s を処理して Idle へ落とす
        measurePeak(s, 48000.0, 0.1);

        // 全ボイス Idle なら次の空 MIDI 処理で buffer は変化しない (idle voice は無音)
        juce::AudioBuffer<float> buf(1, 256);
        for (int i = 0; i < 256; ++i) buf.setSample(0, i, 0.5f);
        juce::MidiBuffer empty;
        s.processBlock(buf, empty);
        bool untouched = true;
        for (int i = 0; i < 256; ++i)
            if (buf.getSample(0, i) != 0.5f) { untouched = false; break; }
        expect(untouched, "after release the voice is idle (buffer untouched on next empty block)");
    }

    // ── ピッチホイール: センター 440 / +2半音 ~494 / -2半音 ~392、鳴動中に即時反映 ──
    void testPitchWheelFrequency()
    {
        beginTest("pitch wheel: +-2 semitone frequency ratio, applied to a sounding voice");
        // A4 = 440Hz。±2 半音は 2^(±2/12) = 1.1225 / 0.8909 → 493.9 / 392.0 Hz
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            s.setPitchWheel(8192);   // center
            expect(std::abs(measureHz(s, 48000.0, 1.0) - 440.0) <= 2.0, "center -> ~440 Hz");
        }
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            measureHz(s, 48000.0, 0.05);   // すでに発音中の状態を作る
            s.setPitchWheel(16383);        // +2 semitones
            expect(std::abs(measureHz(s, 48000.0, 1.0) - 493.9) <= 2.0,
                   "max wheel raises a sounding voice to ~494 Hz (immediate)");
        }
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            measureHz(s, 48000.0, 0.05);
            s.setPitchWheel(0);            // -2 semitones
            expect(std::abs(measureHz(s, 48000.0, 1.0) - 392.0) <= 2.0,
                   "min wheel lowers a sounding voice to ~392 Hz (immediate)");
        }
    }

    // ── pw14 の範囲外はクランプ (jlimit 0..16383) ──
    void testPitchWheelClamp()
    {
        beginTest("pitch wheel: out-of-range pw14 clamps to the +-2 semitone extremes");
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            s.setPitchWheel(999999);   // clamps to 16383 -> +2 semitones
            expect(std::abs(measureHz(s, 48000.0, 1.0) - 493.9) <= 2.0,
                   "pw14 above max clamps to +2 semitones");
        }
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            s.setPitchWheel(-500);     // clamps to 0 -> -2 semitones
            expect(std::abs(measureHz(s, 48000.0, 1.0) - 392.0) <= 2.0,
                   "pw14 below min clamps to -2 semitones");
        }
    }

    // ── SR を変えても 440Hz ノートは ~440Hz (basePhaseInc = freq/sr が正しい) ──
    void testSampleRateIndependence()
    {
        beginTest("note frequency is sample-rate independent (basePhaseInc = freq/sr)");
        InternalSynth s;
        s.prepareToPlay(44100.0, 512);
        s.setWaveform((int) InternalSynth::Waveform::Sine);
        s.noteOn(69, 1.0f);
        expect(std::abs(measureHz(s, 44100.0, 1.0) - 440.0) <= 2.0,
               "A4 measures ~440 Hz at 44100 Hz sample rate");
    }

    // ── MIDI イベント経路: noteOn/noteOff/pitchWheel/allNotesOff のディスパッチ ──
    // ノート操作を直接 API ではなく MidiBuffer 経由で渡し、processBlock のイベント分岐
    // (isNoteOn/isNoteOff/isPitchWheel/isAllNotesOff) と getPitchWheelValue 写像を行使する。
    void testMidiEventPath()
    {
        beginTest("processBlock: dispatches noteOn / noteOff / pitchWheel / allNotesOff from the MidiBuffer");

        // noteOn を MIDI 経由で。ブロック全体で発音すること
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            const int n = 4800;   // 0.1s
            juce::AudioBuffer<float> buf(1, n);
            buf.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
            s.processBlock(buf, midi);
            expect(buf.getMagnitude(0, 0, n) > 0.1f, "MIDI noteOn produces sound");
        }
        // pitchWheel を MIDI 経由で (isPitchWheel 分岐 + getPitchWheelValue 写像を行使)
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 48);   // +2 semitones via MIDI
            juce::AudioBuffer<float> warm(1, 256); warm.clear();
            s.processBlock(warm, midi);   // noteOn + pitchWheel をバッファ経由でディスパッチ
            expect(std::abs(measureHz(s, 48000.0, 1.0) - 493.9) <= 2.0,
                   "MIDI pitchWheel (max) bends a sounding voice to ~494 Hz");
        }
        // noteOff を MIDI 経由で → リリース後 Idle (isNoteOff 分岐を行使)
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            measurePeak(s, 48000.0, 0.05);   // 定常まで進める
            juce::AudioBuffer<float> b(1, 4800); b.clear();   // 4800 > release(2400)
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
            s.processBlock(b, off);   // noteOff をバッファ経由でディスパッチ → Idle へ

            juce::AudioBuffer<float> probe(1, 256);
            for (int i = 0; i < 256; ++i) probe.setSample(0, i, 0.5f);
            juce::MidiBuffer empty;
            s.processBlock(probe, empty);
            bool untouched = true;
            for (int i = 0; i < 256; ++i)
                if (probe.getSample(0, i) != 0.5f) { untouched = false; break; }
            expect(untouched, "MIDI noteOff releases the voice to idle");
        }
        // allNotesOff を MIDI 経由で → 以降 Idle (idle voice は無音)
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform((int) InternalSynth::Waveform::Sine);
            s.noteOn(69, 1.0f);
            measurePeak(s, 48000.0, 0.05);
            const int n = 256;
            juce::AudioBuffer<float> buf(1, n);
            buf.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            s.processBlock(buf, midi);

            juce::AudioBuffer<float> probe(1, 256);
            for (int i = 0; i < 256; ++i) probe.setSample(0, i, 0.5f);
            juce::MidiBuffer empty;
            s.processBlock(probe, empty);
            bool untouched = true;
            for (int i = 0; i < 256; ++i)
                if (probe.getSample(0, i) != 0.5f) { untouched = false; break; }
            expect(untouched, "MIDI allNotesOff silences all voices (idle afterwards)");
        }
    }

    // ── setWaveform のクランプ: 範囲外でもクラッシュせず発音する ──
    void testWaveformClamp()
    {
        beginTest("setWaveform clamps out-of-range index; all waveforms produce sound");
        for (int w : { 0, 1, 2, 5, -1 })   // 5 -> Square, -1 -> Sine にクランプ
        {
            InternalSynth s; s.prepareToPlay(48000.0, 512);
            s.setWaveform(w);
            s.noteOn(69, 1.0f);
            const float peak = measurePeak(s, 48000.0, 0.1);
            expect(peak > 0.1f && peak < 0.4f,
                   ("waveform index " + juce::String(w)
                    + " produces bounded output").toRawUTF8());
        }
    }
};

static InternalSynthTests internalSynthTests;
}
