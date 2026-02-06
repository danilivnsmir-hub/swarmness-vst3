#include "GranularPitchShifter.h"

void GranularPitchShifter::prepare(double sampleRate, int blockSize) {
    mSampleRate = sampleRate;
    mBlockSize = blockSize;

    for (auto& ch : mDelayBuffer) {
        ch.resize(kMaxDelayLength, 0.0f);
    }

    mPitchRatio.reset(sampleRate, mRiseTime / 1000.0);
    mWetGain.reset(sampleRate, 0.001); // 1ms smoothing to avoid clicks
    
    // Initialize grains with staggered phases
    for (int i = 0; i < kNumGrains; ++i) {
        mGrains[i].phase = static_cast<float>(i) / kNumGrains;
        mGrains[i].grainSize = mGrainSize;
        mGrains[i].active = true;
    }

    reset();
}

void GranularPitchShifter::reset() {
    for (auto& ch : mDelayBuffer) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
    mWritePos = 0;
}

void GranularPitchShifter::setOctaveMode(int mode) {
    mOctaveMode = juce::jlimit(0, 4, mode);
    float targetRatio;
    switch (mOctaveMode) {
        case 0: targetRatio = 0.25f; break; // -2 octaves
        case 1: targetRatio = 0.5f; break;  // -1 octave
        case 2: targetRatio = 1.0f; break;  // 0 (no pitch shift)
        case 3: targetRatio = 2.0f; break;  // +1 octave
        case 4: targetRatio = 4.0f; break;  // +2 octaves
        default: targetRatio = 1.0f;
    }
    mPitchRatio.setTargetValue(targetRatio);
}

void GranularPitchShifter::setEngage(bool engaged) {
    mEngaged = engaged;
    mWetGain.setTargetValue(engaged ? 1.0f : 0.0f);
}

void GranularPitchShifter::setRiseTime(float ms) {
    mRiseTime = juce::jlimit(1.0f, 2000.0f, ms);
    mPitchRatio.reset(mSampleRate, mRiseTime / 1000.0);
}

void GranularPitchShifter::setPanic(float amount) {
    mPanic = juce::jlimit(0.0f, 1.0f, amount);
}

void GranularPitchShifter::setChaos(float amount) {
    mChaos = juce::jlimit(0.0f, 1.0f, amount);
}

void GranularPitchShifter::setDynamicPitchOffset(float semitones) {
    mDynamicOffset = semitones;
}

float GranularPitchShifter::hannWindow(float phase) {
    return 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * phase));
}

float GranularPitchShifter::hermiteInterpolate(float* buffer, int bufferSize, float pos) {
    int x0 = static_cast<int>(pos);
    float frac = pos - x0;

    auto getSample = [&](int idx) {
        idx = ((idx % bufferSize) + bufferSize) % bufferSize;
        return buffer[idx];
    };

    float y0 = getSample(x0 - 1);
    float y1 = getSample(x0);
    float y2 = getSample(x0 + 1);
    float y3 = getSample(x0 + 2);

    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void GranularPitchShifter::process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Dynamic pitch offset from slide/randomizer
    float dynamicRatio = std::pow(2.0f, mDynamicOffset / 12.0f);

    for (int sample = 0; sample < numSamples; ++sample) {
        float basePitchRatio = mPitchRatio.getNextValue();
        float wetGain = mWetGain.getNextValue();

        // Apply chaos modulation (random pitch wobble - musical glitch character)
        float chaosModulation = 0.0f;
        if (mChaos > 0.0f) {
            mChaosPhase += (3.0f + mRandom.nextFloat() * 7.0f) / static_cast<float>(mSampleRate);
            if (mChaosPhase >= 1.0f) mChaosPhase -= 1.0f;
            // Slower, more musical chaos (±1.5 semitones max from LFO, ±0.5 from random)
            chaosModulation = mChaos * 1.5f * std::sin(mChaosPhase * juce::MathConstants<float>::twoPi);
            chaosModulation += mChaos * 0.5f * (mRandom.nextFloat() * 2.0f - 1.0f);
        }

        // Apply panic modulation (rapid pitch fluctuation - glitchy character)
        float panicModulation = 0.0f;
        if (mPanic > 0.0f) {
            // ±2 semitones max for more noticeable glitch effect
            panicModulation = mPanic * 2.0f * (mRandom.nextFloat() * 2.0f - 1.0f);
        }

        float totalPitchRatio = basePitchRatio * dynamicRatio * 
                                std::pow(2.0f, (chaosModulation + panicModulation) / 12.0f);

        for (int ch = 0; ch < numChannels; ++ch) {
            float* channelData = buffer.getWritePointer(ch);
            float inputSample = channelData[sample];

            // Write to delay buffer
            mDelayBuffer[ch][mWritePos] = inputSample;

            // Process grains
            float grainOutput = 0.0f;
            for (auto& grain : mGrains) {
                if (!grain.active) continue;

                // Calculate read position with pitch shift
                float readOffset = grain.phase * grain.grainSize;
                float readPos = static_cast<float>(mWritePos) - readOffset;
                if (readPos < 0) readPos += kMaxDelayLength;

                // Pitch shift by adjusting read increment
                // Higher pitch ratio = faster reading = higher pitch
                float readIncrement = totalPitchRatio;
                grain.readPos += readIncrement;

                // Get interpolated sample
                float adjustedReadPos = std::fmod(readPos + grain.readPos, static_cast<float>(kMaxDelayLength));
                float grainSample = hermiteInterpolate(mDelayBuffer[ch].data(), kMaxDelayLength, adjustedReadPos);

                // Apply window
                float window = hannWindow(grain.phase);
                grainOutput += grainSample * window;

                // Advance grain phase
                grain.phase += 1.0f / grain.grainSize;
                if (grain.phase >= 1.0f) {
                    grain.phase -= 1.0f;
                    grain.readPos = 0.0f;
                }
            }

            // Normalize by number of overlapping grains
            grainOutput /= (kNumGrains * 0.5f);

            // FIXED: Proper wet/dry mix - 100% wet when wetGain=1.0
            // When engaged, output is purely the pitch-shifted signal
            channelData[sample] = inputSample * (1.0f - wetGain) + grainOutput * wetGain;
        }

        mWritePos = (mWritePos + 1) % kMaxDelayLength;
    }
}
