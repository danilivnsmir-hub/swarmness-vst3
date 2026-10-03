#include "PresetManager.h"
#include "../Parameters.h"
#include "../DSP/HiveBlock.h"
#include "../DSP/FlowGate.h"

const juce::String PresetManager::extension = ".swpreset";

namespace
{
    /** Footswitches and the footswitch mode are performance settings, not part of a sound. */
    bool isPresetParameter (const juce::String& id)
    {
        return ! isPerformanceParameter (id) && id != ParamIDs::switchMode;
    }

    juce::String sanitiseName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).substring (0, 64);
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    initialiseFactoryPresets();
    moveUnsupportedPresets (getPresetsDirectory());
    takeSnapshot();
}

int PresetManager::moveUnsupportedPresets (const juce::File& dir)
{
    // The first Swarmness versions (the old 1.x / 2.x numbering) wrote presets into the same folder (normalised values, no "plugin" tag).
    // They cannot be read any more: move them into a sub-folder instead of listing broken presets.
    int moved = 0;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*" + extension))
    {
        const auto json = juce::JSON::parse (f);
        if (json["plugin"].toString() == "Swarmness")
            continue;
        const auto oldDir = dir.getChildFile ("Old presets (unsupported)");
        oldDir.createDirectory();
        if (f.moveFileTo (oldDir.getNonexistentChildFile (f.getFileNameWithoutExtension(), extension, false)))
            ++moved;
    }
    return moved;
}

//==============================================================================
void PresetManager::initialiseFactoryPresets()
{
    using namespace ParamIDs;

    // Presets store the sound; the footswitches (SHIFT A / B, VENOM) are played live.
    // Internal IDs: rb* = HIVE, stingMix = SHIFT MIX, fuzz* = SMOKE, flow* = WINGS, panic = ANGER,
    // chaos = FRENZY, speed = BUZZ, rbPrimary = DRONE, rbSecondary = QUEEN, rbMagic = TRAILS.
    // Wings division indices: 3 = 1/8, 4 = 1/16, 5 = 1/32
    const auto trailFill = hivePatternLegacy;   // a ready-made TRAILS step pattern (HiveBlock::Fill), expanded below
    const juce::String recipes ("Pitch tricks - footswitches");
    const juce::String basics ("Start here"), stingCat ("Pitch tricks - footswitches"), hiveCat ("Harmonies & pitch delays"),
                       texture ("Fuzz & textures"), attack ("Rhythm & glitch"), space ("Chain tricks"),
                       amps ("Rigs - clean, crunch, metal");
    // AMP channels: 0 CLEAN, 1 CRUNCH, 2 LEAD. CAB types: 0 1x12, 1 2x12, 2 4x12 BRIT, 3 4x12 MOD
    // Chain slots (lower = earlier). Defaults: SMOKE 10, SHIFT 15, HIVE 20, WASP 22, AMP 23, CAB 26, SWARM 30, WINGS 40,
    // COMB 50, CARVE 60, CRYPT 70.
    const juce::String slotHive (Chain::slotIds[Chain::pitch]),   // 27 = after the CAB: a delay / harmony on a clean rig sits behind the amp
                       slotCrypt (Chain::slotIds[Chain::crypt]), slotCarve (Chain::slotIds[Chain::carve]),
                       slotComb (Chain::slotIds[Chain::comb]), slotSmoke (Chain::slotIds[Chain::smoke]);

    factoryPresets = {
        // ---------------------------------------------------------------- basics
        { "Init", basics, "Everything off: the plug-in is transparent. Start here.", {} },
        { "Clean Shift", basics, "Pure octave shifter on the clean modern engine (RAW off). Hold SHIFT A (+1 oct) / SHIFT B (+2 oct) for a clean, instant jump.",
          { { rise, 0 }, { fall, 0 }, { rbRaw, 0 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 } } },
        { "Slow Rise", basics, "RISE and FALL: hold a footswitch and the pitch sweeps up over ~1 s; release and it slides back down over ~1.5 s.",
          { { rise, 950 }, { fall, 1500 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 } } },
        { "Sticky Clean", amps, "HONEY into the VELVET clean: a compressor pedal in front of the amp - even picking, sticky sustain, a little SWARM.",
          { { hnOn, 1 }, { hnSustain, 65 }, { hnAttack, 70 }, { hnBlend, 100 }, { hnLevel, 1 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampGain, 3 }, { ampTreble, 6 }, { ampLevel, 6 },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 30 },
            { swarmOn, 1 }, { swarmMix, 25 }, { swarmDepth, 35 } } },
        // ---------------------------------------------------------------- recipes
        { "Panic - Octave Panic", recipes, "Fuzz into a footswitch octave: SMOKE into SHIFT. Hold SHIFT A (+1 oct) or SHIFT B (+2 oct); ANGER sours it, FRENZY throws the pitch around, BUZZ grinds, RISE slides into the note.",
          { { rise, 25 }, { fall, 40 }, { panic, 55 }, { chaos, 20 }, { speed, 35 },
            { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 80 }, { fuzzTone, 55 }, { fuzzScoop, 35 }, { fuzzSag, 30 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Panic - Siren Sweep", recipes, "Slow RISE, fast FALL: hold SHIFT B and the note screams up two octaves; BUZZ adds the ring-mod / flanger grind. Tap the switch in rhythm for sirens.",
          { { rise, 220 }, { fall, 70 }, { panic, 30 }, { chaos, 8 }, { speed, 60 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 85 }, { fuzzTone, 50 }, { fuzzScoop, 45 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Glitch Steps", recipes, "STEPS 'Glitch': 16 repeats, each doing something else - up, backwards, random, silent - chopped short by GATE and locked to 1/16.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 55 }, { rbTracking, 90 }, { rbMagic, 70 }, { trailFill, 9 }, { trChop, 47 },
            { rbSync, 1 }, { rbDiv, 4 }, { rbTone, 60 }, { rbMix, 50 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Gallop Octaves", recipes, "STEPS 'Gallop' (x - x x): octave repeats in a gallop rhythm at 1/16 - for tremolo-picked riffs.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 55 }, { rbTracking, 85 }, { rbMagic, 60 }, { trailFill, 7 }, { trChop, 32 },
            { rbSync, 1 }, { rbDiv, 4 }, { rbTone, 60 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        // ----------------------------------------------------------------- SHIFT + MANGLE
        { "Killer Bee", stingCat, "The all-rounder: a bit of ANGER, FRENZY and BUZZ plus SMOKE in front. Hold SHIFT B (+2 oct) for the full shriek.",
          { { rise, 30 }, { panic, 45 }, { chaos, 30 }, { speed, 25 }, { fuzzOn, 1 }, { fuzz, 60 }, { fuzzTone, 55 }, { fuzzScoop, 50 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Stacked Octaves", stingCat, "STACK on: SHIFT A = +1 oct, SHIFT B = +2 oct. Hold A, then add B - a second voice splits off and both octaves ring together; let go of B and it merges back.",
          { { shStack, 1 }, { rise, 40 }, { fall, 120 }, { stingMix, 70 }, { panic, 10 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 } } },
        { "Power Stack", stingCat, "STACK with SHIFT A = -1 oct and SHIFT B = +7 (fifth): hold both for a sub-octave power chord out of one note, into SMOKE after it.",
          { { shStack, 1 }, { shiftA, -12 }, { shiftB, 7 }, { rise, 0 }, { fall, 30 }, { stingMix, 55 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 70 }, { fuzzScoop, 35 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 4 }, { ampBass, 6 }, { ampMid, 6 }, { ampTreble, 5 }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 40 }, { cabDist, 20 }, { inGate, 30 } } },
        { "Dive Bomb", stingCat, "SHIFT A = -1 oct, SHIFT B = -2 oct with a long RISE and a snappy FALL: hold for a slow dive, release to snap back. SMOKE after SWARM in the chain.",
          { { shiftA, -12 }, { shiftB, -24 }, { rise, 450 }, { fall, 60 }, { panic, 15 }, { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 70 }, { fuzzTone, 35 }, { fuzzScoop, 30 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        // ------------------------------------------------------------------ HIVE
        { "Harmony Fifth", hiveCat, "Tight, clean harmony on the modern engine (RAW off): a fifth above (DRONE) plus its octave (QUEEN).",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbSecondary, 25 }, { rbTracking, 95 }, { rbTone, 70 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Atonal Detune", hiveCat, "SNAP off: a quarter-tone-flat double. Instantly wrong in the best way.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -0.4f }, { rbPrimary, 85 }, { rbTracking, 100 }, { rbTone, 65 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Honey Ladder", hiveCat, "TRAILS synced to 1/8 notes: an octave ladder that climbs in time with the song.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 50 }, { rbTracking, 85 }, { rbMagic, 65 }, { rbSync, 1 }, { rbDiv, 3 }, { rbTone, 60 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Descending Spiral", hiveCat, "Negative PITCH with TRAILS: notes fall away in a spiral of fourths.",
          { { rbOn, 1 }, { rbPitch, -5 }, { rbPrimary, 65 }, { rbTracking, 70 }, { rbMagic, 70 }, { rbTime, 240 }, { rbTone, 40 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Drowning Hive", hiveCat, "Atonal down-shift, loose tracking and long TRAILS through the deep SWARM: moaning, gurgling.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -1.7f }, { rbPrimary, 70 }, { rbSecondary, 30 }, { rbTracking, 35 },
            { rbMagic, 85 }, { rbTime, 320 }, { rbTone, 30 }, { swarmOn, 1 }, { swarmDeep, 1 }, { swarmMix, 40 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Bounce Trill", hiveCat, "STEPS 'Bounce' (DOWN / UP): the repeats flip between a fifth up and your note - a trill that stays in key and fades out.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbTracking, 85 }, { rbMagic, 70 }, { trailFill, 1 }, { rbTime, 150 }, { rbTone, 60 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Reverse Hive", hiveCat, "STEPS 'Reverse': an octave voice whose repeats play backwards - ghostly swells that breathe in before every note.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 55 }, { rbTracking, 85 }, { rbMagic, 65 }, { trailFill, 3 }, { rbTime, 380 }, { rbTone, 50 }, { rbMix, 60 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Pitch Delay", hiveCat, "HIVE as a delay: DRY feeds the repeats from your note, every repeat climbs a fifth (all UP) - the classic climbing pitch delay. DRONE off.",
          { { rbOn, 1 }, { trDry, 1 }, { rbPrimary, 0 }, { rbPitch, 7 }, { rbTracking, 100 }, { rbMagic, 60 }, { rbTime, 320 }, { rbTone, 55 }, { rbMix, 50 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Dotted Echo", hiveCat, "A plain dotted-1/8 delay out of HIVE: DRY + all-HOLD steps (FILL 'Echo'), synced. RAW gives the repeats tape-like wobble.",
          { { rbOn, 1 }, { trDry, 1 }, { rbPrimary, 0 }, { rbTracking, 100 }, { rbMagic, 55 }, { trailFill, 5 }, { rbSync, 1 }, { rbDiv, 9 }, { rbTone, 45 }, { rbMix, 45 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Venom Overload", hiveCat, "Long octave TRAILS. Hold the VENOM footswitch: it takes off into self-oscillating squalls and drags SHIFT A in (LINK).",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 50 }, { rbTracking, 60 }, { rbMagic, 92 }, { rbTone, 60 }, { linkOct1, 1 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 } } },
        // ------------------------------------------------------------- textures
        { "Swarm Cloud", texture, "DEEP SWARM over a barely-detuned double: wide, seasick, huge.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, 0.2f }, { rbPrimary, 40 }, { rbTracking, 90 },
            { swarmOn, 1 }, { swarmDeep, 1 }, { swarmDepth, 75 }, { swarmRate, 0.35f }, { swarmMix, 55 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Jumbo Smoke", texture, "Jumbo fuzz: MID voice, huge sustain and a deep SCOOP - the wall-of-fuzz starting point.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 90 }, { fuzzTone, 45 }, { fuzzScoop, 75 }, { fuzzSag, 55 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Doom Cathedral", texture, "DOWN voice: crushing low-mids and the full bottom end, flat mids, a little CLEAN under it and a lot of SAG - every note sags and blooms.",
          { { fuzzOn, 1 }, { fuzzVoice, 0 }, { fuzz, 85 }, { fuzzTone, 35 }, { fuzzScoop, 15 }, { fuzzBlend, 20 }, { fuzzSag, 80 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 4 }, { ampBass, 6 }, { ampMid, 6 }, { ampTreble, 5 }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 40 }, { cabDist, 20 }, { inGate, 40 } } },
        { "Glare Scream", texture, "UP voice with GLARE: tight, screaming upper mids and a gated octave-up that rips on hard picking.",
          { { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 75 }, { fuzzTone, 60 }, { fuzzScoop, 30 }, { fuzzGlare, 70 }, { fuzzSag, 15 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Smoked Out", texture, "SMOKE with GATE and heavy SAG: a dying battery - notes sag, bloom, then sputter and tear apart as they decay.",
          { { fuzzOn, 1 }, { fuzz, 85 }, { fuzzGate, 75 }, { fuzzTone, 45 }, { fuzzScoop, 50 }, { fuzzSag, 90 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Wing Beat Breakdown", texture, "Tempo-synced 1/16 hard WINGS gate on SMOKE. Hold SHIFT A for angry stabs.",
          { { rise, 0 }, { fall, 0 }, { panic, 30 }, { fuzzOn, 1 }, { fuzzVoice, 0 }, { fuzz, 80 }, { fuzzScoop, 60 }, { fuzzBlend, 15 }, { flowOn, 1 }, { flowSync, 1 }, { flowDiv, 4 }, { flowHard, 1 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 4 }, { ampBass, 6 }, { ampMid, 6 }, { ampTreble, 5 }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 40 }, { cabDist, 20 }, { inGate, 30 } } },
        { "Gallop Gate", texture, "WINGS as a pattern gate: the Gallop fill (on, off, on, on) over a quarter note - a rhythm the amp plays for you. Change DIV for the tempo feel.",
          { { flowOn, 1 }, { flowSync, 1 }, { flowDiv, 2 }, { flowHard, 1 }, { flowAmount, 100 }, { wgSteps, 4 }, { wgLevels[0], 100 }, { wgLevels[1], 0 }, { wgLevels[2], 100 }, { wgLevels[3], 100 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 6 }, { cabOn, 1 }, { cabType, 2 } } },
        { "Glitch Wings", texture, "A 16-step Glitch pattern over one bar, smooth, on a fuzz with trails: broken, stuttering textures that still follow the song.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 60 }, { flowOn, 1 }, { flowSync, 1 }, { flowDiv, 0 }, { flowHard, 0 }, { flowAmount, 90 },
            { wgSteps, 16 }, { wgLevels[0], 100 }, { wgLevels[1], 80 }, { wgLevels[2], 0 }, { wgLevels[3], 100 }, { wgLevels[4], 60 }, { wgLevels[5], 0 }, { wgLevels[6], 100 }, { wgLevels[7], 70 },
            { wgLevels[8], 100 }, { wgLevels[9], 0 }, { wgLevels[10], 50 }, { wgLevels[11], 100 }, { wgLevels[12], 0 }, { wgLevels[13], 100 }, { wgLevels[14], 70 }, { wgLevels[15], 90 },
            { revOn, 1 }, { revType, 2 }, { revMix, 25 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Bitten Smoke", texture, "SMOKE with CRUSH: the fuzz through a 6-bit, 8 kHz grinder - digital splinters on top of the diodes, the CLEAN underneath keeps the note readable.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 65 }, { fuzzScoop, 30 }, { fuzzBlend, 20 }, { fuzzCrush, 70 }, { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 3 }, { cabOn, 1 }, { cabType, 2 }, { inGate, 30 } } },
        { "Bell Swarm", texture, "SWARM DEEP with RING up: the chorus voices ring-modulated into metallic bells over the dry note - clangs and sum tones, CRYPT behind it.",
          { { swarmOn, 1 }, { swarmDeep, 1 }, { swarmDepth, 40 }, { swarmRate, 0.4f }, { swarmMix, 50 }, { swarmRing, 72 }, { revOn, 1 }, { revType, 2 }, { revMix, 30 }, { revDecay, 4 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 } } },
        { "Ghost Swarm", texture, "Smooth WINGS tremolo, a quiet octave-up DRONE with trails and SWARM - eerie clean parts.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 30 }, { rbMagic, 35 }, { rbTime, 300 }, { rbTracking, 85 }, { rbTone, 45 },
            { swarmOn, 1 }, { swarmMix, 35 }, { flowOn, 1 }, { flowHard, 0 }, { flowSpeed, 5.5f }, { flowAmount, 70 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        // ---------------------------------------------------------- swarm attack
        { "Broken Radio", attack, "Atonal loose HIVE, dark SMOKE after it and WINGS tremolo: a dying transmission.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -2.6f }, { rbPrimary, 80 }, { rbTracking, 15 }, { rbTone, 30 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzz, 45 }, { fuzzTone, 20 },
            { flowOn, 1 }, { flowHard, 0 }, { flowSpeed, 6.0f }, { flowAmount, 60 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2 }, { ampBass, 3 }, { ampTreble, 4 }, { cabOn, 1 }, { cabType, 0 }, { cabMic, 60 }, { cabDist, 60 } } },
        { "Hive Collapse", attack, "Everything at once. HIVE runs INTO SHIFT here: one stomp on VENOM and the self-oscillating trails, gated SMOKE and all get dragged two octaves up (LINK to SHIFT B).",
          { { panic, 40 }, { chaos, 40 }, { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 60 }, { rbMagic, 100 }, { rbTracking, 55 },
            { juce::String (Chain::slotIds[Chain::shift]), 25 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzz, 90 }, { fuzzScoop, 60 }, { fuzzGlare, 40 }, { fuzzGate, 30 }, { linkOct2, 1 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        // ------------------------------------------------------- chain, EQ & CRYPT
        { "Crypt Doom", space, "DOWN SMOKE into a huge dark ABYSS. CARVE cuts the mud under the fuzz and DUCK keeps the riffs clear - the tail blooms in the gaps.",
          { { fuzzOn, 1 }, { fuzzVoice, 0 }, { fuzz, 85 }, { fuzzTone, 40 }, { fuzzScoop, 20 }, { fuzzSag, 70 },
            { peqOn, 1 }, { peqHpFreq, 70 }, { peqB1Freq, 300 }, { peqB1Gain, -3 }, { peqB1Q, 1.2f }, { peqLpFreq, 9000 },
            { revOn, 1 }, { revType, 3 }, { revDecay, 7 }, { revMix, 32 }, { revTone, 35 }, { revLowCut, 180 }, { revDuck, 55 }, { revPreDelay, 40 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 4 }, { ampBass, 6 }, { ampMid, 6 }, { ampTreble, 5 }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 40 }, { cabDist, 20 }, { inGate, 30 } } },
        { "Smoke in the Crypt", space, "Chain reordered: CRYPT runs INTO SMOKE. The fuzz eats the whole reverb - a sustaining, blooming wall of noise.",
          { { slotCrypt, 5 }, { revOn, 1 }, { revType, 2 }, { revDecay, 3.5f }, { revMix, 45 }, { revTone, 45 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 80 }, { fuzzScoop, 55 }, { fuzzSag, 40 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Tight Before Smoke", space, "CARVE moved in front of SMOKE: low cut and a mid push tighten the fuzz like a boost before an amp - palm mutes stay articulate.",
          { { slotCarve, 5 }, { peqOn, 1 }, { peqHpFreq, 120 }, { peqB2Freq, 900 }, { peqB2Gain, 5 }, { peqB2Q, 0.8f },
            { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 70 }, { fuzzScoop, 35 }, { fuzzGate, 10 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Comb Smile", space, "COMB after SMOKE: the classic scooped 'smile' - deep lows, carved mids, sizzling top.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 75 }, { fuzzScoop, 20 },
            { geqOn, 1 }, { geqBands[1], 3 }, { geqBands[2], 4 }, { geqBands[3], 1 }, { geqBands[4], -4 },
            { geqBands[5], -5 }, { geqBands[6], -2 }, { geqBands[7], 3 }, { geqBands[8], 2 }, { geqBands[9], -6 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Hive Cathedral", space, "Octave-up HIVE voices with TRAILS drowning in a bright PLATE - choir-like, holy and wrong.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 45 }, { rbSecondary, 20 }, { rbMagic, 45 }, { rbTime, 260 }, { rbTracking, 85 }, { rbTone, 55 },
            { revOn, 1 }, { revType, 1 }, { revDecay, 5 }, { revMix, 45 }, { revTone, 65 }, { revMod, 50 }, { revPreDelay, 25 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 },
            { slotHive, 27 } } },
        { "Parallel Smoke", space, "SMOKE on parallel path A, path B left empty = your dry signal. The A/B MIX at the merge blends clean attack and low end under a full fuzz.",
          { { juce::String (Chain::laneIds[Chain::smoke]), 1 }, { Chain::parallelMixIds[0], 35 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 90 }, { fuzzScoop, 60 }, { fuzzSag, 50 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampMid, 5.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Split Swarm", space, "Parallel paths: SWARM on A, CRYPT on B - the chorus stays tight and the reverb never smears it; both meet at the merge.",
          { { juce::String (Chain::laneIds[Chain::swarm]), 1 }, { juce::String (Chain::laneIds[Chain::crypt]), 2 }, { slotCrypt, 35 }, { Chain::parallelMixIds[0], 45 },
            { swarmOn, 1 }, { swarmDepth, 70 }, { swarmRate, 0.8f }, { swarmMix, 50 },
            { revOn, 1 }, { revType, 2 }, { revDecay, 4 }, { revMix, 100 }, { revTone, 55 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampTreble, 6 }, { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 25 } } },
        { "Chug Room", space, "SMOKE with a tight ROOM that ducks hard while you play - space between the chugs, dry and punchy on them.",
          { { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 70 }, { fuzzScoop, 45 }, { fuzzGate, 15 },
            { revOn, 1 }, { revType, 0 }, { revDecay, 1.1f }, { revMix, 35 }, { revDuck, 80 }, { revLowCut, 250 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 }, { inGate, 30 } } },
        // ------------------------------------------------------------- rigs for the job (CLEAN = VELVET, CRUNCH = BRIT, LEAD = STEEL)
        { "Chorus Clean", amps, "The everyday clean: HONEY evens the picking, VELVET into an open 2x12, a touch of SWARM and a short ROOM. Verses, arpeggios, chords.",
          { { hnOn, 1 }, { hnSustain, 35 }, { hnAttack, 60 }, { hnBlend, 80 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampBass, 5 }, { ampMid, 5 }, { ampTreble, 6 }, { ampMaster, 5 },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 20 }, { cabDist, 30 },
            { swarmOn, 1 }, { swarmDepth, 35 }, { swarmRate, 0.5f }, { swarmMix, 30 },
            { revOn, 1 }, { revType, 0 }, { revMix, 18 }, { revDecay, 1.2f } } },
        { "Clean Clouds", amps, "Clean into a huge, ducking HALL with deep SWARM: pads and swells; the reverb blooms between the notes, not over them.",
          { { hnOn, 1 }, { hnSustain, 50 }, { hnAttack, 70 }, { hnBlend, 100 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampBass, 4.5f }, { ampTreble, 6 }, { ampMaster, 5 },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 40 },
            { swarmOn, 1 }, { swarmDeep, 1 }, { swarmDepth, 55 }, { swarmRate, 0.3f }, { swarmMix, 40 },
            { revOn, 1 }, { revType, 2 }, { revMix, 42 }, { revDecay, 7 }, { revPreDelay, 60 }, { revTone, 55 }, { revDuck, 45 }, { revMod, 50 } } },
        { "Shimmer Clean", amps, "Shimmer: an octave-up HIVE with long trails feeds a long HALL - the pad under the notes climbs an octave and keeps ringing. Chords, slow lines.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 12 }, { rbPrimary, 30 }, { rbTracking, 90 }, { rbMagic, 70 }, { rbTime, 400 }, { rbTone, 55 }, { rbMix, 40 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 2.5 }, { ampTreble, 6 }, { ampMaster, 5 },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 35 },
            { revOn, 1 }, { revType, 2 }, { revMix, 45 }, { revDecay, 8 }, { revTone, 60 }, { revMod, 40 }, { revDuck, 30 },
            { slotHive, 27 } } },
        { "Blues Drive", amps, "SMOOTH overdrive at a low DRIVE into VELVET on the edge: it breaks up when you dig in and cleans up with the volume knob. A 1x12, miked a little off.",
          { { drvOn, 1 }, { drvCharacter, 2 }, { drvDrive, 3 }, { drvVolume, 5 }, { drvBright, 5.5f }, { drvAttack, 3 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampGain, 5 }, { ampBass, 5 }, { ampMid, 5.5f }, { ampTreble, 5.5f }, { ampMaster, 4 }, { ampLevel, -6 },
            { cabOn, 1 }, { cabType, 0 }, { cabMic, 35 }, { cabDist, 25 },
            { revOn, 1 }, { revType, 0 }, { revMix, 15 }, { revDecay, 1.0f } } },
        { "Rock Rhythm", amps, "BRIT with the gain up for rock rhythm: barking mids, a tight bottom, the 4x12 miked straight. The chain GATE keeps the stops clean.",
          { { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 6.5f }, { ampBass, 5.5f }, { ampMid, 6.5f }, { ampTreble, 6 }, { ampPresence, 6 }, { ampDepth, 5.5f }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 25 }, { cabDist, 10 }, { inGate, 35 } } },
        { "Classic Fuzz Rock", amps, "A UP fuzz at half way into a barely-crunching BRIT: the fuzz does the hair, the amp the body. Riffs and lead with a '70s edge.",
          { { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 55 }, { fuzzTone, 55 }, { fuzzScoop, 25 }, { fuzzBlend, 15 }, { fuzzSag, 30 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 3 }, { ampBass, 5 }, { ampMid, 6 }, { ampTreble, 5.5f }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 35 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Modern Rhythm", amps, "The modern metal rhythm rig: TIGHT WASP as a boost in front of STEEL, PRESENCE and DEPTH up, the 4x12 MOD close to the cap, the chain GATE at 50 for stops.",
          { { drvOn, 1 }, { drvCharacter, 0 }, { drvDrive, 1.5f }, { drvVolume, 7 }, { drvBright, 6 }, { drvAttack, 7 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 6 }, { ampBass, 5.5f }, { ampMid, 5 }, { ampTreble, 6.5f }, { ampPresence, 6 }, { ampDepth, 6.5f }, { ampMaster, 5.5f },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 20 }, { cabDist, 10 }, { cabLowCut, 80 }, { inGate, 50 } } },
        { "Djent Tight", amps, "Tighter still: the boost's ATTACK high, less gain on STEEL, more MASTER, a 90 Hz cut on the CAB and the chain GATE at 60 - chugs that stop dead.",
          { { drvOn, 1 }, { drvCharacter, 0 }, { drvDrive, 1 }, { drvVolume, 7.5f }, { drvBright, 6.5f }, { drvAttack, 8.5f },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 5 }, { ampBass, 4.5f }, { ampMid, 5.5f }, { ampTreble, 6.5f }, { ampPresence, 6.5f }, { ampDepth, 6 }, { ampMaster, 6.5f },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 15 }, { cabDist, 10 }, { cabLowCut, 95 }, { inGate, 60 } } },
        { "Lead Singing", amps, "A lead that sustains: SMOOTH WASP into STEEL, a little HONEY in front for evenness, a dotted pitch-free echo from HIVE and a PLATE behind it.",
          { { hnOn, 1 }, { hnSustain, 30 }, { hnAttack, 50 }, { hnBlend, 70 },
            { drvOn, 1 }, { drvCharacter, 2 }, { drvDrive, 4 }, { drvVolume, 6 }, { drvBright, 6 }, { drvAttack, 4 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 6.5f }, { ampBass, 5 }, { ampMid, 6 }, { ampTreble, 6 }, { ampPresence, 5.5f }, { ampDepth, 5 }, { ampMaster, 5.5f },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 30 }, { cabDist, 20 }, { cabLowCut, 90 },
            { rbOn, 1 }, { trDry, 1 }, { rbPrimary, 0 }, { rbTracking, 100 }, { rbMagic, 40 }, { trailFill, 5 }, { rbSync, 1 }, { rbDiv, 9 }, { rbTone, 40 }, { rbMix, 30 },
            { revOn, 1 }, { revType, 1 }, { revMix, 22 }, { revDecay, 2.5f }, { inGate, 40 },
            { slotHive, 27 } } },
        { "Slam Chug", amps, "Slam: a voice one semitone up at full level (MIX 50%) in front of STEEL with the gain up and the chain GATE at 55 - the clash gets the amp's grind, the stops are dead silent.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 1 }, { rbPrimary, 100 }, { rbTracking, 100 }, { rbMix, 50 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 7.5f }, { ampBass, 6.5f }, { ampMid, 4 }, { ampTreble, 5.5f }, { ampPresence, 5.5f }, { ampDepth, 7 }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 30 }, { cabDist, 15 }, { cabLowCut, 70 }, { inGate, 55 } } },
        { "Sub Octave Metal", amps, "An octave under everything you play (HIVE -12 at full level, tight tracking) into STEEL: an eight-string out of a six - the chain GATE at 50.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, -12 }, { rbPrimary, 85 }, { rbTracking, 100 }, { rbMix, 50 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 6.5f }, { ampBass, 6 }, { ampMid, 5 }, { ampTreble, 6 }, { ampPresence, 5.5f }, { ampDepth, 7 }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 }, { cabLowCut, 60 }, { inGate, 50 } } },
        { "Stoner Fuzz", amps, "MID fuzz into a BRIT at 3 with the bass up: thick, slow, sagging stoner riffs; the 4x12 miked off the cap for warmth.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 70 }, { fuzzTone, 45 }, { fuzzScoop, 30 }, { fuzzSag, 55 }, { fuzzBlend, 10 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 3 }, { ampBass, 6.5f }, { ampMid, 5.5f }, { ampTreble, 5 }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 40 }, { cabDist, 20 }, { inGate, 30 } } },
        { "Glass Clean", amps, "CLEAN: the crystal clean into an open 2x12, a little SWARM and a HALL behind it.",
          { { hnOn, 1 }, { hnSustain, 40 }, { hnAttack, 60 }, { hnBlend, 70 },
            { ampOn, 1 }, { ampChannel, 0 }, { ampGain, 2.5 }, { ampBass, 4.5f }, { ampMid, 5 }, { ampTreble, 6 }, { ampLevel, 9 }, { ampMaster, 4.5f },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 15 }, { cabDist, 30 },
            { swarmOn, 1 }, { swarmMix, 30 }, { swarmDepth, 40 }, { revOn, 1 }, { revType, 2 }, { revMix, 20 }, { revDecay, 2.8f } } },
        { "Brit Crunch", amps, "CRUNCH: bright, barking upper mids and a tight bottom - rock rhythm into the BRIT 4x12. Roll the guitar's volume back to clean it up.",
          { { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 5 }, { ampBass, 5 }, { ampMid, 7 }, { ampTreble, 6.5f }, { ampPresence, 6 }, { ampLevel, -5 }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 10 } } },
        { "Brit Doom", amps, "CRUNCH with the gain up and the lows in: thick, compressed low-mids for doom and stoner riffs. DEPTH for the thump, a dark ABYSS in the gaps.",
          { { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 8 }, { ampBass, 7 }, { ampMid, 6.5f }, { ampTreble, 4.5f }, { ampDepth, 7 }, { ampMaster, 7 }, { ampLevel, -12.5f }, { cabOn, 1 }, { cabType, 2 }, { cabMic, 45 }, { cabDist, 25 },
            { revOn, 1 }, { revType, 3 }, { revMix, 20 }, { revDecay, 6 }, { revDuck, 60 }, { revLowCut, 200 }, { inGate, 35 } } },
        { "Steel Lead", amps, "LEAD: tight, cutting modern high gain. GATE keeps the stops clean, PRESENCE and DEPTH up for modern metal.",
          { { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 5 }, { ampBass, 5 }, { ampMid, 5 }, { ampTreble, 6.5f }, { ampPresence, 5.5f }, { ampDepth, 6 },
            { ampMaster, 5.5f }, { ampLevel, 2 }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 }, { cabLowCut, 80 }, { inGate, 35 } } },
        { "Hornet Lead", amps, "WASP boosting the LEAD amp, bright and tight, with the modern 4x12 miked close to the cap: a lead rig dialled in by ear.",
          { { input, 3 }, { drvOn, 1 }, { drvDrive, 1 }, { drvVolume, 7 }, { drvBright, 7 }, { drvAttack, 5 }, { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 5 }, { ampBass, 3.6f }, { ampMid, 4 }, { ampTreble, 7 }, { ampPresence, 5 }, { ampDepth, 6.6f },
            { ampLevel, -2.5f }, { ampMaster, 6 }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 15 }, { cabDist, 25 }, { cabLowCut, 60 }, { cabLevel, 4.9f }, { inGate, 45 } } },
        { "Wasp Boost", amps, "The classic tight metal boost: WASP with DRIVE almost off and VOLUME up in front of the LEAD amp - the lows tightened before the amp, "
                              "more bite and a faster attack. Switch WASP off to hear the difference.",
          { { drvOn, 1 }, { drvDrive, 0.5f }, { drvVolume, 7.5f }, { drvBright, 5.5f }, { drvAttack, 6.5f }, { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 4.5f }, { ampBass, 5.5f }, { ampMid, 5.5f }, { ampTreble, 6 }, { ampPresence, 6 }, { ampDepth, 6 },
            { ampMaster, 5.5f }, { ampLevel, 5 }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 }, { cabLowCut, 80 }, { inGate, 40 } } },
        { "Clean Push", amps, "WASP as a clean BOOST into the VELVET amp on the edge of breaking up: flat, no clipper, just more level - the amp does the grit. "
                              "DRIVE sets how hard it is pushed.",
          { { drvOn, 1 }, { drvCharacter, 1 }, { drvDrive, 5 }, { drvVolume, 5 }, { drvBright, 5 }, { drvAttack, 2 }, { ampOn, 1 }, { ampChannel, 0 }, { ampGain, 4.5 }, { ampBass, 5 }, { ampMid, 5 }, { ampTreble, 5.5f }, { ampMaster, 5 }, { ampLevel, -6 },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 25 }, { cabDist, 20 } } },
        { "Smooth Lead", amps, "The classic SMOOTH overdrive into the BRIT amp: soft, symmetric clipping, the mids pushed - a singing, even lead that cleans up with the volume knob.",
          { { drvOn, 1 }, { drvCharacter, 2 }, { drvDrive, 5.5f }, { drvVolume, 6 }, { drvBright, 6 }, { drvAttack, 3.5f }, { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 5 }, { ampBass, 5 }, { ampMid, 6 }, { ampTreble, 5.5f }, { ampPresence, 5.5f }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 25 }, { cabDist, 15 }, { inGate, 30 } } },
        { "Rasp Rhythm", amps, "RASP, the hard-clipping distortion, into a clean amp: the pedal is the whole sound - grainy, saturated, a tight ATTACK for riffs.",
          { { drvOn, 1 }, { drvCharacter, 3 }, { drvDrive, 6.5f }, { drvVolume, 5 }, { drvBright, 4.5f }, { drvAttack, 6 }, { ampOn, 1 }, { ampChannel, 0 }, { ampLevel, 3 }, { ampGain, 3 }, { ampBass, 5.5f }, { ampMid, 5 }, { ampTreble, 5 }, { ampMaster, 5 },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 30 }, { cabDist, 15 }, { inGate, 40 } } },
        { "Sludge Wall", amps, "LEAD scooped and huge: low MID, BASS and DEPTH up, the mic backed off the grille. Slow riffs, big stops.",
          { { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 6 }, { ampBass, 7 }, { ampMid, 3 }, { ampTreble, 6 }, { ampPresence, 5.5f }, { ampDepth, 7.5f },
            { ampMaster, 6.5f }, { ampLevel, -0.5f }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 35 }, { cabDist, 40 }, { inGate, 40 } } },
    };

    // expand the step-pattern shortcuts
    for (auto& fp : factoryPresets)
        migrateLegacyValues (fp.values);

    // "Effects only - no amp": a handful of the effect presets without the rig, for players with their own amp
    const juce::String fxOnly ("Effects only - no amp");
    for (const char* src : { "Clean Shift", "Harmony Fifth", "Pitch Delay", "Dotted Echo", "Killer Bee", "Jumbo Smoke", "Swarm Cloud",
                             "Crypt Doom", "Wing Beat Breakdown", "Sticky Clean" })
        for (const auto& fp : factoryPresets)
            if (fp.name == src)
            {
                auto copy = fp;
                copy.category = fxOnly;
                copy.name = fp.name + " (FX)";
                for (auto id : { ampOn, ampChannel, ampGain, ampBass, ampMid, ampTreble, ampPresence, ampDepth, ampMaster, ampLevel, ampGate,
                                 cabOn, cabType, cabMic, cabDist, cabLowCut, cabLevel })
                    copy.values.erase (id);
                factoryPresets.push_back (copy);
                break;
            }

    // Preset levels: trims measured offline (SwarmnessTests --render, the test guitar) so every
    // preset lands near -16 dBFS RMS; VOLUME shows the trim
    for (auto& fp : factoryPresets)
        if (auto it = presetLevelTrims().find (fp.name); it != presetLevelTrims().end())
            fp.values[output] = it->second;
}

const std::map<juce::String, float>& PresetManager::presetLevelTrims()
{
    static const std::map<juce::String, float> trims {
        { "Atonal Detune", 6.5f },
        { "Bell Swarm", 3.5f },
        { "Bitten Smoke", 6.0f },
        { "Blues Drive", 12.5f },
        { "Bounce Trill", 8.0f },
        { "Brit Crunch", 1.5f },
        { "Brit Doom", 4.0f },
        { "Broken Radio", -2.5f },
        { "Chorus Clean", 8.0f },
        { "Chug Room", 3.5f },
        { "Classic Fuzz Rock", 2.5f },
        { "Clean Clouds", 11.5f },
        { "Clean Push", 8.0f },
        { "Clean Shift", 7.5f },
        { "Comb Smile", -1.0f },
        { "Crypt Doom", 1.0f },
        { "Crypt Doom (FX)", -6.5f },
        { "Descending Spiral", 7.5f },
        { "Dive Bomb", -7.5f },
        { "Djent Tight", -1.5f },
        { "Dotted Echo", 5.0f },
        { "Dotted Echo (FX)", -3.0f },
        { "Drowning Hive", 4.5f },
        { "Gallop Gate", -1.0f },
        { "Gallop Octaves", 8.0f },
        { "Ghost Swarm", 10.5f },
        { "Glare Scream", 3.0f },
        { "Glass Clean", 7.0f },
        { "Glitch Steps", 9.0f },
        { "Glitch Wings", 4.0f },
        { "Harmony Fifth", 6.5f },
        { "Harmony Fifth (FX)", -1.0f },
        { "Hive Cathedral", 5.0f },
        { "Hive Collapse", -3.0f },
        { "Honey Ladder", 7.5f },
        { "Hornet Lead", -1.0f },
        { "Jumbo Smoke", 2.5f },
        { "Jumbo Smoke (FX)", -7.0f },
        { "Killer Bee", 3.0f },
        { "Killer Bee (FX)", -5.5f },
        { "Lead Singing", 2.0f },
        { "Modern Rhythm", 3.5f },
        { "Panic - Octave Panic", 3.0f },
        { "Panic - Siren Sweep", 2.5f },
        { "Parallel Smoke", 4.0f },
        { "Pitch Delay", 7.0f },
        { "Pitch Delay (FX)", -2.0f },
        { "Power Stack", -6.5f },
        { "Rasp Rhythm", 3.5f },
        { "Reverse Hive", 10.0f },
        { "Rock Rhythm", -7.0f },
        { "Shimmer Clean", 11.0f },
        { "Slam Chug", -2.0f },
        { "Slow Rise", 8.0f },
        { "Smoke in the Crypt", 4.0f },
        { "Smoked Out", 2.5f },
        { "Smooth Lead", -2.0f },
        { "Split Swarm", 8.0f },
        { "Stacked Octaves", 7.0f },
        { "Sticky Clean", 9.0f },
        { "Sticky Clean (FX)", 6.0f },
        { "Sub Octave Metal", -4.5f },
        { "Swarm Cloud", 4.5f },
        { "Swarm Cloud (FX)", -4.0f },
        { "Tight Before Smoke", 2.0f },
        { "Venom Overload", 7.5f },
        { "Wasp Boost", -1.5f },
        { "Wing Beat Breakdown", 3.0f },
        { "Wing Beat Breakdown (FX)", -4.5f },
    };
    return trims;
}

juce::StringArray PresetManager::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& fp : factoryPresets)
        names.add (fp.name);
    return names;
}

juce::StringArray PresetManager::getFactoryCategories() const
{
    juce::StringArray cats;
    for (const auto& fp : factoryPresets)
        cats.addIfNotAlreadyThere (fp.category);
    return cats;
}

juce::StringArray PresetManager::getFactoryPresetNames (const juce::String& category) const
{
    juce::StringArray names;
    for (const auto& fp : factoryPresets)
        if (fp.category == category)
            names.add (fp.name);
    return names;
}

juce::String PresetManager::getPresetDescription (const juce::String& name) const
{
    for (const auto& fp : factoryPresets)
        if (fp.name == name)
            return fp.description;
    return {};
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getPresetsDirectory().findChildFiles (juce::File::findFiles, false, "*" + extension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::StringArray PresetManager::getAllPresetNames() const
{
    auto names = getFactoryPresetNames();
    for (const auto& n : getUserPresetNames())
        if (! names.contains (n))
            names.add (n);
    return names;
}

bool PresetManager::isFactoryPreset (const juce::String& name) const
{
    return getFactoryPresetNames().contains (name);
}

bool PresetManager::isUserPreset (const juce::String& name) const
{
    return ! isFactoryPreset (name) && getPresetsDirectory().getChildFile (name + extension).existsAsFile();
}

juce::File PresetManager::getPresetsDirectory()
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Swarmness");
   #elif JUCE_WINDOWS
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Swarmness/Presets");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".swarmness/presets");
   #endif

    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

//==============================================================================
PresetManager::ValueMap PresetManager::captureValues() const
{
    ValueMap values;
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (isPresetParameter (ranged->getParameterID()))
                values[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return values;
}

void PresetManager::applyValues (const ValueMap& values, bool resetOthersToDefault)
{
    for (auto* param : apvts.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr || ! isPresetParameter (ranged->getParameterID()))
            continue;

        float normalised = -1.0f;
        if (auto it = values.find (ranged->getParameterID()); it != values.end())
            normalised = ranged->convertTo0to1 (it->second);
        else if (resetOthersToDefault)
            normalised = ranged->getDefaultValue();

        if (normalised >= 0.0f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
            ranged->endChangeGesture();
        }
    }
}

juce::StringArray PresetManager::parameterIdsOf (int block) const
{
    // groups of the layout -> blocks (the appended groups go to their blocks too; "tricks" holds one knob of each)
    static const std::map<juce::String, int> groups {
        { "shift", Chain::shift }, { "hive", Chain::pitch }, { "hiveStop", Chain::pitch }, { "swarm", Chain::swarm }, { "fuzz", Chain::smoke },
        { "flow", Chain::wings }, { "wingsSteps", Chain::wings }, { "comb", Chain::comb }, { "carve", Chain::carve }, { "crypt", Chain::crypt },
        { "cryptFreeze", Chain::crypt }, { "amp", Chain::amp }, { "wasp", Chain::drive }, { "waspCharacter", Chain::drive }, { "cab", Chain::cab },
        { "honey", Chain::honey } };
    juce::StringArray ids;
    std::function<void (const juce::AudioProcessorParameterGroup&, int)> walk = [&] (const juce::AudioProcessorParameterGroup& g, int owner)
    {
        for (auto* node : g)
        {
            if (auto* sub = node->getGroup())
            {
                auto it = groups.find (sub->getID());
                walk (*sub, it != groups.end() ? it->second : -1);
            }
            else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (node->getParameter()))
            {
                const auto id = ranged->getParameterID();
                int mine = owner;
                if (id == ParamIDs::fuzzCrush) mine = Chain::smoke;
                if (id == ParamIDs::swarmRing) mine = Chain::swarm;
                if (mine == block && isPresetParameter (id) && ! id.startsWith ("chain") && ! id.startsWith ("lane"))
                    ids.add (id);
            }
        }
    };
    walk (apvts.processor.getParameterTree(), -1);
    return ids;
}

juce::String PresetManager::copyBlock (int block) const
{
    juce::String text = "swarmness-block:" + juce::String (Chain::names[block]) + "\n";
    for (const auto& id : parameterIdsOf (block))
        if (auto* p = apvts.getParameter (id))
            text << id << "=" << juce::String (p->convertFrom0to1 (p->getValue()), 4) << "\n";
    return text;
}

bool PresetManager::clipboardHoldsBlock (const juce::String& text, int block)
{
    const auto header = text.upToFirstOccurrenceOf ("\n", false, false).trimEnd();   // CR / LF either way
    return header == "swarmness-block:" + juce::String (Chain::names[block]);
}

bool PresetManager::pasteBlock (int block, const juce::String& text)
{
    if (! clipboardHoldsBlock (text, block))
        return false;
    const auto allowed = parameterIdsOf (block);
    ValueMap values;
    for (const auto& line : juce::StringArray::fromLines (text))
    {
        const auto id = line.upToFirstOccurrenceOf ("=", false, false).trim();
        if (line.contains ("=") && allowed.contains (id))
            values[id] = line.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    }
    applyValues (values, false);
    return ! values.empty();
}

juce::StringArray PresetManager::blockPresetSources (int block) const
{
    const char* powerId = nullptr;
    switch (block)
    {
        case Chain::pitch: powerId = ParamIDs::rbOn; break;   case Chain::smoke: powerId = ParamIDs::fuzzOn; break;
        case Chain::swarm: powerId = ParamIDs::swarmOn; break; case Chain::wings: powerId = ParamIDs::flowOn; break;
        case Chain::comb:  powerId = ParamIDs::geqOn; break;   case Chain::carve: powerId = ParamIDs::peqOn; break;
        case Chain::crypt: powerId = ParamIDs::revOn; break;   case Chain::shift: powerId = ParamIDs::shOn; break;
        case Chain::amp:   powerId = ParamIDs::ampOn; break;   case Chain::cab:   powerId = ParamIDs::cabOn; break;
        case Chain::drive: powerId = ParamIDs::drvOn; break;   case Chain::honey: powerId = ParamIDs::hnOn; break;
        default: return {};
    }
    juce::StringArray names;
    for (const auto& fp : factoryPresets)
        if (! fp.name.endsWith (" (FX)"))
            if (auto it = fp.values.find (powerId); it != fp.values.end() && it->second > 0.5f)
                names.add (fp.name);
    return names;
}

bool PresetManager::applyBlockFromPreset (int block, const juce::String& presetName)
{
    for (const auto& fp : factoryPresets)
        if (fp.name == presetName)
        {
            // the preset's values for this block, defaults for what it does not set
            ValueMap values;
            for (const auto& id : parameterIdsOf (block))
                if (auto* p = apvts.getParameter (id))
                {
                    auto it = fp.values.find (id);
                    values[id] = it != fp.values.end() ? it->second : p->convertFrom0to1 (p->getDefaultValue());
                }
            applyValues (values, false);
            return true;
        }
    return false;
}

juce::File PresetManager::blockPresetsDirectory (int block)
{
    return getPresetsDirectory().getChildFile ("Blocks").getChildFile (Chain::names[juce::jlimit (0, Chain::numBlocks - 1, block)]);
}

juce::StringArray PresetManager::getUserBlockPresets (int block) const
{
    juce::StringArray names;
    for (const auto& f : blockPresetsDirectory (block).findChildFiles (juce::File::findFiles, false, "*.swarmblock"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool PresetManager::saveUserBlockPreset (int block, const juce::String& name)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty())
        return false;
    auto dir = blockPresetsDirectory (block);
    dir.createDirectory();
    return dir.getChildFile (clean + ".swarmblock").replaceWithText (copyBlock (block), false, false, "\n");
}

bool PresetManager::loadUserBlockPreset (int block, const juce::String& name)
{
    const auto f = blockPresetsDirectory (block).getChildFile (name + ".swarmblock");
    return f.existsAsFile() && pasteBlock (block, f.loadFileAsString());
}

bool PresetManager::deleteUserBlockPreset (int block, const juce::String& name)
{
    return blockPresetsDirectory (block).getChildFile (name + ".swarmblock").deleteFile();
}

void PresetManager::takeSnapshot()
{
    std::map<juce::String, float> values;
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (isPresetParameter (ranged->getParameterID()))
                values[ranged->getParameterID()] = ranged->getValue();

    const juce::ScopedLock sl (lock);
    snapshot = std::move (values);
    savedScenes = scenes;
    if (! scenes[(size_t) currentScene].empty())
        savedScenes[(size_t) currentScene] = captureSceneValues();
}

juce::String PresetManager::getCurrentPresetName() const
{
    const juce::ScopedLock sl (lock);
    return currentName;
}

void PresetManager::setCurrentName (const juce::String& name)
{
    const juce::ScopedLock sl (lock);
    currentName = name;
}

bool PresetManager::isDirty() const
{
    const juce::ScopedLock sl (lock);
    for (int k = 0; k < kScenes; ++k)
        if (k != currentScene && scenes[(size_t) k] != savedScenes[(size_t) k])
            return true;
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (auto it = snapshot.find (ranged->getParameterID()); it != snapshot.end())
                if (std::abs (it->second - ranged->getValue()) > 1.0e-4f)
                    return true;
    return false;
}

void PresetManager::restoreFromState (const juce::String& presetName)
{
    setCurrentName (presetName.isNotEmpty() ? presetName : juce::String ("Init"));
    takeSnapshot();
}

//==============================================================================
juce::var PresetManager::toJson (const juce::String& name, const ValueMap& values) const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    root->setProperty ("plugin", "Swarmness");
    root->setProperty ("version", JucePlugin_VersionString);

    auto* params = new juce::DynamicObject();
    for (const auto& [id, value] : values)
        params->setProperty (id, value);
    root->setProperty ("parameters", juce::var (params));
    if (onSaveExtras)
        onSaveExtras (*root);

    return juce::var (root);
}

bool PresetManager::fromJson (const juce::var& json, juce::String& name, ValueMap& values)
{
    auto* params = json["parameters"].getDynamicObject();
    if (params == nullptr)
        return false;

    name = json["name"].toString();
    for (const auto& prop : params->getProperties())
        if (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool())
            values[prop.name.toString()] = (float) (double) prop.value;
    migrateLegacyValues (values);
    return true;
}

void PresetManager::writeTrailFill (ValueMap& values, int fill)
{
    const auto pattern = HiveBlock::makeFill (fill);
    values[ParamIDs::trSteps] = (float) pattern.numSteps;
    for (size_t k = 0; k < (size_t) HiveBlock::kMaxSteps; ++k)
    {
        values[ParamIDs::trLevels[k]] = 100.0f * pattern.level[k];
        values[ParamIDs::trMoves[k]] = (float) pattern.move[k];
    }
}

void PresetManager::writeWingFill (ValueMap& values, int fill)
{
    const auto pattern = FlowGate::makeFill (fill);
    values[ParamIDs::wgSteps] = (float) pattern.numSteps;
    for (size_t k = 0; k < (size_t) FlowGate::kMaxSteps; ++k)
        values[ParamIDs::wgLevels[k]] = 100.0f * pattern.level[k];
}

void PresetManager::migrateLegacyValues (ValueMap& values)
{
    using namespace ParamIDs;
    auto has = [&values] (const char* id) { return values.find (id) != values.end(); };
    auto get = [&values] (const char* id, float def) { auto it = values.find (id); return it != values.end() ? it->second : def; };

    // Beta.19 and older: STING had DIVE and its own RAW / DETUNE
    const bool fromSting = ! has (shiftA) && ! has (shiftB);
    if (fromSting && get (noiseDownLegacy, 0.0f) > 0.5f)
    {
        values[shiftA] = -12.0f;
        values[shiftB] = -24.0f;
    }

    // Beta.25: SHIFT split off HIVE again, each with its own MANGLE / RAW / DETUNE / SNAP.
    // Beta.20-24 shared one set, so both blocks get a copy; STING values (beta.19) go to SHIFT.
    if (! has (shRaw))
    {
        if (fromSting && (has (stingRawLegacy) || has (stingDetuneLegacy)))
        {
            values[shRaw] = get (stingRawLegacy, 1.0f);
            values[shDetune] = get (stingDetuneLegacy, 0.0f);
        }
        else
        {
            values[shRaw] = get (rbRaw, 1.0f);
            values[shDetune] = get (rbDetune, 0.0f);
            values[hvAngerLegacy] = get (panic, 0.0f);
            values[hvFrenzyLegacy] = get (chaos, 0.0f);
            values[hvBuzzLegacy] = get (speed, 0.0f);
        }
        values[shSnap] = get (rbSnap, 1.0f);
    }
    // SHIFT goes where HIVE was (same slot: SHIFT sorts first), on the same lane
    if (! has (Chain::slotIds[Chain::shift]))
    {
        values[Chain::slotIds[Chain::shift]] = get (Chain::slotIds[Chain::pitch], (float) Chain::defaultSlots[Chain::pitch]);
        values[Chain::laneIds[Chain::shift]] = get (Chain::laneIds[Chain::pitch], 0.0f);
    }
    values.erase (hiveFollowLegacy);

    // v3.1: AMP and CAB join the chain right after HIVE (on the main line)
    if (! has (Chain::slotIds[Chain::amp]) && has (Chain::slotIds[Chain::pitch]))
    {
        const float hiveSlot = get (Chain::slotIds[Chain::pitch], (float) Chain::defaultSlots[Chain::pitch]);
        values[Chain::slotIds[Chain::amp]] = juce::jmin ((float) Chain::slotMax, hiveSlot + 3.0f);
        values[Chain::slotIds[Chain::cab]] = juce::jmin ((float) Chain::slotMax, hiveSlot + 6.0f);
    }

    // v3.1: WASP (overdrive) joins right in front of the AMP (same slot: WASP sorts first), same lane
    if (! has (Chain::slotIds[Chain::drive]) && has (Chain::slotIds[Chain::amp]))
    {
        values[Chain::slotIds[Chain::drive]] = get (Chain::slotIds[Chain::amp], (float) Chain::defaultSlots[Chain::amp]);
        values[Chain::laneIds[Chain::drive]] = get (Chain::laneIds[Chain::amp], 0.0f);
    }

    // Beta.25-27: HIVE's ANGER / FRENZY / BUZZ -> the single MANGLE knob
    if (has (hvAngerLegacy) || has (hvFrenzyLegacy) || has (hvBuzzLegacy))
    {
        if (! has (hvMangle))
            values[hvMangle] = 100.0f * HiveBlock::mangleFromParts (get (hvAngerLegacy, 0.0f) * 0.01f, get (hvFrenzyLegacy, 0.0f) * 0.01f,
                                                                   get (hvBuzzLegacy, 0.0f) * 0.01f);
        values.erase (hvAngerLegacy);
        values.erase (hvFrenzyLegacy);
        values.erase (hvBuzzLegacy);
    }

    // GATE turned the right way round: 0 = full repeats, more = shorter chops
    if (auto it = values.find (trGateLegacy); it != values.end())
    {
        if (! has (trChop))
            values[trChop] = juce::jlimit (0.0f, 100.0f, (100.0f - it->second) / 0.95f);
        values.erase (it);
    }
    values.erase (noiseDownLegacy);
    values.erase (stingRawLegacy);
    values.erase (stingDetuneLegacy);

    // Beta.20-22: one PATTERN choice -> the same pattern as TRAILS steps
    if (auto it = values.find (hivePatternLegacy); it != values.end())
    {
        const int fill = juce::jlimit (0, (int) HiveBlock::numFills - 1, juce::roundToInt (it->second));
        values.erase (it);
        if (! has (trMoves[0]))
            writeTrailFill (values, fill);
    }

    // Before the reorderable chain, SMOKE had a PRE / POST switch.
    if (auto it = values.find (ParamIDs::fuzzPostLegacy); it != values.end())
    {
        if (it->second > 0.5f && values.find (Chain::slotIds[Chain::smoke]) == values.end())
            values[Chain::slotIds[Chain::smoke]] = (float) Chain::legacyPostSmokeSlot;
        values.erase (it);
    }
}

bool PresetManager::loadPreset (const juce::String& name)
{
    for (const auto& fp : factoryPresets)
    {
        if (fp.name == name)
        {
            applyValues (fp.values, true);
            loadScenesFromJson ({});
            setCurrentName (name);
            takeSnapshot();
            return true;
        }
    }

    auto file = getPresetsDirectory().getChildFile (name + extension);
    if (! file.existsAsFile())
        return false;

    juce::String storedName;
    ValueMap values;
    const auto json = juce::JSON::parse (file);
    if (! fromJson (json, storedName, values))
        return false;

    applyValues (values, true);
    loadScenesFromJson (json);
    if (onLoadExtras)
        onLoadExtras (json);
    setCurrentName (name);
    takeSnapshot();
    return true;
}

void PresetManager::loadNextPreset (bool userBank)     { stepPreset (userBank, 1); }
void PresetManager::loadPreviousPreset (bool userBank) { stepPreset (userBank, -1); }

void PresetManager::stepPreset (bool userBank, int delta)
{
    const auto names = userBank ? getUserPresetNames() : getFactoryPresetNames();
    if (names.isEmpty()) return;
    const int idx = names.indexOf (getCurrentPresetName());
    const int n = names.size();
    const int next = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
    loadPreset (names[next]);
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = sanitiseName (rawName);
    if (name.isEmpty() || isFactoryPreset (name))
        return false;

    auto file = getPresetsDirectory().getChildFile (name + extension);
    if (! file.replaceWithText (juce::JSON::toString (presetJson (name))))
        return false;

    setCurrentName (name);
    takeSnapshot();
    return true;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    if (! isUserPreset (name))
        return false;

    const int idx = getUserPresetNames().indexOf (name);

    if (! getPresetsDirectory().getChildFile (name + extension).deleteFile())
        return false;

    // Stay in the user bank if possible: move to the neighbouring user preset, else back to Init.
    if (getCurrentPresetName() == name)
    {
        const auto remaining = getUserPresetNames();
        loadPreset (remaining.isEmpty() ? juce::String ("Init")
                                        : remaining[juce::jlimit (0, remaining.size() - 1, idx - 1)]);
    }
    return true;
}

bool PresetManager::importPreset (const juce::File& file)
{
    juce::String name;
    ValueMap values;
    const auto json = juce::JSON::parse (file);
    if (! fromJson (json, name, values))
        return false;

    if (name.isEmpty())
        name = file.getFileNameWithoutExtension();
    name = sanitiseName (name);
    if (isFactoryPreset (name))
        name << " (imported)";
    // never over an existing user preset: "Name (2)", "Name (3)"...
    const auto base = name;
    for (int k = 2; isUserPreset (name); ++k)
        name = base + " (" + juce::String (k) + ")";

    applyValues (values, true);
    loadScenesFromJson (json);
    if (onLoadExtras)
        onLoadExtras (json);
    return saveUserPreset (name);
}

bool PresetManager::exportPreset (const juce::File& file)
{
    return file.replaceWithText (juce::JSON::toString (presetJson (file.getFileNameWithoutExtension())));
}

//==============================================================================
bool PresetManager::isSceneParameter (const juce::String& id)
{
    if (! isPresetParameter (id))
        return false;
    for (int b = 0; b < Chain::numBlocks; ++b)
        if (id == Chain::slotIds[b] || id == Chain::laneIds[b])
            return false;   // the chain order / routing is shared by all scenes
    return true;
}

PresetManager::ValueMap PresetManager::captureSceneValues() const
{
    ValueMap values;
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (isSceneParameter (ranged->getParameterID()))
                values[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return values;
}

void PresetManager::resetScenes()
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& sc : scenes) sc.clear();
        for (auto& sc : savedScenes) sc.clear();
        currentScene = 0;
    }
    if (auto* p = apvts.getParameter (ParamIDs::scene))
        p->setValueNotifyingHost (0.0f);
}

void PresetManager::loadScenesFromJson (const juce::var& json)
{
    resetScenes();
    const juce::ScopedLock sl (lock);
    scenes[0] = captureSceneValues();
    if (auto* list = json["scenes"].getArray())
        for (int k = 1; k < juce::jmin (kScenes, list->size()); ++k)
            if (auto* obj = (*list)[k].getDynamicObject())
            {
                ValueMap values;
                for (const auto& prop : obj->getProperties())
                    values[prop.name.toString()] = (float) (double) prop.value;
                if (values.empty())
                    continue;
                migrateLegacyValues (values);
                // a scene only holds scene parameters; anything missing comes from scene A
                ValueMap full = scenes[0];
                for (const auto& [id, v] : values)
                    if (full.find (id) != full.end())
                        full[id] = v;
                scenes[(size_t) k] = std::move (full);
            }
    savedScenes = scenes;
}

juce::var PresetManager::presetJson (const juce::String& name)
{
    const juce::ScopedLock sl (lock);
    scenes[(size_t) currentScene] = captureSceneValues();
    auto values = captureValues();
    for (const auto& [id, v] : scenes[0])
        values[id] = v;   // the preset's own parameters = scene A
    auto json = toJson (name, values);
    bool any = false;
    juce::Array<juce::var> list;
    for (int k = 0; k < kScenes; ++k)
    {
        auto* obj = new juce::DynamicObject();
        if (k > 0 && ! scenes[(size_t) k].empty())
        {
            any = true;
            for (const auto& [id, v] : scenes[(size_t) k])
                obj->setProperty (id, v);
        }
        list.add (juce::var (obj));
    }
    if (any)
        json.getDynamicObject()->setProperty ("scenes", list);
    return json;
}

bool PresetManager::isSceneUsed (int index) const
{
    const juce::ScopedLock sl (lock);
    return index == currentScene || ! scenes[(size_t) juce::jlimit (0, kScenes - 1, index)].empty();
}

void PresetManager::selectScene (int index)
{
    index = juce::jlimit (0, kScenes - 1, index);
    ValueMap target;
    {
        const juce::ScopedLock sl (lock);
        if (index == currentScene)
            return;
        scenes[(size_t) currentScene] = captureSceneValues();
        if (scenes[(size_t) index].empty())
        {
            // a new scene starts as a copy of the one you come from
            scenes[(size_t) index] = scenes[(size_t) currentScene];
            if (savedScenes[(size_t) index].empty())
                savedScenes[(size_t) index] = scenes[(size_t) index];
        }
        currentScene = index;
        target = scenes[(size_t) index];
    }
    // only what differs: parameters already on the scene's value are left alone
    // (outside the lock: the host hears about these changes and may save meanwhile)
    for (const auto& [id, v] : target)
        if (auto* p = apvts.getParameter (id))
            if (std::abs (p->convertFrom0to1 (p->getValue()) - v) > 1.0e-5f * juce::jmax (1.0f, std::abs (v)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (v)));
                p->endChangeGesture();
            }
    syncSceneSnapshot();
    if (auto* p = apvts.getParameter (ParamIDs::scene))
        if (juce::roundToInt (p->convertFrom0to1 (p->getValue())) != index)
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
}

void PresetManager::copyCurrentSceneTo (int index)
{
    index = juce::jlimit (0, kScenes - 1, index);
    const juce::ScopedLock sl (lock);
    if (index != currentScene)
        scenes[(size_t) index] = captureSceneValues();
}

void PresetManager::syncSceneSnapshot()
{
    const juce::ScopedLock sl (lock);
    for (const auto& [id, v] : savedScenes[(size_t) currentScene])
        if (auto* p = apvts.getParameter (id))
            snapshot[id] = p->convertTo0to1 (v);
}

juce::var PresetManager::scenesToVar()
{
    const juce::ScopedLock sl (lock);
    scenes[(size_t) currentScene] = captureSceneValues();
    juce::Array<juce::var> list;
    for (const auto& sc : scenes)
    {
        auto* obj = new juce::DynamicObject();
        for (const auto& [id, v] : sc)
            obj->setProperty (id, v);
        list.add (juce::var (obj));
    }
    return list;
}

void PresetManager::scenesFromVar (const juce::var& v, int current)
{
    const juce::ScopedLock sl (lock);
    for (auto& sc : scenes) sc.clear();
    if (auto* list = v.getArray())
        for (int k = 0; k < juce::jmin (kScenes, list->size()); ++k)
            if (auto* obj = (*list)[k].getDynamicObject())
            {
                for (const auto& prop : obj->getProperties())
                    scenes[(size_t) k][prop.name.toString()] = (float) (double) prop.value;
                if (! scenes[(size_t) k].empty())
                    migrateLegacyValues (scenes[(size_t) k]);
            }
    currentScene = juce::jlimit (0, kScenes - 1, current);
    savedScenes = scenes;
}
