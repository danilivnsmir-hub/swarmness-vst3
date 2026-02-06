#include "PresetManager.h"

const juce::String PresetManager::kPresetExtension = ".swpreset";
const juce::String PresetManager::kPresetVersion = "2.3.0";

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts)
    : mAPVTS(apvts)
{
    initializeFactoryPresets();
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
    }
}

void PresetManager::initializeFactoryPresets() {
    // Helper lambda to create a preset
    auto createPreset = [](const juce::String& name, const juce::String& desc,
                           std::initializer_list<std::pair<juce::String, float>> paramValues) {
        auto preset = std::make_unique<juce::DynamicObject>();
        preset->setProperty("name", name);
        preset->setProperty("author", "OpenAudio");
        preset->setProperty("description", desc);
        preset->setProperty("version", kPresetVersion);
        preset->setProperty("category", "Factory");
        
        auto params = std::make_unique<juce::DynamicObject>();
        // Set defaults first
        params->setProperty("octaveMode", 2.0f / 3.0f);  // +1 OCT (index 2 of 4)
        params->setProperty("engage", 1.0f);
        params->setProperty("rise", 0.05f);
        params->setProperty("slideRange", 0.5f);
        params->setProperty("slideTime", 0.1f);
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
        params->setProperty("chorusRate", 0.2f);
        params->setProperty("chorusDepth", 0.5f);
        params->setProperty("chorusMix", 0.0f);
        params->setProperty("saturation", 0.0f);
        params->setProperty("mix", 1.0f);
        params->setProperty("drive", 0.0f);
        params->setProperty("outputGain", 0.8f);
        params->setProperty("flowMode", 1.0f);  // Pulse mode
        params->setProperty("flowAmount", 0.0f);
        params->setProperty("flowSpeed", 0.3f);
        
        // Override with preset-specific values
        for (const auto& [key, value] : paramValues) {
            params->setProperty(key, value);
        }
        
        preset->setProperty("parameters", params.release());
        return juce::var(preset.release());
    };
    
    // Factory Preset 1: Init (default clean state)
    mFactoryPresets["Init"] = createPreset(
        "Init", "Default initialization - clean 100% wet octave up", {});
    
    // Factory Preset 2: Octave Up (classic octave up)
    mFactoryPresets["Octave Up"] = createPreset(
        "Octave Up", "Clean octave up effect",
        {{"octaveMode", 2.0f / 3.0f}, {"panic", 0.0f}, {"chaos", 0.0f}});
    
    // Factory Preset 3: Octave Down (one octave down)
    mFactoryPresets["Octave Down"] = createPreset(
        "Octave Down", "Clean octave down effect",
        {{"octaveMode", 1.0f / 3.0f}});
    
    // Factory Preset 4: Glitch (chaotic glitchy effect)
    mFactoryPresets["Glitch"] = createPreset(
        "Glitch", "Chaotic glitchy pitch effect",
        {{"panic", 0.7f}, {"chaos", 0.5f}, {"flowAmount", 0.4f}, {"flowSpeed", 0.7f}});
    
    // Factory Preset 5: Chorus Heavy (thick chorus effect)
    mFactoryPresets["Chorus Heavy"] = createPreset(
        "Chorus Heavy", "Thick lush chorus modulation",
        {{"chorusMix", 0.8f}, {"chorusDepth", 0.7f}, {"chorusRate", 0.3f}});
    
    // Factory Preset 6: Stutter Gate
    mFactoryPresets["Stutter Gate"] = createPreset(
        "Stutter Gate", "Rhythmic gating effect",
        {{"flowAmount", 0.9f}, {"flowSpeed", 0.5f}, {"flowMode", 1.0f}});
    
    // Factory Preset 7: Djent
    mFactoryPresets["Djent"] = createPreset(
        "Djent", "Tight djent-style octave",
        {{"octaveMode", 2.0f / 3.0f}, {"panic", 0.15f}, {"chaos", 0.1f}, 
         {"lowCut", 0.2f}, {"saturation", 0.3f}});
    
    // Factory Preset 8: Warped Tape
    mFactoryPresets["Warped Tape"] = createPreset(
        "Warped Tape", "Warped tape deck modulation",
        {{"chorusMix", 0.5f}, {"chorusDepth", 0.4f}, {"chorusRate", 0.15f},
         {"panic", 0.2f}, {"chaos", 0.3f}});
}
