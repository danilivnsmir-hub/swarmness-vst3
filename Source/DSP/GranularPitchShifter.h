#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

/**
 * GranularPitchShifter - Based on original Noise Glitch algorithm
 * Uses 2 overlapping grains with Hann windowing for smooth pitch shifting.
 * Supports -2, -1, 0, +1, +2 octave shifts with smooth glide.
 */
class GranularPitchShifter
{
public:
    GranularPitchShifter() = default;
    
    void prepare(double sampleRate, int maxBlockSize)
    {
        this->sampleRate = sampleRate;
        
        // Buffer size: ~100ms worth of samples
        bufferSize = static_cast<int>(sampleRate * 0.1);
        delayBufferL.resize(static_cast<size_t>(bufferSize), 0.0f);
        delayBufferR.resize(static_cast<size_t>(bufferSize), 0.0f);
        
        // Grain size: ~15ms for smooth pitch shifting
        grainSize = static_cast<int>(sampleRate * 0.015);
        
        // Initialize grain positions
        writePos = 0;
        readPos1 = 0.0;
        readPos2 = static_cast<double>(grainSize / 2);  // 50% overlap
        
        // Create Hann window for crossfading
        window.resize(static_cast<size_t>(grainSize));
        for (int i = 0; i < grainSize; ++i)
        {
            window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / grainSize));
        }
        
        grainCounter1 = 0;
        grainCounter2 = grainSize / 2;
        
        currentPitchRatio = 1.0;
        targetPitchRatio = 1.0;
        
        // Glide smoothing coefficient (default 50ms rise time)
        updateGlideCoeff(50.0);
        
        // Wet gain for engage/disengage
        wetGain.reset(sampleRate, 0.02);  // 20ms smoothing
        wetGain.setCurrentAndTargetValue(1.0f);
    }
    
    void updateGlideCoeff(double riseTimeMs)
    {
        if (sampleRate > 0)
        {
            double riseSamples = (riseTimeMs * sampleRate) / 1000.0;
            if (riseSamples < 1.0) riseSamples = 1.0;
            glideCoeff = 1.0 - std::exp(-1.0 / riseSamples);
        }
    }
    
    void setOctaveMode(int mode)
    {
        // mode: 0=-2oct, 1=-1oct, 2=0oct, 3=+1oct, 4=+2oct
        switch (mode)
        {
            case 0: targetPitchRatio = 0.25; break;  // -2 octaves
            case 1: targetPitchRatio = 0.5; break;   // -1 octave
            case 2: targetPitchRatio = 1.0; break;   // 0 (no shift)
            case 3: targetPitchRatio = 2.0; break;   // +1 octave
            case 4: targetPitchRatio = 4.0; break;   // +2 octaves
            default: targetPitchRatio = 1.0;
        }
    }
    
    void setEngage(bool engaged)
    {
        isEngaged = engaged;
        wetGain.setTargetValue(engaged ? 1.0f : 0.0f);
    }
    
    void setRiseTime(float ms)
    {
        updateGlideCoeff(ms);
    }
    
    // Modulation input: adds detuning in semitones
    void setModulation(double modSemitones)
    {
        modulationOffset = modSemitones;
    }
    
    void processStereo(float* leftChannel, float* rightChannel, int numSamples)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float wet = wetGain.getNextValue();
            
            // Smooth pitch ratio changes (glide/portamento)
            currentPitchRatio += glideCoeff * (targetPitchRatio - currentPitchRatio);
            
            // Apply modulation offset to pitch ratio
            double modulatedRatio = currentPitchRatio * std::pow(2.0, modulationOffset / 12.0);
            
            // Process left channel
            float inputL = leftChannel[sample];
            delayBufferL[static_cast<size_t>(writePos)] = inputL;
            
            float grain1L = readGrain(delayBufferL, readPos1, grainCounter1);
            float grain2L = readGrain(delayBufferL, readPos2, grainCounter2);
            float wetL = grain1L + grain2L;
            
            // Process right channel
            float inputR = rightChannel[sample];
            delayBufferR[static_cast<size_t>(writePos)] = inputR;
            
            float grain1R = readGrain(delayBufferR, readPos1, grainCounter1);
            float grain2R = readGrain(delayBufferR, readPos2, grainCounter2);
            float wetR = grain1R + grain2R;
            
            // Update read positions based on pitch ratio
            double readIncrement = modulatedRatio;
            
            readPos1 += readIncrement;
            readPos2 += readIncrement;
            
            // Wrap read positions
            if (readPos1 >= bufferSize) readPos1 -= bufferSize;
            if (readPos2 >= bufferSize) readPos2 -= bufferSize;
            if (readPos1 < 0) readPos1 += bufferSize;
            if (readPos2 < 0) readPos2 += bufferSize;
            
            // Update grain counters
            grainCounter1++;
            grainCounter2++;
            
            // Reset grains when they complete - resync to avoid drift
            if (grainCounter1 >= grainSize)
            {
                grainCounter1 = 0;
                readPos1 = static_cast<double>(writePos) - grainSize * 2;
                if (readPos1 < 0) readPos1 += bufferSize;
            }
            
            if (grainCounter2 >= grainSize)
            {
                grainCounter2 = 0;
                readPos2 = static_cast<double>(writePos) - grainSize * 2;
                if (readPos2 < 0) readPos2 += bufferSize;
            }
            
            writePos = (writePos + 1) % bufferSize;
            
            // Mix wet/dry based on engage state
            leftChannel[sample] = inputL * (1.0f - wet) + wetL * wet;
            rightChannel[sample] = inputR * (1.0f - wet) + wetR * wet;
        }
    }
    
    void reset()
    {
        std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
        std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
        writePos = 0;
        readPos1 = 0.0;
        readPos2 = static_cast<double>(grainSize / 2);
        grainCounter1 = 0;
        grainCounter2 = grainSize / 2;
        currentPitchRatio = 1.0;
    }
    
private:
    float readGrain(std::vector<float>& buffer, double pos, int grainPhase)
    {
        // Linear interpolation read from buffer
        int intPos = static_cast<int>(pos);
        double frac = pos - intPos;
        
        int idx0 = intPos % bufferSize;
        int idx1 = (intPos + 1) % bufferSize;
        if (idx0 < 0) idx0 += bufferSize;
        if (idx1 < 0) idx1 += bufferSize;
        
        float sample = static_cast<float>(
            buffer[static_cast<size_t>(idx0)] * (1.0 - frac) +
            buffer[static_cast<size_t>(idx1)] * frac
        );
        
        // Apply window
        if (grainPhase >= 0 && grainPhase < grainSize)
        {
            sample *= window[static_cast<size_t>(grainPhase)];
        }
        else
        {
            sample = 0.0f;
        }
        
        return sample;
    }
    
    double sampleRate = 44100.0;
    int bufferSize = 4410;
    int grainSize = 661;
    
    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    std::vector<float> window;
    
    int writePos = 0;
    double readPos1 = 0.0;
    double readPos2 = 0.0;
    
    int grainCounter1 = 0;
    int grainCounter2 = 0;
    
    double currentPitchRatio = 1.0;
    double targetPitchRatio = 1.0;
    double glideCoeff = 0.001;
    
    double modulationOffset = 0.0;  // In semitones
    bool isEngaged = true;
    
    juce::SmoothedValue<float> wetGain{1.0f};
};
