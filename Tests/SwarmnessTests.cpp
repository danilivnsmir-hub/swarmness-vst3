#include <algorithm>
// Offline, headless verification of the Swarmness DSP.
// Build target: SwarmnessTests   (run via `ctest` or directly)
//
// Optional: `SwarmnessTests --render <dir>` writes WAV renders of every factory preset.

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Pedal/bench/BenchScenarios.h"

#include <chrono>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", condition ? " OK " : "FAIL", what.toRawUTF8());
        if (! condition)
            ++failures;
    }

    void setParam (SwarmnessAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.getAPVTS().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Guitar-ish test signal: decaying harmonic plucks plus a little noise. */
    juce::AudioBuffer<float> makeGuitar (double sr, int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (42);
        const float notes[] = { 82.41f, 110.0f, 146.83f, 196.0f };
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = i / sr;
            const int note = (int) (t / 0.5) % 4;
            const double tn = std::fmod (t, 0.5);
            const double env = std::exp (-tn * 4.0);
            double s = 0.0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (juce::MathConstants<double>::twoPi * notes[note] * h * tn) / (double) h;
            s = 0.3 * env * s + 0.002 * (rng.nextFloat() - 0.5f);
            b.setSample (0, i, (float) s);
            b.setSample (1, i, (float) (s * 0.95));
        }
        return b;
    }

    juce::AudioBuffer<float> makeSine (double sr, int numSamples, double freq, float amp = 0.25f)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr);
            b.setSample (0, i, s);
            b.setSample (1, i, s);
        }
        return b;
    }

    juce::AudioBuffer<float> render (SwarmnessAudioProcessor& p, const juce::AudioBuffer<float>& input, double sr, int blockSize)
    {
        p.prepareToPlay (sr, blockSize);
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        for (int start = 0; start < out.getNumSamples(); start += blockSize)
        {
            const int n = juce::jmin (blockSize, out.getNumSamples() - start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
            p.processBlock (view, midi);
        }
        return out;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    /** RMS of (a[n] - b[n - delay]) relative to RMS of a, in dB, over a window. */
    double nullDb (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int delay, int from, int to)
    {
        double err = 0.0, ref = 0.0;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = from; i < to; ++i)
            {
                const float x = i - delay >= 0 ? in.getSample (ch, i - delay) : 0.0f;
                const float d = out.getSample (ch, i) - x;
                err += d * d;
                ref += x * x;
            }
        return 10.0 * std::log10 ((err + 1.0e-20) / (ref + 1.0e-20));
    }

    /** Dominant frequency (Hz) via FFT with parabolic peak interpolation; also returns the purity in dB. */
    double dominantFrequency (const juce::AudioBuffer<float>& b, double sr, int start, double& purityDb)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data (size * 2, 0.0f);
        juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
        for (int i = 0; i < size && start + i < b.getNumSamples(); ++i)
            data[(size_t) i] = b.getSample (0, start + i);
        window.multiplyWithWindowingTable (data.data(), size);
        fft.performFrequencyOnlyForwardTransform (data.data());

        int peak = 1;
        for (int i = 2; i < size / 2; ++i)
            if (data[(size_t) i] > data[(size_t) peak])
                peak = i;

        double total = 0.0, near = 0.0;
        for (int i = 1; i < size / 2; ++i)
        {
            const double e = (double) data[(size_t) i] * data[(size_t) i];
            total += e;
            if (std::abs (i - peak) <= 6)
                near += e;
        }
        purityDb = 10.0 * std::log10 ((total - near + 1.0e-20) / (near + 1.0e-20));

        const double a = data[(size_t) peak - 1], c = data[(size_t) peak], d = data[(size_t) peak + 1];
        const double offset = 0.5 * (a - d) / (a - 2.0 * c + d);
        return (peak + offset) * sr / size;
    }

    void resetToInit (SwarmnessAudioProcessor& p)
    {
        p.getPresetManager().loadPreset ("Init");
    }

    //==========================================================================
    /** Renders with the NOISE footswitch held for the middle half of the input (like a player would). */
    juce::AudioBuffer<float> renderWithFootswitch (SwarmnessAudioProcessor& p, const juce::AudioBuffer<float>& input,
                                                   double sr, int blockSize, const char* switchId, bool magic = false)
    {
        p.prepareToPlay (sr, blockSize);
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        const int n = out.getNumSamples();
        for (int start = 0; start < n; start += blockSize)
        {
            const bool held = start > n / 4 && start < (3 * n) / 4;
            if (switchId != nullptr) setParam (p, switchId, held ? 1.0f : 0.0f);
            if (magic)               setParam (p, ParamIDs::magicHold, held ? 1.0f : 0.0f);
            const int len = juce::jmin (blockSize, n - start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, len);
            p.processBlock (view, midi);
        }
        return out;
    }

    void testPresetsStable()
    {
        std::printf ("\nFactory presets (with footswitches played): stability at several sample rates / block sizes\n");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int block : { 64, 333, 1024 })
            {
                SwarmnessAudioProcessor p;
                auto input = makeGuitar (sr, (int) (sr * 2.0));
                bool ok = true;
                float peak = 0.0f;
                for (const auto& name : p.getPresetManager().getFactoryPresetNames())
                {
                    for (const char* sw : { ParamIDs::oct1, ParamIDs::oct2 })
                    {
                        p.getPresetManager().loadPreset (name);
                        setParam (p, ParamIDs::flowSync, 0.0f);   // no playhead offline
                        auto out = renderWithFootswitch (p, input, sr, block, sw, true);
                        ok = ok && allFinite (out);
                        peak = juce::jmax (peak, out.getMagnitude (0, out.getNumSamples()));
                    }
                }
                check (ok && peak <= 2.0f, juce::String::formatted ("sr %.0f, block %d: finite, peak %.2f", sr, block, peak));
            }
        }
    }

    void testBypassNull()
    {
        std::printf ("\nBypass is sample-accurate and latency compensated\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Hive Collapse");
        setParam (p, ParamIDs::bypass, 1.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        auto out = render (p, input, sr, 256);
        const int latency = p.getLatencySamples();
        const double db = nullDb (out, input, latency, 4096, input.getNumSamples());
        check (db < -120.0, juce::String::formatted ("bypass null %.1f dB (latency %d samples)", db, latency));
    }

    void testFootswitchesOverrideBypass()
    {
        std::printf ("\nFootswitches work like momentary pedals (even when bypassed / RAINBOW off)\n");
        const double sr = 48000.0;
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, 0.0f);   // exact pitch check: modern engine
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::bypass, 1.0f);
            setParam (p, ParamIDs::oct1, 1.0f);
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            double purity = 0.0;
            const double f = dominantFrequency (out, sr, 48000, purity);
            check (std::abs (1200.0 * std::log2 (f / 440.0)) < 5.0,
                   juce::String::formatted ("bypassed + hold +1 OCT: %.2f Hz (expected 440)", f));
        }
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);                     // RAINBOW off
            setParam (p, ParamIDs::bypass, 1.0f);
            setParam (p, ParamIDs::magicHold, 1.0f);
            auto input = makeGuitar (sr, 48000);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 12000, input.getNumSamples());
            check (db > -20.0, juce::String::formatted ("bypassed + RAINBOW off + hold MAGIC: effect audible (residual %.1f dB)", db));
        }
    }

    void testVenomLink()
    {
        std::printf ("\nLINK: the VENOM footswitch drags the linked octave in\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::rbRaw, 0.0f);
        setParam (p, ParamIDs::rise, 0.0f);
        setParam (p, ParamIDs::rbPrimary, 0.0f);     // hear only the STING octave
        setParam (p, ParamIDs::linkOct1, 1.0f);
        setParam (p, ParamIDs::magicHold, 1.0f);
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 2, 220.0);
        auto out = render (p, input, sr, 256);
        double purity = 0.0;
        const double f = dominantFrequency (out, sr, 48000, purity);
        check (std::abs (1200.0 * std::log2 (f / 440.0)) < 5.0, juce::String::formatted ("hold VENOM with LINK +1: %.2f Hz (expected 440)", f));
    }

    void testDryAlignment()
    {
        std::printf ("\nSTING MIX 0%% with +1 OCT held returns the dry signal aligned with the reported latency\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Killer Bee");
        setParam (p, ParamIDs::fuzzOn, 0.0f);
        setParam (p, ParamIDs::stingMix, 0.0f);
        setParam (p, ParamIDs::oct1, 1.0f);
        const double sr = 44100.0;
        auto input = makeGuitar (sr, 44100);
        auto out = render (p, input, sr, 512);
        const double db = nullDb (out, input, p.getLatencySamples(), 8192, input.getNumSamples());
        check (db < -100.0, juce::String::formatted ("dry null %.1f dB", db));
    }

    void testCleanPathTransparency()
    {
        std::printf ("\nNothing engaged: the plug-in is transparent\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        auto out = render (p, input, sr, 256);
        const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
        check (db < -100.0, juce::String::formatted ("idle residual %.1f dB", db));
    }

    /** Power-weighted mean frequency within +-1/2 octave of 'around' (sidebands of a warbly shifter average out). */
    static double spectralCentroid (const juce::AudioBuffer<float>& b, double sr, double around, int start, int n)
    {
        double num = 0.0, den = 0.0;
        for (double f = around / 1.41; f <= around * 1.41; f += 1.0)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, c = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = start; i < start + n; ++i)
            {
                const double s0 = b.getSample (0, i) + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
            num += power * f;
            den += power;
        }
        return den > 0.0 ? num / den : 0.0;
    }

    /** Mean instantaneous frequency from Schmitt-triggered zero crossings (robust to warble / AM). */
    static double crossingFrequency (const juce::AudioBuffer<float>& b, double sr, int start, int n)
    {
        const float hyst = 0.25f * b.getMagnitude (0, start, n) * 0.2f;
        int first = -1, last = -1, count = 0;
        bool high = b.getSample (0, start) > 0.0f;
        for (int i = start + 1; i < start + n; ++i)
        {
            const float v = b.getSample (0, i);
            if (! high && v > hyst)
            {
                high = true;
                if (first < 0) first = i; else ++count;
                last = i;
            }
            else if (high && v < -hyst)
                high = false;
        }
        return count > 0 ? count * sr / (double) (last - first) : 0.0;
    }

    /** Median of per-frame autocorrelation pitch (robust to the RAW engine's crossfade nulls). */
    static double framePitch (const juce::AudioBuffer<float>& b, double sr, double expected, int start, int n)
    {
        const int frame = 4096, hop = 2048;
        const int minLag = (int) (sr / (expected * 1.5)), maxLag = (int) (sr / (expected / 1.5));
        std::vector<double> pitches;
        const float* x = b.getReadPointer (0);
        for (int f0 = start; f0 + frame + maxLag < start + n; f0 += hop)
        {
            double best = -1.0; int bestLag = 0;
            std::vector<double> r ((size_t) (maxLag + 2), 0.0);
            for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
            {
                double num = 0.0, e1 = 0.0, e2 = 0.0;
                for (int i = 0; i < frame; ++i)
                {
                    num += (double) x[f0 + i] * x[f0 + i + lag];
                    e1  += (double) x[f0 + i] * x[f0 + i];
                    e2  += (double) x[f0 + i + lag] * x[f0 + i + lag];
                }
                r[(size_t) lag] = (e1 > 0 && e2 > 0) ? num / std::sqrt (e1 * e2) : 0.0;
            }
            for (int lag = minLag; lag <= maxLag; ++lag)
                if (r[(size_t) lag] > best) { best = r[(size_t) lag]; bestLag = lag; }
            if (best < 0.5) continue;
            const double a = r[(size_t) bestLag - 1], c = r[(size_t) bestLag + 1], m = r[(size_t) bestLag];
            const double denom = a - 2.0 * m + c;
            const double shift = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
            pitches.push_back (sr / (bestLag + shift));
        }
        if (pitches.empty()) return 0.0;
        std::sort (pitches.begin(), pitches.end());
        return pitches[pitches.size() / 2];
    }

    void testNoiseOctaves()
    {
        std::printf ("\nNOISE footswitches: pitch accuracy (220 Hz sine, no Panic/Chaos/Speed)\n");
        struct Case { const char* sw; bool down; double expected; };
        const Case cases[] = { { ParamIDs::oct1, false, 440.0 }, { ParamIDs::oct2, false, 880.0 },
                               { ParamIDs::oct1, true, 110.0 },  { ParamIDs::oct2, true, 55.0 } };
        for (bool raw : { false, true })
        for (const auto& c : cases)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, raw ? 1.0f : 0.0f);
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::shiftA, c.down ? -12.0f : 12.0f);
            setParam (p, ParamIDs::shiftB, c.down ? -24.0f : 24.0f);
            setParam (p, c.sw, 1.0f);
            const double sr = 48000.0;
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            double purity = 0.0;
            const double f = raw ? framePitch (out, sr, c.expected, 48000, 48000) : dominantFrequency (out, sr, 48000, purity);
            const double cents = 1200.0 * std::log2 (f / c.expected);
            check (std::abs (cents) < (raw ? 20.0 : 5.0), juce::String::formatted ("%s %s %s: %.2f Hz (%+.1f cents), spurious %.1f dB",
                                                                    raw ? "RAW   " : "modern", c.sw, c.down ? "down" : "up  ", f, cents, purity));
        }
    }

    void testFootswitchRelease()
    {
        std::printf ("\nNOISE footswitch release: glides home and returns to the clean signal\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::rise, 100.0f);
        setParam (p, ParamIDs::panic, 60.0f);
        setParam (p, ParamIDs::chaos, 60.0f);
        setParam (p, ParamIDs::speed, 60.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        auto out = renderWithFootswitch (p, input, sr, 128, ParamIDs::oct1);
        // Last quarter: released for > 0.4 s -> must be back to (latency-aligned) dry
        const double db = nullDb (out, input, p.getLatencySamples(), 96000 * 3 / 4 + 24000 / 2, 96000);
        check (db < -60.0, juce::String::formatted ("after release residual %.1f dB", db));
    }

    void testRiseFall()
    {
        std::printf ("\nRISE and FALL are independent glide times\n");
        const double sr = 48000.0;
        auto semisAfter = [sr] (float riseMs, float fallMs, bool released, double seconds)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rise, riseMs);
            setParam (p, ParamIDs::fall, fallMs);
            setParam (p, ParamIDs::oct1, 1.0f);
            p.prepareToPlay (sr, 256);
            auto block = makeSine (sr, 256, 220.0);
            juce::MidiBuffer midi;
            auto run = [&] (double secs) { for (int i = 0; i < (int) (secs * sr / 256); ++i) { juce::AudioBuffer<float> b (block); p.processBlock (b, midi); } };
            run (1.2);                                   // fully risen (RISE <= 1 s)
            if (released) setParam (p, ParamIDs::oct1, 0.0f);
            run (seconds);
            return p.getMeters().pitchSemitones.load();
        };
        const float midRise = [&] { SwarmnessAudioProcessor p; resetToInit (p);
                                    setParam (p, ParamIDs::rise, 1000.0f); setParam (p, ParamIDs::oct1, 1.0f);
                                    p.prepareToPlay (sr, 256); auto block = makeSine (sr, 256, 220.0); juce::MidiBuffer midi;
                                    for (int i = 0; i < (int) (0.5 * sr / 256); ++i) { juce::AudioBuffer<float> b (block); p.processBlock (b, midi); }
                                    return p.getMeters().pitchSemitones.load(); }();
        const float slowFall = semisAfter (0.0f, 1000.0f, true, 0.5);
        const float fastFall = semisAfter (1000.0f, 0.0f, true, 0.05);
        check (midRise > 4.0f && midRise < 8.0f, juce::String::formatted ("RISE 1 s: half way after 0.5 s (%.1f st)", midRise));
        check (slowFall > 4.0f && slowFall < 8.0f, juce::String::formatted ("FALL 1 s: half way back after 0.5 s (%.1f st)", slowFall));
        check (std::abs (fastFall) < 0.5f, juce::String::formatted ("FALL 0 with RISE 1 s: home immediately (%.1f st)", fastFall));
    }

    void testTrailsInterval()
    {
        std::printf ("\nRAINBOW primary voice interval (220 Hz sine, dry removed)\n");
        for (bool raw : { false, true })
        for (float pitch : { 7.0f, -5.0f, 12.0f })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, raw ? 1.0f : 0.0f);
            setParam (p, ParamIDs::rbOn, 1.0f);
            setParam (p, ParamIDs::rbPitch, pitch);
            setParam (p, ParamIDs::rbPrimary, 100.0f);
            setParam (p, ParamIDs::rbTracking, 100.0f);
            const double sr = 48000.0;
            auto input = makeSine (sr, 48000 * 2, 220.0);
            auto out = render (p, input, sr, 256);
            // remove the dry part (rainbow adds voices to the input)
            juce::AudioBuffer<float> voices (out);
            const int lat = p.getLatencySamples();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = lat; i < voices.getNumSamples(); ++i)
                    voices.setSample (ch, i, out.getSample (ch, i) - input.getSample (ch, i - lat));
            double purity = 0.0;
            const double expected = 220.0 * std::pow (2.0, pitch / 12.0);
            const double f = raw ? framePitch (voices, sr, expected, 48000, 48000) : dominantFrequency (voices, sr, 48000, purity);
            const double cents = 1200.0 * std::log2 (f / expected);
            check (std::abs (cents) < (raw ? 20.0 : 5.0),
                   juce::String::formatted ("%s pitch %+.0f st: %.2f Hz (%+.1f cents)", raw ? "RAW   " : "modern", pitch, f, cents));
        }
    }

    void testMagicBounded()
    {
        std::printf ("\nMAGIC at maximum + switch held: self-oscillates but stays bounded\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Hive Collapse");
        setParam (p, ParamIDs::fuzzOn, 0.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000 * 6);
        for (int ch = 0; ch < 2; ++ch)
            input.clear (ch, 48000 * 2, 48000 * 4);   // silence: whatever sounds now is self-oscillation
        auto out = renderWithFootswitch (p, input, sr, 256, nullptr, true);
        const float peak = out.getMagnitude (0, out.getNumSamples());
        const float tail = out.getRMSLevel (0, 48000 * 4, 48000);
        check (allFinite (out) && peak <= 2.0f && tail > 0.01f, juce::String::formatted ("peak %.2f, self-oscillation during silence %.1f dBFS", peak,
                                                                          juce::Decibels::gainToDecibels (tail)));
    }

    void testFuzzLevel()
    {
        std::printf ("\nFUZZ: loud but controlled output level\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        const double inRms = juce::Decibels::gainToDecibels ((double) input.getRMSLevel (0, 24000, 72000));
        const char* voices[] = { "DOWN", "MID", "UP" };
        for (int voice = 0; voice < 3; ++voice)
            for (float fz : { 0.0f, 50.0f, 100.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::fuzzOn, 1.0f);
                setParam (p, ParamIDs::fuzzVoice, (float) voice);
                setParam (p, ParamIDs::fuzz, fz);
                auto out = render (p, input, sr, 256);
                const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
                check (rms - inRms > -3.0 && rms - inRms < 14.0,
                       juce::String::formatted ("%-4s fuzz %3.0f%%: %+.1f dB vs input", voices[voice], fz, rms - inRms));
            }

        for (auto [id, name] : { std::pair { ParamIDs::fuzzGlare, "GLARE" }, std::pair { ParamIDs::fuzzBlend, "BLEND" },
                                 std::pair { ParamIDs::fuzzScoop, "SCOOP" } })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 100.0f);
            setParam (p, ParamIDs::fuzzTone, 100.0f);
            setParam (p, id, 100.0f);
            auto out = render (p, input, sr, 256);
            const float peak = out.getMagnitude (0, out.getNumSamples());
            const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
            check (peak < 2.0f && rms - inRms < 16.0, juce::String::formatted ("%s max: %+.1f dB vs input, peak %.2f", name, rms - inRms, peak));
        }
    }

    static double goertzelDb (const juce::AudioBuffer<float>& b, double sr, double freq, int start, int n)
    {
        const double w = juce::MathConstants<double>::twoPi * freq / sr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = start; i < start + n; ++i)
        {
            const double s0 = b.getSample (0, i) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return juce::Decibels::gainToDecibels (std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / n + 1.0e-12);
    }

    void testGlareOctave()
    {
        std::printf ("\nSMOKE GLARE adds an octave-up\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000, 110.0, 0.3f);
        double second[2] {};
        for (int g = 0; g < 2; ++g)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 60.0f);
            setParam (p, ParamIDs::fuzzGlare, g == 0 ? 0.0f : 100.0f);
            auto out = render (p, input, sr, 256);
            second[g] = goertzelDb (out, sr, 220.0, 12000, 24000) - goertzelDb (out, sr, 110.0, 12000, 24000);
        }
        check (second[1] > second[0] + 6.0,
               juce::String::formatted ("2nd harmonic vs fundamental: %.1f dB -> %.1f dB with GLARE", second[0], second[1]));
    }

    void testHiveMix()
    {
        std::printf ("\nHIVE MIX: 100%% leaves only the HIVE voices; after SHIFT they follow the shifted note\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 2, 220.0, 0.3f);
        for (bool octave : { false, true })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, 0.0f);
            setParam (p, ParamIDs::rbRaw, 0.0f);
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::rbOn, 1.0f);
            setParam (p, ParamIDs::rbPitch, 7.0f);
            setParam (p, ParamIDs::rbPrimary, 100.0f);
            setParam (p, ParamIDs::rbMix, 100.0f);
            if (octave)
                setParam (p, ParamIDs::oct1, 1.0f);
            auto out = render (p, input, sr, 256);
            const double dry   = goertzelDb (out, sr, 220.0, 48000, 24000);
            const double voice = goertzelDb (out, sr, 330.0, 48000, 24000);
            if (! octave)
                check (voice - dry > 30.0, juce::String::formatted ("MIX 100%%: voice %.1f dB above the dry note", voice - dry));
            else
            {
                const double oct = goertzelDb (out, sr, 440.0, 48000, 24000);
                const double shiftedVoice = goertzelDb (out, sr, 659.26, 48000, 24000);
                check (shiftedVoice - oct > 30.0 && shiftedVoice - dry > 30.0,
                       juce::String::formatted ("MIX 100%% + hold +1 OCT (SHIFT -> HIVE): the voice moves to 659 Hz, the octave itself %.1f dB below it", shiftedVoice - oct));
            }
        }
    }

    void testTrails()
    {
        std::printf ("\nHIVE TRAILS: repeats at TIME, decay on their own, no self-oscillation without VENOM\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 5, 220.0, 0.3f);
        for (int ch = 0; ch < 2; ++ch)
            input.clear (ch, 4800, input.getNumSamples() - 4800);    // 100 ms burst, then silence

        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::rbOn, 1.0f);
        setParam (p, ParamIDs::rbPitch, 7.0f);
        setParam (p, ParamIDs::rbPrimary, 100.0f);
        setParam (p, ParamIDs::rbTracking, 100.0f);
        setParam (p, ParamIDs::rbMagic, 100.0f);
        setParam (p, ParamIDs::rbTime, 300.0f);
        auto out = render (p, input, sr, 256);

        // Energy between repeats vs. at the first repeat (~300 ms after the burst)
        const float atRepeat  = out.getRMSLevel (0, (int) (0.30 * sr), (int) (0.08 * sr));
        const float between   = out.getRMSLevel (0, (int) (0.19 * sr), (int) (0.06 * sr));
        const float tail      = out.getRMSLevel (0, (int) (4.0 * sr), (int) (0.8 * sr));
        check (atRepeat > 3.0f * between, juce::String::formatted ("first repeat at TIME: %.1f dB above the gap",
                                                                   juce::Decibels::gainToDecibels (atRepeat / (between + 1.0e-9f))));
        check (tail < 0.003f, juce::String::formatted ("TRAILS 100%% has faded after 4 s: tail %.1f dBFS", juce::Decibels::gainToDecibels (tail + 1.0e-9f)));
    }

    void testInputSensitivity()
    {
        std::printf ("\nINPUT: a plain input gain, how hard the effects are hit\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::input, 12.0f);
            auto out = render (p, input, sr, 256);
            juce::AudioBuffer<float> ref (input);
            ref.applyGain (juce::Decibels::decibelsToGain (12.0f));
            const double db = nullDb (out, ref, p.getLatencySamples(), 4096, input.getNumSamples());
            check (db < -100.0, juce::String::formatted ("INPUT +12 dB, nothing engaged: plain +12 dB gain (residual %.1f dB)", db));
        }
        double rms[2] {};
        for (int k = 0; k < 2; ++k)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 20.0f);
            setParam (p, ParamIDs::input, k == 0 ? -18.0f : 12.0f);
            auto out = render (p, input, sr, 256);
            rms[k] = out.getRMSLevel (0, 12000, 24000);
        }
        // A plain input gain: louder in -> louder out, but SMOKE compresses the 30 dB step heavily
        const double stepDb = juce::Decibels::gainToDecibels (rms[1] / juce::jmax (1.0e-9, rms[0]));
        check (stepDb > 1.0 && stepDb < 20.0,
               juce::String::formatted ("INPUT -18 -> +12 dB into SMOKE: output +%.1f dB (plain gain, fuzz compresses)", stepDb));
    }

    void testFuzzIdleNoise()
    {
        std::printf ("\nSMOKE with nothing played: no self-oscillation, interface hiss not blown up\n");
        const double sr = 48000.0;
        for (float noiseDb : { -200.0f, -90.0f, -75.0f, -68.0f })
        {
            juce::AudioBuffer<float> input (2, 48000 * 2);
            juce::Random rng (7);
            const float amp = juce::Decibels::decibelsToGain (noiseDb, -200.0f);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < input.getNumSamples(); ++i)
                    input.setSample (ch, i, amp * (rng.nextFloat() * 2.0f - 1.0f));

            for (const char* preset : { "Jumbo Smoke", "Glare Scream", "Doom Cathedral", "Smoked Out", "Hive Collapse", "Panic - Octave Panic" })
            {
                SwarmnessAudioProcessor p;
                p.getPresetManager().loadPreset (preset);
                auto out = render (p, input, sr, 256);
                const float rms = out.getRMSLevel (0, 48000, 48000);
                const float db = juce::Decibels::gainToDecibels (rms, -200.0f);
                check (db < noiseDb - 3.0f || db < -100.0f,
                       juce::String::formatted ("%-16s input noise %4.0f dBFS -> output %6.1f dBFS", preset, noiseDb, db));
            }
        }
    }

    /** Time (ms) until the output first reaches half of its steady-state level after a sine starts. */
    static double onsetMs (const juce::AudioBuffer<float>& out, double sr, int startSample)
    {
        const int n = out.getNumSamples();
        const float steady = out.getRMSLevel (0, n - (int) (0.3 * sr), (int) (0.25 * sr));
        const int win = (int) (0.004 * sr);
        for (int i = startSample; i + win < n; i += win / 2)
            if (out.getRMSLevel (0, i, win) > 0.5f * steady)
                return 1000.0 * (i - startSample) / sr;
        return -1.0;
    }

    void reportLag()
    {
        std::printf ("\nPitch engines: onset lag vs. the dry note (220 Hz sine starting at 0.5 s)\n");
        const double sr = 48000.0;
        auto input = makeSine (sr, 48000 * 2, 220.0, 0.3f);
        for (int ch = 0; ch < 2; ++ch)
            input.clear (ch, 0, 24000);
        for (bool raw : { false, true })
        {
            for (const char* sw : { ParamIDs::oct1, ParamIDs::oct2 })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::rbRaw, raw ? 1.0f : 0.0f);
                setParam (p, ParamIDs::rise, 0.0f);
                setParam (p, sw, 1.0f);
                auto out = render (p, input, sr, 256);
                std::printf ("    STING %s %s: %.1f ms\n", raw ? "RAW   " : "modern", sw, onsetMs (out, sr, 24000 + p.getLatencySamples()));
            }
            for (float tracking : { 100.0f, 80.0f, 30.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::rbRaw, raw ? 1.0f : 0.0f);
                setParam (p, ParamIDs::rbOn, 1.0f);
                setParam (p, ParamIDs::rbPitch, 7.0f);
                setParam (p, ParamIDs::rbPrimary, 100.0f);
                setParam (p, ParamIDs::rbTracking, tracking);
                auto out = render (p, input, sr, 256);
                const int lat = p.getLatencySamples();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = lat; i < out.getNumSamples(); ++i)
                        out.setSample (ch, i, out.getSample (ch, i) - input.getSample (ch, i - lat));
                std::printf ("    HIVE  %s tracking %3.0f%%: %.1f ms\n", raw ? "RAW   " : "modern", tracking, onsetMs (out, sr, 24000 + lat));
            }
        }
    }

    double toneDb (const juce::AudioBuffer<float>& b, double sr, double freq, int from, int length);

    void testFuzzSag()
    {
        std::printf ("\nSMOKE SAG: the attack sags and the note blooms back\n");
        const double sr = 48000.0;
        // one plucked note: sharp attack, 1.2 s decay
        juce::AudioBuffer<float> input (2, 48000 * 2);
        for (int i = 0; i < input.getNumSamples(); ++i)
        {
            const double t = i / sr;
            double v = 0.0;
            for (int h = 1; h <= 6; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * 110.0 * h * t) / h;
            v *= 0.4 * std::exp (-t * 2.5);
            input.setSample (0, i, (float) v);
            input.setSample (1, i, (float) v);
        }
        double bloom[2] {};
        for (int k = 0; k < 2; ++k)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, 80.0f);
            setParam (p, ParamIDs::fuzzSag, k == 0 ? 0.0f : 100.0f);
            auto out = render (p, input, sr, 256);
            const int lat = p.getLatencySamples();
            const float attack = out.getRMSLevel (0, lat + 480, 2400);           // 10..60 ms
            const float body   = out.getRMSLevel (0, lat + 19200, 4800);         // 400..500 ms
            bloom[k] = juce::Decibels::gainToDecibels (body / attack);

        }
        check (bloom[1] > bloom[0] + 1.0,
               juce::String::formatted ("body vs attack: %+.1f dB (SAG 0) -> %+.1f dB (SAG 100)", bloom[0], bloom[1]));

        // TOUCH from the circuit (diode-feedback stages, no volume follower): picking 20 dB softer
        // cleans up a lot at low FUZZ, and at high FUZZ is still a little quieter and clearly rounder
        auto fuzzOut = [&] (float fuzzAmount, float amp)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setParam (p, ParamIDs::fuzz, fuzzAmount);
            return render (p, makeSine (sr, 24000, 110.0, amp), sr, 256);
        };
        auto levelDb = [] (const juce::AudioBuffer<float>& b) { return (double) juce::Decibels::gainToDecibels (b.getRMSLevel (0, 12000, 9600)); };
        auto edgeDb = [&] (const juce::AudioBuffer<float>& b) { return toneDb (b, sr, 550.0, 12000, 9600) - toneDb (b, sr, 110.0, 12000, 9600); };
        const auto lowHard = fuzzOut (30.0f, 0.1f), lowSoft = fuzzOut (30.0f, 0.01f);
        const auto hiHard = fuzzOut (80.0f, 0.1f), hiSoft = fuzzOut (80.0f, 0.01f);
        const double lowDrop = levelDb (lowHard) - levelDb (lowSoft), hiDrop = levelDb (hiHard) - levelDb (hiSoft);
        const double hiRound = edgeDb (hiHard) - edgeDb (hiSoft);
        check (lowDrop > 8.0, juce::String::formatted ("TOUCH at FUZZ 30: 20 dB softer picking is %.1f dB quieter (cleans up)", lowDrop));
        check (hiDrop > 0.8 && hiDrop < 6.0 && hiRound > 2.0,
               juce::String::formatted ("TOUCH at FUZZ 80: %.1f dB quieter and %.1f dB less 5th harmonic (compressed, but not a wall)", hiDrop, hiRound));
    }

    void testDetune()
    {
        std::printf ("\nDETUNE: fine offset of the STING octave\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::rbRaw, 0.0f);
        setParam (p, ParamIDs::rise, 0.0f);
        setParam (p, ParamIDs::shDetune, -30.0f);
        setParam (p, ParamIDs::oct1, 1.0f);
        const double sr = 48000.0;
        auto out = render (p, makeSine (sr, 48000 * 2, 220.0), sr, 256);
        double purity = 0.0;
        const double f = dominantFrequency (out, sr, 48000, purity);
        const double cents = 1200.0 * std::log2 (f / 440.0);
        check (std::abs (cents + 30.0) < 5.0, juce::String::formatted ("+1 OCT with DETUNE -30 ct: %.2f Hz (%+.1f cents)", f, cents));
    }

    void testMidiLearn()
    {
        std::printf ("\nFootswitch MIDI learn\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        auto send = [&] (const juce::MidiMessage& m)
        {
            juce::MidiBuffer midi;
            midi.addEvent (m, 0);
            buf.clear();
            p.processBlock (buf, midi);
        };
        auto value = [&] (const char* id) { return p.getAPVTS().getRawParameterValue (id)->load(); };

        using namespace ParamIDs;
        p.startMidiLearn (oct1);                                        // SHIFT A footswitch
        send (juce::MidiMessage::controllerEvent (1, 80, 127));
        check (p.getMidiLearnParam().isEmpty() && p.describeMidiBinding (oct1) == "CC 80", "learned CC 80 for SHIFT A");
        send (juce::MidiMessage::controllerEvent (1, 80, 0));
        send (juce::MidiMessage::controllerEvent (1, 80, 127));
        const bool held = value (oct1) > 0.5f;
        send (juce::MidiMessage::controllerEvent (1, 80, 0));
        check (held && value (oct1) < 0.5f, "MOMENTARY: pedal down = on, up = off");

        p.startMidiLearn (bypass);                                      // ON
        send (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
        const float before = value (bypass);
        send (juce::MidiMessage::noteOff (1, 36));
        send (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
        check (std::abs (value (bypass) - before) > 0.5f, "ON toggles on each note press (" + p.describeMidiBinding (bypass) + ")");

        // One pedal (127 on press, 0 on release) for ON and WINGS together: every press toggles both
        p.startMidiLearn (bypass);
        send (juce::MidiMessage::controllerEvent (1, 70, 127));
        send (juce::MidiMessage::controllerEvent (1, 70, 0));
        p.startMidiLearn (flowOn);
        send (juce::MidiMessage::controllerEvent (1, 70, 127));
        send (juce::MidiMessage::controllerEvent (1, 70, 0));
        const float on0 = value (bypass), wings0 = value (flowOn);
        send (juce::MidiMessage::controllerEvent (1, 70, 127));
        const float on1 = value (bypass), wings1 = value (flowOn);
        send (juce::MidiMessage::controllerEvent (1, 70, 0));
        check (std::abs (on1 - on0) > 0.5f && std::abs (wings1 - wings0) > 0.5f && std::abs (value (bypass) - on1) < 0.5f,
               "one CC drives ON and WINGS: one press toggles both, the release changes nothing");

        // A knob follows the CC value; a selector steps on each press
        p.startMidiLearn (fuzz);
        send (juce::MidiMessage::controllerEvent (1, 21, 10));
        send (juce::MidiMessage::controllerEvent (1, 21, 127));
        const float fuzzTop = value (fuzz);
        send (juce::MidiMessage::controllerEvent (1, 21, 0));
        check (fuzzTop > 99.0f && value (fuzz) < 1.0f, "a knob follows the CC (0..127 -> full range)");
        p.startMidiLearn (fuzzVoice);
        send (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
        const float v0 = value (fuzzVoice);
        send (juce::MidiMessage::noteOff (1, 40));
        send (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
        check (juce::roundToInt (value (fuzzVoice)) == (juce::roundToInt (v0) + 1) % 3, "a selector steps to the next option on each press");

        {
            // while ON is off the host calls processBlockBypassed: the pedal must still switch it back on
            const float onBefore = value (bypass);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 70, 127), 0);
            buf.clear();
            p.processBlockBypassed (buf, midi);
            juce::MidiBuffer up;
            up.addEvent (juce::MidiMessage::controllerEvent (1, 70, 0), 0);
            p.processBlockBypassed (buf, up);
            check (std::abs (value (bypass) - onBefore) > 0.5f, "MIDI also works while the host has the plug-in bypassed (processBlockBypassed)");
        }

        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        SwarmnessAudioProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.describeMidiBinding (oct1) == "CC 80" && q.describeMidiBinding (bypass).contains ("Note") && q.describeMidiBinding (bypass).contains ("CC 70")
               && q.describeMidiBinding (flowOn) == "CC 70", "bindings saved with the session (" + q.describeMidiBinding (bypass) + ")");

        // Sessions up to beta.24: footswitch bindings as midi0..midi3
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->removeAttribute ("midiBindings");
        xml->setAttribute ("midi2", "1:64");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        SwarmnessAudioProcessor r;
        r.setStateInformation (old.getData(), (int) old.getSize());
        check (r.describeMidiBinding (magicHold) == "CC 64", "old session: VENOM footswitch binding kept");
    }

    void testScenes()
    {
        std::printf ("\nScenes A..D\n");
        using namespace ParamIDs;
        SwarmnessAudioProcessor p;
        resetToInit (p);
        auto& pm = p.getPresetManager();
        auto v = [&p] (const char* id) { return p.getAPVTS().getRawParameterValue (id)->load(); };
        setParam (p, fuzzOn, 1.0f);
        setParam (p, fuzz, 30.0f);
        setParam (p, scene, 1.0f);                                   // B: starts as a copy of A
        const bool copied = std::abs (v (fuzz) - 30.0f) < 0.1f && v (fuzzOn) > 0.5f;
        setParam (p, fuzz, 90.0f);
        setParam (p, revOn, 1.0f);
        setParam (p, scene, 0.0f);
        const bool backToA = std::abs (v (fuzz) - 30.0f) < 0.1f && v (revOn) < 0.5f;
        setParam (p, scene, 1.0f);
        check (copied && backToA && std::abs (v (fuzz) - 90.0f) < 0.1f && v (revOn) > 0.5f && pm.getCurrentScene() == 1,
               "a new scene copies the current one; each scene keeps its own settings");

        // the chain order is shared
        setParam (p, Chain::slotIds[Chain::crypt], 5.0f);
        setParam (p, scene, 0.0f);
        check (p.getRequestedLayout().order[0] == Chain::crypt, "the chain order is shared by all scenes");
        setParam (p, Chain::slotIds[Chain::crypt], (float) Chain::defaultSlots[Chain::crypt]);

        // saved with a user preset (scene A = the preset's parameters)
        setParam (p, scene, 1.0f);
        check (pm.isDirty(), "editing a scene marks the preset as modified");
        pm.saveUserPreset ("Scene Test");
        pm.loadPreset ("Init");
        pm.loadPreset ("Scene Test");
        const bool loadedA = pm.getCurrentScene() == 0 && std::abs (v (fuzz) - 30.0f) < 0.1f;
        setParam (p, scene, 1.0f);
        check (loadedA && std::abs (v (fuzz) - 90.0f) < 0.1f && ! pm.isDirty(), "scenes saved with the user preset (loads on A, B comes back)");

        // saved with the session (current scene too)
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        SwarmnessAudioProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto qv = [&q] (const char* id) { return q.getAPVTS().getRawParameterValue (id)->load(); };
        const bool inB = q.getPresetManager().getCurrentScene() == 1 && std::abs (qv (fuzz) - 90.0f) < 0.1f;
        q.getAPVTS().getParameter (scene)->setValueNotifyingHost (0.0f);
        check (inB && std::abs (qv (fuzz) - 30.0f) < 0.1f, "scenes and the current scene saved with the session");

        // MIDI: one pedal per scene
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        auto send = [&] (const juce::MidiMessage& m) { juce::MidiBuffer midi; midi.addEvent (m, 0); buf.clear(); p.processBlock (buf, midi); };
        p.startMidiLearn (scene, 0);
        send (juce::MidiMessage::controllerEvent (1, 30, 127));
        send (juce::MidiMessage::controllerEvent (1, 30, 0));
        send (juce::MidiMessage::controllerEvent (1, 30, 127));
        check (pm.getCurrentScene() == 0 && std::abs (v (fuzz) - 30.0f) < 0.1f && p.describeMidiBinding (scene, 0) == "CC 30",
               "a pedal learned on scene A selects scene A");
        pm.deleteUserPreset ("Scene Test");
    }

    void testUnsupportedPresets()
    {
        std::printf ("\nUser bank: Swarmness 1.x presets are moved out\n");
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("swarmness-preset-test");
        dir.deleteRecursively();
        dir.createDirectory();
        dir.getChildFile ("Chainsaw Massacre.swpreset").replaceWithText (R"({"name":"Chainsaw Massacre","author":"User","version":"1.2.5","parameters":{"saturation":1.0}})");
        dir.getChildFile ("Mine.swpreset").replaceWithText (R"({"name":"Mine","plugin":"Swarmness","version":"3.0.0","parameters":{"fuzzOn":1}})");
        const int moved = PresetManager::moveUnsupportedPresets (dir);
        check (moved == 1 && ! dir.getChildFile ("Chainsaw Massacre.swpreset").exists() && dir.getChildFile ("Mine.swpreset").exists()
               && dir.getChildFile ("Old presets (unsupported)").getChildFile ("Chainsaw Massacre.swpreset").exists(),
               "old (1.x numbering) preset moved to 'Old presets (unsupported)', current presets stay");
        dir.deleteRecursively();
    }

    void testMonoToStereo()
    {
        std::printf ("\nMono guitar on a stereo output\n");
        SwarmnessAudioProcessor p;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        const bool ok = p.setBusesLayout (layout);
        check (ok, "mono in / stereo out layout accepted");
        resetToInit (p);
        setParam (p, ParamIDs::swarmOn, 1.0f);
        setParam (p, ParamIDs::swarmMix, 50.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        input.clear (1, 0, input.getNumSamples());   // only the left (mono) channel carries the guitar
        auto out = render (p, input, sr, 256);
        const float l = out.getRMSLevel (0, 12000, 24000), r = out.getRMSLevel (1, 12000, 24000);
        check (r > 0.5f * l, juce::String::formatted ("both channels carry signal (L %.3f, R %.3f)", l, r));
    }

    void testSwarmBounded()
    {
        std::printf ("\nSWARM: extreme settings stay bounded\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        const double inRms = juce::Decibels::gainToDecibels ((double) input.getRMSLevel (0, 24000, 72000));
        for (bool deep : { false, true })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::swarmOn, 1.0f);
            setParam (p, ParamIDs::swarmDeep, deep ? 1.0f : 0.0f);
            setParam (p, ParamIDs::swarmDepth, 100.0f);
            setParam (p, ParamIDs::swarmRate, 8.0f);
            setParam (p, ParamIDs::swarmMix, 50.0f);
            auto out = render (p, input, sr, 256);
            const float peak = out.getMagnitude (0, out.getNumSamples());
            const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
            check (peak < 1.5f && std::abs (rms - inRms) < 9.0,
                   juce::String::formatted ("%s depth 100%%, mix 50%%: %+.1f dB vs input, peak %.2f", deep ? "deep   " : "classic", rms - inRms, peak));
        }
    }

    void testStateRoundTrip()
    {
        std::printf ("\nState save / restore\n");
        SwarmnessAudioProcessor a;
        a.getPresetManager().loadPreset ("Honey Ladder");
        setParam (a, ParamIDs::fuzz, 42.0f);
        setParam (a, ParamIDs::oct1, 1.0f);   // momentary switch left down
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        SwarmnessAudioProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto value = [&b] (const char* id) { return b.getAPVTS().getRawParameterValue (id)->load(); };
        check (std::abs (value (ParamIDs::fuzz) - 42.0f) < 0.05f && std::abs (value (ParamIDs::rbMagic) - 65.0f) < 0.05f,
               "parameters restored");
        check (value (ParamIDs::oct1) < 0.5f, "momentary footswitch not restored as held");
        check (b.getPresetManager().getCurrentPresetName() == "Honey Ladder", "preset name restored");
    }

    void testPresetDirtyTracking()
    {
        std::printf ("\nPreset dirty tracking\n");
        SwarmnessAudioProcessor p;
        auto& pm = p.getPresetManager();
        pm.loadPreset ("Frenzy");
        check (! pm.isDirty(), "clean after load");
        setParam (p, ParamIDs::fuzzBlend, 12.0f);
        check (pm.isDirty(), "dirty after edit");
        pm.loadPreset ("Frenzy");
        setParam (p, ParamIDs::oct2, 1.0f);
        setParam (p, ParamIDs::bypass, 1.0f);
        check (! pm.isDirty(), "footswitches / bypass do not affect preset state");
    }

    void testPresetBanks()
    {
        std::printf ("\nPreset banks (factory / user)\n");
        SwarmnessAudioProcessor p;
        auto& pm = p.getPresetManager();
        const auto factory = pm.getFactoryPresetNames();

        pm.loadPreset (factory[0]);
        pm.loadNextPreset (false);
        check (pm.getCurrentPresetName() == factory[1], "factory: next steps within the factory bank");
        pm.loadPreset (factory[0]);
        pm.loadPreviousPreset (false);
        check (pm.getCurrentPresetName() == factory[factory.size() - 1], "factory: previous wraps to the last factory preset");

        const juce::String a ("zz bank test A"), b ("zz bank test B");
        pm.saveUserPreset (a);
        pm.saveUserPreset (b);
        const auto user = pm.getUserPresetNames();
        bool noFactory = true;
        for (const auto& n : user)
            noFactory = noFactory && ! factory.contains (n);
        check (noFactory && user.contains (a) && user.contains (b), "user bank lists only user presets");

        pm.loadPreset (factory[factory.size() - 1]);
        pm.loadNextPreset (true);
        check (pm.getCurrentPresetName() == user[0], "user: from a factory preset, next lands on the first user preset");
        pm.loadPreset (b);
        pm.loadNextPreset (false);
        check (pm.getCurrentPresetName() == factory[0], "factory: from a user preset, next lands on the first factory preset");

        pm.loadPreset (b);
        pm.deleteUserPreset (b);
        check (pm.isUserPreset (pm.getCurrentPresetName()), "deleting a user preset moves to a neighbouring user preset");
        pm.deleteUserPreset (a);
    }

    void testPerformance()
    {
        std::printf ("\nPerformance (48 kHz, 128-sample blocks)\n");
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset ("Hive Collapse");
        setParam (p, ParamIDs::panic, 80.0f);
        setParam (p, ParamIDs::chaos, 50.0f);
        setParam (p, ParamIDs::speed, 50.0f);
        setParam (p, ParamIDs::rbSecondary, 50.0f);
        setParam (p, ParamIDs::swarmOn, 1.0f);
        setParam (p, ParamIDs::swarmDeep, 1.0f);
        setParam (p, ParamIDs::flowOn, 1.0f);
        setParam (p, ParamIDs::geqOn, 1.0f);
        setParam (p, ParamIDs::peqOn, 1.0f);
        setParam (p, ParamIDs::revOn, 1.0f);
        setParam (p, ParamIDs::revType, 3.0f);
        setParam (p, ParamIDs::oct1, 1.0f);
        setParam (p, ParamIDs::ampOn, 1.0f);
        setParam (p, ParamIDs::ampChannel, 2.0f);
        setParam (p, ParamIDs::ampGate, 30.0f);
        setParam (p, ParamIDs::cabOn, 1.0f);
        const double sr = 48000.0;
        auto input = makeGuitar (sr, (int) sr * 10);
        const auto t0 = std::chrono::steady_clock::now();
        auto out = render (p, input, sr, 128);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double load = secs / 10.0 * 100.0;
        check (load < 40.0, juce::String::formatted ("everything on (AMP + CAB too): %.2f%% of one core (realtime factor %.0fx)", load, 10.0 / secs));
    }

    //==========================================================================
    // Modular chain, COMB / CARVE EQs, CRYPT reverb

    void setOrder (SwarmnessAudioProcessor& p, std::initializer_list<int> blocks)
    {
        // blocks not listed keep their default place relative to each other, after the listed ones
        Chain::Order order {};
        int i = 0;
        for (int b : blocks)
            order[(size_t) i++] = b;
        for (int b : Chain::defaultOrder())
            if (std::find (blocks.begin(), blocks.end(), b) == blocks.end())
                order[(size_t) i++] = b;
        p.setChainOrder (order);
    }

    /** Gain (dB) of a sine through the plug-in, measured after it settles, latency-aligned. */
    double sineGainDb (SwarmnessAudioProcessor& p, double freq, double sr = 48000.0)
    {
        const int n = (int) sr;
        auto input = makeSine (sr, n, freq, 0.1f);
        auto out = render (p, input, sr, 256);
        const int from = n / 2;
        double eo = 0.0, ei = 0.0;
        for (int i = from; i < n; ++i)
        {
            eo += (double) out.getSample (0, i) * out.getSample (0, i);
            ei += (double) input.getSample (0, i) * input.getSample (0, i);
        }
        return 10.0 * std::log10 ((eo + 1.0e-30) / (ei + 1.0e-30));
    }

    void testChainOrder()
    {
        std::printf ("\nModular chain: order, latency, transparency, click-free reordering, migration\n");
        {
            std::array<float, Chain::numBlocks> slots {};
            for (int b = 0; b < Chain::numBlocks; ++b)
                slots[(size_t) b] = (float) Chain::defaultSlots[b];
            const auto def = Chain::orderFromSlots (slots);
            check (def[0] == Chain::smoke && def[1] == Chain::shift && def[2] == Chain::pitch && def[3] == Chain::drive && def[4] == Chain::amp
                   && def[5] == Chain::cab && def[6] == Chain::swarm && def[10] == Chain::crypt, "default order: SMOKE, SHIFT, HIVE, WASP, AMP, CAB, SWARM, ..., CRYPT");
            slots.fill (0.0f);   // all tied -> default order
            check (Chain::orderFromSlots (slots) == def, "tied slots fall back to the default order");
            Chain::Order custom { Chain::crypt, Chain::comb, Chain::cab, Chain::pitch, Chain::shift, Chain::wings, Chain::smoke, Chain::amp, Chain::carve, Chain::swarm, Chain::drive };
            check (Chain::orderFromSlots (Chain::slotsForOrder (custom)) == custom, "slots <-> order round trip");
        }

        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            p.prepareToPlay (sr, 256);
            const int latencyDefault = p.getLatencySamples();
            setOrder (p, { Chain::crypt, Chain::carve, Chain::comb, Chain::wings, Chain::swarm, Chain::pitch, Chain::shift, Chain::smoke });
            auto out = render (p, input, sr, 256);
            check (p.getLatencySamples() == latencyDefault, juce::String::formatted ("latency independent of the order (%d samples)", latencyDefault));
            const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
            check (db < -120.0, juce::String::formatted ("all blocks off, reversed chain: transparent (null %.1f dB)", db));
        }
        {
            // SMOKE -> CRYPT vs CRYPT -> SMOKE must sound different
            auto renderWith = [&] (std::initializer_list<int> order)
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::fuzzOn, 1.0f);
                setParam (p, ParamIDs::revOn, 1.0f);
                setParam (p, ParamIDs::revMix, 50.0f);
                setOrder (p, order);
                return render (p, input, sr, 256);
            };
            auto a = renderWith ({ Chain::smoke, Chain::shift, Chain::pitch, Chain::swarm, Chain::wings, Chain::comb, Chain::carve, Chain::crypt });
            auto b = renderWith ({ Chain::crypt, Chain::shift, Chain::pitch, Chain::swarm, Chain::wings, Chain::comb, Chain::carve, Chain::smoke });
            const double diff = nullDb (a, b, 0, 4096, a.getNumSamples());
            check (diff > -6.0 && allFinite (a) && allFinite (b), juce::String::formatted ("fuzz -> reverb differs from reverb -> fuzz (%.1f dB)", diff));
        }
        {
            // Reordering while a tone plays: a short dip, no clicks
            SwarmnessAudioProcessor p;
            resetToInit (p);
            auto tone = makeSine (sr, 48000, 440.0, 0.25f);
            p.prepareToPlay (sr, 128);
            juce::AudioBuffer<float> out (tone);
            juce::MidiBuffer midi;
            for (int start = 0; start < out.getNumSamples(); start += 128)
            {
                if (start == 24064)
                    setOrder (p, { Chain::wings, Chain::swarm, Chain::shift, Chain::pitch, Chain::smoke, Chain::comb, Chain::carve, Chain::crypt });
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, 128);
                p.processBlock (view, midi);
            }
            float maxStep = 0.0f;
            int quiet = 0;
            for (int i = 1; i < out.getNumSamples(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            for (int i = 24000; i < 26000; ++i)
                if (std::abs (out.getSample (0, i)) < 0.01f && std::abs (tone.getSample (0, i - p.getLatencySamples())) > 0.1f)
                    ++quiet;
            check (maxStep < 0.04f && quiet < 48 * 20, juce::String::formatted ("reorder mid-note: max step %.4f, dip %.1f ms", maxStep, quiet / 48.0));
            check (p.getRequestedChainOrder()[0] == Chain::wings, "the new order is active");
        }
        {
            // Sessions from beta.15: SMOKE POST -> SMOKE after SWARM
            SwarmnessAudioProcessor a;
            juce::MemoryBlock mb;
            a.getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            if (auto* slot = xml->getChildByAttribute ("id", Chain::slotIds[Chain::smoke]))
                xml->removeChildElement (slot, true);
            auto* legacy = xml->createNewChildElement ("PARAM");
            legacy->setAttribute ("id", "fuzzPost");
            legacy->setAttribute ("value", 1.0);
            juce::MemoryBlock legacyState;
            juce::AudioProcessor::copyXmlToBinary (*xml, legacyState);

            SwarmnessAudioProcessor b;
            b.setStateInformation (legacyState.getData(), (int) legacyState.getSize());
            const auto order = b.getRequestedChainOrder();
            auto pos = [&order] (int block) { return (int) (std::find (order.begin(), order.end(), block) - order.begin()); };
            check (pos (Chain::smoke) > pos (Chain::swarm) && pos (Chain::smoke) < pos (Chain::wings), "old session with SMOKE POST: SMOKE lands after SWARM");

            // ...and user presets
            auto file = juce::File::createTempFile (".swpreset");
            file.replaceWithText (R"({"name":"Legacy Post","plugin":"Swarmness","parameters":{"fuzzOn":1,"fuzzPost":1}})");
            b.getPresetManager().importPreset (file);
            const auto order2 = b.getRequestedChainOrder();
            auto pos2 = [&order2] (int block) { return (int) (std::find (order2.begin(), order2.end(), block) - order2.begin()); };
            check (pos2 (Chain::smoke) > pos2 (Chain::swarm), "old user preset with SMOKE POST: SMOKE lands after SWARM");

            // importing it again never replaces the user preset of the same name
            b.getPresetManager().importPreset (file);
            check (b.getPresetManager().isUserPreset ("Legacy Post") && b.getPresetManager().isUserPreset ("Legacy Post (2)")
                       && b.getPresetManager().getCurrentPresetName() == "Legacy Post (2)",
                   "importing a preset with an existing name keeps both (\"Legacy Post (2)\")");
            b.getPresetManager().deleteUserPreset ("Legacy Post (2)");
            b.getPresetManager().deleteUserPreset ("Legacy Post");
            file.deleteFile();
        }
    }

    void testSpliceContinuity()
    {
        // Every splice of the modern pitch engine ended with one sample of the faded-out head at full gain:
        // a click per splice (every ~20 ms on an octave up). A shifted sine must stay smooth.
        std::printf ("\nPitch engine: splices leave no clicks\n");
        const double sr = 48000.0;
        for (int semis : { 12, 7, -12 })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::shOn, 1.0f);
            setParam (p, ParamIDs::shiftA, (float) semis);
            setParam (p, ParamIDs::stingMix, 100.0f);
            setParam (p, ParamIDs::shRaw, 0.0f);
            auto out = render (p, makeSine (sr, (int) sr * 3, 220.0, 0.3f), sr, 256);
            const int from = (int) (0.5 * sr), to = (int) (2.5 * sr);
            double sumSq = 0.0, peak = 0.0;
            std::vector<float> d2 ((size_t) (to - from));
            for (int i = from; i < to; ++i)
            {
                const float v = out.getSample (0, i + 1) - 2.0f * out.getSample (0, i) + out.getSample (0, i - 1);
                d2[(size_t) (i - from)] = v;
                sumSq += (double) v * v;
                peak = juce::jmax (peak, (double) std::abs (v));
            }
            const double rms = std::sqrt (sumSq / (double) d2.size());
            const int outliers = (int) std::count_if (d2.begin(), d2.end(), [rms] (float v) { return std::abs (v) > 6.0 * rms; });
            check (outliers == 0 && peak / rms < 10.0 * std::sqrt (2.0),
                   juce::String::formatted ("SHIFT %+d st on a sine: 2nd-difference peak / rms %.1f dB, %d outlier samples", semis,
                                            20.0 * std::log10 (peak / rms), outliers));
        }
        // HIVE's REVERSE repeats: two heads read the loop backwards and crossfade - the wow / flutter on the
        // loop delay must not make a head jump when the phase wraps (a reversed sine stays a sine)
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbOn, 1.0f);
            setParam (p, ParamIDs::trDry, 1.0f);
            setParam (p, ParamIDs::rbPrimary, 0.0f);
            setParam (p, ParamIDs::rbTracking, 100.0f);
            setParam (p, ParamIDs::rbMagic, 70.0f);
            setParam (p, ParamIDs::rbTime, 300.0f);
            setParam (p, ParamIDs::rbMix, 100.0f);
            setParam (p, ParamIDs::rbRaw, 0.0f);
            for (int k = 0; k < 8; ++k)
            {
                setParam (p, ParamIDs::trLevels[k], 100.0f);
                setParam (p, ParamIDs::trMoves[k], (float) HiveBlock::reverse);
            }
            auto out = render (p, makeSine (sr, (int) sr * 4, 220.0, 0.3f), sr, 256);
            const int from = (int) (1.0 * sr), to = (int) (3.5 * sr);
            double sumSq = 0.0, peak = 0.0;
            std::vector<float> d2 ((size_t) (to - from));
            for (int i = from; i < to; ++i)
            {
                const float v = out.getSample (0, i + 1) - 2.0f * out.getSample (0, i) + out.getSample (0, i - 1);
                d2[(size_t) (i - from)] = v;
                sumSq += (double) v * v;
                peak = juce::jmax (peak, (double) std::abs (v));
            }
            const double rms = std::sqrt (sumSq / (double) d2.size());
            const int outliers = (int) std::count_if (d2.begin(), d2.end(), [rms] (float v) { return std::abs (v) > 6.0 * rms; });
            check (outliers == 0 && peak / rms < 10.0 * std::sqrt (2.0),
                   juce::String::formatted ("HIVE REVERSE repeats of a sine: 2nd-difference peak / rms %.1f dB, %d outlier samples",
                                            20.0 * std::log10 (peak / rms), outliers));
        }
    }

    void testNonFiniteInput()
    {
        std::printf ("\nNon-finite input\n");
        SwarmnessAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        for (auto* id : { ParamIDs::fuzzOn, ParamIDs::swarmOn, ParamIDs::revOn, ParamIDs::rbOn })
            setParam (p, id, 1.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> bad (2, 256);
        bad.clear();
        bad.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
        bad.setSample (1, 20, std::numeric_limits<float>::infinity());
        p.processBlock (bad, midi);
        auto guitar = makeGuitar (48000.0, 256 * 40);
        float peak = 0.0f;
        bool finite = true;
        for (int start = 0; start < guitar.getNumSamples(); start += 256)
        {
            juce::AudioBuffer<float> b (2, 256);
            for (int ch = 0; ch < 2; ++ch)
                b.copyFrom (ch, 0, guitar, juce::jmin (ch, guitar.getNumChannels() - 1), start, 256);
            p.processBlock (b, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    finite = finite && std::isfinite (b.getSample (ch, i));
            peak = juce::jmax (peak, b.getMagnitude (0, 256));
        }
        check (finite && peak > 0.01f, juce::String::formatted ("a NaN / Inf from the host doesn't silence it (peak after: %.3f)", peak));
    }

    void testParameterOrder()
    {
        std::printf ("\nParameter order (hosts that store automation by index)\n");
        SwarmnessAudioProcessor p;
        const auto& params = p.getParameters();
        const char* expected[] { "oct1", "oct2", "shiftA", "shiftB", "rise", "fall", "shOn", "stingMix", "shStack",
                                 "panic", "chaos", "speed", "shSnap", "shRaw", "shDetune", "rbOn" };
        bool same = params.size() > (int) std::size (expected);
        for (size_t i = 0; same && i < std::size (expected); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[(int) i]))
                same = r->getParameterID() == expected[i];
        check (same, "SHIFT's parameters come first, in their old order, then HIVE (" + juce::String (params.size()) + " parameters)");
    }

    void testMissingFiles()
    {
        std::printf ("\nSessions keep references to missing files\n");
        const auto gone = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("swarmness-gone").getChildFile ("Cab 4x12.wav");
        SwarmnessAudioProcessor a;
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->setAttribute ("cabIR", gone.getFullPathName());
        xml->setAttribute ("namModel", gone.withFileExtension ("nam").getFullPathName());
        juce::MemoryBlock withMissing;
        juce::AudioProcessor::copyXmlToBinary (*xml, withMissing);

        SwarmnessAudioProcessor b;
        b.setStateInformation (withMissing.getData(), (int) withMissing.getSize());
        juce::MemoryBlock saved;
        b.getStateInformation (saved);
        auto again = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        check (again->getStringAttribute ("cabIR") == gone.getFullPathName()
                   && again->getStringAttribute ("namModel") == gone.withFileExtension ("nam").getFullPathName(),
               "a missing IR / capture stays in the session after load + save");
        check (b.getCabIRDescription().startsWith ("Missing: ") && b.getCabIRFile() == juce::File(),
               "...and the CAB shows it as missing: \"" + b.getCabIRDescription() + "\"");
        b.clearCabIR();
        juce::MemoryBlock cleared;
        b.getStateInformation (cleared);
        check (juce::AudioProcessor::getXmlFromBinary (cleared.getData(), (int) cleared.getSize())->getStringAttribute ("cabIR").isEmpty(),
               "CLEAR drops the missing reference");
    }

    void setLanes (SwarmnessAudioProcessor& p, std::initializer_list<std::pair<int, int>> lanes)
    {
        for (auto [block, lane] : lanes)
            setParam (p, Chain::laneIds[block], (float) lane);
    }

    void testParallelRouting()
    {
        std::printf ("\nParallel paths (split -> A || B -> merge)\n");
        {
            Chain::Layout l { { Chain::smoke, Chain::shift, Chain::pitch, Chain::swarm, Chain::crypt, Chain::wings, Chain::comb, Chain::carve, Chain::drive, Chain::amp, Chain::cab }, {} };
            l.lanes[Chain::swarm] = Chain::pathA;
            l.lanes[Chain::crypt] = Chain::pathB;
            const auto plan = Chain::planFor (l);
            const auto& split = plan.stages[3];
            check (plan.numStages == 10 && plan.numSplits == 1 && split.parallel && split.numA == 1 && split.numB == 1
                   && split.a[0] == Chain::swarm && split.b[0] == Chain::crypt && plan.stages[4].block == Chain::wings,
                   "plan: SMOKE, SHIFT, HIVE -> [SWARM || CRYPT] -> WINGS, COMB, CARVE");

            // two splits with series blocks between: [SMOKE, SHIFT || HIVE] -> WASP -> AMP -> CAB -> SWARM -> [WINGS || COMB] -> CARVE -> CRYPT
            Chain::Layout two { Chain::defaultOrder(), {} };
            two.lanes[Chain::smoke] = Chain::pathA;  two.lanes[Chain::shift] = Chain::pathA;  two.lanes[Chain::pitch] = Chain::pathB;
            two.lanes[Chain::wings] = Chain::pathA;  two.lanes[Chain::comb]  = Chain::pathB;
            const auto p2 = Chain::planFor (two);
            check (p2.numSplits == 2 && p2.numStages == 8 && p2.stages[0].parallel && p2.stages[0].split == 0
                   && p2.stages[1].block == Chain::drive && p2.stages[2].block == Chain::amp && p2.stages[4].block == Chain::swarm
                   && p2.stages[5].parallel && p2.stages[5].split == 1 && p2.stages[5].a[0] == Chain::wings && p2.stages[5].b[0] == Chain::comb,
                   "two splits separated by a series block, each with its own MIX");
        }

        const double sr = 48000.0;
        auto input = makeGuitar (sr, 48000);
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setLanes (p, { { Chain::smoke, Chain::pathA }, { Chain::crypt, Chain::pathB } });
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
            check (db < -120.0, juce::String::formatted ("blocks off in both paths, MIX 50%%: transparent (null %.1f dB)", db));
        }
        {
            // SMOKE on path A, B empty (dry), MIX fully to B: exactly the dry signal, latency-aligned
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::fuzzOn, 1.0f);
            setLanes (p, { { Chain::smoke, Chain::pathA } });
            setParam (p, Chain::parallelMixIds[0], 100.0f);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
            check (db < -120.0, juce::String::formatted ("dry path B is aligned with SMOKE's latency (null %.1f dB)", db));

            setParam (p, Chain::parallelMixIds[0], 50.0f);
            auto blend = render (p, input, sr, 256);
            setLanes (p, { { Chain::smoke, Chain::series } });
            auto series = render (p, input, sr, 256);
            const double diff = nullDb (blend, series, 0, 4096, input.getNumSamples());
            check (diff > -10.0 && allFinite (blend), juce::String::formatted ("fuzz || dry differs from series fuzz (%.1f dB)", diff));
        }
        {
            // Two splits, both transparent while their blocks are off
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setLanes (p, { { Chain::smoke, Chain::pathA }, { Chain::swarm, Chain::pathA }, { Chain::crypt, Chain::pathB } });
            setParam (p, Chain::slotIds[Chain::crypt], 35.0f);
            setParam (p, Chain::parallelMixIds[1], 70.0f);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 4096, input.getNumSamples());
            check (db < -120.0, juce::String::formatted ("[SMOKE || dry] -> PITCH -> [SWARM || CRYPT], all off: transparent (null %.1f dB)", db));
        }
        {
            // Moving a block into a path while playing: short dip, no clicks
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::swarmOn, 1.0f);
            auto tone = makeSine (sr, 48000, 220.0, 0.25f);
            p.prepareToPlay (sr, 128);
            juce::AudioBuffer<float> out (tone);
            juce::MidiBuffer midi;
            for (int start = 0; start < out.getNumSamples(); start += 128)
            {
                if (start == 24064)
                    setLanes (p, { { Chain::swarm, Chain::pathB } });
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, 128);
                p.processBlock (view, midi);
            }
            float maxStep = 0.0f;
            for (int i = 1; i < out.getNumSamples(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            check (maxStep < 0.04f && allFinite (out), juce::String::formatted ("series -> parallel while playing: max step %.4f", maxStep));
        }
    }

    //==========================================================================
    // Unified HIVE block

    /** Level (dB) of one frequency in a window, Goertzel. */
    double toneDb (const juce::AudioBuffer<float>& b, double sr, double freq, int from, int length)
    {
        const double w = juce::MathConstants<double>::twoPi * freq / sr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < juce::jmin (b.getNumSamples(), from + length); ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - from) / length);
            const double s0 = hann * b.getSample (0, i) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return 10.0 * std::log10 (power / ((double) length * length) + 1.0e-30);
    }

    /** One plucked 220 Hz note (0.12 s) followed by silence. */
    juce::AudioBuffer<float> pluck (double sr, int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        b.clear();
        for (int i = 0; i < (int) (0.12 * sr); ++i)
        {
            const double t = i / sr;
            const float v = (float) (0.3 * std::exp (-t * 6.0) * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t));
            b.setSample (0, i + 4800, v);
            b.setSample (1, i + 4800, v);
        }
        return b;
    }

    void testHiveBlock()
    {
        std::printf ("\nHIVE: SHIFT intervals, FOLLOW, STEPS, migration\n");
        const double sr = 48000.0;
        {
            // SHIFT A as a fifth, SHIFT B wins while both are held
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, 0.0f);
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::shiftA, 7.0f);
            setParam (p, ParamIDs::shiftB, -12.0f);
            setParam (p, ParamIDs::oct1, 1.0f);
            auto input = makeSine (sr, 48000 * 2, 220.0);
            double purity = 0.0;
            const double f1 = dominantFrequency (render (p, input, sr, 256), sr, 48000, purity);
            check (std::abs (1200.0 * std::log2 (f1 / 329.628)) < 5.0, juce::String::formatted ("SHIFT A = +7 (fifth): %.2f Hz (expected 329.63)", f1));
            setParam (p, ParamIDs::oct2, 1.0f);
            const double f2 = dominantFrequency (render (p, input, sr, 256), sr, 48000, purity);
            check (std::abs (1200.0 * std::log2 (f2 / 110.0)) < 5.0, juce::String::formatted ("A + B held: B wins (-12): %.2f Hz", f2));
        }
        {
            // The chain order decides what HIVE harmonises: after SHIFT (default) the shifted note,
            // with SHIFT || HIVE in parallel the played one
            auto droneAt = [&] (bool parallel)
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::rbRaw, 0.0f);
                setParam (p, ParamIDs::shRaw, 0.0f);
                setParam (p, ParamIDs::rise, 0.0f);
                setParam (p, ParamIDs::oct1, 1.0f);          // SHIFT A +12 -> 440
                setParam (p, ParamIDs::rbOn, 1.0f);
                setParam (p, ParamIDs::rbPitch, 7.0f);
                setParam (p, ParamIDs::rbPrimary, 100.0f);
                setParam (p, ParamIDs::rbTracking, 100.0f);
                if (parallel)
                    setLanes (p, { { Chain::shift, Chain::pathA }, { Chain::pitch, Chain::pathB } });
                auto out = render (p, makeSine (sr, 48000 * 2, 220.0), sr, 256);
                return std::pair<double, double> { toneDb (out, sr, 329.63, 48000, 32768), toneDb (out, sr, 659.26, 48000, 32768) };
            };
            const auto par = droneAt (true), ser = droneAt (false);
            check (ser.second > ser.first + 10.0, juce::String::formatted ("SHIFT -> HIVE: DRONE on the shifted note (659 Hz %+.1f dB vs 329.6 Hz %+.1f dB)", ser.second, ser.first));
            check (par.first > par.second + 10.0, juce::String::formatted ("SHIFT || HIVE: DRONE on the played note (329.6 Hz %+.1f dB vs 659 Hz %+.1f dB)", par.first, par.second));
        }
        {
            // SHIFT power button: SHIFT A all the time (no footswitch), and the plug-in's ON still bypasses it
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::shRaw, 0.0f);
            setParam (p, ParamIDs::rise, 0.0f);
            setParam (p, ParamIDs::shOn, 1.0f);
            auto input = makeSine (sr, 48000 * 2, 220.0);
            double purity = 0.0;
            const double f = dominantFrequency (render (p, input, sr, 256), sr, 48000, purity);
            check (std::abs (1200.0 * std::log2 (f / 440.0)) < 5.0, juce::String::formatted ("SHIFT on, no footswitch: +1 oct all the time (%.2f Hz)", f));
            setParam (p, ParamIDs::bypass, 1.0f);
            const double fb = dominantFrequency (render (p, input, sr, 256), sr, 48000, purity);
            check (std::abs (1200.0 * std::log2 (fb / 220.0)) < 5.0, juce::String::formatted ("plug-in bypassed: SHIFT on is bypassed too (%.2f Hz)", fb));
        }
        {
            // HIVE as a delay: DRY feeds the repeats from the note itself (DRONE at 0), HOLD steps = plain echoes
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::rbRaw, 0.0f);
            setParam (p, ParamIDs::rbOn, 1.0f);
            setParam (p, ParamIDs::rbPrimary, 0.0f);
            setParam (p, ParamIDs::rbTracking, 100.0f);
            setParam (p, ParamIDs::rbMagic, 70.0f);
            setParam (p, ParamIDs::rbTone, 100.0f);
            setParam (p, ParamIDs::rbTime, 250.0f);
            setParam (p, ParamIDs::trDry, 1.0f);
            PresetManager::ValueMap values;
            PresetManager::writeTrailFill (values, HiveBlock::fillEcho);
            for (const auto& [id, v] : values)
                setParam (p, id.toRawUTF8(), v);
            auto out = render (p, pluck (sr, (int) (sr * 1.5)), sr, 128);
            const double echo = toneDb (out, sr, 220.0, (int) (0.37 * sr), 4096), gap = toneDb (out, sr, 220.0, (int) (0.26 * sr), 2048);
            const double note = toneDb (out, sr, 220.0, 4800, 4096);
            check (echo > note - 12.0 && echo > gap + 10.0,
                   juce::String::formatted ("DRY: an echo of the played note at TIME (%.1f dB vs the note, %.1f dB quieter just before it)", echo - note, echo - gap));
        }
        {
            // STACK: A + B held = both intervals; off = B wins
            auto stacked = [&] (bool stack)
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::shRaw, 0.0f);
                setParam (p, ParamIDs::rise, 0.0f);
                setParam (p, ParamIDs::shStack, stack ? 1.0f : 0.0f);
                setParam (p, ParamIDs::oct1, 1.0f);          // +12 -> 440
                setParam (p, ParamIDs::oct2, 1.0f);          // +24 -> 880
                auto out = render (p, makeSine (sr, 48000 * 2, 220.0), sr, 256);
                return std::pair<double, double> { toneDb (out, sr, 440.0, 48000, 32768), toneDb (out, sr, 880.0, 48000, 32768) };
            };
            const auto off = stacked (false), on = stacked (true);
            check (off.second > off.first + 15.0, juce::String::formatted ("A + B, STACK off: B wins (880 Hz %+.1f dB, 440 Hz %+.1f dB)", off.second, off.first));
            check (std::abs (on.first - on.second) < 6.0 && on.first > off.first + 15.0,
                   juce::String::formatted ("A + B, STACK on: both sound (440 Hz %+.1f dB, 880 Hz %+.1f dB)", on.first, on.second));
        }
        {
            // STEPS: the first repeats of a plucked 220 Hz note, DRONE a fifth up
            auto repeats = [&] (int fill, float gate = 100.0f)
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::rbRaw, 0.0f);
                setParam (p, ParamIDs::rbOn, 1.0f);
                setParam (p, ParamIDs::rbPitch, 7.0f);
                setParam (p, ParamIDs::rbPrimary, 100.0f);
                setParam (p, ParamIDs::rbTracking, 100.0f);
                setParam (p, ParamIDs::rbMix, 100.0f);
                setParam (p, ParamIDs::rbMagic, 80.0f);
                setParam (p, ParamIDs::rbTone, 100.0f);
                setParam (p, ParamIDs::rbTime, 250.0f);
                setParam (p, ParamIDs::trChop, (100.0f - gate) / 0.95f);
                PresetManager::ValueMap values;
                PresetManager::writeTrailFill (values, fill);
                for (const auto& [id, v] : values)
                    setParam (p, id.toRawUTF8(), v);
                return render (p, pluck (sr, (int) (sr * 2.5)), sr, 128);
            };
            // note at 0.1 s; repeat 1 (step 1) around 0.35..0.45 s, repeat 2 (step 2) around 0.6..0.7 s
            const int r1 = (int) (0.37 * sr), r2 = (int) (0.62 * sr), len = 4096;
            const auto ladder = repeats (HiveBlock::fillLadder);
            check (toneDb (ladder, sr, 493.88, r1, len) > toneDb (ladder, sr, 220.0, r1, len) + 6.0,
                   juce::String::formatted ("STEPS Ladder (all UP): repeat 1 one more fifth up (494 Hz %+.1f dB vs 220 Hz %+.1f dB)",
                                            toneDb (ladder, sr, 493.88, r1, len), toneDb (ladder, sr, 220.0, r1, len)));
            const auto bounceOut = repeats (HiveBlock::fillBounce);
            const double b1note = toneDb (bounceOut, sr, 220.0, r1, len), b1up = toneDb (bounceOut, sr, 493.88, r1, len);
            const double b2fifth = toneDb (bounceOut, sr, 329.63, r2, len), b2note = toneDb (bounceOut, sr, 220.0, r2, len);
            check (b1note > b1up + 6.0 && b2fifth > b2note + 3.0,
                   juce::String::formatted ("STEPS Bounce (DOWN, UP): repeat 1 back on the note (220 Hz %+.1f vs 494 Hz %+.1f dB), repeat 2 up again (330 Hz %+.1f vs 220 Hz %+.1f dB)",
                                            b1note, b1up, b2fifth, b2note));
            {
                auto side = [&] (int from) { return 20.0 * std::log10 ((bounceOut.getRMSLevel (0, from, len) + 1e-9) / (bounceOut.getRMSLevel (1, from, len) + 1e-9)); };
                const double s1 = side (r1), s2 = side (r2);
                check (s1 * s2 < 0.0 && std::abs (s1 - s2) > 6.0,
                       juce::String::formatted ("DOWN / UP steps ping-pong: repeat 1 L/R %+.1f dB, repeat 2 %+.1f dB", s1, s2));
            }
            {
                // LEVEL 0 = a silent repeat: Offbeat (0, 1) mutes repeat 1, repeat 2 still sounds (the tail runs on)
                const auto off = repeats (HiveBlock::fillOffbeat);
                const double q1 = juce::Decibels::gainToDecibels (off.getRMSLevel (0, r1, len) + 1.0e-9f);
                const double q2 = juce::Decibels::gainToDecibels (off.getRMSLevel (0, r2, len) + 1.0e-9f);
                check (q2 > q1 + 15.0, juce::String::formatted ("STEP LEVEL 0: repeat 1 silent (%.1f dB), repeat 2 sounds (%.1f dB)", q1, q2));

                // GATE chops each step: with 30% the second half of every repeat is (almost) silent
                const auto chopped = repeats (HiveBlock::fillEcho, 30.0f);
                const auto full = repeats (HiveBlock::fillEcho, 100.0f);
                const int late = (int) (0.35 * sr + 0.6 * 0.25 * sr);
                const double cut = juce::Decibels::gainToDecibels (chopped.getRMSLevel (0, late, 2400) + 1.0e-9f)
                                 - juce::Decibels::gainToDecibels (full.getRMSLevel (0, late, 2400) + 1.0e-9f);
                check (cut < -15.0, juce::String::formatted ("GATE 30%%: the end of each step is chopped (%.1f dB vs full)", cut));
            }
            for (int fill = 0; fill < HiveBlock::numFills; ++fill)
            {
                const auto out = fill == 0 ? ladder : (fill == 1 ? bounceOut : repeats (fill));
                const float tail = out.getRMSLevel (0, (int) (0.35 * sr), (int) (1.0 * sr));
                const float end  = out.getRMSLevel (0, (int) (2.2 * sr), (int) (0.3 * sr));
                check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 2.0f && tail > 5.0e-4f && end < tail,
                       juce::String::formatted ("FILL %s: repeats after the note (RMS %.4f), fading (%.5f later), bounded",
                                                ParamChoices::trailFills[fill].toRawUTF8(), tail, end));
            }
        }
        {
            // Old sessions / presets: DIVE -> negative SHIFT intervals, STING RAW / DETUNE -> shared
            SwarmnessAudioProcessor a;
            juce::MemoryBlock mb;
            a.getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            for (auto* id : { ParamIDs::shiftA, ParamIDs::shiftB, ParamIDs::shRaw, ParamIDs::shDetune, ParamIDs::shSnap,
                              ParamIDs::hvMangle, Chain::slotIds[Chain::shift], Chain::laneIds[Chain::shift] })
                if (auto* e = xml->getChildByAttribute ("id", id))
                    xml->removeChildElement (e, true);
            auto addLegacy = [&xml] (const char* id, double v)
            {
                auto* e = xml->createNewChildElement ("PARAM");
                e->setAttribute ("id", id);
                e->setAttribute ("value", v);
            };
            addLegacy ("noiseDown", 1.0);
            addLegacy ("stingRaw", 0.0);
            addLegacy ("stingDetune", -20.0);
            addLegacy ("hivePattern", 2.0);   // beta.20-22 SCATTER
            addLegacy ("trGate", 43.0);       // beta.23-25 GATE: 43% of each step sounds
            if (auto* e = xml->getChildByAttribute ("id", ParamIDs::trChop))
                xml->removeChildElement (e, true);
            for (auto* id : { ParamIDs::trMoves[0], ParamIDs::trMoves[5] })
                if (auto* e = xml->getChildByAttribute ("id", id))
                    xml->removeChildElement (e, true);
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary (*xml, legacy);
            SwarmnessAudioProcessor b;
            b.setStateInformation (legacy.getData(), (int) legacy.getSize());
            auto v = [&b] (const char* id) { return b.getAPVTS().getRawParameterValue (id)->load(); };
            check (v (ParamIDs::shiftA) < -11.5f && v (ParamIDs::shiftB) < -23.5f && v (ParamIDs::shRaw) < 0.5f && std::abs (v (ParamIDs::shDetune) + 20.0f) < 0.1f,
                   "old session: DIVE -> SHIFT -12 / -24, STING RAW off and DETUNE -20 ct carried over to SHIFT");
            check (b.getRequestedLayout().order[0] == Chain::smoke && b.getRequestedLayout().order[1] == Chain::shift
                   && b.getRequestedLayout().order[2] == Chain::pitch, "old session: SHIFT lands right before HIVE");
            {
                PresetManager::ValueMap beta25 { { "hvAnger", 0.0f }, { "hvFrenzy", 55.0f }, { "hvBuzz", 0.0f }, { ParamIDs::shRaw, 1.0f } };
                PresetManager::migrateLegacyValues (beta25);
                const auto m = HiveBlock::mangleFor (beta25[ParamIDs::hvMangle] * 0.01f);
                check (beta25.count ("hvFrenzy") == 0 && std::abs (m.frenzy - 0.55f) < 0.02f,
                       juce::String::formatted ("beta.25 HIVE FRENZY 55%% -> MANGLE %.0f%% (frenzy %.2f)", beta25[ParamIDs::hvMangle], m.frenzy));
            }
            check (juce::roundToInt (v (ParamIDs::trMoves[0])) == HiveBlock::random && juce::roundToInt (v (ParamIDs::trMoves[5])) == HiveBlock::random
                   && juce::roundToInt (v (ParamIDs::trSteps)) == 8,
                   "old session: PATTERN SCATTER -> 8 RANDOM steps");
            check (std::abs (v (ParamIDs::trChop) - 60.0f) < 0.5f, juce::String::formatted ("old session: GATE 43%% open -> GATE %.0f%% chop", v (ParamIDs::trChop)));

            auto file = juce::File::createTempFile (".swpreset");
            file.replaceWithText (R"({"name":"Legacy Dive","plugin":"Swarmness","parameters":{"noiseDown":1,"rise":400}})");
            b.getPresetManager().importPreset (file);
            check (v (ParamIDs::shiftA) < -11.5f && std::abs (v (ParamIDs::rise) - 400.0f) < 0.5f, "old user preset with DIVE imports as SHIFT -12");
            b.getPresetManager().deleteUserPreset ("Legacy Dive");
            file.deleteFile();
        }
    }

    void testGraphicEq()
    {
        std::printf ("\nCOMB graphic EQ\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::geqOn, 1.0f);
        setParam (p, ParamIDs::geqBands[5], 12.0f);    // 1 kHz
        setParam (p, ParamIDs::geqBands[1], -12.0f);   // 62 Hz
        const double at1k = sineGainDb (p, 1000.0), at62 = sineGainDb (p, 62.5), at8k = sineGainDb (p, 8000.0);
        check (std::abs (at1k - 12.0) < 1.0, juce::String::formatted ("1 kHz band +12 dB: %+.2f dB", at1k));
        check (std::abs (at62 + 12.0) < 1.0, juce::String::formatted ("62 Hz band -12 dB: %+.2f dB", at62));
        check (std::abs (at8k) < 1.0, juce::String::formatted ("8 kHz untouched: %+.2f dB", at8k));

        std::array<float, swarm::GraphicEq::numBands> gains {};
        gains[5] = 12.0f;
        gains[1] = -12.0f;
        const float predicted = swarm::GraphicEq::responseDb (gains, 0.0f, 48000.0, 1000.0);
        check (std::abs (predicted - (float) at1k) < 0.3f, juce::String::formatted ("editor curve matches the DSP (%.2f vs %.2f dB)", predicted, at1k));

        setParam (p, ParamIDs::geqLevel, -6.0f);
        const double lvl = sineGainDb (p, 8000.0);
        check (std::abs (lvl + 6.0) < 0.5, juce::String::formatted ("LEVEL -6 dB: %+.2f dB", lvl));
    }

    void testParametricEq()
    {
        std::printf ("\nCARVE parametric EQ\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        setParam (p, ParamIDs::peqOn, 1.0f);
        setParam (p, ParamIDs::peqHpFreq, 200.0f);
        const double at50 = sineGainDb (p, 50.0), at2k = sineGainDb (p, 2000.0);
        check (at50 < -40.0, juce::String::formatted ("low cut 200 Hz, 24 dB/oct: 50 Hz at %.1f dB", at50));
        check (std::abs (at2k) < 0.5, juce::String::formatted ("2 kHz passes: %+.2f dB", at2k));

        setParam (p, ParamIDs::peqHpFreq, ParamRanges::peqHpOff);
        setParam (p, ParamIDs::peqB2Freq, 800.0f);
        setParam (p, ParamIDs::peqB2Gain, 10.0f);
        setParam (p, ParamIDs::peqB2Q, 2.0f);
        setParam (p, ParamIDs::peqLpFreq, 5000.0f);
        const double bell = sineGainDb (p, 800.0), cut = sineGainDb (p, 15000.0);
        check (std::abs (bell - 10.0) < 0.5, juce::String::formatted ("bell 800 Hz +10 dB: %+.2f dB", bell));
        check (cut < -30.0, juce::String::formatted ("high cut 5 kHz: 15 kHz at %.1f dB", cut));

        swarm::ParametricEq::Settings s;
        s.bellHz[1] = 800.0f; s.bellDb[1] = 10.0f; s.bellQ[1] = 2.0f; s.lpHz = 5000.0f;
        const float predicted = swarm::ParametricEq::responseDb (s, 48000.0, 800.0);
        check (std::abs (predicted - (float) bell) < 0.3f, juce::String::formatted ("editor curve matches the DSP (%.2f vs %.2f dB)", predicted, bell));

        // Sweeping a band hard while playing stays finite and click-free-ish
        auto input = makeGuitar (48000.0, 48000);
        p.prepareToPlay (48000.0, 64);
        juce::MidiBuffer midi;
        float peak = 0.0f;
        for (int start = 0; start < input.getNumSamples(); start += 64)
        {
            setParam (p, ParamIDs::peqB1Freq, 40.0f * std::pow (400.0f, (float) (start % 12000) / 12000.0f));
            setParam (p, ParamIDs::peqB1Gain, (start / 64) % 2 == 0 ? 18.0f : -18.0f);
            juce::AudioBuffer<float> view (input.getArrayOfWritePointers(), 2, start, 64);
            p.processBlock (view, midi);
            peak = juce::jmax (peak, view.getMagnitude (0, 0, 64));
        }
        check (allFinite (input) && peak < 2.0f, juce::String::formatted ("wild automation: finite, peak %.2f", peak));
    }

    /** RT60 from the backward-integrated energy decay of an impulse response (-5..-25 dB fit). */
    double measureRt60 (const juce::AudioBuffer<float>& ir, int start, double sr)
    {
        const int n = ir.getNumSamples();
        std::vector<double> edc ((size_t) n, 0.0);
        double acc = 0.0;
        for (int i = n - 1; i >= start; --i)
        {
            const double v = 0.5 * ((double) ir.getSample (0, i) * ir.getSample (0, i) + (double) ir.getSample (1, i) * ir.getSample (1, i));
            acc += v;
            edc[(size_t) i] = acc;
        }
        const double total = edc[(size_t) start];
        int t5 = -1, t25 = -1;
        for (int i = start; i < n; ++i)
        {
            const double db = 10.0 * std::log10 (edc[(size_t) i] / total + 1.0e-30);
            if (t5 < 0 && db <= -5.0) t5 = i;
            if (t25 < 0 && db <= -25.0) { t25 = i; break; }
        }
        if (t5 < 0 || t25 < 0)
            return -1.0;
        return 3.0 * (t25 - t5) / sr;   // 20 dB span x 3 = 60 dB
    }

    juce::AudioBuffer<float> reverbImpulse (SwarmnessAudioProcessor& p, double sr, double seconds)
    {
        juce::AudioBuffer<float> in (2, (int) (sr * seconds));
        in.clear();
        in.setSample (0, 4800, 0.5f);   // after the start-up fades
        in.setSample (1, 4800, 0.5f);
        return render (p, in, sr, 256);
    }

    void testReverb()
    {
        std::printf ("\nCRYPT reverb\n");
        const double sr = 48000.0;
        for (int type : { 0, 2, 3 })
            for (float decay : { 1.0f, 4.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::revOn, 1.0f);
                setParam (p, ParamIDs::revMix, 100.0f);
                setParam (p, ParamIDs::revType, (float) type);
                setParam (p, ParamIDs::revDecay, decay);
                setParam (p, ParamIDs::revTone, 80.0f);
                auto out = reverbImpulse (p, sr, decay * 1.6 + 0.5);
                const double rt = measureRt60 (out, 4800, sr);
                check (rt > decay * 0.65 && rt < decay * 1.35 && allFinite (out),
                       juce::String::formatted ("%s, DECAY %.1f s: measured RT60 %.2f s", ParamChoices::reverbTypes[type].toRawUTF8(), decay, rt));
            }

        {
            // quality of the HALL tail: wide (decorrelated L / R), dense, highs die faster than lows with a dark TONE
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revMix, 100.0f);
            setParam (p, ParamIDs::revDecay, 3.0f);
            setParam (p, ParamIDs::revTone, 20.0f);
            setParam (p, ParamIDs::revLowCut, 20.0f);
            auto out = reverbImpulse (p, sr, 3.0);
            const int from = 4800 + (int) (0.15 * sr), len = (int) (0.5 * sr);
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (int i = from; i < from + len; ++i)
            {
                const double l = out.getSample (0, i), r = out.getSample (1, i);
                lr += l * r; ll += l * l; rr += r * r;
            }
            const double corr = lr / std::sqrt (ll * rr + 1.0e-30);
            check (std::abs (corr) < 0.3, juce::String::formatted ("HALL tail: left / right correlation %.2f (wide)", corr));

            // echo density: in 50..100 ms after the hit, most 1 ms windows already carry energy
            int filled = 0;
            const float tailRms = out.getRMSLevel (0, 4800 + 2400, 2400);
            for (int w = 0; w < 50; ++w)
                if (out.getRMSLevel (0, 4800 + 2400 + w * 48, 48) > 0.2f * tailRms)
                    ++filled;
            check (filled > 40, juce::String::formatted ("HALL: dense after 50 ms (%d of 50 ms windows filled)", filled));

            // two-band decay: 4 kHz loses more between 0.2 s and 1.0 s than 250 Hz does
            auto band = [&] (double hz, double t) { return toneDb (out, sr, hz, 4800 + (int) (t * sr), 8192); };
            const double lowDrop = band (250.0, 0.2) - band (250.0, 1.0), highDrop = band (4000.0, 0.2) - band (4000.0, 1.0);
            check (highDrop > lowDrop + 6.0, juce::String::formatted ("dark TONE: 4 kHz decays faster (%.1f dB) than 250 Hz (%.1f dB)", highDrop, lowDrop));
        }
        {
            // level: noise in, 100% wet at a medium hall
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revMix, 100.0f);
            auto input = makeGuitar (sr, (int) sr * 3);
            auto out = render (p, input, sr, 256);
            const double ratio = 10.0 * std::log10 (std::pow (out.getRMSLevel (0, (int) sr, (int) sr * 2), 2.0) / std::pow (input.getRMSLevel (0, (int) sr, (int) sr * 2), 2.0));
            check (ratio > -9.0 && ratio < 3.0, juce::String::formatted ("wet level vs dry (HALL 2.5 s): %+.1f dB", ratio));
        }
        {
            // extreme settings: 20 s ABYSS, full MOD, loud input
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revMix, 100.0f);
            setParam (p, ParamIDs::revType, 3.0f);
            setParam (p, ParamIDs::revDecay, 20.0f);
            setParam (p, ParamIDs::revMod, 100.0f);
            setParam (p, ParamIDs::revTone, 100.0f);
            setParam (p, ParamIDs::revSize, 100.0f);
            auto input = makeGuitar (sr, (int) sr * 8);
            input.applyGain (3.0f);
            auto out = render (p, input, sr, 512);
            const float lateRms = out.getRMSLevel (0, (int) sr * 6, (int) sr * 2);
            check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) <= 2.0f && lateRms < 1.0f,
                   juce::String::formatted ("ABYSS 20 s, MOD 100%%: bounded (late RMS %.2f)", lateRms));
        }
        {
            // switching off: the tail rings out (spill-over), then the block goes idle and is exactly dry again
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revDecay, 0.5f);
            setParam (p, ParamIDs::revMix, 50.0f);
            auto input = makeGuitar (sr, (int) sr * 4);
            for (int i = (int) sr; i < input.getNumSamples(); ++i)
                input.setSample (0, i, 0.0f), input.setSample (1, i, 0.0f);
            p.prepareToPlay (sr, 256);
            juce::AudioBuffer<float> out (input);
            juce::MidiBuffer midi;
            for (int start = 0; start < out.getNumSamples(); start += 256)
            {
                if (start == 47872)
                    setParam (p, ParamIDs::revOn, 0.0f);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, 256);
                p.processBlock (view, midi);
            }
            const float tail = out.getRMSLevel (0, (int) sr + 2000, 4000);
            check (tail > 1.0e-3f, juce::String::formatted ("switched off: tail rings out (RMS %.4f after the input stops)", tail));

            auto more = makeGuitar (sr, (int) sr);
            auto dry = more;
            juce::AudioBuffer<float> out2 (more);
            for (int start = 0; start < out2.getNumSamples(); start += 256)
            {
                juce::AudioBuffer<float> view (out2.getArrayOfWritePointers(), 2, start, juce::jmin (256, out2.getNumSamples() - start));
                p.processBlock (view, midi);
            }
            const double db = nullDb (out2, dry, p.getLatencySamples(), 4096, out2.getNumSamples());
            check (db < -120.0, juce::String::formatted ("after the tail: exactly dry again (null %.1f dB)", db));
        }
        {
            // DUCK: the wet dips while playing
            auto wetWhilePlaying = [&] (float duck)
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::revOn, 1.0f);
                setParam (p, ParamIDs::revMix, 100.0f);
                setParam (p, ParamIDs::revDuck, duck);
                auto out = render (p, makeGuitar (sr, (int) sr * 2), sr, 256);
                return out.getRMSLevel (0, (int) sr / 2, (int) sr);
            };
            const float plain = wetWhilePlaying (0.0f), ducked = wetWhilePlaying (100.0f);
            const double dip = 20.0 * std::log10 (ducked / plain);
            check (dip < -6.0, juce::String::formatted ("DUCK 100%%: wet %.1f dB while playing", dip));
        }
        {
            // IR mode
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revMix, 100.0f);
            setParam (p, ParamIDs::revType, 4.0f);
            auto silent = reverbImpulse (p, sr, 0.5);
            check (silent.getMagnitude (0, silent.getNumSamples()) < 1.0e-6f, "IR mode without an IR: silent wet");

            // write a 0.6 s decaying-noise IR
            auto irFile = juce::File::createTempFile (".wav");
            {
                juce::AudioBuffer<float> ir (2, (int) (sr * 0.6));
                juce::Random rng (7);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < ir.getNumSamples(); ++i)
                        ir.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / (float) (sr * 0.08)));
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = irFile.createOutputStream();
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
                writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }
            const auto error = p.loadReverbIR (irFile);
            check (error.isEmpty() && p.getReverbIRDescription().contains ("0.6 s"), "IR loads: " + p.getReverbIRDescription() + error);

            // the convolution prepares the IR on a background thread: feed blocks until it answers
            p.prepareToPlay (sr, 256);
            juce::MidiBuffer midi;
            float response = 0.0f;
            for (int attempt = 0; attempt < 200 && response < 1.0e-3f; ++attempt)
            {
                juce::AudioBuffer<float> b (2, 256);
                b.clear();
                if (attempt % 20 == 0)
                    b.setSample (0, 0, 0.5f), b.setSample (1, 0, 0.5f);
                p.processBlock (b, midi);
                response = juce::jmax (response, b.getMagnitude (0, 256));
                juce::Thread::sleep (5);
            }
            check (response > 1.0e-3f, juce::String::formatted ("IR mode convolves (peak %.3f)", response));
            {
                juce::MidiBuffer m;
                auto input = makeGuitar (sr, (int) sr * 2);
                juce::AudioBuffer<float> out (input);
                for (int start = 0; start < out.getNumSamples(); start += 256)
                {
                    juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, juce::jmin (256, out.getNumSamples() - start));
                    p.processBlock (view, m);
                }
                const double ratio = 20.0 * std::log10 (out.getRMSLevel (0, (int) sr / 2, (int) sr) / input.getRMSLevel (0, (int) sr / 2, (int) sr));
                check (ratio > -9.0 && ratio < 3.0, juce::String::formatted ("IR wet level vs dry: %+.1f dB", ratio));
            }

            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            SwarmnessAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (q.getReverbIRFile() == irFile, "IR path saved with the session");
            p.clearReverbIR();
            check (p.getReverbIRDescription().isEmpty(), "IR cleared");
            irFile.deleteFile();
        }
    }

    void renderPresets (const juce::File& dir)
    {
        dir.createDirectory();
        const double sr = 48000.0;
        auto input = makeGuitar (sr, (int) sr * 4);
        SwarmnessAudioProcessor p;
        for (const auto& name : p.getPresetManager().getFactoryPresetNames())
        {
            p.getPresetManager().loadPreset (name);
            setParam (p, ParamIDs::flowSync, 0.0f);
            auto out = renderWithFootswitch (p, input, sr, 256, ParamIDs::oct1, true);
            auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            if (stream != nullptr)
                if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                                            .withNumChannels (2)
                                                                                            .withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            std::printf ("  rendered %s\n", file.getFullPathName().toRawUTF8());
        }
    }
    //==============================================================================
    /** One AMP / CAB block on its own (no chain), 48 kHz. */
    juce::AudioBuffer<float> renderAmp (const AmpBlock::Settings& s, const juce::AudioBuffer<float>& input, AmpBlock* use = nullptr)
    {
        AmpBlock local;
        AmpBlock& a = use != nullptr ? *use : local;
        if (use == nullptr)
            a.prepare (48000.0, 256);
        a.setParams (s);
        juce::AudioBuffer<float> out (input);
        for (int start = 0; start < out.getNumSamples(); start += 256)
        {
            const int n = juce::jmin (256, out.getNumSamples() - start);
            float* ptr[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            a.process (ptr, 2, n);
        }
        return out;
    }

    juce::File namExample (const char* name)
    {
       #ifdef SWARMNESS_NAM_EXAMPLES
        return juce::File (SWARMNESS_NAM_EXAMPLES).getChildFile (name);
       #else
        juce::ignoreUnused (name);
        return {};
       #endif
    }

    void testAmp()
    {
        std::printf ("\nAMP: channels, CHARACTER, tone stack, gain, NAM\n");
        const double sr = 48000.0;
        auto guitar = makeGuitar (sr, 48000);

        // FMV tone stack: the passive circuit's classic shape
        {
            const float lo = AmpBlock::toneStackDb (AmpBlock::clean, 1.0f, 0.5f, 0.5f, 0.5f, 100.0f);
            const float midDip = AmpBlock::toneStackDb (AmpBlock::clean, 1.0f, 0.5f, 0.5f, 0.5f, 500.0f);
            const float hi = AmpBlock::toneStackDb (AmpBlock::clean, 1.0f, 0.5f, 0.5f, 0.5f, 5000.0f);
            check (midDip < lo - 3.0f && midDip < hi - 3.0f,
                   juce::String::formatted ("CLEAN stack at noon: scooped mids (100 Hz %.1f, 500 Hz %.1f, 5 kHz %.1f dB)", lo, midDip, hi));
            const float bassUp = AmpBlock::toneStackDb (AmpBlock::crunch, 0.0f, 1.0f, 0.5f, 0.5f, 80.0f)
                               - AmpBlock::toneStackDb (AmpBlock::crunch, 0.0f, 0.0f, 0.5f, 0.5f, 80.0f);
            const float trebleUp = AmpBlock::toneStackDb (AmpBlock::crunch, 0.0f, 0.5f, 0.5f, 1.0f, 4000.0f)
                                 - AmpBlock::toneStackDb (AmpBlock::crunch, 0.0f, 0.5f, 0.5f, 0.0f, 4000.0f);
            const float midUp = AmpBlock::toneStackDb (AmpBlock::lead, 0.0f, 0.5f, 1.0f, 0.5f, 600.0f)
                              - AmpBlock::toneStackDb (AmpBlock::lead, 0.0f, 0.5f, 0.0f, 0.5f, 600.0f);
            check (bassUp > 6.0f && trebleUp > 6.0f && midUp > 6.0f,
                   juce::String::formatted ("BASS / TREBLE / MID ranges: %.1f / %.1f / %.1f dB", bassUp, trebleUp, midUp));
        }

        // The three channel amps (CLEAN = CHROME, CRUNCH = BRIT, LEAD = STEEL): bounded, level-matched
        double minDb = 1e9, maxDb = -1e9;
        for (int chn = 0; chn < 3; ++chn)
            for (float character : { AmpBlock::channelSide (chn) })
            {
                AmpBlock::Settings s;
                s.on = true;
                s.channel = chn;
                s.character = character;
                auto out = renderAmp (s, guitar);
                const double db = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 4800, 43200));
                minDb = juce::jmin (minDb, db);
                maxDb = juce::jmax (maxDb, db);
                check (allFinite (out) && out.getMagnitude (0, 0, out.getNumSamples()) < 2.0f,
                       juce::String::formatted ("%-6s %.1f: bounded, %.1f dB RMS", AmpBlock::channelModel (chn), character, db));
            }
        check (maxDb - minDb < 3.0, juce::String::formatted ("the three amps at noon within %.1f dB of each other", maxDb - minDb));

        auto harmonics = [&] (int chn, float gain)
        {
            AmpBlock::Settings s;
            s.on = true;
            s.channel = chn;
            s.gain = gain;
            auto out = renderAmp (s, makeSine (sr, 24000, 110.0, 0.1f));
            return toneDb (out, sr, 330.0, 12000, 9600) - toneDb (out, sr, 110.0, 12000, 9600);
        };
        const double cleanLow = harmonics (AmpBlock::clean, 0.2f), leadHigh = harmonics (AmpBlock::lead, 0.9f);
        check (leadHigh > cleanLow + 15.0, juce::String::formatted ("3rd harmonic: CLEAN low gain %.1f dB, LEAD high gain %.1f dB", cleanLow, leadHigh));

        // The knobs act like the real circuit: PRESENCE / DEPTH take feedback away at the top / bottom,
        // MASTER pushes the power amp, MID moves the stack's scoop
        {
            auto sineDb = [&] (AmpBlock::Settings st, double hz, float amp)
            {
                auto out = renderAmp (st, makeSine (sr, 24000, hz, amp));
                return toneDb (out, sr, hz, 12000, 9600);
            };
            AmpBlock::Settings st;
            st.on = true; st.channel = AmpBlock::lead; st.character = 0.0f; st.gain = 0.0f;
            auto withKnob = [&] (float AmpBlock::Settings::* knob, float v) { auto t = st; t.*knob = v; return t; };
            const double pres = sineDb (withKnob (&AmpBlock::Settings::presence, 1.0f), 5000.0, 0.02f) - sineDb (withKnob (&AmpBlock::Settings::presence, 0.0f), 5000.0, 0.02f);
            const double depth = sineDb (withKnob (&AmpBlock::Settings::depth, 1.0f), 85.0, 0.02f) - sineDb (withKnob (&AmpBlock::Settings::depth, 0.0f), 85.0, 0.02f);
            const double midRef = sineDb (withKnob (&AmpBlock::Settings::mid, 1.0f), 600.0, 0.02f) - sineDb (withKnob (&AmpBlock::Settings::mid, 0.0f), 600.0, 0.02f);
            check (pres > 4.0 && depth > 4.0 && midRef > 6.0,
                   juce::String::formatted ("STEEL: PRESENCE +%.1f dB at 5 kHz, DEPTH +%.1f dB at 85 Hz, MID +%.1f dB at 600 Hz", pres, depth, midRef));
            // MASTER: the power amp breaks up as it goes up (clean preamp)
            AmpBlock::Settings cl;
            cl.on = true; cl.channel = AmpBlock::clean; cl.character = 1.0f; cl.gain = 0.35f;
            auto h3At = [&] (float master)
            {
                auto t = cl; t.master = master;
                auto out = renderAmp (t, makeSine (sr, 24000, 110.0, 0.1f));
                return toneDb (out, sr, 330.0, 12000, 9600) - toneDb (out, sr, 110.0, 12000, 9600);
            };
            const double low = h3At (0.3f), high = h3At (1.0f);
            check (high > low + 15.0, juce::String::formatted ("VELVET: MASTER 3 -> 10 = power-amp breakup (3rd harmonic %.1f -> %.1f dB)", low, high));
            // Responsive: turning the guitar down cleans a crunch amp up
            AmpBlock::Settings cr;
            cr.on = true; cr.channel = AmpBlock::crunch; cr.character = 0.0f; cr.gain = 0.5f;
            auto h3In = [&] (float amp)
            {
                auto out = renderAmp (cr, makeSine (sr, 24000, 110.0, amp));
                return toneDb (out, sr, 330.0, 12000, 9600) - toneDb (out, sr, 110.0, 12000, 9600);
            };
            const double hard = h3In (0.2f), soft = h3In (0.0125f);
            check (soft < hard - 15.0, juce::String::formatted ("BRIT cleans up with the guitar's volume: 3rd harmonic %.1f dB at full, %.1f dB at -24 dB", hard, soft));
        }

        // CHARACTER really moves the circuit: the two ends of a channel differ
        {
            AmpBlock::Settings a, b;
            a.on = b.on = true;
            a.channel = b.channel = AmpBlock::lead;
            a.character = 0.0f;
            b.character = 1.0f;
            auto oa = renderAmp (a, guitar), ob = renderAmp (b, guitar);
            const double diff = nullDb (oa, ob, 0, 4800, 48000);
            check (diff > -10.0, juce::String::formatted ("LEAD: STEEL vs SLUDGE differ (null %.1f dB)", diff));
        }

        // Off = untouched; on / off without clicks
        {
            AmpBlock::Settings s;
            auto out = renderAmp (s, guitar);
            check (nullDb (out, guitar, 0, 0, guitar.getNumSamples()) < -200.0, "off: bit-transparent");
        }

        // GATE silences the hiss between notes, keeps the notes
        {
            juce::AudioBuffer<float> in (2, 48000);
            juce::Random rng (3);
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const float noise = 0.0005f * (rng.nextFloat() - 0.5f);
                const float note = i < 12000 ? 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * i / sr) : 0.0f;
                in.setSample (0, i, note + noise);
                in.setSample (1, i, note + noise);
            }
            AmpBlock::Settings s;
            s.on = true;
            s.channel = AmpBlock::lead;
            s.gain = 0.9f;
            auto open = renderAmp (s, in);
            s.gate = 0.5f;
            auto gated = renderAmp (s, in);
            const double hissOpen = juce::Decibels::gainToDecibels (open.getRMSLevel (0, 30000, 18000));
            const double hissGated = juce::Decibels::gainToDecibels (gated.getRMSLevel (0, 30000, 18000) + 1e-9f);
            const double noteLoss = juce::Decibels::gainToDecibels (gated.getRMSLevel (0, 2000, 8000) / open.getRMSLevel (0, 2000, 8000));
            check (hissGated < hissOpen - 30.0 && std::abs (noteLoss) < 1.0,
                   juce::String::formatted ("GATE: hiss %.1f -> %.1f dB, the note keeps its level (%.2f dB)", hissOpen, hissGated, noteLoss));
        }

        // NAM: the example captures load and play (48 kHz model at 44.1 kHz: resampled)
        const auto wavenet = namExample ("wavenet.nam");
        if (wavenet.existsAsFile())
        {
            for (auto* file : { "wavenet.nam", "lstm.nam", "A2.nam" })
            {
                std::string err;
                auto model = NamModel::load (namExample (file).loadFileAsString().toStdString(), err);
                check (model != nullptr, juce::String ("NAM ") + file + " loads" + (model != nullptr ? " (" + juce::String (model->getArchitecture()) + ")" : ": " + juce::String (err)));
                if (model == nullptr) continue;
                for (double rate : { 48000.0, 44100.0 })
                {
                    AmpBlock a;
                    a.prepare (rate, 256);
                    std::string e2;
                    a.setNamModel (NamModel::load (namExample (file).loadFileAsString().toStdString(), e2));
                    AmpBlock::Settings s;
                    s.on = true;
                    s.channel = AmpBlock::nam;
                    auto in = makeGuitar (rate, (int) rate);
                    auto out = renderAmp (s, in, &a);
                    const double db = juce::Decibels::gainToDecibels (out.getRMSLevel (0, (int) (rate * 0.2), (int) (rate * 0.7)));
                    check (allFinite (out) && db > -60.0 && db < 6.0,
                           juce::String::formatted ("NAM %s at %.1f kHz: plays (%.1f dB RMS)", file, rate / 1000.0, db));
                }
            }
            // a sine through a capture at 44.1 kHz comes out at its pitch (the resampling is right)
            {
                AmpBlock a;
                a.prepare (44100.0, 256);
                std::string e;
                a.setNamModel (NamModel::load (wavenet.loadFileAsString().toStdString(), e));
                AmpBlock::Settings s;
                s.on = true;
                s.channel = AmpBlock::nam;
                auto out = renderAmp (s, makeSine (44100.0, 44100, 440.0, 0.1f), &a);
                double purity = 0.0;
                const double f = dominantFrequency (out, 44100.0, 22050, purity);
                check (std::abs (f - 440.0) < 2.0, juce::String::formatted ("NAM at 44.1 kHz: 440 Hz in -> %.1f Hz out", f));
            }
            // A2: shown as A2, both sizes (Full / Lite) play
            {
                std::string e;
                auto m = NamModel::load (namExample ("A2.nam").loadFileAsString().toStdString(), e);
                check (m != nullptr && m->getArchitecture() == "A2" && m->isSlimmable(), "A2 capture: recognised as A2, slimmable");
                double lvl[2] {};
                for (int k = 0; k < 2; ++k)
                {
                    AmpBlock a;
                    a.prepare (48000.0, 256);
                    a.setNamSize (k == 0 ? 1.0 : 0.0);
                    std::string e2;
                    a.setNamModel (NamModel::load (namExample ("A2.nam").loadFileAsString().toStdString(), e2));
                    AmpBlock::Settings s;
                    s.on = true;
                    s.channel = AmpBlock::nam;
                    auto out = renderAmp (s, makeGuitar (48000.0, 48000), &a);
                    lvl[k] = allFinite (out) ? juce::Decibels::gainToDecibels (out.getRMSLevel (0, 9600, 24000)) : -999.0;
                }
                check (lvl[0] > -60.0 && lvl[1] > -60.0 && std::abs (lvl[0] - lvl[1]) < 6.0,
                       juce::String::formatted ("A2 Full %.1f dB RMS, Lite %.1f dB RMS", lvl[0], lvl[1]));
            }
            // NAM's own knobs: 5 = neutral, OUTPUT is a clean gain, INPUT drives it, the amp's knobs don't touch it
            {
                auto run = [&] (std::function<void (AmpBlock::Settings&)> f)
                {
                    AmpBlock a;
                    a.prepare (48000.0, 256);
                    std::string e;
                    a.setNamModel (NamModel::load (wavenet.loadFileAsString().toStdString(), e));
                    AmpBlock::Settings s;
                    s.on = true;
                    s.channel = AmpBlock::nam;
                    f (s);
                    auto out = renderAmp (s, makeGuitar (48000.0, 48000), &a);
                    return juce::Decibels::gainToDecibels (out.getRMSLevel (0, 9600, 24000));
                };
                const double ref = run ([] (AmpBlock::Settings&) {});
                const double ampKnobs = run ([] (AmpBlock::Settings& s) { s.gain = 0.0f; s.bass = 0.0f; s.treble = 1.0f; s.master = 0.0f; });
                const double outUp = run ([] (AmpBlock::Settings& s) { s.namOutput = 0.5f + 6.0f / 36.0f; });
                const double inDown = run ([] (AmpBlock::Settings& s) { s.namInput = 0.0f; });
                const double bassDown = run ([] (AmpBlock::Settings& s) { s.namBass = 0.0f; });
                check (std::abs (ampKnobs - ref) < 0.01, juce::String::formatted ("NAM ignores the amp models' knobs (%.2f dB)", ampKnobs - ref));
                check (std::abs (outUp - ref - 6.0) < 0.1, juce::String::formatted ("NAM OUTPUT +6 dB: %+.2f dB", outUp - ref));
                check (inDown < ref - 3.0, juce::String::formatted ("NAM INPUT at 0: %+.1f dB", inDown - ref));
                check (bassDown < ref - 0.5, juce::String::formatted ("NAM BASS at 0: %+.1f dB", bassDown - ref));
            }
            // through the processor: file, description, session round trip
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                const auto err = p.loadNamModel (wavenet);
                check (err.isEmpty(), "processor loads a .nam: " + (err.isEmpty() ? p.getNamModelDescription() : err));
                juce::MemoryBlock mb;
                p.getStateInformation (mb);
                SwarmnessAudioProcessor q;
                q.setStateInformation (mb.getData(), (int) mb.getSize());
                check (q.getNamModelFile() == wavenet, "NAM capture path saved with the session");
                check (p.loadNamModel (juce::File::createTempFile (".nam")).isNotEmpty(), "a broken .nam is refused with a message");
            }
        }
        else
        {
            std::printf ("  (NAM example captures not found - skipped)\n");
        }
        for (int chn = 0; chn < 3; ++chn)
        {
            // the circuit models in real time at 48 kHz (LEAD at full gain = the most grid current)
            AmpBlock a;
            a.prepare (48000.0, 128);
            AmpBlock::Settings s;
            s.on = true;
            s.channel = chn;
            s.gain = 0.9f;
            s.master = 0.8f;
            a.setParams (s);
            auto in = makeGuitar (48000.0, 48000 * 5);
            const auto t0 = std::chrono::steady_clock::now();
            for (int start = 0; start < in.getNumSamples(); start += 128)
            {
                float* ptr[2] { in.getWritePointer (0, start), in.getWritePointer (1, start) };
                a.process (ptr, 2, 128);
            }
            const double load = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 5.0 * 100.0;
            check (load < 20.0, juce::String::formatted ("%s circuit model: %.1f%% of one core", ParamChoices::ampChannels[chn].toRawUTF8(), load));
        }
        {
            // TONE3000 sign-in pieces: PKCE S256 challenge, base64url, the browser's callback
            check (Tone3000::codeChallengeFor ("dBjftJeZ4CVP-mJ92IjGp3Ioq6Y0t4kEFzr1LVgsQJ0") == "_3BvrKfwzKcl7_-QgZsEYWs6xAgHEnw4Cu02jEYfy2I",
                   "TONE3000: PKCE S256 code challenge");
            const auto q = Tone3000::parseQuery ("GET /callback?code=ab%2Fc&state=xyz&tone_id=42 HTTP/1.1");
            check (q["__path"] == "/callback" && q["code"] == "ab/c" && q["state"] == "xyz" && q["tone_id"] == "42",
                   "TONE3000: callback query parsed");
            check (Tone3000::redirectUri() == "http://127.0.0.1:43167/callback", "TONE3000: loopback redirect " + Tone3000::redirectUri());
        }
        const auto a2 = namExample ("A2.nam");
        if (a2.existsAsFile())
        {
            // a full-size current capture, in real time at 48 kHz
            AmpBlock a;
            a.prepare (48000.0, 128);
            std::string e;
            a.setNamModel (NamModel::load (a2.loadFileAsString().toStdString(), e));
            AmpBlock::Settings s;
            s.on = true;
            s.channel = AmpBlock::nam;
            a.setParams (s);
            auto in = makeGuitar (48000.0, 48000 * 5);
            const auto t0 = std::chrono::steady_clock::now();
            for (int start = 0; start < in.getNumSamples(); start += 128)
            {
                float* ptr[2] { in.getWritePointer (0, start), in.getWritePointer (1, start) };
                a.process (ptr, 2, 128);
            }
            const double load = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 5.0 * 100.0;
            check (load < 20.0, juce::String::formatted ("NAM A2 capture: %.1f%% of one core", load));
        }
    }

    void testCab()
    {
        std::printf ("\nCAB: modelled cabinets, MIC, DISTANCE, IR\n");
        // the modelled responses: a speaker's band, the mic moves the top
        for (int t = 0; t < (int) CabBlock::ir; ++t)
        {
            // a 1/3-octave average (the curve carries each speaker's breakup ripple)
            auto avg = [t] (float hz)
            {
                float sum = 0.0f;
                for (int k = -3; k <= 3; ++k) sum += CabBlock::responseDb (t, 0.3f, 0.2f, hz * std::pow (2.0f, (float) k / 18.0f));
                return sum / 7.0f;
            };
            const float low = avg (40.0f), top = avg (10000.0f), pres = avg (2200.0f);
            check (low < -3.0f && top < -20.0f && pres > -7.0f,
                   juce::String::formatted ("%s: 40 Hz %.1f dB, 2.2 kHz %.1f dB, 10 kHz %.1f dB", ParamChoices::cabTypes[t].toRawUTF8(), low, pres, top));
        }
        const float cap = CabBlock::responseDb (CabBlock::modern4x12, 0.0f, 0.2f, 4000.0f);
        const float edge = CabBlock::responseDb (CabBlock::modern4x12, 1.0f, 0.2f, 4000.0f);
        check (edge < cap - 6.0f, juce::String::formatted ("MIC: cap %.1f dB vs edge %.1f dB at 4 kHz", cap, edge));
        const float close = CabBlock::responseDb (CabBlock::modern4x12, 0.3f, 0.0f, 120.0f);
        const float far = CabBlock::responseDb (CabBlock::modern4x12, 0.3f, 1.0f, 120.0f);
        check (close > far + 2.0f, juce::String::formatted ("DISTANCE: proximity bass %.1f dB close vs %.1f dB far", close, far));

        // the minimum-phase IR: energy up front, response matches the design
        {
            auto ir = CabBlock::designIR (CabBlock::modern4x12, 0.3f, 0.0f, 48000.0);
            const double early = ir.getRMSLevel (0, 0, 96), late = ir.getRMSLevel (0, 960, ir.getNumSamples() - 960);
            check (early > late * 10.0, juce::String::formatted ("IR is minimum phase (first 2 ms %.1f dB above the rest)", juce::Decibels::gainToDecibels (early / late)));
        }

        // through the processor: AMP + CAB on the default chain
        const double sr = 48000.0;
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::cabOn, 1.0f);
            p.prepareToPlay (sr, 256);
            auto noise = juce::AudioBuffer<float> (2, 48000);
            juce::Random rng (9);
            for (int i = 0; i < noise.getNumSamples(); ++i)
            {
                const float v = 0.2f * (rng.nextFloat() - 0.5f);
                noise.setSample (0, i, v);
                noise.setSample (1, i, v);
            }
            juce::AudioBuffer<float> out;
            for (int k = 0; k < 20; ++k)   // the convolution loads the IR on its own thread
            {
                out = render (p, noise, sr, 256);
                if (toneDb (out, sr, 9000.0, 24000, 16384) < toneDb (noise, sr, 9000.0, 24000, 16384) - 15.0) break;
                juce::Thread::sleep (20);
            }
            const double top = toneDb (out, sr, 9000.0, 24000, 16384) - toneDb (noise, sr, 9000.0, 24000, 16384);
            const double mid = toneDb (out, sr, 1500.0, 24000, 16384) - toneDb (noise, sr, 1500.0, 24000, 16384);
            check (top < -15.0 && std::abs (mid) < 8.0, juce::String::formatted ("CAB on: 9 kHz %.1f dB, 1.5 kHz %.1f dB", top, mid));
        }
        {
            // a loaded IR (a short decaying click) replaces the model
            auto irFile = juce::File::createTempFile (".wav");
            {
                juce::AudioBuffer<float> ir (1, 2400);
                ir.clear();
                for (int i = 0; i < 2400; ++i)
                    ir.setSample (0, i, (i % 7 == 0 ? 1.0f : -0.3f) * std::exp (-i / 200.0f));
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = irFile.createOutputStream();
                auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
                w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }
            SwarmnessAudioProcessor p;
            resetToInit (p);
            const auto err = p.loadCabIR (irFile);
            check (err.isEmpty(), "CAB IR loads: " + (err.isEmpty() ? p.getCabIRDescription() : err));
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            SwarmnessAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (q.getCabIRFile() == irFile, "CAB IR path saved with the session");
            irFile.deleteFile();
        }
        {
            // an IR loaded while the plug-in is running (as in a DAW) takes over from the modelled cabinet
            auto irFile = juce::File::createTempFile (".wav");
            {
                juce::AudioBuffer<float> ir (1, 4800);
                ir.clear();
                ir.setSample (0, 0, 1.0f);
                ir.setSample (0, 2400, 0.8f);   // a clear echo at 50 ms: nothing like a cabinet
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = irFile.createOutputStream();
                auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
                w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::cabOn, 1.0f);
            p.prepareToPlay (sr, 256);
            juce::MidiBuffer midi;
            auto block = [&] (bool click)
            {
                juce::AudioBuffer<float> b (2, 256);
                b.clear();
                if (click) { b.setSample (0, 0, 0.5f); b.setSample (1, 0, 0.5f); }
                p.processBlock (b, midi);
                return b;
            };
            for (int k = 0; k < 40; ++k) block (false);
            const auto err = p.loadCabIR (irFile);
            setParam (p, ParamIDs::cabType, (float) CabBlock::ir);
            for (int k = 0; k < 40; ++k) { block (false); juce::Thread::sleep (5); }
            // a click, then look for the echo 2400 samples later
            juce::AudioBuffer<float> out (2, 256 * 16);
            for (int k = 0; k < 16; ++k)
                out.copyFrom (0, k * 256, block (k == 0), 0, 0, 256);
            int at = 0;   // the plug-in's latency
            for (int i = 0; i < 256; ++i)
                if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, at))) at = i;
            const float direct = std::abs (out.getSample (0, at)), echo = out.getMagnitude (0, at + 2400 - 4, 9);
            check (err.isEmpty() && echo > 0.3f * direct && direct > 0.0f,
                   juce::String::formatted ("IR loaded while running plays (direct %.3f, its echo at 50 ms %.3f)", direct, echo));
            irFile.deleteFile();
        }
        {
            // two IR slots: MIX blends them, B is time-aligned to A (no comb filter), INV B flips B
            juce::AudioBuffer<float> a (1, 2400), b (1, 2400);
            a.clear(); b.clear();
            juce::Random rng (3);
            for (int i = 48; i < 2400; ++i)
            {
                const float v = (rng.nextFloat() - 0.5f) * std::exp (-(i - 48) / 200.0f);
                a.setSample (0, i, v);
                if (i + 30 < 2400) b.setSample (0, i + 30, v);   // the same cabinet, 30 samples later (a mic further back)
            }
            auto run = [&] (float mix, bool inv, bool withB)
            {
                CabBlock cb;
                CabBlock::Settings st;
                st.on = true; st.type = CabBlock::ir; st.lowCutHz = 20.0f; st.highCutHz = 20000.0f;
                cb.setParams (st);
                cb.setUserIR (juce::AudioBuffer<float> (a), 48000.0, 0);
                if (withB) cb.setUserIR (juce::AudioBuffer<float> (b), 48000.0, 1);
                cb.setUserMix (mix, inv);
                cb.prepare (48000.0, 256);
                juce::AudioBuffer<float> x (2, 4096);
                x.clear();
                x.setSample (0, 0, 1.0f); x.setSample (1, 0, 1.0f);
                for (int start = 0; start < 4096; start += 256)
                {
                    float* ptr[2] { x.getWritePointer (0, start), x.getWritePointer (1, start) };
                    cb.process (ptr, 2, 256);
                }
                return x;
            };
            const auto onlyA = run (0.5f, false, false), blend = run (0.5f, false, true), allB = run (1.0f, false, true);
            const auto inverted = run (0.5f, true, true);
            // aligned and levelled, the blend of a cabinet with its own delayed copy is that cabinet
            const double blendVsA = nullDb (blend, onlyA, 0, 0, 4096);
            const double bVsA = nullDb (allB, onlyA, 0, 0, 4096);
            const double invLevel = juce::Decibels::gainToDecibels (inverted.getRMSLevel (0, 0, 4096) / (onlyA.getRMSLevel (0, 0, 4096) + 1.0e-12f));
            check (blendVsA < -40.0 && bVsA < -40.0 && invLevel < -40.0,
                   juce::String::formatted ("IR A / B: B aligned to A (blend %.0f dB, B alone %.0f dB off A), INV B cancels (%.0f dB)",
                                            blendVsA, bVsA, invLevel));
        }
        {
            // IR resampling keeps the top octave (a linear interpolator loses 1-2 dB there)
            juce::AudioBuffer<float> src (1, 4410);
            juce::Random rng (5);
            for (int i = 0; i < src.getNumSamples(); ++i)
                src.setSample (0, i, i < 64 ? 0.0f : (rng.nextFloat() - 0.5f) * std::exp (-(i - 64) / 300.0f));   // files keep ~1 ms before the sound
            auto magDb = [] (const juce::AudioBuffer<float>& h, double rate, double hz)
            {
                std::complex<double> acc;
                for (int i = 0; i < h.getNumSamples(); ++i)
                    acc += (double) h.getSample (0, i) * std::polar (1.0, -juce::MathConstants<double>::twoPi * hz * i / rate);
                return 20.0 * std::log10 (std::abs (acc) + 1.0e-12);
            };
            double worst = 0.0;
            for (double to : { 48000.0, 96000.0 })
            {
                const auto up = irtools::resample (src, 44100.0, to);
                const auto back = irtools::resample (up, to, 44100.0);
                for (double hz : { 100.0, 1000.0, 8000.0, 12000.0, 16000.0 })
                    worst = juce::jmax (worst, std::abs (magDb (up, to, hz) - magDb (src, 44100.0, hz)),
                                        std::abs (magDb (back, 44100.0, hz) - magDb (src, 44100.0, hz)));
            }
            check (worst < 0.15, juce::String::formatted ("IR resampling 44.1 <-> 48 / 96 kHz: within %.3f dB up to 16 kHz", worst));
        }
        {
            // a stereo IR plays in stereo, and a loaded IR is running from the very first block
            auto irFile = juce::File::createTempFile (".wav");
            {
                juce::AudioBuffer<float> ir (2, 2205);
                for (int i = 0; i < ir.getNumSamples(); ++i)
                {
                    ir.setSample (0, i, (i % 5 == 0 ? 1.0f : -0.2f) * std::exp (-i / 150.0f));
                    ir.setSample (1, i, i < 20 ? 0.0f : (i % 3 == 0 ? 0.8f : -0.3f) * std::exp (-(i - 20) / 250.0f));
                }
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = irFile.createOutputStream();
                auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (24));
                w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::cabOn, 1.0f);
            setParam (p, ParamIDs::cabType, (float) CabBlock::ir);
            const auto err = p.loadCabIR (irFile);
            check (err.isEmpty() && p.getCabIRDescription().contains ("stereo"), "stereo CAB IR: " + (err.isEmpty() ? p.getCabIRDescription() : err));
            juce::AudioBuffer<float> click (2, 4800);
            click.clear();
            click.setSample (0, 100, 0.5f);
            click.setSample (1, 100, 0.5f);
            const auto out = render (p, click, sr, 256);   // no waiting for a background thread
            double diff = 0.0, tail = 0.0;
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                diff += std::pow (out.getSample (0, i) - out.getSample (1, i), 2.0);
                if (i > 400) tail += std::pow (out.getSample (0, i), 2.0);
            }
            check (diff > 1.0e-4 && tail > 1.0e-6,
                   juce::String::formatted ("stereo IR: left and right differ (%.1f dB), the IR rings from the first block (tail %.1f dB)",
                                            10.0 * std::log10 (diff + 1.0e-20), 10.0 * std::log10 (tail + 1.0e-20)));
            irFile.deleteFile();
        }
        {
            // the amp presets play at sane levels (the modelled cabinet IR loads on its own thread)
            auto guitar = makeGuitar (sr, 96000);
            double lo = 1e9, hi = -1e9;
            for (const auto& name : SwarmnessAudioProcessor().getPresetManager().getFactoryPresetNames ("Amps & Cabs"))
            {
                SwarmnessAudioProcessor p;
                p.getPresetManager().loadPreset (name);
                p.prepareToPlay (sr, 256);
                juce::Thread::sleep (60);
                auto out = render (p, guitar, sr, 256);
                const double db = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 9600, 86400));
                lo = juce::jmin (lo, db); hi = juce::jmax (hi, db);
                std::printf ("    %-20s %6.1f dB RMS\n", name.toRawUTF8(), db);
            }
            check (lo > -30.0 && hi < -4.0, juce::String::formatted ("amp presets between %.1f and %.1f dB RMS", lo, hi));
        }
        {
            // old sessions / presets: AMP + CAB land right after HIVE
            PresetManager::ValueMap v { { Chain::slotIds[Chain::pitch], 50.0f } };
            PresetManager::migrateLegacyValues (v);
            check (v[Chain::slotIds[Chain::amp]] == 53.0f && v[Chain::slotIds[Chain::cab]] == 56.0f, "older presets: AMP and CAB follow HIVE");
            check (v[Chain::slotIds[Chain::drive]] == 53.0f, "older presets: WASP sits right in front of the AMP");
            PresetManager::ValueMap w { { Chain::slotIds[Chain::amp], 80.0f }, { Chain::laneIds[Chain::amp], 2.0f } };
            PresetManager::migrateLegacyValues (w);
            check (w[Chain::slotIds[Chain::drive]] == 80.0f && w[Chain::laneIds[Chain::drive]] == 2.0f, "v3.1 betas: WASP joins the AMP's slot and lane");
            std::array<float, Chain::numBlocks> slots {};
            for (int b = 0; b < Chain::numBlocks; ++b)
                slots[(size_t) b] = (float) Chain::defaultSlots[b];
            slots[Chain::amp] = slots[Chain::drive] = 80.0f;
            const auto order = Chain::orderFromSlots (slots);
            const auto at = [&] (int b) { return std::find (order.begin(), order.end(), b) - order.begin(); };
            check (at (Chain::drive) + 1 == at (Chain::amp), "same slot: WASP sorts right before the AMP");
        }
    }

    juce::AudioBuffer<float> renderWasp (const DriveBlock::Settings& s, const juce::AudioBuffer<float>& input, double sr = 48000.0)
    {
        DriveBlock d;
        d.prepare (sr, 256);
        d.setParams (s);
        d.reset();
        juce::AudioBuffer<float> out (input);
        for (int start = 0; start < out.getNumSamples(); start += 256)
        {
            const int n = juce::jmin (256, out.getNumSamples() - start);
            float* ptr[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            d.process (ptr, 2, n);
        }
        return out;
    }

    void testTuner()
    {
        std::printf ("\nTUNER\n");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            float worst = 0.0f;
            for (double f : { 46.25, 55.0, 61.74, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26 })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                p.prepareToPlay (sr, 256);
                p.getTuner().active.store (true);
                // a plucked-string-like tone: rich harmonics, a weak fundamental on the low strings
                juce::AudioBuffer<float> b (2, (int) (sr * 0.4));
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    double v = 0.0;
                    for (int h = 1; h <= 12; ++h)
                        v += (h == 1 && f < 70.0 ? 0.3 : 1.0) / h * std::sin (juce::MathConstants<double>::twoPi * f * h * i / sr + h);
                    b.setSample (0, i, (float) (0.1 * v * std::exp (-i / sr * 2.0)));
                    b.setSample (1, i, b.getSample (0, i));
                }
                render (p, b, sr, 256);
                std::vector<float> w;
                p.getTuner().read (w, 2048);
                const auto est = TunerEstimate::estimate (w, p.getTuner().getRate());
                const auto r = TunerOverlay::read (est.hz, 440.0f);
                const auto want = TunerOverlay::read ((float) f, 440.0f);
                worst = juce::jmax (worst, r.midi == want.midi ? std::abs (r.cents - want.cents) : 999.0f);
            }
            check (worst < 1.5f, juce::String::formatted ("%.1f kHz: F#1 .. E5 read within %.2f cents", sr / 1000.0, worst));
        }
        {
            // steady: a held A2 4 cents sharp, with pick noise, read by the tuner window tick by tick
            SwarmnessAudioProcessor p;
            resetToInit (p);
            p.prepareToPlay (48000.0, 256);
            TunerOverlay t (p.getTuner());
            t.setBounds (0, 0, 1100, 800);
            t.open();
            juce::Random rng (11);
            const double f = 110.0 * std::pow (2.0, 4.0 / 1200.0);
            int n = 0, lo = 99, hi = -99, notes = 0;
            juce::MidiBuffer midi;
            for (int tick = 0; tick < 60; ++tick)
            {
                juce::AudioBuffer<float> b (2, 1600);   // 1/30 s
                for (int i = 0; i < b.getNumSamples(); ++i, ++n)
                {
                    double v = 0.0;
                    for (int h = 1; h <= 8; ++h)
                        v += 1.0 / h * std::sin (juce::MathConstants<double>::twoPi * f * h * n / 48000.0 + h);
                    const float s = (float) (0.08 * v * std::exp (-n / 48000.0 * 0.4)) + 0.004f * (rng.nextFloat() - 0.5f);
                    b.setSample (0, i, s);
                    b.setSample (1, i, s);
                }
                p.processBlock (b, midi);
                t.timerCallbackForTests();
                if (tick >= 20)
                {
                    lo = juce::jmin (lo, t.getDisplayedCents());
                    hi = juce::jmax (hi, t.getDisplayedCents());
                    notes += t.getMidi() == 45 ? 1 : 0;
                }
            }
            t.close();
            check (notes == 40 && hi - lo <= 1 && lo >= 3 && hi <= 5,
                   juce::String::formatted ("TUNER is steady: a held A2 +4 cents reads %+d..%+d cents, the note never flickers (%d / 40)", lo, hi, notes));
        }
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            p.getTuner().active.store (true);
            p.getTuner().mute.store (true);
            auto out = render (p, makeSine (48000.0, 24000, 220.0, 0.3f), 48000.0, 256);
            check (out.getMagnitude (0, 12000, 12000) < 1.0e-5f, "TUNER with MUTE: the output is silent");
        }
    }

    void testWasp()
    {
        std::printf ("\nWASP: overdrive (asymmetric hard clipping, ATTACK, BRIGHT, GATE)\n");
        const double sr = 48000.0;
        auto guitar = makeGuitar (sr, 48000);
       #ifdef SWARMNESS_NAM_EXAMPLES
        {
            // NAM mode: a pedal capture instead of the circuit, its own gain kept, INPUT / OUTPUT trims
            auto renderNam = [&] (DriveBlock::Settings s)
            {
                std::string e;
                DriveBlock d;
                d.prepare (sr, 256);
                d.setNamModel (NamModel::load (namExample ("A2.nam").loadFileAsString().toStdString(), e));
                d.setParams (s);
                d.reset();
                juce::AudioBuffer<float> out (guitar);
                for (int start = 0; start < out.getNumSamples(); start += 256)
                {
                    const int n = juce::jmin (256, out.getNumSamples() - start);
                    float* ptr[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
                    d.process (ptr, 2, n);
                }
                return out;
            };
            DriveBlock::Settings s;
            s.on = true;
            s.nam = true;
            const auto base = renderNam (s);
            auto t = s; t.namOutput = 0.5f + 6.0f / 36.0f;
            const double outDb = juce::Decibels::gainToDecibels (renderNam (t).getRMSLevel (0, 9600, 38400) / base.getRMSLevel (0, 9600, 38400));
            auto w = s; w.nam = false;
            const double vsWasp = nullDb (base, renderWasp (w, guitar), 0, 9600, 38400);
            check (allFinite (base) && base.getRMSLevel (0, 9600, 38400) > 1.0e-3 && std::abs (outDb - 6.0) < 0.05 && vsWasp > -10.0,
                   juce::String::formatted ("NAM mode: the capture plays (%.1f dB), OUTPUT +6 dB = %+.2f dB, not the WASP circuit",
                                            juce::Decibels::gainToDecibels (base.getRMSLevel (0, 9600, 38400)), outDb));
            SwarmnessAudioProcessor p;
            const auto err = p.loadNamModel (namExample ("A2.nam"), true);
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            SwarmnessAudioProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (err.isEmpty() && q.getNamModelFile (true) == namExample ("A2.nam") && q.getNamModelFile (false) == juce::File(),
                   "NAM mode: the pedal capture is saved with the session (apart from the AMP's)");
        }
       #endif
        {
            DriveBlock::Settings s;
            auto out = renderWasp (s, guitar);
            check (nullDb (out, guitar, 0, 0, guitar.getNumSamples()) < -200.0, "off: bit-transparent");
        }
        DriveBlock::Settings s;
        s.on = true;
        const double inDb = juce::Decibels::gainToDecibels (guitar.getRMSLevel (0, 9600, 38400));
        for (float drive : { 0.0f, 0.3f, 0.6f, 1.0f })
        {
            auto t = s; t.drive = drive;
            auto out = renderWasp (t, guitar);
            const double db = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 9600, 38400)) - inDb;
            check (allFinite (out) && std::abs (db) < 6.0, juce::String::formatted ("DRIVE %.0f, VOLUME 5: %+.1f dB vs the input", drive * 10.0f, db));
        }
        auto third = [&] (DriveBlock::Settings t, double hz, float amp)
        {
            auto out = renderWasp (t, makeSine (sr, 24000, hz, amp));
            return toneDb (out, sr, hz * 3.0, 12000, 9600) - toneDb (out, sr, hz, 12000, 9600);
        };
        {
            auto lo = s, hi = s; lo.drive = 0.0f; hi.drive = 1.0f;
            // DRIVE 0 still has gain (like the real pedal it grinds a little on a hot pick), but plays clean when soft
            const double a = third (lo, 220.0, 0.01f), b = third (hi, 220.0, 0.01f), c = third (hi, 220.0, 0.1f);
            check (a < -40.0 && b > a + 15.0 && c > -20.0,
                   juce::String::formatted ("3rd harmonic, soft: DRIVE 0 %.1f dB, DRIVE 10 %.1f dB; hot, DRIVE 10: %.1f dB", a, b, c));
        }
        auto toneAt = [&] (DriveBlock::Settings t, double hz)
        {
            auto out = renderWasp (t, makeSine (sr, 24000, hz, 0.01f));
            return toneDb (out, sr, hz, 12000, 9600);
        };
        {
            auto loose = s, tight = s; loose.attack = 0.0f; tight.attack = 1.0f;
            const double l = toneAt (loose, 80.0) - toneAt (loose, 1000.0), t = toneAt (tight, 80.0) - toneAt (tight, 1000.0);
            check (t < l - 15.0, juce::String::formatted ("ATTACK tightens the lows: 80 Hz vs 1 kHz %.1f dB -> %.1f dB", l, t));
            const double model = DriveBlock::responseDb (tight, 80.0) - DriveBlock::responseDb (tight, 1000.0);
            check (std::abs (model - t) < 1.5, juce::String::formatted ("the editor's response matches the audio (%.1f vs %.1f dB)", model, t));
        }
        {
            auto dark = s, bright = s; dark.bright = 0.0f; bright.bright = 1.0f;
            const double d = toneAt (dark, 4000.0) - toneAt (dark, 500.0), b = toneAt (bright, 4000.0) - toneAt (bright, 500.0);
            check (b > d + 12.0, juce::String::formatted ("BRIGHT: 4 kHz vs 500 Hz %.1f dB -> %.1f dB", d, b));
        }
        {
            auto quiet = s, loud = s; quiet.volume = 0.25f; loud.volume = 0.75f;
            const double q = toneAt (quiet, 1000.0), l = toneAt (loud, 1000.0);
            check (std::abs ((l - q) - (DriveBlock::volumeDb (0.75f) - DriveBlock::volumeDb (0.25f))) < 0.5,
                   juce::String::formatted ("VOLUME 2.5 -> 7.5: %+.1f dB", l - q));
        }
        {
            // GATE: the hiss after a note is gone, the note itself is not
            juce::AudioBuffer<float> in (2, 48000);
            juce::Random rng (3);
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const float note = i < 12000 ? 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f * (float) i / 48000.0f) : 0.0f;
                const float v = note + 0.0006f * (rng.nextFloat() * 2.0f - 1.0f);
                in.setSample (0, i, v); in.setSample (1, i, v);
            }
            auto open = s, gated = s; open.drive = 0.8f; gated.drive = 0.8f; gated.gate = 0.5f;
            auto a = renderWasp (open, in), b = renderWasp (gated, in);
            const double tailOpen = juce::Decibels::gainToDecibels (a.getRMSLevel (0, 30000, 16000) + 1.0e-9f);
            const double tailGated = juce::Decibels::gainToDecibels (b.getRMSLevel (0, 30000, 16000) + 1.0e-9f);
            const double noteDiff = juce::Decibels::gainToDecibels (b.getRMSLevel (0, 2000, 8000) / a.getRMSLevel (0, 2000, 8000));
            check (tailGated < tailOpen - 30.0 && std::abs (noteDiff) < 0.5,
                   juce::String::formatted ("GATE: hiss %.1f -> %.1f dB, the note %+.2f dB", tailOpen, tailGated, noteDiff));
        }
        for (double rate : { 44100.0, 96000.0, 192000.0 })
        {
            auto t = s; t.drive = 1.0f; t.volume = 1.0f; t.bright = 1.0f;
            auto hot = makeGuitar (rate, (int) rate);
            hot.applyGain (4.0f);
            auto out = renderWasp (t, hot, rate);
            check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 12.0f,
                   juce::String::formatted ("%.1f kHz, everything up, hot input: bounded (peak %.2f)", rate / 1000.0, out.getMagnitude (0, out.getNumSamples())));
        }
        {
            // through the processor: WASP in front of the AMP
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::drvOn, 1.0f);
            setParam (p, ParamIDs::drvDrive, 6.0f);
            setParam (p, ParamIDs::ampOn, 1.0f);
            auto out = render (p, guitar, sr, 256);
            check (allFinite (out) && out.getRMSLevel (0, 9600, 38400) > 0.001f, "processor: WASP -> AMP plays");
        }
        {
            DriveBlock d;
            d.prepare (48000.0, 256);
            DriveBlock::Settings t = s; t.drive = 0.7f;
            d.setParams (t);
            auto buf = makeGuitar (48000.0, 256 * 400);
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int start = 0; start < buf.getNumSamples(); start += 256)
            {
                float* ptr[2] { buf.getWritePointer (0, start), buf.getWritePointer (1, start) };
                d.process (ptr, 2, 256);
            }
            const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            const double load = 100.0 * secs / (buf.getNumSamples() / 48000.0);
            check (load < 10.0, juce::String::formatted ("WASP stereo at 48 kHz: %.1f%% of one core", load));
        }
    }

    void testBadImpulseResponses()
    {
        std::printf ("\nCRYPT / CAB: broken impulse responses\n");
        const double sr = 48000.0;
        auto write = [&] (std::function<float (int)> gen, bool isFloat)
        {
            auto f = juce::File::createTempFile (".wav");
            juce::AudioBuffer<float> ir (2, 48000);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                    ir.setSample (ch, i, gen (i));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = f.createOutputStream();
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (isFloat ? 32 : 24)
                                                     .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                : juce::AudioFormatWriterOptions::SampleFormat::integral));
            w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            return f;
        };
        juce::Random rng (8);
        std::vector<float> noise (48000);
        for (auto& v : noise) v = rng.nextFloat() * 2.0f - 1.0f;
        auto decay = [&] (int i) { return noise[(size_t) i] * std::exp (-i / 6000.0f); };

        auto silent = write ([] (int) { return 0.0f; }, false);
        auto tiny = write ([&] (int i) { return 1.0e-9f * decay (i); }, true);
        {
            SwarmnessAudioProcessor p;
            check (p.loadReverbIR (silent).contains ("silent") && p.loadReverbIR (tiny).contains ("silent")
                   && p.loadCabIR (silent).contains ("silent"), "a silent / near-silent IR is refused with a message");
        }
        for (auto [name, gen] : { std::pair<const char*, std::function<float (int)>> { "NaN / inf samples", [&] (int i) { return i == 300 ? std::numeric_limits<float>::quiet_NaN() : (i == 301 ? std::numeric_limits<float>::infinity() : decay (i)); } },
                                  { "huge float values", [&] (int i) { return 5000.0f * decay (i); } },
                                  { "DC offset", [&] (int i) { return 0.4f + 0.3f * decay (i); } } })
        {
            auto f = write (gen, true);
            SwarmnessAudioProcessor p;
            resetToInit (p);
            const auto err = p.loadReverbIR (f);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revType, 4.0f);
            setParam (p, ParamIDs::revMix, 50.0f);
            p.prepareToPlay (sr, 256);
            juce::AudioBuffer<float> in (2, 256 * 940);   // a whole number of blocks
            in.clear();
            auto g = makeGuitar (sr, 48000);
            for (int ch = 0; ch < 2; ++ch) in.copyFrom (ch, 0, g, ch, 0, 48000);
            juce::MidiBuffer midi;
            float playing = 0.0f, after = 0.0f;
            bool finite = true;
            for (int start = 0; start < in.getNumSamples(); start += 256)
            {
                if (start == 0) juce::Thread::sleep (200);   // the convolution loads on its own thread
                if (start == 48000 * 3) setParam (p, ParamIDs::revOn, 0.0f);
                juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, start, 256);
                p.processBlock (view, midi);
                const float m = view.getMagnitude (0, 0, 256);
                finite = finite && std::isfinite (m);
                if (start < 48000 * 2) playing = juce::jmax (playing, m);
                if (start > 48000 * 4) after = juce::jmax (after, m);
            }
            check (err.isEmpty() && finite && playing < 1.5f && after < 1.0e-4f,
                   juce::String::formatted ("IR with %s: loads, peak %.2f while playing, silent after (%.1e)", name, playing, after));
            f.deleteFile();
        }
        silent.deleteFile();
        tiny.deleteFile();
    }

}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 2 && juce::String (argv[1]) == "--fuzz-dyn")
    {
        // SMOKE output level / brightness vs picking strength (diagnostic)
        const double sr = 48000.0;
        for (float fz : { 30.0f, 70.0f, 100.0f })
            for (int voice = 0; voice < 3; ++voice)
            {
                std::printf ("FUZZ %3.0f VOICE %d:", fz, voice);
                for (float amp : { 0.3f, 0.1f, 0.03f, 0.01f, 0.003f })
                {
                    SwarmnessAudioProcessor p;
                    resetToInit (p);
                    setParam (p, ParamIDs::fuzzOn, 1.0f);
                    setParam (p, ParamIDs::fuzz, fz);
                    setParam (p, ParamIDs::fuzzVoice, (float) voice);
                    auto out = render (p, makeSine (sr, 24000, 110.0, amp), sr, 256);
                    const double rms = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 12000, 9600));
                    const double h1 = toneDb (out, sr, 110.0, 12000, 9600), h5 = toneDb (out, sr, 550.0, 12000, 9600);
                    std::printf ("  %5.1f dB (h5-h1 %+5.1f)", rms, h5 - h1);
                }
                std::printf ("\n");
            }
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--amp-dyn")
    {
        // AMP models: level (guitar RMS) and 3rd-harmonic ratio (110 Hz sine at -20 dBFS) vs GAIN (diagnostic)
        const double sr = 48000.0;
        auto guitar = makeGuitar (sr, 48000);
        for (int chn = 0; chn < 3; ++chn)
            for (float character : { 0.0f, 0.5f, 1.0f })
            {
                std::printf ("%-6s %.1f ", AmpBlock::channelModel (chn), character);
                for (float gain : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f })
                {
                    AmpBlock::Settings s;
                    s.on = true; s.channel = chn; s.character = character; s.gain = gain;
                    auto out = renderAmp (s, guitar);
                    auto sine = renderAmp (s, makeSine (sr, 24000, 110.0, 0.1f));
                    const double rms = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 4800, 43200));
                    const double h3 = toneDb (sine, sr, 330.0, 12000, 9600) - toneDb (sine, sr, 110.0, 12000, 9600);
                    const double h2 = toneDb (sine, sr, 220.0, 12000, 9600) - toneDb (sine, sr, 110.0, 12000, 9600);
                    std::printf ("  g%.1f %6.1f dB (h2 %+5.1f h3 %+5.1f)", gain, rms, h2, h3);
                }
                std::printf ("\n");
            }
        return 0;
    }

    if (argc >= 6 && juce::String (argv[1]) == "--amp-spec")
    {
        // harmonics of a 110 Hz sine: --amp-spec channel character gain amplitude [master]
        const double sr = 48000.0;
        AmpBlock::Settings s;
        s.on = true; s.channel = juce::String (argv[2]).getIntValue(); s.character = juce::String (argv[3]).getFloatValue();
        s.gain = juce::String (argv[4]).getFloatValue();
        if (argc >= 7) s.master = juce::String (argv[6]).getFloatValue();
        auto out = renderAmp (s, makeSine (sr, 48000, 110.0, juce::String (argv[5]).getFloatValue()));
        const double h1 = toneDb (out, sr, 110.0, 24000, 19200);
        std::printf ("RMS %.1f dB, fundamental %.1f dB; harmonics re fundamental:", juce::Decibels::gainToDecibels (out.getRMSLevel (0, 24000, 19200)), h1);
        for (int h = 2; h <= 9; ++h)
            std::printf (" h%d %+.1f", h, toneDb (out, sr, 110.0 * h, 24000, 19200) - h1);
        std::printf ("  | 55 Hz %+.1f, 165.7 Hz (between) %+.1f\n", toneDb (out, sr, 55.0, 24000, 19200) - h1, toneDb (out, sr, 137.0, 24000, 19200) - h1);
        std::printf ("  other:");
        for (double f : { 3.0, 8.0, 15.0, 30.0, 1000.0, 5000.0, 10000.0, 18000.0, 23000.0 })
            std::printf (" %.0fHz %+.1f", f, toneDb (out, sr, f, 24000, 19200) - h1);
        double dc = 0; for (int i = 24000; i < 43200; ++i) dc += out.getSample (0, i);
        std::printf ("  DC %.2e\n", dc / 19200.0);
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--amp-balance")
    {
        // octave-band spectrum of the guitar signal through each model at noon (dB re 1 kHz band) + aliasing check
        const double sr = 48000.0;
        juce::dsp::FFT fft (13);
        const int n = 8192;
        std::printf ("            ");
        const double bands[] { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
        for (double b : bands) std::printf (" %6.0f", b);
        std::printf ("   alias(1.32k sine, g1)\n");
        for (int chn = 0; chn < 3; ++chn)
            for (float character : { 0.0f, 0.5f, 1.0f })
            {
                AmpBlock::Settings s;
                s.on = true; s.channel = chn; s.character = character; s.gain = chn == 0 ? 0.6f : 0.6f;
                // pink-ish noise in: the output's band levels show the amp's voicing (driven)
                juce::AudioBuffer<float> noise (2, 96000);
                {
                    juce::Random rng (5);
                    float b0 = 0, b1 = 0, b2 = 0;
                    for (int i = 0; i < noise.getNumSamples(); ++i)
                    {
                        const float w = rng.nextFloat() * 2.0f - 1.0f;
                        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                        const float v = 0.05f * (b0 + b1 + b2 + w * 0.1848f);
                        noise.setSample (0, i, v); noise.setSample (1, i, v);
                    }
                }
                auto spectrum = [&] (const juce::AudioBuffer<float>& b)
                {
                    std::vector<double> power ((size_t) n / 2, 0.0);
                    for (int start = 4800; start + n <= b.getNumSamples(); start += n / 2)
                    {
                        std::vector<float> buf ((size_t) n * 2, 0.0f);
                        for (int i = 0; i < n; ++i)
                            buf[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / n));
                        fft.performFrequencyOnlyForwardTransform (buf.data());
                        for (int k = 0; k < n / 2; ++k) power[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
                    }
                    return power;
                };
                auto band = [&] (const std::vector<double>& power, double centre)
                {
                    double e = 0.0;
                    for (int k = 1; k < n / 2; ++k)
                    {
                        const double f = k * sr / n;
                        if (f >= centre / std::sqrt (2.0) && f < centre * std::sqrt (2.0)) e += power[(size_t) k];
                    }
                    return 10.0 * std::log10 (e + 1e-30);
                };
                const auto pin = spectrum (noise), pout = spectrum (renderAmp (s, noise));
                const double ref = band (pout, 1000.0) - band (pin, 1000.0);
                std::printf ("%-6s %.1f ", AmpBlock::channelModel (chn), character);
                for (double b : bands) std::printf (" %+6.1f", band (pout, b) - band (pin, b) - ref);
                // aliasing: a 3520 Hz sine at full gain; everything that is not a harmonic of it
                s.gain = 1.0f;
                auto sine = renderAmp (s, makeSine (sr, 48000, 1318.5, 0.1f));
                std::vector<float> buf ((size_t) n * 2, 0.0f);
                for (int i = 0; i < n; ++i)
                    buf[(size_t) i] = sine.getSample (0, 24000 + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / n));
                fft.performFrequencyOnlyForwardTransform (buf.data());
                double harm = 0.0, other = 0.0;
                for (int k = 1; k < n / 2; ++k)
                {
                    const double f = k * sr / n;
                    const double m = std::fmod (f, 1318.5);
                    const bool isHarm = m < 20.0 || m > 1298.5;
                    (isHarm ? harm : other) += (double) buf[(size_t) k] * buf[(size_t) k];
                }
                std::printf ("   %+6.1f dB\n", 10.0 * std::log10 (other / harm));
            }
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--knob-audit")
    {
        // every continuous parameter at min / mid / max with its block on: output level and brightness (diagnostic)
        const double sr = 48000.0;
        auto guitar = makeGuitar (sr, 48000);
        auto powerFor = [] (const juce::String& id) -> const char*
        {
            using namespace ParamIDs;
            if (id.startsWith ("fuzz")) return fuzzOn;
            if (id.startsWith ("swarm")) return swarmOn;
            if (id.startsWith ("flow")) return flowOn;
            if (id.startsWith ("geq")) return geqOn;
            if (id.startsWith ("peq")) return peqOn;
            if (id.startsWith ("rev")) return revOn;
            if (id.startsWith ("amp")) return ampOn;
            if (id.startsWith ("cab")) return cabOn;
            if (id.startsWith ("rb") || id.startsWith ("tr") || id.startsWith ("hv")) return rbOn;
            if (id.startsWith ("sh") || id == stingMix || id == panic || id == chaos || id == speed || id == shiftA || id == shiftB || id == rise || id == fall) return shOn;
            return nullptr;
        };
        // pink noise in bursts (so gates, trackers and tails all get something), brightness = highs - lows
        juce::AudioBuffer<float> pink (2, 48000);
        {
            juce::Random rng (5);
            float b0 = 0, b1 = 0, b2 = 0;
            for (int i = 0; i < pink.getNumSamples(); ++i)
            {
                const float w = rng.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                const float env = (i % 12000) < 9000 ? 1.0f : 0.0f;
                const float v = 0.08f * env * (b0 + b1 + b2 + w * 0.1848f) + guitar.getSample (0, i);
                pink.setSample (0, i, v); pink.setSample (1, i, v);
            }
        }
        guitar = pink;
        auto band = [&] (const juce::AudioBuffer<float>& b, double lo, double hi)
        {
            double e = 0;
            for (double f = lo; f < hi; f *= 1.19)
                e += std::pow (10.0, toneDb (b, sr, f, 9600, 32768) / 10.0);
            return 10.0 * std::log10 (e + 1e-30);
        };
        auto centroid = [&] (const juce::AudioBuffer<float>& b) { return band (b, 2000, 8000) - band (b, 100, 400); };
        SwarmnessAudioProcessor probe;
        for (auto* param : probe.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            if (ranged == nullptr || dynamic_cast<juce::AudioParameterBool*> (param) != nullptr || dynamic_cast<juce::AudioParameterChoice*> (param) != nullptr)
                continue;
            const auto id = ranged->paramID;
            if (id.startsWith ("chain") || id.startsWith ("lane") || id.startsWith ("trL") || id == "scene") continue;
            std::printf ("%-14s", id.toRawUTF8());
            for (float norm : { 0.0f, 0.5f, 1.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                if (auto* pw = powerFor (id)) setParam (p, pw, 1.0f);
                if (id.startsWith ("rev") && id != "revMix") setParam (p, ParamIDs::revMix, 100.0f);
                p.getAPVTS().getParameter (id)->setValueNotifyingHost (norm);
                auto out = render (p, guitar, sr, 256);
                std::printf ("  [%s] %6.1f dB %+6.1f", p.getAPVTS().getParameter (id)->getCurrentValueAsText().substring (0, 9).paddedRight (' ', 9).toRawUTF8(),
                             juce::Decibels::gainToDecibels (out.getRMSLevel (0, 9600, 38400)), centroid (out));
            }
            std::printf ("\n");
        }
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--rev-size")
    {
        // CRYPT: wet level and "spaciousness" vs SIZE (diagnostic)
        const double sr = 48000.0;
        for (int type : { 0, 1, 2, 3 })
            for (float size : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::revOn, 1.0f);
                setParam (p, ParamIDs::revType, (float) type);
                setParam (p, ParamIDs::revMix, 100.0f);
                setParam (p, ParamIDs::revSize, size);
                setParam (p, ParamIDs::revDecay, 2.5f);
                juce::AudioBuffer<float> in (2, 48000 * 3);
                in.clear();
                for (int i = 0; i < 480; ++i) { in.setSample (0, i, 0.5f * std::sin (i * 0.3f)); in.setSample (1, i, 0.5f * std::sin (i * 0.3f)); }
                auto out = render (p, in, sr, 256);
                std::printf ("type %d size %3.0f: 0-100 ms %6.1f dB, 100-500 ms %6.1f dB, 0.5-1.5 s %6.1f dB\n", type, size,
                             juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, 4800)),
                             juce::Decibels::gainToDecibels (out.getRMSLevel (0, 4800, 19200)),
                             juce::Decibels::gainToDecibels (out.getRMSLevel (0, 24000, 48000)));
            }
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--bad-irs")
    {
        // CRYPT with pathological impulse responses: after the playing stops, the wet must die away
        const double sr = 48000.0;
        struct Case { const char* name; std::function<float (int, juce::Random&)> gen; int len; bool isFloat; };
        std::vector<Case> cases {
            { "normal decay", [] (int i, juce::Random& r) { return (r.nextFloat() * 2 - 1) * std::exp (-i / 9600.0f); }, 96000, false },
            { "NaN inside", [] (int i, juce::Random& r) { return i == 500 ? std::numeric_limits<float>::quiet_NaN() : (r.nextFloat() * 2 - 1) * std::exp (-i / 9600.0f); }, 96000, true },
            { "inf inside", [] (int i, juce::Random& r) { return i == 500 ? std::numeric_limits<float>::infinity() : (r.nextFloat() * 2 - 1) * std::exp (-i / 9600.0f); }, 96000, true },
            { "almost silent", [] (int i, juce::Random& r) { return 1.0e-9f * (r.nextFloat() * 2 - 1) * std::exp (-i / 9600.0f); }, 96000, true },
            { "huge float", [] (int i, juce::Random& r) { return 5000.0f * (r.nextFloat() * 2 - 1) * std::exp (-i / 9600.0f); }, 96000, true },
            { "DC block", [] (int, juce::Random&) { return 0.5f; }, 96000, false },
            { "silence", [] (int, juce::Random&) { return 0.0f; }, 96000, false },
            { "12 s noise", [] (int, juce::Random& r) { return 0.3f * (r.nextFloat() * 2 - 1); }, 48000 * 14, false },
        };
        for (auto& c : cases)
        {
            auto irFile = juce::File::createTempFile (".wav");
            {
                juce::AudioBuffer<float> ir (2, c.len);
                juce::Random rng (3);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < c.len; ++i)
                        ir.setSample (ch, i, c.gen (i, rng));
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = irFile.createOutputStream();
                auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2)
                                                          .withBitsPerSample (c.isFloat ? 32 : 24)
                                                          .withSampleFormat (c.isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                       : juce::AudioFormatWriterOptions::SampleFormat::integral));
                w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }
            SwarmnessAudioProcessor p;
            resetToInit (p);
            const auto err = p.loadReverbIR (irFile);
            setParam (p, ParamIDs::revOn, 1.0f);
            setParam (p, ParamIDs::revType, 4.0f);
            setParam (p, ParamIDs::revMix, 50.0f);
            p.prepareToPlay (sr, 256);
            juce::Thread::sleep (300);
            juce::AudioBuffer<float> in (2, 48000 * 16);
            in.clear();
            auto g = makeGuitar (sr, 48000);
            for (int ch = 0; ch < 2; ++ch) in.copyFrom (ch, 0, g, ch, 0, 48000);
            juce::MidiBuffer midi;
            float lateOn = 0.0f, lateOff = 0.0f, playing = 0.0f;
            bool finite = true;
            for (int start = 0; start < in.getNumSamples(); start += 256)
            {
                if (start == 48000 * 14) setParam (p, ParamIDs::revOn, 0.0f);
                juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, start, 256);
                p.processBlock (view, midi);
                const float m = view.getMagnitude (0, 0, 256);
                finite = finite && std::isfinite (m);
                if (start < 48000) playing = juce::jmax (playing, m);
                else if (start > 48000 * 13 && start < 48000 * 14) lateOn = juce::jmax (lateOn, m);
                else if (start > 48000 * 15) lateOff = juce::jmax (lateOff, m);
            }
            std::printf ("%-14s load: %-28s playing peak %.3f  12 s after: %.2e  after OFF: %.2e  %s\n", c.name,
                         err.isEmpty() ? "ok" : err.substring (0, 28).toRawUTF8(), playing, lateOn, lateOff, finite ? "" : "NON-FINITE");
            irFile.deleteFile();
        }
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--amp-tables")
    {
        const auto& t = ampsim::tables();
        for (int r = 0; r < 6; ++r)
            for (int st = 0; st < 4; ++st)
            {
                std::printf ("%-10s st%d thr %5.2f :", AmpBlock::referenceName (r / 2, r % 2), st, t[(size_t) r][(size_t) st].thr);
                for (float v : { -30.0f, -8.0f, -4.0f, -2.0f, -1.0f, -0.1f, 0.1f, 1.0f, 2.0f, 4.0f, 8.0f })
                    std::printf (" %7.1f", t[(size_t) r][(size_t) st] (v));
                std::printf ("\n");
            }
        return 0;
    }

    if (argc >= 2 && juce::String (argv[1]) == "--amp-probe")
    {
        // peak volts through the circuit for a 110 Hz sine (0.1 FS) - diagnostic
        const double sr = 48000.0;
        for (int chn = 0; chn < 3; ++chn)
            for (float character : { 0.0f, 1.0f })
                for (float gain : { 0.0f, 0.5f, 1.0f })
                {
                    AmpBlock a;
                    a.prepare (sr, 256);
                    AmpBlock::Settings s;
                    s.on = true; s.channel = chn; s.character = character; s.gain = gain;
                    a.probing = true;
                    auto in = makeSine (sr, 24000, 110.0, 0.1f);
                    renderAmp (s, in, &a);
                    a.probeReset();
                    auto out = renderAmp (s, makeSine (sr, 24000, 110.0, 0.1f), &a);
                    const double hf = toneDb (out, sr, 21000.0, 4000, 16000);
                    std::printf ("%-10s g%.1f  st %6.1f %6.1f %6.1f %6.1f  stack %6.1f  pa %5.2f  i %5.2f  sup %4.2f  out %5.1f dB  21k %6.1f\n",
                                 AmpBlock::referenceName (chn, (int) character), gain, a.probe[0], a.probe[1], a.probe[2], a.probe[3],
                                 a.probe[4], a.probe[5], a.probe[6], a.probe[7],
                                 juce::Decibels::gainToDecibels (out.getRMSLevel (0, 4000, 16000)), hf);
                }
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--screenshot")
    {
        // Renders the editor to PNG (useful for design review / docs).
        SwarmnessAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        if (argc >= 4)
            p.getPresetManager().loadPreset (argv[3]);
        p.getAPVTS().getParameter (ParamIDs::oct1)->setValueNotifyingHost (1.0f);
        const float scale = argc >= 5 ? juce::String (argv[4]).getFloatValue() : 1.0f;
        if (argc >= 6)
            p.setUiPage (juce::String (argv[5]).getIntValue());
        const bool mini = argc >= 7 && juce::String (argv[6]) == "mini";
        if (argc >= 7 && juce::String (argv[6]) == "nam")   // the AMP in NAM mode, with an example capture
        {
            p.getAPVTS().getParameter (ParamIDs::ampChannel)->setValueNotifyingHost (1.0f);
            p.loadNamModel (namExample ("A2.nam"));
            p.getAPVTS().getParameter (ParamIDs::cabType)->setValueNotifyingHost (1.0f);
            // and WASP in NAM mode with a capture
            p.getAPVTS().getParameter (ParamIDs::drvNam)->setValueNotifyingHost (1.0f);
            p.getAPVTS().getParameter (ParamIDs::drvOn)->setValueNotifyingHost (1.0f);
            p.loadNamModel (namExample ("A2.nam"), true);
            p.getAPVTS().getParameter (ParamIDs::cabType)->setValueNotifyingHost (1.0f);
            for (int k = 0; k < 2 && argc >= 8 + k; ++k)   // optional IRs for slots A and B
                p.loadCabIR (juce::File (argv[7 + k]), k);
        }
        p.setUiMini (mini);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        const bool tunerShot = argc >= 7 && juce::String (argv[6]) == "tuner";
        const bool infoShot = argc >= 7 && juce::String (argv[6]) == "info";
        editor->setSize (juce::roundToInt (MainPanel::baseWidth * scale), juce::roundToInt ((mini ? MainPanel::miniHeight : MainPanel::baseHeight) * scale));

        // Feed a little audio so meters and the pitch trace show activity.
        auto input = makeGuitar (48000.0, 256);
        juce::MidiBuffer midi;
        for (int i = 0; i < 120; ++i)
        {
            juce::AudioBuffer<float> b (input);
            p.processBlock (b, midi);
            if (i % 4 == 0)
                if (auto* ed = dynamic_cast<SwarmnessAudioProcessorEditor*> (editor.get()))
                    ed->refresh();
        }

        if (tunerShot)
        {
            // open the tuner and play it an A2 a touch sharp
            std::function<TunerOverlay* (juce::Component&)> find = [&] (juce::Component& c) -> TunerOverlay*
            {
                if (auto* t = dynamic_cast<TunerOverlay*> (&c)) return t;
                for (auto* ch : c.getChildren())
                    if (auto* t = find (*ch)) return t;
                return nullptr;
            };
            if (auto* t = find (*editor))
            {
                t->open();
                auto tone = makeSine (48000.0, 9600, 110.0 * std::pow (2.0, 4.0 / 1200.0), 0.2f);
                for (int k = 0; k < 20; ++k)
                {
                    juce::AudioBuffer<float> b (tone);
                    p.processBlock (b, midi);
                }
                for (int k = 0; k < 20; ++k)
                    t->timerCallbackForTests();
            }
        }
        if (infoShot)   // the help overlay
        {
            std::function<InfoOverlay* (juce::Component&)> find = [&] (juce::Component& c) -> InfoOverlay*
            {
                if (auto* o = dynamic_cast<InfoOverlay*> (&c)) return o;
                for (auto* ch : c.getChildren())
                    if (auto* o = find (*ch)) return o;
                return nullptr;
            };
            if (auto* o = find (*editor))
            {
                o->setVisible (true);
                o->toFront (false);
            }
        }
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--compare-pedal")
    {
        // The JUCE-free pedal build must sound exactly like the plug-in build of the same engine.
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        const auto ref = swarmness::bench::render (swarmness::bench::scenarios().back(), 3);
        const auto* other = static_cast<const float*> (mb.getData());
        const size_t n = juce::jmin (ref.size(), mb.getSize() / sizeof (float));
        double err = 0.0, sig = 0.0, maxDiff = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double d = (double) ref[i] - other[i];
            err += d * d;
            sig += (double) ref[i] * ref[i];
            maxDiff = juce::jmax (maxDiff, std::abs (d));
        }
        const double nullDb = 10.0 * std::log10 ((err + 1e-30) / (sig + 1e-30));
        std::printf ("pedal build vs plug-in build: %zu samples, null %.1f dB, max diff %.2e (%s)\n",
                     n, nullDb, maxDiff, maxDiff == 0.0 ? "bit-exact" : "same CPU family expected bit-exact; other CPUs / libm: < -60 dB");
        // Same compiler + CPU: bit-exact. Another architecture's libm (sin / exp) differs in the last bits.
        return n == ref.size() && nullDb < -60.0 ? 0 : 1;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--render")
    {
        renderPresets (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        return 0;
    }

    if (argc >= 4 && juce::String (argv[1]) == "--process")
    {
        // --process in.wav out.wav [preset=Name] [cabir=file.wav] [paramId=value ...]: the whole plug-in on a file (mono in, stereo out)
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[2])));
        if (r == nullptr) { std::printf ("cannot read %s\n", argv[2]); return 1; }
        const double sr = r->sampleRate;
        juce::AudioBuffer<float> in (2, (int) r->lengthInSamples);
        r->read (&in, 0, in.getNumSamples(), 0, true, true);
        if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
        SwarmnessAudioProcessor p;
        resetToInit (p);
        for (int i = 4; i < argc; ++i)
        {
            const juce::String kv (argv[i]);
            const auto k = kv.upToFirstOccurrenceOf ("=", false, false), v = kv.fromFirstOccurrenceOf ("=", false, false);
            if (k == "preset") { p.getPresetManager().loadPreset (v); continue; }
            if (k == "cabir")
            {
                if (const auto err = p.loadCabIR (juce::File (v)); err.isNotEmpty())
                    std::printf ("%s\n", err.toRawUTF8());
                continue;
            }
            if (auto* param = p.getAPVTS().getParameter (k))
                param->setValueNotifyingHost (param->convertTo0to1 (v.getFloatValue()));
            else
                std::printf ("unknown parameter %s\n", k.toRawUTF8());
        }
        auto out = render (p, in, sr, 256);
        juce::File f (argv[3]);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        std::printf ("latency %d samples\n", p.getLatencySamples());
        return 0;
    }
    if (argc >= 4 && juce::String (argv[1]) == "--hive-probe")
    {
        // --hive-probe in.wav outdir [raw=0|1]: HIVE alone as a DRY / all-HOLD delay, every stage dumped to WAV
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[2])));
        if (r == nullptr) { std::printf ("cannot read %s\n", argv[2]); return 1; }
        juce::AudioBuffer<float> in (2, (int) r->lengthInSamples);
        r->read (&in, 0, in.getNumSamples(), 0, true, true);
        if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
        const double sr = r->sampleRate;
        HiveBlock hive;
        hive.prepare (sr, 256);
        HiveBlock::Settings s;
        s.voicesOn = true; s.drone = 0.0f; s.queen = 0.0f; s.tracking = 1.0f;
        s.trails = 0.55f; s.repeatSeconds = 0.3f; s.tone = 0.6f; s.fromDry = true; s.gate = 1.0f; s.mix = 0.5f;
        s.steps.numSteps = 8;
        for (int k = 0; k < 8; ++k) { s.steps.level[(size_t) k] = 1.0f; s.steps.move[(size_t) k] = HiveBlock::hold; }
        for (int i = 4; i < argc; ++i)   // raw=0|1  moves=0,4,0,4 (0 hold, 1 up, 2 down, 3 random, 4 reverse)
        {
            const juce::String kv (argv[i]);
            if (kv == "raw=1") s.raw = true;
            if (kv == "raw=0") s.raw = false;
            if (kv.startsWith ("moves="))
            {
                juce::StringArray m;
                m.addTokens (kv.fromFirstOccurrenceOf ("=", false, false), ",", "");
                s.steps.numSteps = juce::jlimit (1, 8, m.size());
                for (int k = 0; k < m.size() && k < 8; ++k)
                    s.steps.move[(size_t) k] = (HiveBlock::StepMove) juce::jlimit (0, 4, m[k].getIntValue());
            }
        }
        hive.setParams (s);
        std::map<juce::String, std::array<std::vector<float>, 2>> taps;
        hive.debugTap = [&taps] (const char* name, const float* const* data, int ch, int n)
        {
            auto& t = taps[name];
            for (int c = 0; c < 2; ++c)
                t[(size_t) c].insert (t[(size_t) c].end(), data[juce::jmin (c, ch - 1)], data[juce::jmin (c, ch - 1)] + n);
        };
        auto& out = taps["out"];
        for (int start = 0; start < in.getNumSamples(); start += 256)
        {
            const int n = juce::jmin (256, in.getNumSamples() - start);
            float* ptr[2] { in.getWritePointer (0, start), in.getWritePointer (1, start) };
            hive.process (ptr, 2, n);
            for (int c = 0; c < 2; ++c)
                out[(size_t) c].insert (out[(size_t) c].end(), ptr[c], ptr[c] + n);
        }
        const juce::File dir (argv[3]);
        dir.createDirectory();
        for (auto& [name, t] : taps)
        {
            juce::AudioBuffer<float> b (2, (int) t[0].size());
            for (int c = 0; c < 2; ++c)
                b.copyFrom (c, 0, t[(size_t) c].data(), b.getNumSamples());
            auto f = dir.getChildFile (name + ".wav");
            f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        std::printf ("wrote %d taps\n", (int) taps.size());
        return 0;
    }

    if (argc >= 4 && juce::String (argv[1]) == "--perf-blocks")
    {
        // --perf-blocks "<preset>" <block> [paramId=value ...]: per-block processing time at 48 kHz - what a
        // live buffer sees (dropouts come from the slowest blocks, not the average)
        SwarmnessAudioProcessor p;
        p.getPresetManager().loadPreset (argv[2]);
        const int block = juce::jmax (16, juce::String (argv[3]).getIntValue());
        for (int i = 4; i < argc; ++i)
        {
            const juce::String kv (argv[i]);
            if (auto* param = p.getAPVTS().getParameter (kv.upToFirstOccurrenceOf ("=", false, false)))
                param->setValueNotifyingHost (param->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
        }
        const double sr = 48000.0;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        auto input = makeGuitar (sr, (int) sr * 8);
        p.getAPVTS().getParameter (ParamIDs::oct1)->setValueNotifyingHost (1.0f);
        std::vector<double> ms;
        juce::MidiBuffer midi;
        for (int start = 0; start + block <= input.getNumSamples(); start += block)
        {
            juce::AudioBuffer<float> b (2, block);
            for (int ch = 0; ch < 2; ++ch)
                b.copyFrom (ch, 0, input, juce::jmin (ch, input.getNumChannels() - 1), start, block);
            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (b, midi);
            ms.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        }
        const double budget0 = 1000.0 * block / sr;
        for (size_t i = 0; i < ms.size(); ++i)
            if (ms[i] > budget0 * 0.5)
                std::printf ("  slow block #%d at %.2f s: %.3f ms\n", (int) i, (double) i * block / sr, ms[i]);
        std::sort (ms.begin(), ms.end());
        const double budget = 1000.0 * block / sr, mean = std::accumulate (ms.begin(), ms.end(), 0.0) / (double) ms.size();
        const int over = (int) std::count_if (ms.begin(), ms.end(), [budget] (double v) { return v > budget; });
        std::printf ("%-20s block %4d (budget %.2f ms): mean %.3f ms (%.0f%%), p99 %.3f, max %.3f ms (%.0f%% of budget), %d of %d blocks over budget\n",
                     argv[2], block, budget, mean, 100.0 * mean / budget, ms[(size_t) ((double) ms.size() * 0.99)], ms.back(),
                     100.0 * ms.back() / budget, over, (int) ms.size());
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--only")
    {
        const juce::String which (argv[2]);
        if (which == "chain")   testChainOrder();
        if (which == "hive")    testHiveBlock();
        if (which == "magic")   testMagicBounded();
        if (which == "parallel") testParallelRouting();
        if (which == "comb")    testGraphicEq();
        if (which == "carve")   testParametricEq();
        if (which == "reverb")  { testReverb(); testBadImpulseResponses(); }
        if (which == "amp")     testAmp();
        if (which == "cab")     testCab();
        if (which == "wasp")    testWasp();
        if (which == "splices") testSpliceContinuity();
        if (which == "tuner")   testTuner();
        std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
        return failures == 0 ? 0 : 1;
    }

    std::printf ("Swarmness DSP tests\n");
    testPresetsStable();
    testBypassNull();
    testFootswitchesOverrideBypass();
    testVenomLink();
    testDryAlignment();
    testCleanPathTransparency();
    testNoiseOctaves();
    testFootswitchRelease();
    testRiseFall();
    testTrailsInterval();
    testMagicBounded();
    testFuzzLevel();
    testGlareOctave();
    testHiveMix();
    testTrails();
    reportLag();
    testFuzzIdleNoise();
    testFuzzSag();
    testMonoToStereo();
    testMidiLearn();
    testUnsupportedPresets();
    testScenes();
    testDetune();
    testInputSensitivity();
    testSwarmBounded();
    testStateRoundTrip();
    testPresetDirtyTracking();
    testPresetBanks();
    testHiveBlock();
    testChainOrder();
    testParallelRouting();
    testNonFiniteInput();
    testSpliceContinuity();
    testMissingFiles();
    testParameterOrder();
    testGraphicEq();
    testParametricEq();
    testReverb();
    testBadImpulseResponses();
    testAmp();
    testCab();
    testWasp();
    testPerformance();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
