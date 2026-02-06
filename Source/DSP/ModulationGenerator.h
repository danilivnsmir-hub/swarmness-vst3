#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <random>

/**
 * ModulationGenerator - Original Noise Glitch algorithm
 * Generates modulation signals for Panic, Chaos, and Speed effects.
 * - Panic: slow random pitch drift (0.5-2.5 Hz), up to ±12 semitones
 * - Chaos: fast random pitch jumps (5-35 Hz), up to ±24 semitones
 * - Speed: high-frequency FM modulation (20-320 Hz)
 */
class ModulationGenerator
{
public:
    ModulationGenerator() : rng(std::random_device{}()), dist(-1.0f, 1.0f) {}
    
    void prepare(double sampleRate)
    {
        this->sampleRate = sampleRate;
        
        panicPhase = 0.0;
        chaosPhase = 0.0;
        speedPhase = 0.0;
        
        smoothedRandom = 0.0f;
        chaosTarget = 0.0f;
        chaosSampleCounter = 0;
        
        panicFreq = 1.0;
        chaosFreq = 10.0;
        chaosSamplesPerJump = static_cast<int>(sampleRate / chaosFreq);
        speedFreq = 50.0;
    }
    
    void setParams(float panicAmount, float chaosAmount, float speedAmount)
    {
        // Parameters are 0-1 normalized
        this->panicAmount = panicAmount;
        this->chaosAmount = chaosAmount;
        this->speedAmount = speedAmount;
        
        // Adjust frequencies based on amounts
        panicFreq = 0.5 + panicAmount * 2.0;    // 0.5-2.5 Hz
        chaosFreq = 5.0 + chaosAmount * 30.0;   // 5-35 Hz
        speedFreq = 20.0 + speedAmount * 300.0; // 20-320 Hz
        
        if (sampleRate > 0)
            chaosSamplesPerJump = std::max(1, static_cast<int>(sampleRate / chaosFreq));
    }
    
    /**
     * Returns combined pitch modulation in semitones.
     * Panic: slow detuning (up to ±12 semitones)
     * Chaos: random jumps (up to ±24 semitones)
     */
    float getPitchModulation()
    {
        float result = 0.0f;
        
        // === PANIC: Slow smooth random pitch drift ===
        if (panicAmount > 0.001f)
        {
            panicPhase += panicFreq / sampleRate;
            if (panicPhase >= 1.0)
            {
                panicPhase -= 1.0;
                panicTarget = dist(rng);
            }
            
            float smoothCoeff = static_cast<float>(1.0 - std::exp(-panicFreq * 2.0 / sampleRate));
            smoothedRandom += smoothCoeff * (panicTarget - smoothedRandom);
            
            // Up to ±12 semitones at 100% panic
            result += smoothedRandom * panicAmount * 12.0f;
        }
        
        // === CHAOS: Fast random pitch jumps ===
        if (chaosAmount > 0.001f)
        {
            chaosSampleCounter++;
            if (chaosSampleCounter >= chaosSamplesPerJump)
            {
                chaosSampleCounter = 0;
                chaosTarget = dist(rng);
            }
            
            float chaosSmooth = static_cast<float>(1.0 - std::exp(-chaosFreq * 4.0 / sampleRate));
            currentChaos += chaosSmooth * (chaosTarget - currentChaos);
            
            // Up to ±24 semitones at 100% chaos
            result += currentChaos * chaosAmount * 24.0f;
        }
        
        return result;
    }
    
    /**
     * Returns FM modulation factor for Speed parameter.
     * High-frequency oscillation applied to the signal.
     * Returns value in range -1 to +1.
     */
    float getFMModulation()
    {
        if (speedAmount < 0.001f)
            return 0.0f;
        
        speedPhase += speedFreq / sampleRate;
        if (speedPhase >= 1.0)
            speedPhase -= 1.0;
        
        float fm = std::sin(static_cast<float>(speedPhase * 2.0 * juce::MathConstants<double>::pi));
        
        // Add harmonics for "shrill" character
        float fm2 = std::sin(static_cast<float>(speedPhase * 4.0 * juce::MathConstants<double>::pi)) * 0.3f;
        float fm3 = std::sin(static_cast<float>(speedPhase * 6.0 * juce::MathConstants<double>::pi)) * 0.15f;
        
        return (fm + fm2 + fm3) * speedAmount;
    }
    
    void reset()
    {
        panicPhase = 0.0;
        chaosPhase = 0.0;
        speedPhase = 0.0;
        smoothedRandom = 0.0f;
        chaosTarget = 0.0f;
        currentChaos = 0.0f;
        panicTarget = 0.0f;
        chaosSampleCounter = 0;
    }
    
private:
    double sampleRate = 44100.0;
    
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist;
    
    float panicAmount = 0.0f;
    float chaosAmount = 0.0f;
    float speedAmount = 0.0f;
    
    double panicPhase = 0.0;
    double panicFreq = 1.0;
    float smoothedRandom = 0.0f;
    float panicTarget = 0.0f;
    
    double chaosPhase = 0.0;
    double chaosFreq = 10.0;
    int chaosSamplesPerJump = 4410;
    int chaosSampleCounter = 0;
    float chaosTarget = 0.0f;
    float currentChaos = 0.0f;
    
    double speedPhase = 0.0;
    double speedFreq = 50.0;
};


/**
 * RingModulator for "Speed" effect
 * Creates aggressive FM/ring mod sounds
 */
class RingModulator
{
public:
    void prepare(double sampleRate)
    {
        this->sampleRate = sampleRate;
        phase = 0.0;
    }
    
    void setFrequency(float freqHz)
    {
        frequency = freqHz;
    }
    
    void setAmount(float amt)
    {
        // amt is 0-1 normalized
        amount = amt;
    }
    
    float processSample(float input)
    {
        if (amount < 0.001f)
            return input;
        
        phase += frequency / sampleRate;
        if (phase >= 1.0)
            phase -= 1.0;
        
        float modulator = std::sin(static_cast<float>(phase * 2.0 * juce::MathConstants<double>::pi));
        
        // Mix dry and ring-modulated signal
        float wet = input * modulator;
        return input * (1.0f - amount) + wet * amount;
    }
    
    void reset()
    {
        phase = 0.0;
    }
    
private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    double frequency = 100.0;
    float amount = 0.0f;
};
