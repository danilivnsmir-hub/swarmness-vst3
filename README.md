# Swarmness v2.1.0 "Tone Shaping Edition"

**Granular Pitch Shifter VST3 Plugin for Metalcore/Djent**

![Version](https://img.shields.io/badge/version-2.1.0-orange)
![JUCE](https://img.shields.io/badge/JUCE-8.0-blue)
![C++](https://img.shields.io/badge/C++-17-green)

## Overview

Swarmness is a professional-grade granular pitch shifter plugin designed for metalcore, djent, ambient, and experimental music production. It adds a "swarm" of harmonics on top of your original signal with extensive modulation and tone shaping capabilities.

## Features

### 🎸 VOLTAGE Section (Pitch Control)
- **Octave Mode**: +1 OCT, +2 OCT, -1 OCT
- **Engage**: Enable/disable pitch shifting
- **Rise**: Attack time for pitch effect (0-2000ms)

#### Slide (Digitech Ricochet-style)
- **Range**: -24 to +24 semitones
- **Time**: 50-5000ms glide time
- **Direction**: Up, Down, Both
- **Auto Slide**: Continuous oscillation mode
- **Position**: Manual slide control
- **Return**: Return to original pitch

#### Random Pitch
- **Range**: 0-24 semitones
- **Rate**: 0.1-10 Hz
- **Smooth**: Transition smoothing
- **Mode**: Jump or Glide

### 🌀 MODULATION Section
- **Panic**: Rapid pitch fluctuation intensity
- **Chaos**: Random modulation amount
- **Speed**: LFO rate (0.5-50 Hz exponential)

### 🎛️ TONE SHAPING Section
- **Low Cut**: Analog HPF (20-500 Hz)
- **High Cut**: Analog LPF (1k-20k Hz) with tape saturation
- **Saturation**: Tube-style soft clipping

#### Chorus
- **Rate**: 0.1-5 Hz
- **Depth**: Modulation depth
- **Mix**: Wet/dry chorus mix

### 📤 OUTPUT Section
- **Mix**: Overall wet/dry (0-100%)
- **Output Gain**: -24 to +6 dB

### 🔄 FLOW Section (Bypass Control)
- **Mode**: Static or Pulse
- **Rate**: Pulse LFO rate (0.1-10 Hz)
- **Probability**: Random trigger probability
- **Footswitch**: Manual on/off with LED indicator

## Factory Presets

1. **Init** - Default initialization
2. **Djent Classic** - Tight octave for djent
3. **Metalcore Mayhem** - Wide stereo aggressive
4. **Ricochet Up** - Auto slide up effect
5. **Glitch Apocalypse** - Maximum chaos

## Preset System

- Format: `.swpreset` (JSON)
- Save/Load/Export/Import functionality
- Presets stored in:
  - macOS: `~/Library/Audio/Presets/Swarmness/`
  - Linux: `~/.swarmness/presets/`
  - Windows: `Documents/Swarmness/Presets/`

## Building

### Requirements
- CMake 3.22+
- C++17 compiler
- JUCE 8.0

### Linux
```bash
cd swarmness_plugin
mkdir build && cd build
cmake .. -DJUCE_PATH=/path/to/JUCE
make -j$(nproc)
```

### macOS
```bash
cd swarmness_plugin
mkdir build && cd build
cmake .. -DJUCE_PATH=/path/to/JUCE \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
make -j$(sysctl -n hw.ncpu)
```

## DSP Signal Chain

```
Input → Dry Copy
      ↓
1. GranularPitchShifter (with modulation)
      ↓
2. AnalogFilterEngine (HPF + LPF)
      ↓
3. Saturation
      ↓
4. ChorusEngine
      ↓
5. DCBlocker
      ↓
6. Wet/Dry Mix
      ↓
7. FlowEngine Gate
      ↓
8. Output Gain
      ↓
Output
```

## Project Structure

```
swarmness_plugin/
├── CMakeLists.txt
├── README.md
└── Source/
    ├── PluginProcessor.cpp/h
    ├── PluginEditor.cpp/h
    ├── DSP/
    │   ├── GranularPitchShifter.cpp/h
    │   ├── PitchSlideEngine.cpp/h
    │   ├── PitchRandomizer.cpp/h
    │   ├── Modulation.cpp/h
    │   ├── AnalogFilterEngine.h
    │   ├── ChorusEngine.cpp/h
    │   ├── FlowEngine.cpp/h
    │   ├── DCBlocker.cpp/h
    │   └── Saturation.cpp/h
    ├── GUI/
    │   ├── MetalLookAndFeel.cpp/h
    │   ├── RotaryKnob.cpp/h
    │   ├── FootswitchButton.cpp/h
    │   └── PresetPanel.cpp/h
    └── Preset/
        └── PresetManager.cpp/h
```

## Technical Specifications

| Parameter | Value |
|-----------|-------|
| Plugin Format | VST3 |
| Sample Rates | 44.1k, 48k, 88.2k, 96k, 176.4k, 192k |
| Bit Depth | 32-bit float |
| Latency | ~46ms (granular buffer) |
| Bundle ID | com.OpenAudio.Swarmness |
| Manufacturer Code | OpAu |
| Plugin Code | SwMs |

## License

MIT License - © 2026 OpenAudio

## Changelog

### v2.1.0 (2026-02-05)
- New TONE SHAPING section
- AnalogFilterEngine with HPF/LPF
- ChorusEngine (Classic mode)
- Saturation effect
- Dark metal GUI theme
- 950x750px window size

### v2.0.0 (2026-01-15)
- Renamed to Swarmness
- Complete GUI redesign
- Rotary knob controls
- Footswitch with LED
