#include "DCBlocker.h"
#include <cmath>

void DCBlocker::prepare(double sampleRate) {
    mSampleRate = sampleRate;
    // One-pole highpass at ~5Hz
    float fc = 5.0f / static_cast<float>(sampleRate);
    mCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc);
    reset();
}

void DCBlocker::reset() {
    mX1.fill(0.0f);
    mY1.fill(0.0f);
}

void DCBlocker::process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        float x1 = mX1[ch];
        float y1 = mY1[ch];
        
        // DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1], where R ≈ 0.995
        const float R = 0.995f;
        
        for (int i = 0; i < numSamples; ++i) {
            float x = data[i];
            float y = x - x1 + R * y1;
            data[i] = y;
            x1 = x;
            y1 = y;
        }
        
        mX1[ch] = x1;
        mY1[ch] = y1;
    }
}
