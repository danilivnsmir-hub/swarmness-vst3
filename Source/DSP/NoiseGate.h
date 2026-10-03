#pragma once

#include "DSPUtils.h"

#include <cmath>
#include <vector>

/**
 * The noise gate of AMP, WASP and SMOKE's input: keyed from the guitar, it computes a gain curve the
 * block applies to its output (and, softened, to the amp's input), so what the amp does after a note -
 * the hiss, the thump of its stages settling - is gated too, not only the guitar's noise.
 *
 *  Key       high-passed at 70 Hz (mains hum doesn't hold it open), linked across channels
 *  Opening   on the peak (15 ms decay - a period of the lowest string, so a low note doesn't ripple
 *            the decision): the first sample over the threshold opens it
 *  Closing   on a 6 ms RMS, 8 dB of hysteresis, 12 ms hold - short enough to separate 16th-note chugs
 *  Below     an expander with a knee, not a wall: from 0 dB at the threshold, 3 dB per dB (2 for
 *            SMOKE's automatic gate) over the first 6 dB - a note's tail is turned down, not chopped - and
 *            6 dB per dB beyond that, so a floor well under the threshold is gone
 *  Attack    0.3 ms on a transient (the level jumped), 15 ms on a swell (it crept up) - no click when a
 *            note that fades in opens the gate under a loud amp output
 *  Release   12 ms after a mute (the level fell 12 dB under its 40 ms average), 150 ms on a decay
 *  Lookahead the block delays its audio by `lookahead` so the ramp finishes before the transient
 *            reaches the output (the block reports it as latency; 0 = none)
 *  Threshold GATE knob: 0 = off, then -75 .. -20 dBFS (peak); or automatic (SMOKE's always-on input
 *            gate): the noise floor (the peak level's minimum - down at once, up at 1 dB / s) + 16 dB
 *            (the noise's peaks sit some 10 dB over that minimum),
 *            never below -58 dBFS (a clean rig keeps the old fixed threshold) nor above -46 (a fuzz's
 *            sustain is not cut)
 */
class NoiseGate
{
public:
    void prepare (double sampleRate, int maxBlockSize, double lookaheadSeconds = 0.0)
    {
        fs = sampleRate;
        lookahead = (int) std::round (lookaheadSeconds * fs);
        const int n = juce::jmax (1, maxBlockSize);
        curve.assign ((size_t) n, 1.0f);
        inputCurve.assign ((size_t) n, 1.0f);
        for (auto& d : delayBuf)
            d.assign ((size_t) juce::jmax (1, lookahead), 0.0f);
        for (auto& f : keyHp)
        {
            f.setType (swarm::SVF::Type::highPass);
            f.setParams (sampleRate, 70.0f, 0.7071f);
        }
        peakDecay  = (float) std::exp (-1.0 / (0.015 * fs));
        rmsFastC   = (float) (1.0 - std::exp (-1.0 / (0.006 * fs)));
        rmsSlowC   = (float) (1.0 - std::exp (-1.0 / (0.040 * fs)));
        attackFast = (float) (1.0 - std::exp (-1.0 / (0.0003 * fs)));
        attackSlow = (float) (1.0 - std::exp (-1.0 / (0.015 * fs)));
        relFast    = (float) (1.0 - std::exp (-1.0 / (0.012 * fs)));
        relSlow    = (float) (1.0 - std::exp (-1.0 / (0.150 * fs)));
        floorDown  = (float) (1.0 - std::exp (-1.0 / (0.050 * fs)));
        floorUp    = (float) (1.0 / fs);   // dB per sample: 1 dB / s
        holdSamples = (int) (0.012 * fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : keyHp) f.reset();
        for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
        peak = rmsFast2 = rmsSlow2 = 0.0f;
        hold = 0;
        floorDb = -60.0f;
        // an automatic gate starts closed (nothing has been played), a knob gate open (it may be off)
        open = ! autoThreshold;
        gainDb = open ? 0.0f : -70.0f;
    }

    /** Automatic threshold (SMOKE): the knob is ignored, the threshold follows the noise floor + 16 dB. */
    void setAutoThreshold (bool shouldTrackTheFloor) noexcept
    {
        autoThreshold = shouldTrackTheFloor;
        reset();
    }

    /** GATE knob (0..1) -> threshold in dBFS (0 = off). */
    static float thresholdDb (float knob) noexcept { return -75.0f + 55.0f * juce::jlimit (0.0f, 1.0f, knob); }

    int lookaheadSamples() const noexcept { return lookahead; }
    // diagnostics
    float currentGainDb() const noexcept { return gainDb; }
    float lastTargetDb() const noexcept { return lastTarget; }
    float peakDbNow() const noexcept { return db (peak); }
    float rmsDbNow() const noexcept { return db (std::sqrt (rmsFast2)); }
    float slowDbNow() const noexcept { return db (std::sqrt (rmsSlow2)); }
    bool isOpen() const noexcept { return open; }
    float thresholdNowDb() const noexcept { return autoThreshold ? juce::jlimit (-58.0f, -46.0f, floorDb + 16.0f) : thresholdDb (lastKnob); }

    /**
     * Computes the gain curve for this block from the key (the block's input, before the lookahead
     * delay); the curve applies to the delayed audio. nullptr when the gate is off and fully open.
     * inputCurve(): the same, never below -20 dB - for the amp's input, so it isn't starved.
     */
    const float* compute (const float* const* key, int numCh, int numSamples, float knob) noexcept
    {
        const int n = juce::jmin (numSamples, (int) curve.size());
        lastKnob = knob;
        const bool off = ! autoThreshold && knob <= 0.001f;
        if (off && gainDb >= -0.01f)
        {
            gainDb = 0.0f;
            open = true;
            // keep following the key, so the floor estimate is ready when it is switched on
            for (int i = 0; i < n; ++i)
                feed (key, numCh, i);
            return nullptr;
        }
        numCh = juce::jmin (numCh, 2);
        for (int i = 0; i < n; ++i)
        {
            const float sc = feed (key, numCh, i);
            juce::ignoreUnused (sc);
            const float pkDb = db (peak), rmsDb = db (std::sqrt (rmsFast2)), slowDb = db (std::sqrt (rmsSlow2));
            const float thOpen = off ? -200.0f : thresholdNowDb(), thClose = thOpen - 8.0f;

            if (pkDb > thOpen || (open && rmsDb > thClose))
            {
                open = true;
                hold = holdSamples;
            }
            else if (hold > 0)
                --hold;
            else
                open = false;

            float target = 0.0f;
            if (! open)
            {
                const float under = thClose - rmsDb, knee = 6.0f, slope = autoThreshold ? 2.0f : 3.0f;
                target = -slope * juce::jmin (knee, under) - 6.0f * juce::jmax (0.0f, under - knee);   // from 0 dB: no step at the threshold
                target = juce::jlimit (-70.0f, 0.0f, target);
            }
            lastTarget = target;

            if (target > gainDb)
            {
                // opening: a transient (the level jumped over its recent average) gets the fast ramp,
                // a swell the slow one
                const bool transient = pkDb - slowDb > 10.0f;
                gainDb += (transient ? attackFast : attackSlow) * (target - gainDb);
            }
            else
            {
                // closing: quick after a mute (the level fell far under its average), gentle on a decay
                const bool mute = slowDb - rmsDb > 12.0f;
                gainDb += (mute ? relFast : relSlow) * (target - gainDb);
            }
            const float g = juce::Decibels::decibelsToGain (gainDb);
            curve[(size_t) i] = g;
            inputCurve[(size_t) i] = juce::jmax (0.1f, g);
        }
        return curve.data();
    }

    const float* inputCurve_() const noexcept { return inputCurve.data(); }

    /** The lookahead: delays the audio in place (always - the latency must not depend on the gate). */
    void delayInPlace (float* const* audio, int numCh, int numSamples) noexcept
    {
        if (lookahead <= 0)
            return;
        numCh = juce::jmin (numCh, 2);
        const int len = lookahead;
        int pos = delayPos;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                auto& d = delayBuf[(size_t) c];
                const float out = d[(size_t) pos];
                d[(size_t) pos] = audio[c][i];
                audio[c][i] = out;
            }
            if (++pos >= len)
                pos = 0;
        }
        delayPos = pos;
    }

private:
    static float db (float v) noexcept { return 20.0f * std::log10 (v + 1.0e-7f); }

    /** One key sample into the detectors; returns the high-passed, linked level. */
    float feed (const float* const* key, int numCh, int i) noexcept
    {
        float sc = 0.0f;
        for (int c = 0; c < juce::jmin (numCh, 2); ++c)
            sc = juce::jmax (sc, std::abs (keyHp[(size_t) c].process (key[c][i])));
        peak = juce::jmax (sc, peak * peakDecay);
        rmsFast2 += rmsFastC * (sc * sc - rmsFast2);
        rmsSlow2 += rmsSlowC * (sc * sc - rmsSlow2);
        if (autoThreshold)
        {
            // the noise floor: the peak level's minimum - follows it down quickly, creeps up at 1 dB / s
            // (the gate opens on peaks, so the floor must be measured the same way)
            const float pkDb = db (peak);
            if (pkDb < floorDb) floorDb += floorDown * (pkDb - floorDb);
            else                floorDb = juce::jmin (floorDb + floorUp, pkDb);
        }
        return sc;
    }

    double fs = 44100.0;
    int lookahead = 0, delayPos = 0;
    std::vector<float> curve, inputCurve;
    std::array<std::vector<float>, 2> delayBuf;
    std::array<swarm::SVF, 2> keyHp;
    float peakDecay = 0.98f, rmsFastC = 0.005f, rmsSlowC = 0.0005f, attackFast = 0.07f, attackSlow = 0.005f, relFast = 0.002f, relSlow = 0.0003f;
    float floorDown = 0.0005f, floorUp = 0.0001f;
    float peak = 0.0f, rmsFast2 = 0.0f, rmsSlow2 = 0.0f, gainDb = 0.0f, floorDb = -60.0f, lastKnob = 0.0f, lastTarget = 0.0f;
    int hold = 0, holdSamples = 384;
    bool open = true, autoThreshold = false;
};
