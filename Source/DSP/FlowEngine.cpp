#include "FlowEngine.h"

void FlowEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    mSampleRate = spec.sampleRate;
    mSmoothGain.reset(spec.sampleRate, 0.005); // 5ms for click-free transitions
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
    mPulseRate = juce::jlimit(0.1f, 10.0f, hz);
}

void FlowEngine::setPulseProbability(float prob) {
    mPulseProbability = juce::jlimit(0.0f, 1.0f, prob);
}

bool FlowEngine::isCurrentlyOn() const {
    return mCurrentState;
}

float FlowEngine::process() {
    if (mMode == Static) {
        mCurrentState = mStaticOn;
        return mSmoothGain.getNextValue();
    }

    // Pulse mode - LFO-driven with probability check at zero-crossings
    float phaseIncrement = mPulseRate / static_cast<float>(mSampleRate);
    mLFOPhase += phaseIncrement;
    if (mLFOPhase >= 1.0f) mLFOPhase -= 1.0f;

    // Detect zero crossing (at 0.0 and 0.5 in phase)
    bool atCrossing = (mLFOPhase < phaseIncrement) || 
                      (std::abs(mLFOPhase - 0.5f) < phaseIncrement);

    if (atCrossing && !mLastCrossing) {
        // Check probability at each zero crossing
        if (mRandom.nextFloat() < mPulseProbability) {
            mCurrentState = !mCurrentState;
        }
    }
    mLastCrossing = atCrossing;

    mSmoothGain.setTargetValue(mCurrentState ? 1.0f : 0.0f);
    return mSmoothGain.getNextValue();
}
