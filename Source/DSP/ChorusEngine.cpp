#include "ChorusEngine.h"
#include <cmath>

void ChorusEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    mSampleRate = spec.sampleRate;
    
    for (auto& ch : mDelayBuffer) {
        ch.resize(kMaxDelayLength, 0.0f);
    }

    mSmoothMix.reset(spec.sampleRate, 0.02);
    reset();
}

void ChorusEngine::reset() {
    for (auto& ch : mDelayBuffer) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
    mWritePos = 0;
    mLFOPhases = {0.0f, 0.33f, 0.66f};
}

void ChorusEngine::setRate(float hz) {
    mRate = juce::jlimit(0.1f, 5.0f, hz);
}

void ChorusEngine::setDepth(float depth) {
    mDepth = juce::jlimit(0.0f, 1.0f, depth);
}

void ChorusEngine::setMix(float mix) {
    mMix = juce::jlimit(0.0f, 1.0f, mix);
    mSmoothMix.setTargetValue(mMix);
}

void ChorusEngine::setFeedback(float fb) {
    mFeedback = juce::jlimit(0.0f, 0.9f, fb);
}

float ChorusEngine::hermiteInterpolate(const std::vector<float>& buffer, float pos) {
    int size = static_cast<int>(buffer.size());
    int x0 = static_cast<int>(pos);
    float frac = pos - x0;

    auto getSample = [&](int idx) {
        idx = ((idx % size) + size) % size;
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

void ChorusEngine::process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    const float baseDelay = 7.0f;  // ms
    const float modDepth = 3.0f;   // ms
    const float baseDelaySamples = baseDelay * 0.001f * static_cast<float>(mSampleRate);
    const float modDepthSamples = modDepth * 0.001f * static_cast<float>(mSampleRate) * mDepth;

    for (int sample = 0; sample < numSamples; ++sample) {
        float mix = mSmoothMix.getNextValue();
        
        if (mix < 0.001f) {
            mWritePos = (mWritePos + 1) % kMaxDelayLength;
            continue;
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            float* data = buffer.getWritePointer(ch);
            float inputSample = data[sample];

            // Write to delay buffer with feedback
            mDelayBuffer[ch][mWritePos] = inputSample;

            // Process 3 voices
            float chorusOut = 0.0f;
            for (int v = 0; v < kNumVoices; ++v) {
                // LFO modulation for each voice
                float lfoValue = std::sin(mLFOPhases[v] * juce::MathConstants<float>::twoPi);
                float delaySamples = baseDelaySamples + lfoValue * modDepthSamples;

                float readPos = static_cast<float>(mWritePos) - delaySamples;
                if (readPos < 0) readPos += kMaxDelayLength;

                float delaySample = hermiteInterpolate(mDelayBuffer[ch], readPos);
                chorusOut += delaySample;

                // Advance LFO phase
                if (ch == 0) {  // Only advance once per sample
                    mLFOPhases[v] += mRate / static_cast<float>(mSampleRate);
                    if (mLFOPhases[v] >= 1.0f) mLFOPhases[v] -= 1.0f;
                }
            }

            chorusOut /= kNumVoices;

            // Apply feedback
            mDelayBuffer[ch][mWritePos] += chorusOut * mFeedback;

            // Mix wet/dry
            data[sample] = inputSample * (1.0f - mix) + chorusOut * mix;
        }

        mWritePos = (mWritePos + 1) % kMaxDelayLength;
    }
}
