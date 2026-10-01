#pragma once

#include <JuceHeader.h>
#include "NamRunner.h"
#include "NoiseGate.h"

#include <array>
#include <cmath>
#include <complex>
#include <vector>

/**
 * WASP - an overdrive / boost for the front of an amp, with a low-end tightening control and a
 * gate, in four characters (one circuit, four sets of values - see Tune):
 *
 *   TIGHT   the modern "precision" metal drive: a non-inverting op-amp gain stage (the gain leg,
 *           R + 47 nF, only boosts the upper mids and up; soft diodes in its feedback round the
 *           first bit of overload), then a hard clipper - a small series resistor into diodes to
 *           ground, more diodes on one side than the other, so the negative half flattens first:
 *           square, asymmetric (even harmonics) clipping, not the smooth compression of a classic
 *           overdrive. Fitted to captures of the real pedal.
 *   BOOST   a transparent clean boost: the gain leg is wide open (flat down to the lows), no
 *           clipper, only the op-amp's rails and a pair of far-off diodes when it is pushed hard.
 *   SMOOTH  the classic mid-focused overdrive: the same mid-boosting gain leg, a pair of silicon
 *           diodes in the feedback - symmetric, soft - and a darker tone filter; no hard clipper.
 *   RASP    a hard-clipping distortion: more gain from a wide leg, nothing in the feedback, a
 *           symmetric silicon pair to ground behind a resistor, a strong low-pass tone - grainy
 *           and saturated, fuzz-like at the top of DRIVE.
 *
 * The circuit, solved per sample (oversampled): ATTACK (a 2nd-order high-pass, 20 Hz .. 500 Hz)
 * -> op-amp gain with feedback diodes -> the rail knee -> the hard clipper -> BRIGHT (a tilt
 * around the tone corner) -> a two-pole top roll-off. GATE is a noise gate keyed from the input,
 * VOLUME the output level (5 = about unity at DRIVE 3).
 *
 * NAM mode runs a loaded pedal capture (.nam) instead, with its own INPUT / OUTPUT trims (5 = the
 * capture as it is: its own gain is the pedal's) and the same GATE.
 */
class DriveBlock
{
public:
    struct Settings
    {
        bool on = false;
        float volume = 0.5f, drive = 0.3f, bright = 0.5f, attack = 0.5f;   // 0..1 (knob 0..10)
        float gate = 0.0f;                                                  // 0..1, 0 = off
        int character = 0;                                                  // tight, boost, smooth, rasp
        bool nam = false;                                                   // a pedal capture instead of WASP
        float namInput = 0.5f, namOutput = 0.5f;                            // 0..1, 5 = as captured (+-18 dB)
    };

    enum Character : int { tight = 0, boost, smooth, rasp, numCharacters };
    static const char* characterName (int c) noexcept
    {
        static const char* names[] { "TIGHT", "BOOST", "SMOOTH", "RASP" };
        return names[juce::jlimit (0, numCharacters - 1, c)];
    }

    /** The circuit's values - one set per character (TIGHT's are fitted to captures of the real pedal;
        SwarmnessAmpLab --set wasp:k=v edits them). */
    struct Tune
    {
        // TIGHT: fitted (SwarmnessAmpLab + CMA-ES) to three captures of the real pedal: waveforms at four
        // levels, H2 / H3 / H5 and compression over 40 dB of input, the small-signal response
        float rfMin = 5.539e+04f;        // DRIVE at 0 still has gain
        float rfSpan = 500000.0f;        // the DRIVE pot
        float legOhms = 4707.0f;      // gain leg (with legFarads: its corner)
        float legFarads = 47.0e-9f;
        float fbIs = 1.616e-07f;         // feedback diodes: soft, early
        float fbAsym = 0.907f;
        float hardRs = 516.8f;        // then the hard clipper: series resistor into diodes to ground (0 = none)
        float hardIs = 1.581e-07f;
        float hardNpos = 2.338f, hardNneg = 0.7015f;   // asymmetric: the negative side flattens first
        float toneHz = 754.4f, brightLo = -10.29f, brightSpan = 20.0f;
        float outBase = 2063.0f, outSpan = 5000.0f;
        float makeup = 3.4f;          // VOLUME 5 at DRIVE ~3 = unity
        float inDb = -14.96f;         // level into the circuit (vs our 0 dBFS = 2.5 V)
    };
    static std::array<Tune, numCharacters>& tunes() noexcept
    {
        static std::array<Tune, numCharacters> t = []
        {
            std::array<Tune, numCharacters> a {};
            // BOOST: a wide-open gain leg (34 Hz corner), +6 .. +25 dB, no clipper, diodes far off
            auto& b = a[boost];
            b.rfMin = 5000.0f; b.rfSpan = 75000.0f; b.legOhms = 4700.0f; b.legFarads = 1.0e-6f;
            b.fbIs = 1.0e-12f; b.fbAsym = 1.0f; b.hardRs = 0.0f;
            b.toneHz = 2000.0f; b.brightLo = -4.0f; b.brightSpan = 8.0f;
            b.outBase = 8000.0f; b.outSpan = 6000.0f;
            b.makeup = 2.8f;
            // SMOOTH: the classic 4.7k + 47 nF leg, 51k + 500k of gain, a symmetric silicon pair in the feedback
            auto& m = a[smooth];
            m.rfMin = 51000.0f; m.rfSpan = 500000.0f; m.legOhms = 4700.0f; m.legFarads = 47.0e-9f;
            m.fbIs = 2.5e-9f; m.fbAsym = 1.0f; m.hardRs = 0.0f;
            m.toneHz = 720.0f; m.brightLo = -14.0f; m.brightSpan = 20.0f;
            m.outBase = 2500.0f; m.outSpan = 5000.0f;
            m.makeup = 2.5f;
            // RASP: a wide 1k + 1 uF leg (160 Hz), 20k + 300k of gain, nothing in the feedback, a symmetric
            // silicon pair to ground behind 1k, a strong tone low-pass
            auto& r = a[rasp];
            r.rfMin = 5000.0f; r.rfSpan = 300000.0f; r.legOhms = 1000.0f; r.legFarads = 1.0e-6f;
            r.fbIs = 1.0e-14f; r.fbAsym = 1.0f;
            r.hardRs = 1000.0f; r.hardIs = 2.5e-9f; r.hardNpos = 1.0f; r.hardNneg = 1.0f;
            r.toneHz = 1000.0f; r.brightLo = -16.0f; r.brightSpan = 24.0f;
            r.outBase = 3000.0f; r.outSpan = 5000.0f;
            r.makeup = 1.3f;
            return a;
        }();
        return t;
    }
    static Tune& tuneFor (int character) noexcept { return tunes()[(size_t) juce::jlimit (0, numCharacters - 1, character)]; }
    /** TIGHT's values (the lab tool edits them). */
    static Tune& tune() noexcept { return tuneFor (tight); }

    //==============================================================================
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        osLog2 = sampleRate < 60000.0 ? 2 : (sampleRate < 120000.0 ? 1 : 0);
        fsOs = fs * (1 << osLog2);
        oversampler.reset();
        if (osLog2 > 0)
        {
            oversampler = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) 2, (size_t) osLog2,
                                                                            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversampler->initProcessing ((size_t) maxBlock);
        }
        dryCopy.setSize (2, maxBlock, false, false, true);
        namCopy.setSize (2, maxBlock, false, false, true);
        namRunner.prepare (sampleRate, maxBlock);
        noiseGate.prepare (sampleRate, maxBlock, kGateLookaheadSeconds);
        reset();
    }

    void reset()
    {
        for (auto& c : ch)
            c = {};
        if (oversampler != nullptr) oversampler->reset();
        noiseGate.reset();
        attackHz = attackFor (settings.attack);
        activeCharacter = settings.character;
        characterGain.reset (fs, 0.006);
        characterGain.setCurrentAndTargetValue (1.0f);
        onGain.reset (fs, 0.02);
        onGain.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        volume.reset (fs, 0.03);
        volume.setCurrentAndTargetValue (volumeGain (settings.volume));
        namRunner.reset();
        namMix.reset (fs, 0.03);
        namMix.setCurrentAndTargetValue (settings.nam && namRunner.hasModel() ? 1.0f : 0.0f);
    }

    void setParams (const Settings& s) noexcept { settings = s; }

    /** NAM mode: a pedal capture (message thread; nullptr clears). */
    void setNamModel (std::unique_ptr<NamModel> model) { namRunner.setModel (std::move (model)); }
    bool hasNamModel() const noexcept { return namRunner.hasModel(); }
    void setNamSize (double size01) { namRunner.setSize (size01); }
    void releaseRetired() { namRunner.releaseRetired(); }
    static float namKnobDb (float knob) noexcept { return (knob - 0.5f) * 36.0f; }

    //==============================================================================
    /** The gate's lookahead: the block always delays its audio by this much. */
    static constexpr double kGateLookaheadSeconds = 0.001;
    int getLatencySamples() const noexcept { return noiseGate.lookaheadSamples(); }

    void process (float* const* audio, int numCh, int numSamples) noexcept
    {
        numCh = juce::jmin (numCh, 2);
        namRunner.pickUp();
        // the gate: keyed from the input before the lookahead delay (always applied, so the latency is
        // constant), the curve applied after the drive
        const float* gateCurve = noiseGate.compute (audio, numCh, numSamples, settings.gate);
        noiseGate.delayInPlace (audio, numCh, numSamples);

        onGain.setTargetValue (settings.on ? 1.0f : 0.0f);
        if (! settings.on && ! onGain.isSmoothing())
        {
            onGain.setCurrentAndTargetValue (0.0f);
            return;   // off: the (delayed) signal passes untouched
        }
        for (int c = 0; c < numCh; ++c)
            dryCopy.copyFrom (c, 0, audio[c], numSamples);

        const bool useNam = settings.nam && namRunner.isActive();
        namMix.setTargetValue (useNam ? 1.0f : 0.0f);
        const bool runNam = useNam || namMix.isSmoothing();
        const bool runWasp = ! useNam || namMix.isSmoothing();
        if (runNam)
        {
            for (int c = 0; c < numCh; ++c)
                namCopy.copyFrom (c, 0, audio[c], numSamples);
            float* n[2] { namCopy.getWritePointer (0), namCopy.getWritePointer (1) };
            namRunner.process (n, numCh, numSamples, juce::Decibels::decibelsToGain (namKnobDb (settings.namInput)),
                               juce::Decibels::decibelsToGain (namKnobDb (settings.namOutput)));
        }

        if (runWasp)
        {
            // a character switch: fade out (~6 ms), swap the circuit, fade back in
            if (settings.character != activeCharacter)
            {
                characterGain.setTargetValue (0.0f);
                if (characterGain.getCurrentValue() <= 0.001f)
                {
                    activeCharacter = settings.character;
                    for (auto& c : ch)
                        c = {};
                    characterGain.setTargetValue (1.0f);
                }
            }
            else
                characterGain.setTargetValue (1.0f);
            updateCoefficients (numSamples);
            juce::dsp::AudioBlock<float> block (audio, (size_t) numCh, (size_t) numSamples);
            float* os[2] { audio[0], numCh > 1 ? audio[1] : nullptr };
            int nOs = numSamples;
            if (oversampler != nullptr)
            {
                auto up = oversampler->processSamplesUp (block);
                nOs = (int) up.getNumSamples();
                for (int c = 0; c < numCh; ++c)
                    os[c] = up.getChannelPointer ((size_t) c);
            }
            for (int c = 0; c < numCh; ++c)
            {
                auto& st = ch[(size_t) c];
                for (int i = 0; i < nOs; ++i)
                    os[c][i] = processSample (st, os[c][i]);
            }
            if (oversampler != nullptr)
                oversampler->processSamplesDown (block);
        }

        volume.setTargetValue (volumeGain (settings.volume));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = volume.getNextValue() * characterGain.getNextValue();
            const float gate = gateCurve != nullptr ? gateCurve[i] : 1.0f;
            const float on = onGain.getNextValue();
            const float m = namMix.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                float wet = 0.0f;
                if (runWasp)
                {
                    auto& st = ch[(size_t) c];
                    // DC blocker (the clipper is symmetric, but the ATTACK filter sweep can leave a step)
                    const float x = audio[c][i];
                    const float y = x - st.dcX + dcR * st.dcY;
                    st.dcX = x; st.dcY = y;
                    wet = y * g;
                }
                if (runNam)
                    wet = runWasp ? wet + m * (namCopy.getSample (c, i) - wet) : namCopy.getSample (c, i);
                audio[c][i] = dryCopy.getSample (c, i) + on * (wet * gate - dryCopy.getSample (c, i));
            }
        }
    }

    //==============================================================================
    /** VOLUME knob (0..1) in dB: 0 at noon, +12 dB at 10, -30 dB just above 0 (0 = silent). */
    static float volumeDb (float v) noexcept { return v >= 0.5f ? (v - 0.5f) * 24.0f : (v - 0.5f) * 60.0f; }
    static float attackFor (float v) noexcept { return 20.0f * std::pow (25.0f, v); }
    static float feedbackOhms (float drive, const Tune& t) noexcept
    {
        const float taper = (std::exp (4.0f * drive) - 1.0f) / (std::exp (4.0f) - 1.0f);   // audio taper pot
        return t.rfMin + t.rfSpan * taper;
    }

    int getOversampling() const noexcept { return 1 << osLog2; }

    /** Small-signal response (diodes not conducting) of the whole pedal at VOLUME 5, dB (editor / tests). */
    static float responseDb (const Settings& s, double hz) noexcept
    {
        using cd = std::complex<double>;
        const double w = 2.0 * juce::MathConstants<double>::pi * hz;
        const cd jw (0.0, w);
        const double wa = 2.0 * juce::MathConstants<double>::pi * attackFor (s.attack);
        const cd hp = (jw * jw) / (jw * jw + jw * wa / 0.7071 + wa * wa);
        const auto& t = tuneFor (s.character);
        const double rf = feedbackOhms (s.drive, t);
        const cd zLeg = (double) t.legOhms + 1.0 / (jw * (double) t.legFarads);
        const cd zF = rf / (1.0 + jw * rf * 51.0e-12);
        const cd core = 1.0 + zF / zLeg;
        const cd lp = 1.0 / (1.0 + jw / (2.0 * juce::MathConstants<double>::pi * t.toneHz));
        const double bg = juce::Decibels::decibelsToGain (t.brightLo + t.brightSpan * s.bright);
        const cd tone = lp + bg * (1.0 - lp);
        const cd out1 = 1.0 / (1.0 + jw / (2.0 * juce::MathConstants<double>::pi * (t.outBase + t.outSpan * s.bright)));
        const cd out = out1 * out1;
        return (float) (20.0 * std::log10 (std::abs (hp * core * tone * out) * t.makeup * juce::Decibels::decibelsToGain (t.inDb) + 1.0e-12));
    }

    /** Static clipping curve above the leg's corner: output volts for input volts (editor / tests). */
    static float transferVolts (float drive, float xVolts, int character = tight) noexcept
    {
        const auto& t = tuneFor (character);
        const float invR = 1.0f / feedbackOhms (drive, t), i = xVolts / t.legOhms;
        float v = 0.0f;
        for (int it = 0; it < 60; ++it)
        {
            const float e = std::exp (juce::jlimit (-40.0f, 40.0f, v / kNVt));
            const float f = v * invR + t.fbIs * (e - 1.0f) - t.fbIs * t.fbAsym * (1.0f / e - 1.0f) - i;
            const float d = invR + (t.fbIs * e + t.fbIs * t.fbAsym / e) / kNVt;
            const float step = juce::jlimit (-0.1f, 0.1f, f / d);
            v -= step;
            if (std::abs (step) < 1.0e-7f)
                break;
        }
        float y = railKnee (xVolts + v);
        float h = 0.0f;
        for (int it = 0; it < 60 && t.hardRs > 0.0f; ++it)
            if (hardStep (h, y, 1.0f / t.hardRs, t) < 1.0e-7f)
                break;
        return t.hardRs > 0.0f ? h : y;
    }

    /** The op-amp's output swings to ~4.2 V on a 9 V supply: a soft knee above 3 V. */
    static float railKnee (float y) noexcept
    {
        return std::abs (y) > 3.0f ? std::copysign (3.0f + 1.2f * std::tanh ((std::abs (y) - 3.0f) / 1.2f), y) : y;
    }
    /** One Newton step of the hard clipper: (h - y) / Rs + Is (e^(h/Np nVt) - 1) - Is (e^(-h/Nn nVt) - 1) = 0. */
    static float hardStep (float& h, float y, float invRs, const Tune& t) noexcept
    {
        const float np = t.hardNpos * kNVt, nn = t.hardNneg * kNVt;
        const float ep = std::exp (juce::jlimit (-40.0f, 40.0f, h / np)), en = std::exp (juce::jlimit (-40.0f, 40.0f, -h / nn));
        const float f = (h - y) * invRs + t.hardIs * (ep - 1.0f) - t.hardIs * (en - 1.0f);
        const float d = invRs + t.hardIs * (ep / np + en / nn);
        const float step = juce::jlimit (-0.2f, 0.2f, f / d);
        h -= step;
        return std::abs (step);
    }
    static constexpr float kVolts = 2.5f;          // 0 dBFS = 2.5 V (same scale as AMP)


private:
    struct ChannelState
    {
        float hpX1 = 0.0f, hpX2 = 0.0f, hpY1 = 0.0f, hpY2 = 0.0f;   // ATTACK (2nd-order high-pass)
        float legX = 0.0f, legY = 0.0f;                             // 720 Hz gain leg
        float v = 0.0f;                                             // diode voltage (Newton warm start)
        float h = 0.0f;                                             // hard clipper node (warm start)
        float cf = 0.0f;                                            // feedback cap
        float toneLp = 0.0f, outLp = 0.0f, outLp2 = 0.0f;
        float dcX = 0.0f, dcY = 0.0f;
    };

    static constexpr float kNVt = 1.752f * 0.02585f;

    float processSample (ChannelState& st, float in) noexcept
    {
        const float x0 = in * kVolts * inGain;

        // ATTACK: 2nd-order high-pass in front of everything
        const float x = hb0 * x0 + hb1 * st.hpX1 + hb2 * st.hpX2 - ha1 * st.hpY1 - ha2 * st.hpY2;
        st.hpX2 = st.hpX1; st.hpX1 = x0;
        st.hpY2 = st.hpY1; st.hpY1 = x;

        // gain leg: 4.7k + 47 nF to ground from the inverting input -> current = HP720(x) / 4.7k
        const float leg = legA * (st.legY + x - st.legX);
        st.legX = x; st.legY = leg;
        const float i = leg * invLeg;

        // feedback: Rf || two diodes; solve i = v/Rf + 2 Is sinh(v / nVt)
        float v = st.v;
        for (int it = 0; it < 6; ++it)
        {
            const float e = std::exp (juce::jlimit (-40.0f, 40.0f, v / kNVt));
            const float f = v * invRf + fbIs * (e - 1.0f) - fbIsNeg * (1.0f / e - 1.0f) - i;
            const float d = invRf + (fbIs * e + fbIsNeg / e) / kNVt;
            const float step = juce::jlimit (-0.1f, 0.1f, f / d);
            v -= step;
            if (std::abs (step) < 1.0e-6f)
                break;
        }
        st.v = v;

        // 51 pF across the feedback: the clipped part loses its top as Rf grows
        st.cf += cfA * (v - st.cf);
        float y = railKnee (x + st.cf);

        // hard clipping: diodes to ground behind a series resistor, more diodes on one side
        if (invRs > 0.0f)
        {
            const auto& t = tuneFor (activeCharacter);
            for (int it = 0; it < 8; ++it)
                if (hardStep (st.h, y, invRs, t) < 1.0e-6f)
                    break;
            y = st.h;
        }

        // BRIGHT: tilt around 720 Hz (1k / 0.22 uF), then the output roll-off
        st.toneLp += toneA * (y - st.toneLp);
        y = st.toneLp + brightG * (y - st.toneLp);
        st.outLp += outA * (y - st.outLp);
        st.outLp2 += outA * (st.outLp - st.outLp2);
        return st.outLp2 / kVolts * makeup;
    }

    static float onePoleA (double hz, double rate) noexcept
    {
        return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * juce::jmin (hz, rate * 0.45) / rate));
    }
    static float volumeGain (float v) noexcept { return v <= 0.0f ? 0.0f : juce::Decibels::decibelsToGain (volumeDb (v)); }

    void updateCoefficients (int numSamples) noexcept
    {
        // ATTACK glides (a filter sweep, not a jump)
        const float target = attackFor (settings.attack);
        const float k = 1.0f - std::exp (-(float) numSamples / (0.03f * (float) fs));
        attackHz += k * (target - attackHz);
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * attackHz / fsOs;
            const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * 0.7071);
            const double a0 = 1.0 + alpha;
            hb0 = (float) ((1.0 + cw) * 0.5 / a0); hb1 = (float) (-(1.0 + cw) / a0); hb2 = hb0;
            ha1 = (float) (-2.0 * cw / a0);         ha2 = (float) ((1.0 - alpha) / a0);
        }
        const auto& t = tuneFor (activeCharacter);
        legA = (float) std::exp (-1.0 / (t.legOhms * t.legFarads) / fsOs);   // corner 1 / (2 pi R C)
        invLeg = 1.0f / t.legOhms;
        fbIs = t.fbIs;
        fbIsNeg = t.fbIs * t.fbAsym;
        invRs = t.hardRs > 0.0f ? 1.0f / t.hardRs : 0.0f;
        makeup = t.makeup;
        inGain = juce::Decibels::decibelsToGain (t.inDb);
        const float rf = feedbackOhms (settings.drive, t);
        invRf = 1.0f / rf;
        cfA = onePoleA (1.0 / (2.0 * juce::MathConstants<double>::pi * rf * 51.0e-12), fsOs);
        toneA = onePoleA (t.toneHz, fsOs);
        brightG = juce::Decibels::decibelsToGain (t.brightLo + t.brightSpan * settings.bright);   // -14 .. +6 dB above ~720 Hz
        outA = onePoleA (t.outBase + t.outSpan * settings.bright, fsOs);
        dcR = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 8.0 / fs);
    }


    Settings settings;
    double fs = 44100.0, fsOs = 176400.0;
    int maxBlock = 512, osLog2 = 2;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> dryCopy, namCopy;
    NamRunner namRunner { false };   // a pedal's capture keeps its own gain
    std::array<ChannelState, 2> ch {};
    int activeCharacter = 0;
    float attackHz = 150.0f;
    float hb0 = 1.0f, hb1 = 0.0f, hb2 = 0.0f, ha1 = 0.0f, ha2 = 0.0f;
    float invLeg = 1.0f / 4700.0f, fbIs = 2.52e-9f, fbIsNeg = 1.26e-9f, invRs = 0.0f, makeup = 0.9f, inGain = 1.0f;
    float legA = 0.0f, invRf = 1.0f / 50000.0f, cfA = 1.0f, toneA = 0.0f, brightG = 1.0f, outA = 1.0f, dcR = 0.999f;
    NoiseGate noiseGate;
    juce::SmoothedValue<float> onGain, volume, namMix, characterGain;
};
