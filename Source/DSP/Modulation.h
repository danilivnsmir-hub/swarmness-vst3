#pragma once
#include <JuceHeader.h>

class Modulation {
public:
    Modulation() = default;
    ~Modulation() = default;

    void prepare(double sampleRate);
    void reset();
    void setLFORate(float normalizedSpeed);  // 0-1, maps to 0.5-50Hz exponentially
    void setLFODepth(float depth);           // 0-1
    void setRandomAmount(float amount);      // 0-1
    float getNextModulationValue();

private:
    double mSampleRate = 44100.0;
    float mLFORate = 1.0f;
    float mLFODepth = 0.0f;
    float mRandomAmount = 0.0f;
    
    float mLFOPhase = 0.0f;
    float mSampleHoldValue = 0.0f;
    float mSampleHoldCounter = 0.0f;
    
    juce::Random mRandom;
};
