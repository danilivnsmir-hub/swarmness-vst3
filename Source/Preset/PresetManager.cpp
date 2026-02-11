#include "PresetManager.h"

const juce::String PresetManager::kPresetExtension = ".swpreset";
const juce::String PresetManager::kPresetVersion = "1.2.7";

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts)
    : mAPVTS(apvts)
{
    initializeFactoryPresets();
    // Defer listener registration to avoid issues during construction
    // saveSnapshot() called after listeners are registered
}

void PresetManager::initializeDirtyTracking() {
    // Called after processor is fully constructed
    if (!mListenersRegistered) {
        registerParameterListeners();
        saveSnapshot();
        mListenersRegistered = true;
    }
}

PresetManager::~PresetManager() {
    if (mListenersRegistered) {
        unregisterParameterListeners();
    }
}

void PresetManager::registerParameterListeners() {
    for (auto* param : mAPVTS.processor.getParameters()) {
        if (auto* paramWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            mAPVTS.addParameterListener(paramWithID->getParameterID(), this);
        }
    }
}

void PresetManager::unregisterParameterListeners() {
    for (auto* param : mAPVTS.processor.getParameters()) {
        if (auto* paramWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            mAPVTS.removeParameterListener(paramWithID->getParameterID(), this);
        }
    }
}

void PresetManager::parameterChanged(const juce::String& parameterID, float newValue) {
    // Safety check - don't process if not fully initialized
    if (!mListenersRegistered) return;
    
    // Check if value differs from snapshot
    auto it = mSnapshot.find(parameterID);
    if (it != mSnapshot.end()) {
        if (std::abs(it->second - newValue) > 0.0001f) {
            markDirty();
        }
    } else {
        markDirty();
    }
}

void PresetManager::markDirty() {
    mIsDirty = true;
}

void PresetManager::markClean() {
    mIsDirty = false;
}

void PresetManager::saveSnapshot() {
    mSnapshot.clear();
    for (auto* param : mAPVTS.processor.getParameters()) {
        if (auto* paramWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            mSnapshot[paramWithID->getParameterID()] = param->getValue();
        }
    }
}

bool PresetManager::hasChangedFromSnapshot() const {
    for (auto* param : mAPVTS.processor.getParameters()) {
        if (auto* paramWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            auto it = mSnapshot.find(paramWithID->getParameterID());
            if (it != mSnapshot.end()) {
                if (std::abs(it->second - param->getValue()) > 0.0001f) {
                    return true;
                }
            }
        }
    }
    return false;
}

juce::File PresetManager::getPresetsDirectory() const {
    #if JUCE_MAC
        auto presetDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Presets/Swarmness");
    #elif JUCE_WINDOWS
        auto presetDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("Swarmness/Presets");
    #else
        auto presetDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile(".swarmness/presets");
    #endif
    
    if (!presetDir.exists())
        presetDir.createDirectory();
    
    return presetDir;
}

juce::var PresetManager::createPresetObject(const juce::String& name) const {
    auto preset = std::make_unique<juce::DynamicObject>();
    preset->setProperty("name", name);
    preset->setProperty("author", "User");
    preset->setProperty("description", "");
    preset->setProperty("version", kPresetVersion);
    preset->setProperty("category", "User");
    
    auto params = std::make_unique<juce::DynamicObject>();
    for (auto* param : mAPVTS.processor.getParameters()) {
        if (auto* paramWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            params->setProperty(paramWithID->getParameterID(), param->getValue());
        }
    }
    preset->setProperty("parameters", params.release());
    
    return juce::var(preset.release());
}

void PresetManager::applyPresetFromVar(const juce::var& presetData) {
    if (auto* params = presetData["parameters"].getDynamicObject()) {
        for (const auto& prop : params->getProperties()) {
            if (auto* param = mAPVTS.getParameter(prop.name.toString())) {
                param->setValueNotifyingHost(static_cast<float>(prop.value));
            }
        }
    }
}

void PresetManager::savePreset(const juce::String& name) {
    auto presetFile = getPresetsDirectory().getChildFile(name + kPresetExtension);
    auto presetData = createPresetObject(name);
    
    juce::String jsonString = juce::JSON::toString(presetData, true);
    presetFile.replaceWithText(jsonString);
    mCurrentPresetName = name;
    saveSnapshot();
    markClean();
}

void PresetManager::loadPreset(const juce::String& name) {
    // Check factory presets first
    if (mFactoryPresets.count(name) > 0) {
        loadFactoryPreset(name);
        return;
    }
    
    auto presetFile = getPresetsDirectory().getChildFile(name + kPresetExtension);
    loadPresetFromFile(presetFile);
}

void PresetManager::loadPresetFromFile(const juce::File& file) {
    if (!file.existsAsFile()) return;
    
    auto jsonString = file.loadFileAsString();
    auto presetData = juce::JSON::parse(jsonString);
    
    if (presetData.isObject()) {
        applyPresetFromVar(presetData);
        mCurrentPresetName = presetData["name"].toString();
        saveSnapshot();
        markClean();
    }
}

bool PresetManager::exportPreset(const juce::File& file) {
    auto presetData = createPresetObject(file.getFileNameWithoutExtension());
    juce::String jsonString = juce::JSON::toString(presetData, true);
    return file.replaceWithText(jsonString);
}

bool PresetManager::importPreset(const juce::File& file) {
    if (!file.existsAsFile()) return false;
    
    auto jsonString = file.loadFileAsString();
    auto presetData = juce::JSON::parse(jsonString);
    
    if (presetData.isObject()) {
        applyPresetFromVar(presetData);
        mCurrentPresetName = presetData["name"].toString();
        return true;
    }
    return false;
}

void PresetManager::deletePreset(const juce::String& name) {
    auto presetFile = getPresetsDirectory().getChildFile(name + kPresetExtension);
    if (presetFile.existsAsFile())
        presetFile.deleteFile();
    
    // v1.2.7: Switch to another preset after deletion
    auto presets = getPresetList();
    if (!presets.isEmpty()) {
        // Find a different preset to load
        juce::String nextPreset;
        for (const auto& preset : presets) {
            if (preset != name) {
                nextPreset = preset;
                break;
            }
        }
        if (nextPreset.isNotEmpty()) {
            loadPreset(nextPreset);
        } else if (presets.size() > 0) {
            loadPreset(presets[0]);
        }
    }
}

// v1.2.7: Save current preset (overwrite if user preset)
void PresetManager::saveCurrentPreset() {
    if (!isFactoryPreset(mCurrentPresetName) && mCurrentPresetName.isNotEmpty()) {
        savePreset(mCurrentPresetName);
    }
}

// v1.2.7: Save as new preset
void PresetManager::savePresetAs(const juce::String& name) {
    savePreset(name);
}

// v1.2.7: Get display name with * prefix if dirty
juce::String PresetManager::getDisplayName() const {
    if (mIsDirty) {
        return "* " + mCurrentPresetName;
    }
    return mCurrentPresetName;
}

// v1.2.7: Get current preset index in list
int PresetManager::getCurrentPresetIndex() const {
    auto presets = getPresetList();
    return presets.indexOf(mCurrentPresetName);
}

// v1.2.7: Load previous preset
void PresetManager::loadPreviousPreset() {
    auto presets = getPresetList();
    if (presets.isEmpty()) return;
    
    int currentIndex = presets.indexOf(mCurrentPresetName);
    int newIndex = (currentIndex <= 0) ? presets.size() - 1 : currentIndex - 1;
    loadPreset(presets[newIndex]);
}

// v1.2.7: Load next preset
void PresetManager::loadNextPreset() {
    auto presets = getPresetList();
    if (presets.isEmpty()) return;
    
    int currentIndex = presets.indexOf(mCurrentPresetName);
    int newIndex = (currentIndex >= presets.size() - 1) ? 0 : currentIndex + 1;
    loadPreset(presets[newIndex]);
}

juce::StringArray PresetManager::getPresetList() const {
    juce::StringArray presets;
    
    // Add factory presets first
    for (const auto& [name, _] : mFactoryPresets) {
        presets.add(name);
    }
    
    // Add user presets
    auto files = getPresetsDirectory().findChildFiles(juce::File::findFiles, false, "*" + kPresetExtension);
    for (const auto& file : files) {
        auto name = file.getFileNameWithoutExtension();
        if (!presets.contains(name))
            presets.add(name);
    }
    
    return presets;
}

juce::StringArray PresetManager::getFactoryPresetNames() const {
    juce::StringArray names;
    for (const auto& [name, _] : mFactoryPresets) {
        names.add(name);
    }
    return names;
}

void PresetManager::loadFactoryPreset(const juce::String& name) {
    if (mFactoryPresets.count(name) > 0) {
        applyPresetFromVar(mFactoryPresets[name]);
        mCurrentPresetName = name;
        saveSnapshot();
        markClean();
    }
}

bool PresetManager::isFactoryPreset(const juce::String& name) const {
    return mFactoryPresets.count(name) > 0;
}

void PresetManager::initializeFactoryPresets() {
    // Helper lambda to create a preset
    // v3.0.0: octaveMode now has 5 options: -2, -1, 0, +1, +2
    // Index mapping: 0=-2, 1=-1, 2=0, 3=+1, 4=+2
    auto createPreset = [](const juce::String& name, const juce::String& desc,
                           std::initializer_list<std::pair<juce::String, float>> paramValues) {
        auto preset = std::make_unique<juce::DynamicObject>();
        preset->setProperty("name", name);
        preset->setProperty("author", "Swarmness");
        preset->setProperty("description", desc);
        preset->setProperty("version", kPresetVersion);
        preset->setProperty("category", "Factory");
        
        auto params = std::make_unique<juce::DynamicObject>();
        // Set defaults first - neutral starting point
        // octaveMode: 3/4 = +1 OCT (index 3 of 5)
        params->setProperty("octaveMode", 3.0f / 4.0f);
        params->setProperty("engage", 1.0f);
        params->setProperty("rise", 0.05f);
        params->setProperty("slideRange", 0.0f);
        params->setProperty("slideTime", 0.0f);
        params->setProperty("slideDirection", 0.0f);
        params->setProperty("autoSlide", 0.0f);
        params->setProperty("slidePosition", 0.0f);
        params->setProperty("slideReturn", 1.0f);
        params->setProperty("randomRange", 0.0f);
        params->setProperty("randomRate", 0.1f);
        params->setProperty("randomSmooth", 0.5f);
        params->setProperty("randomMode", 0.0f);
        params->setProperty("panic", 0.0f);
        params->setProperty("chaos", 0.0f);
        params->setProperty("speed", 0.0f);
        params->setProperty("lowCut", 0.0f);
        params->setProperty("highCut", 1.0f);
        params->setProperty("chorusRate", 0.0f);
        params->setProperty("chorusDepth", 0.0f);
        params->setProperty("chorusMix", 0.0f);
        params->setProperty("saturation", 0.0f);
        params->setProperty("mix", 1.0f);
        params->setProperty("drive", 0.0f);
        params->setProperty("outputGain", 0.8f);
        params->setProperty("flowMode", 1.0f);
        params->setProperty("flowAmount", 0.0f);
        params->setProperty("flowSpeed", 0.3f);
        params->setProperty("globalBypass", 0.0f);
        
        // Override with preset-specific values
        for (const auto& [key, value] : paramValues) {
            params->setProperty(key, value);
        }
        
        preset->setProperty("parameters", params.release());
        return juce::var(preset.release());
    };
    
    // ========== v1.2.6 FACTORY PRESETS (8 Heavy Music Presets) ==========
    // Designed for momentary engagement - short rise times for instant response
    
    // 1. Breakdown Chaos - for breakdowns, momentary
    mFactoryPresets["Breakdown Chaos"] = createPreset(
        "Breakdown Chaos", "For breakdown sections - instant chaos",
        {{"octaveMode", 1.0f / 4.0f}, // -1 OCT
         {"randomRange", 0.5f}, {"randomRate", 0.4f},   // range: 12 st, speed: 0.4
         {"rise", 0.025f},                              // 50ms - short!
         {"chaos", 0.8f}, {"panic", 0.5f}, {"speed", 0.3f}, // anger: 0.8, rush: 0.5, rate: 0.3
         {"lowCut", 0.04f}, {"highCut", 0.6f}, {"saturation", 0.4f}, // midBoost: 0.4
         {"chorusDepth", 0.2f}, {"chorusRate", 0.15f}, {"chorusMix", 0.25f},
         {"flowAmount", 0.0f}, {"flowSpeed", 0.0f},
         {"mix", 1.0f}, {"drive", 0.4f}, {"outputGain", 0.8f}});
    
    // 2. Slam - brutal low slam accent
    mFactoryPresets["Slam"] = createPreset(
        "Slam", "Brutal low slam riff accent",
        {{"octaveMode", 0.0f}, // -2 OCT
         {"randomRange", 0.25f}, {"randomRate", 0.2f},  // range: 6 st
         {"rise", 0.015f},                              // 30ms - very short
         {"chaos", 0.6f}, {"panic", 0.7f}, {"speed", 0.2f},
         {"lowCut", 0.02f}, {"highCut", 0.4f}, {"saturation", 0.7f}, // high midBoost for density
         {"chorusDepth", 0.3f}, {"chorusRate", 0.1f}, {"chorusMix", 0.4f},
         {"chorusMode", 1.0f},                          // deep ON
         {"flowAmount", 0.2f}, {"flowSpeed", 0.3f},
         {"mix", 1.0f}, {"drive", 0.6f}, {"outputGain", 0.75f}});
    
    // 3. Atmospheric Void - ambient (единственный с длинным rise)
    mFactoryPresets["Atmospheric Void"] = createPreset(
        "Atmospheric Void", "Ambient transitions - Sleep Token style",
        {{"octaveMode", 2.0f / 4.0f}, // 0 (no shift)
         {"randomRange", 0.125f}, {"randomRate", 0.1f}, // range: 3 st
         {"rise", 0.25f},                               // 500ms - long for ambient
         {"chaos", 0.1f}, {"panic", 0.05f}, {"speed", 0.08f},
         {"lowCut", 0.05f}, {"highCut", 0.9f}, {"saturation", 0.1f},
         {"chorusDepth", 0.6f}, {"chorusRate", 0.25f}, {"chorusMix", 0.5f},
         {"chorusMode", 1.0f},                          // deep ON
         {"flowAmount", 0.4f}, {"flowSpeed", 0.2f},
         {"mix", 0.7f}, {"drive", 0.1f}, {"outputGain", 0.85f}});
    
    // 4. Nu-Metal Aggression - Alpha Wolf style
    mFactoryPresets["Nu-Metal Aggression"] = createPreset(
        "Nu-Metal Aggression", "Nu-metalcore aggression - Alpha Wolf style",
        {{"octaveMode", 1.0f / 4.0f}, // -1 OCT
         {"randomRange", 0.33f}, {"randomRate", 0.35f}, // range: 8 st
         {"rise", 0.03f},                               // 60ms
         {"chaos", 0.65f}, {"panic", 0.45f}, {"speed", 0.35f},
         {"lowCut", 0.03f}, {"highCut", 0.5f}, {"saturation", 0.5f},
         {"chorusDepth", 0.25f}, {"chorusRate", 0.2f}, {"chorusMix", 0.3f},
         {"flowAmount", 0.15f}, {"flowSpeed", 0.25f},
         {"mix", 1.0f}, {"drive", 0.5f}, {"outputGain", 0.8f}});
    
    // 5. Hardcore Fury - fast hardcore punk
    mFactoryPresets["Hardcore Fury"] = createPreset(
        "Hardcore Fury", "Fast hardcore punk - high energy",
        {{"octaveMode", 2.0f / 4.0f}, // 0 (no shift)
         {"randomRange", 0.17f}, {"randomRate", 0.6f},  // range: 4 st
         {"rise", 0.0125f},                             // 25ms - very fast
         {"chaos", 0.85f}, {"panic", 0.75f}, {"speed", 0.55f},
         {"lowCut", 0.05f}, {"highCut", 0.7f}, {"saturation", 0.3f},
         {"chorusDepth", 0.1f}, {"chorusRate", 0.3f}, {"chorusMix", 0.15f},
         {"flowAmount", 0.3f}, {"flowSpeed", 0.6f},
         {"mix", 1.0f}, {"drive", 0.35f}, {"outputGain", 0.85f}});
    
    // 6. Djent Stab - short tight djent accent
    mFactoryPresets["Djent Stab"] = createPreset(
        "Djent Stab", "Short tight djent accent",
        {{"octaveMode", 2.0f / 4.0f}, // 0 (no shift)
         {"randomRange", 0.08f}, {"randomRate", 0.15f}, // range: 2 st
         {"rise", 0.0075f},                             // 15ms - maximum short
         {"chaos", 0.35f}, {"panic", 0.25f}, {"speed", 0.45f},
         {"lowCut", 0.04f}, {"highCut", 0.6f}, {"saturation", 0.45f},
         {"chorusDepth", 0.15f}, {"chorusRate", 0.35f}, {"chorusMix", 0.2f},
         {"flowAmount", 0.5f}, {"flowSpeed", 0.4f},
         {"mix", 0.9f}, {"drive", 0.25f}, {"outputGain", 0.8f}});
    
    // 7. Chainsaw Massacre - Swedish death metal HM-2 style
    mFactoryPresets["Chainsaw Massacre"] = createPreset(
        "Chainsaw Massacre", "Swedish death metal HM-2 chainsaw tone",
        {{"octaveMode", 2.0f / 4.0f}, // 0 (no shift)
         {"randomRange", 0.0f}, {"randomRate", 0.0f},   // no pitch random
         {"rise", 0.01f},                               // 20ms
         {"chaos", 0.0f}, {"panic", 0.0f}, {"speed", 0.0f}, // effects off
         {"lowCut", 0.03f}, {"highCut", 0.5f}, {"saturation", 1.0f}, // MAX midBoost - key to chainsaw!
         {"chorusDepth", 0.0f}, {"chorusRate", 0.0f}, {"chorusMix", 0.0f},
         {"flowAmount", 0.0f}, {"flowSpeed", 0.0f},
         {"mix", 0.0f}, {"drive", 0.0f}, {"outputGain", 0.9f}}); // dry signal + mid boost
    
    // 8. Scream Machine - harsh vocals
    mFactoryPresets["Scream Machine"] = createPreset(
        "Scream Machine", "For harsh vocals processing",
        {{"octaveMode", 3.0f / 4.0f}, // +1 OCT
         {"randomRange", 0.75f}, {"randomRate", 0.5f},  // range: 18 st
         {"rise", 0.02f},                               // 40ms
         {"chaos", 0.95f}, {"panic", 0.55f}, {"speed", 0.4f}, // maximum chaos
         {"lowCut", 0.1f}, {"highCut", 0.4f}, {"saturation", 0.65f},
         {"chorusDepth", 0.2f}, {"chorusRate", 0.4f}, {"chorusMix", 0.25f},
         {"flowAmount", 0.25f}, {"flowSpeed", 0.45f},
         {"mix", 0.85f}, {"drive", 0.7f}, {"outputGain", 0.7f}});
}
