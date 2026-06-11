// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Utawave

#include "InternalSynth.h"

InternalSynth::InternalSynth() = default;

void InternalSynth::prepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate;
    // エンベロープ係数は sr + 固定 attackS/releaseS のみに依存するため、ここで 1 度だけ算出する
    aStep = 1.0f / juce::jmax(1.0f, attackS  * (float)sr);
    rStep = 1.0f / juce::jmax(1.0f, releaseS * (float)sr);
    allNotesOff();
}

void InternalSynth::releaseResources()
{
    allNotesOff();
}

void InternalSynth::allNotesOff()
{
    for (auto& v : voices)
    {
        v.note = -1;
        v.stage = Stage::Idle;
        v.envelope = 0.0f;
        v.phase = 0.0;
    }
}

void InternalSynth::noteOn(int midiNote, float velocity)
{
    // 空き、または同じノートを再アタックするボイスを探す
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.note == midiNote) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (v.stage == Stage::Idle) { target = &v; break; }
    if (target == nullptr)
    {
        // 最古ボイスを奪う（Release 中を優先、なければ Sustain）
        target = &voices[0];
        for (auto& v : voices)
            if (v.stage == Stage::Release) { target = &v; break; }
    }

    const double freq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
    target->note         = midiNote;
    target->velocity     = juce::jlimit(0.0f, 1.0f, velocity);
    target->phase        = 0.0;
    target->basePhaseInc = freq / sr;
    target->phaseInc     = target->basePhaseInc * pitchBendRatio;
    target->stage        = Stage::Attack;
}

void InternalSynth::setPitchWheel(int pw14)
{
    // -8192..+8191 を ±range 半音にマップ
    const double norm  = (juce::jlimit(0, 16383, pw14) - 8192) / 8192.0;
    const double semis = norm * kPitchBendRangeSemis;
    pitchBendRatio = std::pow(2.0, semis / 12.0);
    // 鳴っているボイスへ即時反映
    for (auto& v : voices)
        if (v.stage != Stage::Idle)
            v.phaseInc = v.basePhaseInc * pitchBendRatio;
}

void InternalSynth::noteOff(int midiNote)
{
    for (auto& v : voices)
        if (v.note == midiNote
            && v.stage != Stage::Idle
            && v.stage != Stage::Release)
            v.stage = Stage::Release;
}

// PolyBLEP: 位相不連続点を帯域制限してエイリアシングを抑える補正項。
// dt = 1サンプル当たりの位相増分 (= freq / sampleRate)。
// 詳細は Välimäki "Polynomial Transition Regions for the Bandlimited Polynomial Anti-aliasing"
// もしくは Antti Huovilainen の polyBLEP 実装を参照。
static inline double polyBlep(double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

float InternalSynth::oscillate(Waveform w, double phase, double dt) const
{
    const double p = phase - std::floor(phase);
    switch (w)
    {
        case Waveform::Sine:
            return (float) std::sin(p * juce::MathConstants<double>::twoPi);
        case Waveform::Saw:
        {
            // ナイーブ Saw: 2p - 1。位相 0 の不連続点を polyBLEP で補正
            double s = 2.0 * p - 1.0;
            s -= polyBlep(p, dt);
            return (float) s;
        }
        case Waveform::Square:
        {
            // ナイーブ Square: p < 0.5 → +1、else -1。
            // 0 と 0.5 の二つの不連続点を polyBLEP で補正 (符号は逆)
            double s = (p < 0.5) ? 1.0 : -1.0;
            s += polyBlep(p, dt);
            // 0.5 シフトした位相で 2 つ目の不連続点
            double p2 = p + 0.5;
            if (p2 >= 1.0) p2 -= 1.0;
            s -= polyBlep(p2, dt);
            return (float) s;
        }
    }
    return 0.0f;
}

void InternalSynth::processVoice(Voice& v, juce::AudioBuffer<float>& buffer,
                                  int startSample, int numSamples)
{
    if (v.stage == Stage::Idle) return;

    auto* l = buffer.getWritePointer(0, startSample);
    auto* r = buffer.getNumChannels() >= 2 ? buffer.getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Attack → Hold → Release（D/S は廃止）
        switch (v.stage)
        {
            case Stage::Attack:
                v.envelope += aStep;
                if (v.envelope >= 1.0f) { v.envelope = 1.0f; v.stage = Stage::Hold; }
                break;
            case Stage::Hold:
                v.envelope = 1.0f;
                break;
            case Stage::Release:
                v.envelope -= rStep;
                if (v.envelope <= 0.0f) { v.envelope = 0.0f; v.stage = Stage::Idle; v.note = -1; }
                break;
            default: break;
        }
        if (v.stage == Stage::Idle) break;

        const float s = oscillate(waveform, v.phase, v.phaseInc) * v.envelope * v.velocity * 0.25f;
        l[i] += s;
        if (r) r[i] += s;
        v.phase += v.phaseInc;
        if (v.phase >= 1.0) v.phase -= 1.0;
    }
}

void InternalSynth::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int total = buffer.getNumSamples();
    if (total <= 0) return;

    // 無音 MIDI トラックは早期 return: 処理する MIDI イベントが無く、かつ発音中ボイスも無いとき。
    // (どちらか一方でもあれば従来通り処理する。可聴な挙動は変わらない)
    if (midi.isEmpty())
    {
        bool anyActive = false;
        for (auto& v : voices)
            if (v.stage != Stage::Idle) { anyActive = true; break; }
        if (!anyActive) return;
    }

    int lastSample = 0;
    for (const auto evt : midi)
    {
        const int pos = juce::jlimit(0, total, evt.samplePosition);
        const int n = pos - lastSample;
        if (n > 0)
            for (auto& v : voices) processVoice(v, buffer, lastSample, n);

        const auto& m = evt.getMessage();
        if (m.isNoteOn())            noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())       noteOff(m.getNoteNumber());
        else if (m.isPitchWheel())    setPitchWheel(m.getPitchWheelValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();

        lastSample = pos;
    }
    const int tail = total - lastSample;
    if (tail > 0)
        for (auto& v : voices) processVoice(v, buffer, lastSample, tail);
}
