#include "Saturation.h"
#include <cmath>

void Saturation::prepare(double sampleRate) {
    mSampleRate = sampleRate;
    mSmoothDrive.reset(sampleRate, 0.02);
    mSmoothMix.reset(sampleRate, 0.02);
    reset();
}

void Saturation::reset() {
    mSmoothDrive.setCurrentAndTargetValue(mDrive);
    mSmoothMix.setCurrentAndTargetValue(mMix);
}

void Saturation::setDrive(float drive) {
    mDrive = juce::jlimit(0.0f, 1.0f, drive);
    mSmoothDrive.setTargetValue(mDrive);
}

void Saturation::setMix(float mix) {
    mMix = juce::jlimit(0.0f, 1.0f, mix);
    mSmoothMix.setTargetValue(mMix);
}

void Saturation::process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample) {
        float drive = mSmoothDrive.getNextValue();
        float mix = mSmoothMix.getNextValue();

        if (drive < 0.001f) continue;

        // Gain factor from drive (1.0 to 10.0)
        float gain = 1.0f + drive * 9.0f;

        for (int ch = 0; ch < numChannels; ++ch) {
            float* data = buffer.getWritePointer(ch);
            float dry = data[sample];
            
            // Apply gain and soft clip with tanh
            float wet = std::tanh(dry * gain) / std::tanh(gain);
            
            // Asymmetric saturation for tube-like character
            if (wet > 0.0f) {
                wet = wet * (1.0f + drive * 0.2f * wet);
                wet = std::tanh(wet);
            }
            
            // Mix
            data[sample] = dry * (1.0f - mix) + wet * mix;
        }
    }
}
