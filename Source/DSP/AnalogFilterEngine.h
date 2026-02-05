#pragma once
#include <JuceHeader.h>

class AnalogFilterEngine {
public:
    AnalogFilterEngine() = default;
    ~AnalogFilterEngine() = default;

    void prepare(const juce::dsp::ProcessSpec& spec) {
        mSampleRate = spec.sampleRate;
        
        mHighPass.prepare(spec);
        mHighPass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        mHighPass.setCutoffFrequency(mLowCutFreq);
        mHighPass.setResonance(0.707f);

        mLowPass.prepare(spec);
        mLowPass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        mLowPass.setCutoffFrequency(mHighCutFreq);
        mLowPass.setResonance(0.707f);

        reset();
    }

    void reset() {
        mHighPass.reset();
        mLowPass.reset();
    }

    void setLowCut(float hz) {
        mLowCutFreq = juce::jlimit(20.0f, 500.0f, hz);
        mHighPass.setCutoffFrequency(mLowCutFreq);
    }

    void setHighCut(float hz) {
        mHighCutFreq = juce::jlimit(1000.0f, 20000.0f, hz);
        mLowPass.setCutoffFrequency(mHighCutFreq);
    }

    void setTapeSaturation(bool enabled) {
        mTapeSaturation = enabled;
    }

    void process(juce::AudioBuffer<float>& buffer) {
        auto block = juce::dsp::AudioBlock<float>(buffer);
        auto context = juce::dsp::ProcessContextReplacing<float>(block);
        
        mHighPass.process(context);
        mLowPass.process(context);

        // Apply subtle tape saturation on high frequencies if enabled
        if (mTapeSaturation) {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                auto* data = buffer.getWritePointer(ch);
                for (int i = 0; i < buffer.getNumSamples(); ++i) {
                    // Soft saturation using tanh
                    data[i] = std::tanh(data[i] * 1.2f) / 1.1f;
                }
            }
        }
    }

private:
    double mSampleRate = 44100.0;
    float mLowCutFreq = 20.0f;
    float mHighCutFreq = 20000.0f;
    bool mTapeSaturation = false;

    juce::dsp::StateVariableTPTFilter<float> mHighPass;
    juce::dsp::StateVariableTPTFilter<float> mLowPass;
};
