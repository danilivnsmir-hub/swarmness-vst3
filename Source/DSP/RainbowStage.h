#pragma once

#include "DSPUtils.h"
#include "PitchVoice.h"
#include <array>
#include <vector>

/**
 * process() replaces the buffer with the harmony VOICES ONLY (built from the played note);
 * the processor mixes them in parallel with the STING path, so intervals never stack.
 *
 * HIVE (internally "rainbow"): two harmony voices with regeneration, inspired by
 * EarthQuaker's Rainbow Machine - but with one job per control, so it behaves predictably.
 *
 *  PITCH     : interval of the primary voice (DRONE), -12..+12 st, continuous or snapped
 *  PRIMARY   : DRONE level (added to the input)
 *  SECONDARY : QUEEN level - an octave of the DRONE (above for up-shifts, below for down)
 *  TONE      : brightness of the voices and of the trails
 *  TRACKING  : only the tracking character: high = tight, low = lag and repeating grains
 *  TRAILS    : regeneration of the DRONE. Every repeat is shifted by PITCH again, so the
 *              trail climbs / falls in even steps and fades out; it never self-oscillates
 *  TIME      : time between repeats (free, or a tempo division when synced)
 *
 * The VENOM footswitch (magicHeld) is the only way into self-oscillation: loop gain above
 * one plus an unshifted resonant path, tanh-limited so it never explodes.
 *
 * Spiral ceiling: the loop is band-limited (steep low-pass / high-pass), so a trail that
 * climbs or falls past the useful range fades out instead of turning into a squeal or rumble.
 *
 * RAW (default): FV-1-era character on top of the in-tune shifter - a slow random pitch warble
 * (deeper at low TRACKING), rougher splices and cheap-converter emulation. Off: clean engine.
 *
 * Naturalness: each voice drifts a few cents on its own slow random walk, DRONE sits slightly
 * left and QUEEN slightly right, and up-shifted voices are darkened in proportion to the
 * shift ("anti-chipmunk").
 */
class RainbowStage
{
public:
    static constexpr int kControlBlock = 32;
    static constexpr double kMaxRepeatSeconds = 2.0;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        for (auto* v : { &primary, &secondary })
        {
            v->prepare (sr, 2);
            v->setLoFi (26000.0f, 13.0f, 10000.0f);   // shared RAW voicing with STING
        }

        const int maxLag = (int) std::ceil (sr * 0.2) + 8;
        lagLine.setMaximumDelayInSamples (maxLag);
        lagLine.prepare ({ sr, (juce::uint32) juce::jmax (maxBlockSize, kControlBlock), 2 });

        const int size = juce::jmax (maxBlockSize, kControlBlock);
        primBuf.setSize (2, size, false, false, true);
        secBuf .setSize (2, size, false, false, true);
        inBuf  .setSize (2, size, false, false, true);

        onSmoothed.reset (sr, 0.03);   primLevel.reset (sr, 0.03);
        secLevel.reset (sr, 0.03);     loopGain.reset (sr, 0.05);
        resonanceSmoothed.reset (sr, 0.05);
        lagSmoothed.reset (sr, 0.15);
        onSmoothed.setCurrentAndTargetValue (0.0f);
        lagSmoothed.setCurrentAndTargetValue (0.0f);

        loopSize = juce::nextPowerOfTwo ((int) std::ceil (sr * kMaxRepeatSeconds) + 64);
        loopMask = loopSize - 1;
        for (auto& b : loopBuf)
            b.assign ((size_t) loopSize, 0.0f);
        loopDelay.setTime (sr / kControlBlock, 0.12);
        loopDelay.reset (loopDelayTarget);

        for (int ch = 0; ch < 2; ++ch)
        {
            loopLp[(size_t) ch].setType (swarm::SVF::Type::lowPass);
            loopLp[(size_t) ch].setParams (sr, 3500.0f, 0.7071f);
            loopHp[(size_t) ch].setType (swarm::SVF::Type::highPass);
            loopHp[(size_t) ch].setParams (sr, 45.0f, 0.7071f);
            subsonic[(size_t) ch].setType (swarm::SVF::Type::highPass);
            subsonic[(size_t) ch].setParams (sr, 38.0f, 0.7071f);
        }

        const double controlRate = sr / kControlBlock;
        for (auto& d : driftCents)
            d.setTime (controlRate, 0.35);

        for (auto& d : dc) d.prepare (sr);
        reset();
    }

    void reset()
    {
        primary.reset();
        secondary.reset();
        lagLine.reset();
        clearLoop();
        toneState = {};
        primLp = secLp = loopTone = { 0.0f, 0.0f };
        for (auto& f : loopLp) f.reset();
        for (auto& f : loopHp) f.reset();
        for (auto& f : subsonic) f.reset();
        for (auto& d : dc) d.reset();
        for (auto& d : driftCents) d.reset (0.0f);
        driftTarget = { 0.0f, 0.0f };
        driftCounter = 0;
        lastPrimRatio = lastSecRatio = 1.0f;
    }

    void setParams (bool on, float pitchSemis, float primary01, float secondary01, float tone01,
                    float tracking01, float trails01, float repeatSeconds, bool magicHeld, bool rawEngine = true,
                    float detuneCentsIn = 0.0f) noexcept
    {
        detuneCents = detuneCentsIn;
        primary  .setRaw (rawEngine);
        secondary.setRaw (rawEngine);
        // RAW: the Rainbow-Machine-style warble - deeper and slower as TRACKING goes down
        primary  .setRawCharacter (7.0f + 10.0f * (1.0f - tracking01), 4.0f, 0.5f);
        secondary.setRawCharacter (9.0f + 12.0f * (1.0f - tracking01), 3.3f, 0.5f);

        onSmoothed.setTargetValue (on ? 1.0f : 0.0f);
        pitch = pitchSemis;
        primLevel.setTargetValue (primary01);
        secLevel .setTargetValue (secondary01);
        toneCoeff = std::exp (-swarm::kTwoPi * (400.0f * std::pow (40.0f, tone01)) / (float) sampleRate);

        primary  .setTightness (tracking01);
        secondary.setTightness (tracking01);
        const float loose = 1.0f - tracking01;
        lagSmoothed.setTargetValue (loose * loose * 0.12f * (float) sampleRate);

        loopDelayTarget = (float) (juce::jlimit (0.02, kMaxRepeatSeconds - 0.01, (double) repeatSeconds) * sampleRate);

        // TRAILS: loop gain below one -> even, predictable decay. The footswitch goes past one.
        // VENOM: just past unity, so the trails swell and sustain instead of shrieking
        loopGain.setTargetValue (magicHeld ? 1.3f : 0.9f * std::pow (juce::jlimit (0.0f, 1.0f, trails01), 0.8f));
        resonanceSmoothed.setTargetValue (magicHeld ? 0.5f : 0.0f);
    }

    bool isActive() const noexcept { return onSmoothed.getCurrentValue() > 0.0f || onSmoothed.isSmoothing(); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        if (! isActive())
        {
            // Keep the loop quiet so re-engaging starts clean.
            if (! loopCleared)
                clearLoop();
            for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
                juce::FloatVectorOperations::clear (audio[ch], numSamples);
            return;
        }

        loopCleared = false;
        numChannels = juce::jmin (numChannels, 2);

        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = juce::jmin (kControlBlock, numSamples - start);
            const float loopD = juce::jmax ((float) (kControlBlock + 2), loopDelay.process (loopDelayTarget));

            // Humanise: slow random pitch drift, independent per voice
            if (++driftCounter >= (int) (0.4 * sampleRate / kControlBlock))
            {
                driftCounter = 0;
                driftTarget[0] = 3.0f * rng.nextBipolar();
                driftTarget[1] = 6.0f * rng.nextBipolar();
            }
            // DETUNE spreads the voices: DRONE up, QUEEN down (on top of the natural drift)
            const float primCents = driftCents[0].process (driftTarget[0]) + detuneCents;
            const float secCents  = driftCents[1].process (driftTarget[1]) - detuneCents;

            const float baseRatio = std::pow (2.0f, pitch / 12.0f);
            const float primRatio = baseRatio * std::pow (2.0f, primCents / 1200.0f);
            const float secRatio  = (pitch >= 0.0f ? baseRatio * 2.0f : baseRatio * 0.5f) * std::pow (2.0f, secCents / 1200.0f);

            // Anti-chipmunk: darken up-shifted voices in proportion to the shift
            const auto voiceLpCoeff = [this] (float ratio)
            {
                const float hz = 16000.0f / std::pow (juce::jmax (1.0f, ratio), 0.9f);
                return std::exp (-swarm::kTwoPi * hz / (float) sampleRate);
            };
            const bool raw = primary.isRaw();   // RAW keeps the converters' own darkness instead
            const float primLpCoeff = raw ? 0.0f : voiceLpCoeff (primRatio);
            const float secLpCoeff  = raw ? 0.0f : voiceLpCoeff (secRatio);

            // Shifter input = (lagged) input + the regeneration loop, so every repeat is shifted again.
            for (int i = 0; i < n; ++i)
            {
                const float lag = lagSmoothed.getNextValue();
                const float g = loopGain.getNextValue();
                lagLine.setDelay (lag);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    lagLine.pushSample (ch, audio[ch][start + i]);
                    const float x = lagLine.popSample (ch);
                    const float fb = readLoop (ch, (float) (loopWrite + i) - loopD);
                    const float in = x + g * 0.5f * std::tanh (2.0f * fb);
                    inBuf  .setSample (ch, i, in);
                    primBuf.setSample (ch, i, in);
                    secBuf .setSample (ch, i, in);
                }
            }

            float* p[2] = { primBuf.getWritePointer (0), primBuf.getWritePointer (numChannels > 1 ? 1 : 0) };
            float* s[2] = { secBuf .getWritePointer (0), secBuf .getWritePointer (numChannels > 1 ? 1 : 0) };
            primary  .process (p, numChannels, n, lastPrimRatio, primRatio);
            secondary.process (s, numChannels, n, lastSecRatio,  secRatio);
            lastPrimRatio = primRatio;
            lastSecRatio  = secRatio;

            for (int i = 0; i < n; ++i)
            {
                const float on = onSmoothed.getNextValue();
                const float pl = primLevel.getNextValue();
                const float sl = secLevel.getNextValue();
                const float res = resonanceSmoothed.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& plp = primLp[(size_t) ch];
                    auto& slp = secLp[(size_t) ch];
                    const float pvRaw = primBuf.getSample (ch, i);
                    plp = pvRaw + primLpCoeff * (plp - pvRaw);
                    const float svRaw = secBuf.getSample (ch, i);
                    slp = svRaw + secLpCoeff * (slp - svRaw);

                    // Stereo placement: DRONE slightly left, QUEEN slightly right
                    const bool right = numChannels > 1 && ch == 1;
                    const float pPan = right ? 0.72f : 1.0f;
                    const float sPan = numChannels > 1 && ! right ? 0.72f : 1.0f;

                    const float voices = subsonic[(size_t) ch].process (plp * pl * pPan + slp * sl * sPan);
                    auto& z = toneState[(size_t) ch];
                    z = voices + toneCoeff * (z - voices);

                    // Loop: only the DRONE (so each repeat is one more PITCH step), TONE-filtered and
                    // band-limited (spiral ceiling). The unshifted resonance only exists while VENOM is held.
                    const float fbIn = plp + res * inBuf.getSample (ch, i);
                    auto& lt = loopTone[(size_t) ch];
                    lt = fbIn + toneCoeff * (lt - fbIn);
                    const float banded = loopHp[(size_t) ch].process (loopLp[(size_t) ch].process (lt));
                    loopBuf[(size_t) ch][(size_t) ((loopWrite + i) & loopMask)] = dc[(size_t) ch].process (banded);

                    // The stage outputs the voices only; the processor adds them to the STING path.
                    audio[ch][start + i] = on * 0.9f * std::tanh (z * (1.0f / 0.9f));
                }
            }

            loopWrite = (loopWrite + n) & loopMask;
        }
    }

private:
    void clearLoop() noexcept
    {
        for (auto& b : loopBuf)
            std::fill (b.begin(), b.end(), 0.0f);
        loopCleared = true;
    }

    float readLoop (int ch, float pos) const noexcept
    {
        const float fl = std::floor (pos);
        const int i0 = (int) fl;
        const float frac = pos - fl;
        const auto& b = loopBuf[(size_t) ch];
        const float a = b[(size_t) (i0 & loopMask)];
        return a + frac * (b[(size_t) ((i0 + 1) & loopMask)] - a);
    }

    double sampleRate = 44100.0;
    PitchVoice primary, secondary;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> lagLine { 1 };
    juce::AudioBuffer<float> primBuf, secBuf, inBuf;
    juce::SmoothedValue<float> onSmoothed, primLevel, secLevel, loopGain, resonanceSmoothed, lagSmoothed;

    std::array<std::vector<float>, 2> loopBuf;
    int loopSize = 0, loopMask = 0, loopWrite = 0;
    bool loopCleared = false;
    float loopDelayTarget = 8000.0f;
    swarm::OnePole loopDelay;
    std::array<float, 2> toneState {}, loopTone {}, primLp {}, secLp {};
    std::array<swarm::SVF, 2> loopLp, loopHp, subsonic;
    std::array<swarm::DCBlocker, 2> dc;

    std::array<swarm::OnePole, 2> driftCents;
    std::array<float, 2> driftTarget {};
    int driftCounter = 0;
    swarm::FastRandom rng { 0x5EED1E5u };

    float pitch = 7.0f, toneCoeff = 0.0f, detuneCents = 0.0f;
    float lastPrimRatio = 1.0f, lastSecRatio = 1.0f;
};
