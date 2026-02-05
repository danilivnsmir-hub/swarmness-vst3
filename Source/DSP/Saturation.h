#pragma once
#include <JuceHeader.h>

class Saturation {
public:
    Saturation() = default;
    ~Saturation() = default;

    void prepare(double sampleRate);
    void reset();
    void setDrive(float drive);  // 0-1
    void setMix(float mix);      // 0-1
    void process(juce::AudioBuffer<float>& buffer);

private:
    double mSampleRate = 44100.0;
    float mDrive = 0.0f;
    float mMix = 1.0f;
    
    juce::SmoothedValue<float> mSmoothDrive{0.0f};
    juce::SmoothedValue<float> mSmoothMix{1.0f};
};
