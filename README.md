# Swarmness v2.1.0 "Tone Shaping Edition"

**Granular Pitch Shifter VST3 Plugin for Metalcore/Djent**

![Version](https://img.shields.io/badge/version-2.1.0-orange)
![JUCE](https://img.shields.io/badge/JUCE-8.0-blue)
![C++](https://img.shields.io/badge/C++-17-green)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

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
  - Windows: `Documents\Swarmness\Presets\`

---

## Building

### Requirements (All Platforms)

| Requirement | Version |
|------------|---------|
| CMake | 3.22+ |
| C++ Compiler | C++17 support |
| JUCE | 8.0 |

---

### 🪟 Windows Build Instructions

#### Prerequisites

1. **Visual Studio 2022** (recommended) or Visual Studio 2019
   - Download: https://visualstudio.microsoft.com/
   - During installation, select:
     - "Desktop development with C++"
     - Windows 10/11 SDK (latest version)

2. **CMake 3.22+**
   - Download: https://cmake.org/download/
   - Or install via winget: `winget install Kitware.CMake`

3. **JUCE 8.0**
   - Download: https://juce.com/download/
   - Extract to a known location, e.g., `C:\JUCE`

#### Build with Visual Studio (GUI)

```powershell
# Open PowerShell or CMD in the swarmness_plugin folder

# Generate Visual Studio solution
cmake -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_PATH="C:\JUCE"

# Open in Visual Studio
start build\Swarmness.sln
```

Then in Visual Studio:
1. Set build configuration to **Release**
2. Right-click on "Swarmness_VST3" → Build

#### Build with CMake (Command Line)

```powershell
cd swarmness_plugin

# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_PATH="C:\JUCE"

# Build Release
cmake --build build --config Release --target Swarmness_VST3

# Build Debug (for development)
cmake --build build --config Debug --target Swarmness_VST3
```

#### Using Environment Variable (Alternative)

```powershell
# Set JUCE_PATH environment variable
$env:JUCE_PATH = "C:\JUCE"

# Then build without -DJUCE_PATH
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

#### Output Location

After successful build, the VST3 plugin will be at:
```
build\Swarmness_artefacts\Release\VST3\Swarmness.vst3\
```

#### Installation on Windows

Copy the `Swarmness.vst3` folder to one of:
- User: `C:\Users\<YourName>\AppData\Local\Programs\Common Files\VST3\`
- System: `C:\Program Files\Common Files\VST3\`

Or use CMake install:
```powershell
cmake --install build --config Release
```

#### Troubleshooting Windows Build

| Issue | Solution |
|-------|----------|
| "JUCE_PATH not defined" | Add `-DJUCE_PATH="C:\path\to\JUCE"` |
| Generator not found | Ensure Visual Studio is installed with C++ workload |
| Access denied | Run PowerShell as Administrator for system install |
| Plugin not loading in DAW | Ensure x64 build matches 64-bit DAW |

---

### 🍎 macOS Build Instructions

#### Prerequisites
- Xcode 14+ with Command Line Tools
- CMake 3.22+
- JUCE 8.0

#### Build

```bash
cd swarmness_plugin
mkdir build && cd build

cmake .. -DJUCE_PATH=/path/to/JUCE \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

make -j$(sysctl -n hw.ncpu)
```

#### Output Location
```
build/Swarmness_artefacts/VST3/Swarmness.vst3
```

#### Installation
```bash
cp -R build/Swarmness_artefacts/VST3/Swarmness.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

---

### 🐧 Linux Build Instructions

#### Prerequisites
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake git libasound2-dev libfreetype6-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev \
    mesa-common-dev libfreeglut3-dev libcurl4-openssl-dev

# Fedora
sudo dnf install gcc-c++ cmake git alsa-lib-devel freetype-devel \
    libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel \
    libXcomposite-devel mesa-libGL-devel freeglut-devel libcurl-devel
```

#### Build
```bash
cd swarmness_plugin
mkdir build && cd build
cmake .. -DJUCE_PATH=/path/to/JUCE
make -j$(nproc)
```

#### Output Location
```
build/Swarmness_artefacts/VST3/Swarmness.vst3
```

#### Installation
```bash
cp -R build/Swarmness_artefacts/VST3/Swarmness.vst3 ~/.vst3/
```

---

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

## System Requirements

### Windows
- Windows 10 or later (64-bit)
- VST3-compatible DAW (Reaper, Cubase, FL Studio, etc.)

### macOS
- macOS 11.0 (Big Sur) or later
- Apple Silicon (M1/M2) or Intel x86_64
- VST3-compatible DAW

### Linux
- 64-bit Linux distribution
- ALSA or JACK audio
- VST3-compatible DAW (Reaper, Bitwig, Ardour)

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
- **Improved Windows compatibility**
- **Cross-platform CMake configuration**

### v2.0.0 (2026-01-15)
- Renamed to Swarmness
- Complete GUI redesign
- Rotary knob controls
- Footswitch with LED
