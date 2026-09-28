#pragma once

#include <JuceHeader.h>

#include <array>
#include <cmath>
#include <complex>
#include <vector>

/**
 * WASP - a tight overdrive / boost for the front of an amp (in the spirit of the modern
 * "precision" metal drives: a TS-family core with a low-end tightening control and a gate).
 *
 * The core is the classic non-inverting op-amp clipper, solved as a circuit:
 *   - the gain leg (4.7k + 47 nF) only passes what is above ~720 Hz, so the lows stay clean and
 *     the clipped signal sits on top of the dry one - the mid hump and the "amp in front of an amp"
 *     feel come from there;
 *   - two silicon diodes in the feedback path clip softly; the feedback voltage is the implicit
 *     solution of  i_leg = v/Rf + 2 Is sinh(v / (n Vt))  (Newton, per sample, oversampled);
 *   - the 51 pF feedback cap rolls the clipped part off (more DRIVE = darker fizz, like the real one).
 * ATTACK tightens the low end in front of the clipper (20 Hz .. 500 Hz), BRIGHT tilts the output
 * filter, GATE is a noise gate keyed from the input, VOLUME the output level (5 = about unity).
 */
class DriveBlock
{
public:
    struct Settings
    {
        bool on = false;
        float volume = 0.5f, drive = 0.3f, bright = 0.5f, attack = 0.5f;   // 0..1 (knob 0..10)
        float gate = 0.0f;                                                  // 0..1, 0 = off
    };

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
        gateCurve.assign ((size_t) maxBlock, 1.0f);
        reset();
    }

    void reset()
    {
        for (auto& c : ch)
            c = {};
        if (oversampler != nullptr) oversampler->reset();
        gateEnv = 0.0f; gateGain = 1.0f; gateHold = 0;
        attackHz = attackFor (settings.attack);
        onGain.reset (fs, 0.02);
        onGain.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        volume.reset (fs, 0.03);
        volume.setCurrentAndTargetValue (volumeGain (settings.volume));
    }

    void setParams (const Settings& s) noexcept { settings = s; }

    //==============================================================================
    void process (float* const* audio, int numCh, int numSamples) noexcept
    {
        numCh = juce::jmin (numCh, 2);
        onGain.setTargetValue (settings.on ? 1.0f : 0.0f);
        if (! settings.on && ! onGain.isSmoothing())
        {
            onGain.setCurrentAndTargetValue (0.0f);
            return;   // off: the signal passes untouched
        }
        for (int c = 0; c < numCh; ++c)
            dryCopy.copyFrom (c, 0, audio[c], numSamples);

        updateGate (audio, numCh, numSamples);   // keyed from the input, applied after the drive
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

        volume.setTargetValue (volumeGain (settings.volume));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = volume.getNextValue() * gateCurve[(size_t) juce::jmin (i, (int) gateCurve.size() - 1)];
            const float on = onGain.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                auto& st = ch[(size_t) c];
                // DC blocker (the clipper is symmetric, but the ATTACK filter sweep can leave a step)
                const float x = audio[c][i];
                const float y = x - st.dcX + dcR * st.dcY;
                st.dcX = x; st.dcY = y;
                const float wet = y * g;
                audio[c][i] = dryCopy.getSample (c, i) + on * (wet - dryCopy.getSample (c, i));
            }
        }
    }

    //==============================================================================
    /** VOLUME knob (0..1) in dB: 0 at noon, +12 dB at 10, -30 dB just above 0 (0 = silent). */
    static float volumeDb (float v) noexcept { return v >= 0.5f ? (v - 0.5f) * 24.0f : (v - 0.5f) * 60.0f; }
    static float attackFor (float v) noexcept { return 20.0f * std::pow (25.0f, v); }
    static float feedbackOhms (float drive) noexcept
    {
        const float taper = (std::exp (4.0f * drive) - 1.0f) / (std::exp (4.0f) - 1.0f);   // audio taper pot
        return kRfMin + 500000.0f * taper;
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
        const double rf = feedbackOhms (s.drive);
        const cd zLeg = (double) kLegOhms + 1.0 / (jw * 47.0e-9);
        const cd zF = rf / (1.0 + jw * rf * 51.0e-12);
        const cd core = 1.0 + zF / zLeg;
        const cd lp = 1.0 / (1.0 + jw / (2.0 * juce::MathConstants<double>::pi * 723.0));
        const double bg = juce::Decibels::decibelsToGain (-14.0 + 20.0 * s.bright);
        const cd tone = lp + bg * (1.0 - lp);
        const cd out1 = 1.0 / (1.0 + jw / (2.0 * juce::MathConstants<double>::pi * (5000.0 + 5000.0 * s.bright)));
        const cd out = out1 * out1;
        return (float) (20.0 * std::log10 (std::abs (hp * core * tone * out) * kMakeup + 1.0e-12));
    }

    /** Static clipping curve above the leg's corner: output volts for input volts (editor / tests). */
    static float transferVolts (float drive, float xVolts) noexcept
    {
        const float invR = 1.0f / feedbackOhms (drive), i = xVolts / kLegOhms;
        float v = 0.0f;
        for (int it = 0; it < 60; ++it)
        {
            const float e = std::exp (juce::jlimit (-40.0f, 40.0f, v / kNVt));
            const float f = v * invR + kIs * (e - 1.0f) - kIsNeg * (1.0f / e - 1.0f) - i;
            const float d = invR + (kIs * e + kIsNeg / e) / kNVt;
            const float step = juce::jlimit (-0.1f, 0.1f, f / d);
            v -= step;
            if (std::abs (step) < 1.0e-7f)
                break;
        }
        return xVolts + v;
    }
    static constexpr float kVolts = 2.5f;          // 0 dBFS = 2.5 V (same scale as AMP)

private:
    struct ChannelState
    {
        float hpX1 = 0.0f, hpX2 = 0.0f, hpY1 = 0.0f, hpY2 = 0.0f;   // ATTACK (2nd-order high-pass)
        float legX = 0.0f, legY = 0.0f;                             // 720 Hz gain leg
        float v = 0.0f;                                             // diode voltage (Newton warm start)
        float cf = 0.0f;                                            // feedback cap
        float toneLp = 0.0f, outLp = 0.0f, outLp2 = 0.0f;
        float dcX = 0.0f, dcY = 0.0f;
    };

    static constexpr float kIs = 2.52e-9f;         // 1N914
    static constexpr float kIsNeg = 0.5f * kIs;    // the other diode clips a little later: a touch of even harmonics
    static constexpr float kRfMin = 33000.0f;      // DRIVE at 0 still has gain (x8): the TS-family mid hump and bite
    static constexpr float kNVt = 1.752f * 0.02585f;
    static constexpr float kLegOhms = 4700.0f;
    static constexpr float kMakeup = 0.9f;          // VOLUME 5 at DRIVE ~3 = unity

    float processSample (ChannelState& st, float in) noexcept
    {
        const float x0 = in * kVolts;

        // ATTACK: 2nd-order high-pass in front of everything
        const float x = hb0 * x0 + hb1 * st.hpX1 + hb2 * st.hpX2 - ha1 * st.hpY1 - ha2 * st.hpY2;
        st.hpX2 = st.hpX1; st.hpX1 = x0;
        st.hpY2 = st.hpY1; st.hpY1 = x;

        // gain leg: 4.7k + 47 nF to ground from the inverting input -> current = HP720(x) / 4.7k
        const float leg = legA * (st.legY + x - st.legX);
        st.legX = x; st.legY = leg;
        const float i = leg / kLegOhms;

        // feedback: Rf || two diodes; solve i = v/Rf + 2 Is sinh(v / nVt)
        float v = st.v;
        for (int it = 0; it < 6; ++it)
        {
            const float e = std::exp (juce::jlimit (-40.0f, 40.0f, v / kNVt));
            const float f = v * invRf + kIs * (e - 1.0f) - kIsNeg * (1.0f / e - 1.0f) - i;
            const float d = invRf + (kIs * e + kIsNeg / e) / kNVt;
            const float step = juce::jlimit (-0.1f, 0.1f, f / d);
            v -= step;
            if (std::abs (step) < 1.0e-6f)
                break;
        }
        st.v = v;

        // 51 pF across the feedback: the clipped part loses its top as Rf grows
        st.cf += cfA * (v - st.cf);
        float y = x + st.cf;
        // the op-amp's output swings to ~4.2 V on a 9 V supply: a soft knee above 3 V
        if (std::abs (y) > 3.0f)
            y = std::copysign (3.0f + 1.2f * std::tanh ((std::abs (y) - 3.0f) / 1.2f), y);

        // BRIGHT: tilt around 720 Hz (1k / 0.22 uF), then the output roll-off
        st.toneLp += toneA * (y - st.toneLp);
        y = st.toneLp + brightG * (y - st.toneLp);
        st.outLp += outA * (y - st.outLp);
        st.outLp2 += outA * (st.outLp - st.outLp2);
        return st.outLp2 / kVolts * kMakeup;
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
        legA = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 720.0 / fsOs);
        const float rf = feedbackOhms (settings.drive);
        invRf = 1.0f / rf;
        cfA = onePoleA (1.0 / (2.0 * juce::MathConstants<double>::pi * rf * 51.0e-12), fsOs);
        toneA = onePoleA (723.0, fsOs);
        brightG = juce::Decibels::decibelsToGain (-14.0f + 20.0f * settings.bright);   // -14 .. +6 dB above ~720 Hz
        outA = onePoleA (5000.0 + 5000.0 * settings.bright, fsOs);
        dcR = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 8.0 / fs);
    }

    void updateGate (float* const* audio, int numCh, int numSamples) noexcept
    {
        const int n = juce::jmin (numSamples, (int) gateCurve.size());
        if (settings.gate <= 0.001f && gateGain >= 0.9999f)
        {
            std::fill (gateCurve.begin(), gateCurve.begin() + n, 1.0f);
            return;
        }
        const float open = juce::Decibels::decibelsToGain (-86.0f + 56.0f * settings.gate), close = open * 0.5f;
        const float envRel = std::exp (-1.0f / (0.01f * (float) fs));
        const float attack = 1.0f - std::exp (-1.0f / (0.0008f * (float) fs));
        const float release = 1.0f - std::exp (-1.0f / (0.07f * (float) fs));
        const int holdSamples = (int) (0.04 * fs);
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
                peak = juce::jmax (peak, std::abs (audio[c][i]));
            gateEnv = juce::jmax (peak, gateEnv * envRel);
            if (settings.gate <= 0.001f || gateEnv > open || (gateEnv > close && gateHold > 0))
                gateHold = holdSamples;
            else if (gateHold > 0)
                --gateHold;
            const float t = gateHold > 0 ? 1.0f : 0.0f;
            gateGain += (t > gateGain ? attack : release) * (t - gateGain);
            gateCurve[(size_t) i] = gateGain;
        }
    }

    Settings settings;
    double fs = 44100.0, fsOs = 176400.0;
    int maxBlock = 512, osLog2 = 2;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> dryCopy;
    std::array<ChannelState, 2> ch {};
    float attackHz = 150.0f;
    float hb0 = 1.0f, hb1 = 0.0f, hb2 = 0.0f, ha1 = 0.0f, ha2 = 0.0f;
    float legA = 0.0f, invRf = 1.0f / 50000.0f, cfA = 1.0f, toneA = 0.0f, brightG = 1.0f, outA = 1.0f, dcR = 0.999f;
    float gateEnv = 0.0f, gateGain = 1.0f;
    int gateHold = 0;
    std::vector<float> gateCurve;
    juce::SmoothedValue<float> onGain, volume;
};
