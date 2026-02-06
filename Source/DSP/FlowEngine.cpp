#include "FlowEngine.h"

void FlowEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    mSampleRate = spec.sampleRate;
    mSmoothGain.reset(spec.sampleRate, 0.002); // 2ms smoothing for snappy gates
    reset();
}

void FlowEngine::reset() {
    mLFOPhase = 0.0f;
    mCurrentState = true;
    mLastCrossing = false;
    mSmoothGain.setCurrentAndTargetValue(1.0f);
}

void FlowEngine::setMode(Mode mode) {
    mMode = mode;
    if (mode == Static) {
        mSmoothGain.setTargetValue(mStaticOn ? 1.0f : 0.0f);
    }
}

void FlowEngine::setStaticState(bool on) {
    mStaticOn = on;
    if (mMode == Static) {
        mSmoothGain.setTargetValue(on ? 1.0f : 0.0f);
    }
}

void FlowEngine::setPulseRate(float hz) {
    mPulseRate = juce::jlimit(0.1f, 20.0f, hz);  // Extended range for faster stutters
}

void FlowEngine::setPulseProbability(float prob) {
    mPulseProbability = juce::jlimit(0.0f, 1.0f, prob);
}

void FlowEngine::setFlowAmount(float amount) {
    mFlowAmount = juce::jlimit(0.0f, 1.0f, amount);
}

bool FlowEngine::isCurrentlyOn() const {
    return mCurrentState;
}

float FlowEngine::process() {
    // If flow amount is 0, no gating effect
    if (mFlowAmount < 0.01f) {
        return 1.0f;
    }
    
    if (mMode == Static) {
        mCurrentState = mStaticOn;
        return mSmoothGain.getNextValue();
    }

    // Pulse mode - LFO-driven rhythmic gate
    float phaseIncrement = mPulseRate / static_cast<float>(mSampleRate);
    mLFOPhase += phaseIncrement;
    if (mLFOPhase >= 1.0f) mLFOPhase -= 1.0f;

    // Gate pattern based on probability
    // Higher probability = more "on" time
    // At 0.5 probability, it's 50/50 duty cycle
    bool shouldBeOn;
    
    if (mPulseProbability > 0.9f) {
        // Almost always on (tremolo-like)
        shouldBeOn = mLFOPhase < 0.9f;
    } else if (mPulseProbability < 0.1f) {
        // Mostly off (choppy stutter)
        shouldBeOn = mLFOPhase < 0.1f;
    } else {
        // Variable duty cycle based on probability
        shouldBeOn = mLFOPhase < mPulseProbability;
    }

    // Detect state changes for smooth transitions
    if (shouldBeOn != mCurrentState) {
        mCurrentState = shouldBeOn;
        mSmoothGain.setTargetValue(shouldBeOn ? 1.0f : 0.0f);
    }

    // Apply flow amount to control the depth of gating
    float gateValue = mSmoothGain.getNextValue();
    // Mix between full signal (1.0) and gated signal based on flowAmount
    return 1.0f - mFlowAmount * (1.0f - gateValue);
}
