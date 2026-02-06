#include "PresetManager.h"

const juce::String PresetManager::kPresetExtension = ".swpreset";
const juce::String PresetManager::kPresetVersion = "2.4.0";

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
        // Set defaults first - neutral starting point
        params->setProperty("octaveMode", 2.0f / 3.0f);  // +1 OCT (index 2 of 4)
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
        params->setProperty("flowMode", 1.0f);  // Pulse mode
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
    
    // Init - нейтральные настройки, 100% wet, без эффектов
    mFactoryPresets["Init"] = createPreset(
        "Init", "Neutral settings, 100% wet, no effects active",
        {{"octaveMode", 2.0f / 3.0f}, {"mix", 1.0f}, {"panic", 0.0f}, {"chaos", 0.0f},
         {"chorusDepth", 0.0f}, {"chorusRate", 0.0f}, {"chorusMix", 0.0f},
         {"flowAmount", 0.0f}});
    
    // Octave Up - +1 октава, минимальный drift, чистый pitch shift
    mFactoryPresets["Octave Up"] = createPreset(
        "Octave Up", "Clean +1 octave pitch shift",
        {{"octaveMode", 2.0f / 3.0f}, {"rise", 0.02f}, {"panic", 0.0f}, {"chaos", 0.0f}});
    
    // Octave Down - -1 октава, минимальный drift
    mFactoryPresets["Octave Down"] = createPreset(
        "Octave Down", "Clean -1 octave pitch shift",
        {{"octaveMode", 1.0f / 3.0f}, {"rise", 0.02f}, {"panic", 0.0f}, {"chaos", 0.0f}});
    
    // Double Octave - +2 октавы
    mFactoryPresets["Double Octave"] = createPreset(
        "Double Octave", "+2 octaves for super high pitch",
        {{"octaveMode", 1.0f}, {"rise", 0.03f}, {"panic", 0.0f}, {"chaos", 0.0f}});
    
    // Glitch Madness - высокий drift, chaos, быстрый flow
    mFactoryPresets["Glitch Madness"] = createPreset(
        "Glitch Madness", "Chaotic glitchy madness with fast flow",
        {{"panic", 0.85f}, {"chaos", 0.75f}, {"rise", 0.4f},
         {"flowAmount", 0.7f}, {"flowSpeed", 0.85f}, {"flowMode", 1.0f},
         {"saturation", 0.4f}});
    
    // Tape Warble - wow/flutter эффект, медленная модуляция
    mFactoryPresets["Tape Warble"] = createPreset(
        "Tape Warble", "Vintage tape deck warble effect",
        {{"chorusMix", 0.65f}, {"chorusDepth", 0.5f}, {"chorusRate", 0.12f},
         {"panic", 0.15f}, {"chaos", 0.2f}, {"rise", 0.1f}});
    
    // Stutter Gate - активный flow с быстрым pulse
    mFactoryPresets["Stutter Gate"] = createPreset(
        "Stutter Gate", "Rhythmic stutter/gate effect",
        {{"flowAmount", 0.95f}, {"flowSpeed", 0.7f}, {"flowMode", 1.0f},
         {"panic", 0.1f}});
    
    // Djent Tight - -1 октава, минимальный grain, tight sound
    mFactoryPresets["Djent Tight"] = createPreset(
        "Djent Tight", "Tight low octave for djent/metal",
        {{"octaveMode", 1.0f / 3.0f}, {"rise", 0.01f}, {"panic", 0.05f}, {"chaos", 0.0f},
         {"lowCut", 0.15f}, {"saturation", 0.35f}, {"drive", 0.2f}});
    
    // Ambient Shimmer - +1 октава, много chorus, медленный rate
    mFactoryPresets["Ambient Shimmer"] = createPreset(
        "Ambient Shimmer", "Ethereal shimmer with lush modulation",
        {{"octaveMode", 2.0f / 3.0f}, {"rise", 0.15f},
         {"chorusMix", 0.7f}, {"chorusDepth", 0.6f}, {"chorusRate", 0.08f},
         {"panic", 0.1f}, {"chaos", 0.15f}});
    
    // Lo-Fi Chaos - случайный pitch, много drift, saturation
    mFactoryPresets["Lo-Fi Chaos"] = createPreset(
        "Lo-Fi Chaos", "Degraded lo-fi with random pitch artifacts",
        {{"panic", 0.6f}, {"chaos", 0.5f}, {"rise", 0.35f},
         {"saturation", 0.6f}, {"drive", 0.4f},
         {"chorusMix", 0.3f}, {"chorusDepth", 0.3f}, {"chorusRate", 0.2f},
         {"lowCut", 0.1f}, {"highCut", 0.7f}});
    
    // ========== THE NOISE STYLE PRESETS ==========
    
    // The Noise Classic - Immediate glitchy octave shift, 100% wet
    mFactoryPresets["The Noise Classic"] = createPreset(
        "The Noise Classic", "Classic Noise pedal tone - aggressive glitchy octave",
        {{"octaveMode", 2.0f / 3.0f},  // +1 OCT
         {"engage", 1.0f},
         {"rise", 0.0f},               // Instant response
         {"panic", 0.65f},             // High grain for glitchy texture
         {"chaos", 0.3f},              // Some pitch instability
         {"mix", 1.0f},                // 100% wet
         {"flowAmount", 0.4f},         // Active flow for stutter
         {"flowSpeed", 0.5f},
         {"flowMode", 1.0f},           // Pulse mode
         {"saturation", 0.25f},
         {"drive", 0.15f}});
    
    // The Noise Chaos - Random octave madness, maximum aggression
    mFactoryPresets["The Noise Chaos"] = createPreset(
        "The Noise Chaos", "Maximum chaos - random octave shifts with fast stutter",
        {{"octaveMode", 0.0f},         // -2 OCT for crazy lows
         {"engage", 1.0f},
         {"rise", 0.02f},              // Near-instant
         {"panic", 0.9f},              // Maximum grain chaos
         {"chaos", 0.85f},             // High drift/randomness
         {"mix", 1.0f},                // 100% wet
         {"flowAmount", 0.85f},        // Aggressive stutter
         {"flowSpeed", 0.9f},          // Fast flow
         {"flowMode", 1.0f},           // Pulse mode
         {"saturation", 0.5f},
         {"drive", 0.35f},
         {"randomRange", 0.5f},        // Random pitch jumps
         {"randomRate", 0.6f}});
    
    // The Noise Clean - Simple octave, minimal effects
    mFactoryPresets["The Noise Clean"] = createPreset(
        "The Noise Clean", "Clean octave shift with minimal coloration",
        {{"octaveMode", 2.0f / 3.0f},  // +1 OCT
         {"engage", 1.0f},
         {"rise", 0.01f},              // Very fast rise
         {"panic", 0.1f},              // Minimal grain
         {"chaos", 0.05f},             // Minimal drift
         {"mix", 1.0f},                // 100% wet
         {"flowAmount", 0.0f},         // No flow
         {"saturation", 0.0f},
         {"drive", 0.0f},
         {"chorusMix", 0.0f}});
}
