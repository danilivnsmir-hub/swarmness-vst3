#pragma once
#include <JuceHeader.h>

class FlowEngine {
public:
    enum Mode { Static, Pulse };

    FlowEngine() = default;
    ~FlowEngine() = default;

    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();
    void setMode(Mode mode);
    void setStaticState(bool on);       // For footswitch
    void setPulseRate(float hz);        // 0.1-20 Hz
    void setPulseProbability(float prob);  // 0-1 (duty cycle)
    void setFlowAmount(float amount);   // 0-1 (depth of effect)
    bool isCurrentlyOn() const;         // For LED indicator
    float process();                    // Returns smoothed gain (0-1)

private:
    double mSampleRate = 44100.0;
    Mode mMode = Pulse;                 // Default to Pulse for obvious effect
    bool mStaticOn = true;
    float mPulseRate = 2.0f;
    float mPulseProbability = 0.5f;
    float mFlowAmount = 0.0f;           // Depth of gate effect

    float mLFOPhase = 0.0f;
    bool mCurrentState = true;
    bool mLastCrossing = false;

    juce::SmoothedValue<float> mSmoothGain{1.0f};
    juce::Random mRandom;
};
