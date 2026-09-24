// Offline, headless verification of the Swarmness DSP.
// Build target: SwarmnessTests   (run via `ctest` or directly)
//
// Optional: `SwarmnessTests --render <dir>` writes WAV renders of every factory preset.

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    void testPresetsStable()
    {
        std::printf ("\nFactory presets: stability at several sample rates / block sizes\n");
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
                    p.getPresetManager().loadPreset (name);
                    setParam (p, ParamIDs::flowSync, 0.0f);   // no playhead offline
                    auto out = render (p, input, sr, block);
                    ok = ok && allFinite (out);
                    peak = juce::jmax (peak, out.getMagnitude (0, out.getNumSamples()));
                }
                check (ok && peak < 4.0f, juce::String::formatted ("sr %.0f, block %d: finite, peak %.2f", sr, block, peak));
            }
        }
    }

    void testBypassNull()
    {
        std::printf ("\nBypass is sample-accurate and latency compensated\n");
        for (int quality : { 0, 1 })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::quality, (float) quality);
            setParam (p, ParamIDs::bypass, 1.0f);
            setParam (p, ParamIDs::drive, 60.0f);
            const double sr = 48000.0;
            auto input = makeGuitar (sr, 48000);
            auto out = render (p, input, sr, 256);
            const int latency = p.getLatencySamples();
            const double db = nullDb (out, input, latency, 4096, input.getNumSamples());
            check (db < -120.0, juce::String::formatted ("%s: bypass null %.1f dB (latency %d)", quality ? "Studio" : "Live", db, latency));
        }
    }

    void testDryAlignment()
    {
        std::printf ("\nMix 0%% returns the dry signal aligned with the reported latency\n");
        for (int quality : { 0, 1 })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::quality, (float) quality);
            setParam (p, ParamIDs::mix, 0.0f);
            const double sr = 44100.0;
            auto input = makeGuitar (sr, 44100);
            auto out = render (p, input, sr, 512);
            const double db = nullDb (out, input, p.getLatencySamples(), 8192, input.getNumSamples());
            check (db < -100.0, juce::String::formatted ("%s: dry null %.1f dB", quality ? "Studio" : "Live", db));
        }
    }

    void testCleanPathTransparency()
    {
        std::printf ("\nVoltage off, mix 100%%: effect path is transparent\n");
        for (int quality : { 0, 1 })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::quality, (float) quality);
            setParam (p, ParamIDs::pitchOn, 0.0f);
            setParam (p, ParamIDs::mix, 100.0f);
            const double sr = 48000.0;
            auto input = makeSine (sr, 48000, 1000.0);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 16384, input.getNumSamples());
            check (db < -50.0, juce::String::formatted ("%s: clean-path residual %.1f dB", quality ? "Studio" : "Live", db));
        }
    }

    void testUnityReconstruction()
    {
        std::printf ("\nVoltage on at unison (0 oct, 0 st): pitch engines reconstruct the input\n");
        for (int quality : { 0, 1 })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::quality, (float) quality);
            setParam (p, ParamIDs::octave, 2.0f);
            setParam (p, ParamIDs::mix, 100.0f);
            const double sr = 48000.0;
            auto input = makeGuitar (sr, 96000);
            auto out = render (p, input, sr, 256);
            const double db = nullDb (out, input, p.getLatencySamples(), 48000, input.getNumSamples());
            if (db > -60.0)
                for (int lag = 0; lag < 9000; ++lag)
                    if (nullDb (out, input, lag, 48000, 52000) < -40.0)
                        std::printf ("    matches at lag %d (reported %d)\n", lag, p.getLatencySamples());
            check (db < -60.0, juce::String::formatted ("%s: unison residual %.1f dB", quality ? "Studio" : "Live", db));
        }
    }

    void testDriveLoudness()
    {
        std::printf ("\nDrive: automatic gain compensation keeps loudness steady\n");
        const double sr = 48000.0;
        auto input = makeGuitar (sr, 96000);
        double reference = 0.0;
        for (float drive : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            SwarmnessAudioProcessor p;
            resetToInit (p);
            setParam (p, ParamIDs::pitchOn, 0.0f);
            setParam (p, ParamIDs::mix, 100.0f);
            setParam (p, ParamIDs::drive, drive);
            auto out = render (p, input, sr, 256);
            const double rms = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, 24000, 72000));
            if (drive == 0.0f) reference = rms;
            check (std::abs (rms - reference) < 4.0, juce::String::formatted ("drive %3.0f%%: %+.1f dB vs clean", drive, rms - reference));
        }
    }

    void testQualitySwitch()
    {
        std::printf ("\nSwitching engines while running\n");
        SwarmnessAudioProcessor p;
        resetToInit (p);
        const double sr = 44100.0;
        p.prepareToPlay (sr, 256);
        auto input = makeGuitar (sr, 256);
        juce::MidiBuffer midi;
        bool ok = true;
        for (int i = 0; i < 400; ++i)
        {
            if (i % 50 == 0)
                setParam (p, ParamIDs::quality, (float) ((i / 50) % 2));
            juce::AudioBuffer<float> b (input);
            p.processBlock (b, midi);
            ok = ok && allFinite (b);
        }
        check (ok, "no invalid output while toggling Live/Studio");
    }

    void testPitchAccuracy()
    {
        std::printf ("\nPitch accuracy and purity (220 Hz sine, mix 100%%)\n");
        struct Case { int octave; int semi; double expected; };
        const Case cases[] = { { 2, 0, 220.0 }, { 3, 0, 440.0 }, { 1, 0, 110.0 }, { 2, 7, 329.63 }, { 4, 0, 880.0 }, { 0, 0, 55.0 } };

        for (int quality : { 0, 1 })
        {
            for (const auto& c : cases)
            {
                SwarmnessAudioProcessor p;
                resetToInit (p);
                setParam (p, ParamIDs::quality, (float) quality);
                setParam (p, ParamIDs::octave, (float) c.octave);
                setParam (p, ParamIDs::semitone, (float) c.semi);
                setParam (p, ParamIDs::rise, 0.0f);
                setParam (p, ParamIDs::mix, 100.0f);

                const double sr = 48000.0;
                auto input = makeSine (sr, 48000 * 2, 220.0);
                auto out = render (p, input, sr, 256);

                double purity = 0.0;
                const double f = dominantFrequency (out, sr, 48000, purity);
                const double cents = 1200.0 * std::log2 (f / c.expected);
                check (std::abs (cents) < 5.0 && purity < (quality == 1 ? -30.0 : -14.0),
                       juce::String::formatted ("%s oct %+d semi %+d: %.2f Hz (%+.1f cents), spurious %.1f dB",
                                                quality ? "Studio" : "Live  ", c.octave - 2, c.semi, f, cents, purity));
            }
        }
    }

    void testStateRoundTrip()
    {
        std::printf ("\nState save / restore\n");
        SwarmnessAudioProcessor a;
        a.getPresetManager().loadPreset ("Swarm Cloud");
        setParam (a, ParamIDs::drive, 42.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        SwarmnessAudioProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        const float drive = b.getAPVTS().getRawParameterValue (ParamIDs::drive)->load();
        const float swarmMix = b.getAPVTS().getRawParameterValue (ParamIDs::swarmMix)->load();
        check (std::abs (drive - 42.0f) < 0.05f && std::abs (swarmMix - 60.0f) < 0.05f, "parameters restored");
        check (b.getPresetManager().getCurrentPresetName() == "Swarm Cloud", "preset name restored");
    }

    void testPresetDirtyTracking()
    {
        std::printf ("\nPreset dirty tracking\n");
        SwarmnessAudioProcessor p;
        auto& pm = p.getPresetManager();
        pm.loadPreset ("Glitch Anger");
        check (! pm.isDirty(), "clean after load");
        setParam (p, ParamIDs::mix, 12.0f);
        check (pm.isDirty(), "dirty after edit");
        setParam (p, ParamIDs::bypass, 1.0f);
        pm.loadPreset ("Glitch Anger");
        check (! pm.isDirty(), "bypass does not affect preset state");
    }

    void testPerformance()
    {
        std::printf ("\nPerformance (48 kHz, 128-sample blocks, everything on)\n");
        for (int quality : { 0, 1 })
        {
            SwarmnessAudioProcessor p;
            p.getPresetManager().loadPreset ("Swarm Cloud");
            setParam (p, ParamIDs::quality, (float) quality);
            setParam (p, ParamIDs::drive, 50.0f);
            setParam (p, ParamIDs::flowAmount, 50.0f);
            setParam (p, ParamIDs::anger, 50.0f);
            const double sr = 48000.0;
            auto input = makeGuitar (sr, (int) sr * 10);
            const auto t0 = std::chrono::steady_clock::now();
            auto out = render (p, input, sr, 128);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double load = secs / 10.0 * 100.0;
            check (load < 25.0, juce::String::formatted ("%s: %.2f%% of one core (realtime factor %.0fx)",
                                                         quality ? "Studio" : "Live", load, 10.0 / secs));
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
            auto out = render (p, input, sr, 256);
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
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String (argv[1]) == "--screenshot")
    {
        // Renders the editor to PNG (useful for design review / docs).
        SwarmnessAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        if (argc >= 4)
            p.getPresetManager().loadPreset (argv[3]);
        const float scale = argc >= 5 ? juce::String (argv[4]).getFloatValue() : 1.0f;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (juce::roundToInt (1000 * scale), juce::roundToInt (640 * scale));

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

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--render")
    {
        renderPresets (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        return 0;
    }

    std::printf ("Swarmness DSP tests\n");
    testPresetsStable();
    testBypassNull();
    testDryAlignment();
    testCleanPathTransparency();
    testUnityReconstruction();
    testPitchAccuracy();
    testDriveLoudness();
    testQualitySwitch();
    testStateRoundTrip();
    testPresetDirtyTracking();
    testPerformance();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
