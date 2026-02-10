#include "PresetManager.h"

const juce::String PresetManager::kPresetExtension = ".swpreset";
const juce::String PresetManager::kPresetVersion = "3.2.2";

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
    
    // ========== v3.0.0 PRESETS ==========
    
    // Init - neutral settings, no effects
    mFactoryPresets["Init"] = createPreset(
        "Init", "Neutral settings - 100% wet, no effects active",
        {{"octaveMode", 2.0f / 4.0f}, // 0 (no shift)
         {"mix", 1.0f}, 
         {"chaos", 0.0f}, {"panic", 0.0f},
         {"chorusDepth", 0.0f}, {"chorusRate", 0.0f}, {"chorusMix", 0.0f}});
    
    // Octave Up - +1 octave, clean pitch shift
    mFactoryPresets["Octave Up"] = createPreset(
        "Octave Up", "Clean +1 octave pitch shift",
        {{"octaveMode", 3.0f / 4.0f}, // +1 OCT
         {"chaos", 0.0f}, {"panic", 0.0f}});
    
    // Octave Down - -1 octave, clean
    mFactoryPresets["Octave Down"] = createPreset(
        "Octave Down", "Clean -1 octave pitch shift",
        {{"octaveMode", 1.0f / 4.0f}, // -1 OCT
         {"chaos", 0.0f}, {"panic", 0.0f}});
    
    // Glitch - high chaos and panic
    mFactoryPresets["Glitch"] = createPreset(
        "Glitch", "Glitchy octave shift with chaos",
        {{"octaveMode", 3.0f / 4.0f}, // +1 OCT
         {"chaos", 0.7f}, {"panic", 0.6f}, {"speed", 0.5f},
         {"randomRange", 0.3f}, {"randomRate", 0.4f}});
    
    // Chorus Heavy - full chorus wet
    mFactoryPresets["Chorus Heavy"] = createPreset(
        "Chorus Heavy", "Lush heavy chorus effect",
        {{"octaveMode", 2.0f / 4.0f}, // 0 (no shift)
         {"chorusDepth", 0.8f}, {"chorusRate", 0.3f}, {"chorusMix", 0.9f}});
    
    // The Noise Classic - aggressive glitchy octave
    mFactoryPresets["The Noise Classic"] = createPreset(
        "The Noise Classic", "Classic Noise pedal tone - aggressive glitchy octave",
        {{"octaveMode", 3.0f / 4.0f}, // +1 OCT
         {"chaos", 0.65f}, {"panic", 0.5f},
         {"speed", 0.4f},
         {"mix", 1.0f},
         {"saturation", 0.25f}});
    
    // Ambient Shimmer - ethereal modulation
    mFactoryPresets["Ambient Shimmer"] = createPreset(
        "Ambient Shimmer", "Ethereal shimmer with lush modulation",
        {{"octaveMode", 3.0f / 4.0f}, // +1 OCT
         {"chorusMix", 0.7f}, {"chorusDepth", 0.6f}, {"chorusRate", 0.08f},
         {"chaos", 0.1f}, {"panic", 0.1f}});
    
    // Lo-Fi - degraded sound
    mFactoryPresets["Lo-Fi"] = createPreset(
        "Lo-Fi", "Degraded lo-fi with pitch artifacts",
        {{"octaveMode", 2.0f / 4.0f}, // 0
         {"chaos", 0.5f}, {"panic", 0.4f},
         {"saturation", 0.6f},
         {"chorusMix", 0.3f}, {"chorusDepth", 0.3f}, {"chorusRate", 0.2f},
         {"lowCut", 0.1f}, {"highCut", 0.7f}});
    
    // Double Octave - +2 octaves
    mFactoryPresets["Double Octave"] = createPreset(
        "Double Octave", "+2 octaves for super high pitch",
        {{"octaveMode", 1.0f}, // +2 OCT
         {"chaos", 0.0f}, {"panic", 0.0f}});
    
    // Sub Bass - -2 octaves
    mFactoryPresets["Sub Bass"] = createPreset(
        "Sub Bass", "-2 octaves for deep sub bass",
        {{"octaveMode", 0.0f}, // -2 OCT
         {"chaos", 0.0f}, {"panic", 0.0f},
         {"lowCut", 0.0f}, {"highCut", 0.5f}});
}
