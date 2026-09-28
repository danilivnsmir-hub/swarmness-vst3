#include "PresetManager.h"
#include "../Parameters.h"
#include "../DSP/HiveBlock.h"

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
    // Swarmness 1.x wrote presets into the same folder (normalised values, no "plugin" tag).
    // They cannot be read any more: move them into a sub-folder instead of listing broken presets.
    int moved = 0;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*" + extension))
    {
        const auto json = juce::JSON::parse (f);
        if (json["plugin"].toString() == "Swarmness")
            continue;
        const auto oldDir = dir.getChildFile ("Swarmness 1.x (unsupported)");
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
    const juce::String recipes ("Recipes - The Noise, Slam, glitch");
    const juce::String basics ("Basics"), stingCat ("Shift - hold SHIFT A / B"), hiveCat ("Hive Voices & Trails"),
                       texture ("Smoke, Swarm & Wings"), attack ("Swarm Attack"), space ("Chain, EQ & Crypt"),
                       amps ("Amps & Cabs");
    // AMP channels: 0 CLEAN, 1 CRUNCH, 2 LEAD. CAB types: 0 1x12, 1 2x12, 2 4x12 BRIT, 3 4x12 MOD
    // Chain slots (lower = earlier). Defaults: SMOKE 10, PITCH 20, SWARM 30, WINGS 40, COMB 50, CARVE 60, CRYPT 70.
    const juce::String slotCrypt (Chain::slotIds[Chain::crypt]), slotCarve (Chain::slotIds[Chain::carve]),
                       slotComb (Chain::slotIds[Chain::comb]), slotSmoke (Chain::slotIds[Chain::smoke]);

    factoryPresets = {
        // ---------------------------------------------------------------- basics
        { "Init", basics, "Everything off: the plug-in is transparent. Start here.", {} },
        { "Clean Shift", basics, "Pure octave shifter on the clean modern engine (RAW off). Hold SHIFT A (+1 oct) / SHIFT B (+2 oct) for a clean, instant jump.",
          { { rise, 0 }, { fall, 0 }, { rbRaw, 0 } } },
        { "Shift Doubler", basics, "SHIFT MIX at 50%: hold a footswitch and the octave is added on top of your dry note (both at full level) instead of replacing it.",
          { { rise, 0 }, { fall, 0 }, { stingMix, 50 } } },
        { "Slow Rise", basics, "RISE and FALL: hold a footswitch and the pitch sweeps up over ~1 s; release and it slides back down over ~1.5 s.",
          { { rise, 950 }, { fall, 1500 } } },

        // ---------------------------------------------------------------- recipes
        { "Noise - Octave Panic", recipes, "The Noise (Tallon / Alpha Wolf) approach: SMOKE into a Whammy-style shift. Hold SHIFT A (+1 oct) or SHIFT B (+2 oct); ANGER = Panic, FRENZY = Chaos, BUZZ = Speed, RISE = Rise.",
          { { rise, 25 }, { fall, 40 }, { panic, 55 }, { chaos, 20 }, { speed, 35 },
            { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 80 }, { fuzzTone, 55 }, { fuzzScoop, 35 }, { fuzzSag, 30 } } },
        { "Noise - Whammy Sweep", recipes, "Slow RISE, fast FALL: hold SHIFT B and the note screams up two octaves; BUZZ adds the ring-mod / flanger grind. Tap the switch in rhythm for sirens.",
          { { rise, 220 }, { fall, 70 }, { panic, 30 }, { chaos, 8 }, { speed, 60 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 85 }, { fuzzTone, 50 }, { fuzzScoop, 45 } } },
        { "Noise - In-Key Chaos", recipes, "FRENZY with SNAP: the pitch jumps between 4ths, 5ths and octaves instead of random pitches - the chaos stays musical. Hold SHIFT A.",
          { { rise, 10 }, { chaos, 70 }, { panic, 15 }, { speed, 15 }, { shSnap, 1 },
            { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 75 }, { fuzzScoop, 30 } } },
        { "Slam - Semitone Clash", recipes, "Slam-style dissonance: a constant voice one semitone above, both at full level (MIX 50%), tight and clean, into a DOWN fuzz. SHIFT A / B add -1 / -2 oct layers (SHIFT MIX 50%).",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 1 }, { rbPrimary, 100 }, { rbTracking, 100 }, { rbMix, 50 },
            { shiftA, -12 }, { shiftB, -24 }, { stingMix, 50 }, { rise, 0 }, { fall, 0 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 80 }, { fuzzTone, 45 }, { fuzzScoop, 25 }, { fuzzGate, 10 } } },
        { "Slam - Minor Second Down", recipes, "A semitone BELOW plus its octave under it (QUEEN): heavier, darker clash for slams and breakdowns.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, -1 }, { rbPrimary, 90 }, { rbSecondary, 45 }, { rbTracking, 100 }, { rbMix, 50 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 85 }, { fuzzTone, 40 }, { fuzzScoop, 20 } } },
        { "Slam - Tritone Dread", recipes, "Tritone voice with a slightly sour DETUNE and a touch of ANGER - evil, beating dissonance that still tracks tightly.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 6 }, { rbPrimary, 75 }, { rbTracking, 100 }, { rbMix, 50 }, { rbDetune, 12 }, { panic, 20 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 80 }, { fuzzScoop, 30 } } },
        { "Slam - Sub Layer", recipes, "One and two octaves under the guitar as a constant layer (DRONE -12, QUEEN -24). Clean engine, tight tracking.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, -12 }, { rbPrimary, 80 }, { rbSecondary, 45 }, { rbTracking, 100 }, { rbMix, 50 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 75 }, { fuzzScoop, 30 } } },
        { "Glitch Steps", recipes, "STEPS 'Glitch': 16 repeats, each doing something else - up, backwards, random, silent - chopped short by GATE and locked to 1/16.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 55 }, { rbTracking, 90 }, { rbMagic, 70 }, { trailFill, 9 }, { trChop, 47 },
            { rbSync, 1 }, { rbDiv, 4 }, { rbTone, 60 }, { rbMix, 50 } } },
        { "Stutter Echo", recipes, "STEPS 'Stutter': short chopped echoes with gaps (GATE 45%), a fifth up now and then - rhythmic noise instead of a wash.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbTracking, 90 }, { rbMagic, 65 }, { trailFill, 6 }, { trChop, 58 },
            { rbSync, 1 }, { rbDiv, 3 }, { rbTone, 55 } } },
        { "Gallop Octaves", recipes, "STEPS 'Gallop' (x - x x): octave repeats in a gallop rhythm at 1/16 - for tremolo-picked riffs.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 55 }, { rbTracking, 85 }, { rbMagic, 60 }, { trailFill, 7 }, { trChop, 32 },
            { rbSync, 1 }, { rbDiv, 4 }, { rbTone, 60 } } },
        { "Offbeat Fifths", recipes, "STEPS 'Offbeat': only every second repeat sounds - fifths answering on the offbeat, 1/8 synced.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbTracking, 90 }, { rbMagic, 70 }, { trailFill, 8 },
            { rbSync, 1 }, { rbDiv, 3 }, { rbTone, 55 } } },

        // ----------------------------------------------------------------- SHIFT + MANGLE
        { "Killer Bee", stingCat, "The all-rounder: a bit of ANGER, FRENZY and BUZZ plus SMOKE in front. Hold SHIFT B (+2 oct) for the full shriek.",
          { { rise, 30 }, { panic, 45 }, { chaos, 30 }, { speed, 25 }, { fuzzOn, 1 }, { fuzz, 60 }, { fuzzTone, 55 }, { fuzzScoop, 50 } } },
        { "Angry Hive", stingCat, "ANGER only: the octave is torn into two detuned voices - sour, beating dissonance.",
          { { rise, 15 }, { panic, 85 } } },
        { "Frenzy", stingCat, "FRENZY only: the pitch jumps randomly around the octave, faster than your picking.",
          { { rise, 10 }, { chaos, 75 } } },
        { "Hornet Buzz", stingCat, "High BUZZ: all-pass feedback and AM turn the octave into metallic ring-mod noise.",
          { { rise, 0 }, { fall, 0 }, { speed, 92 } } },
        { "Stacked Octaves", stingCat, "STACK on: SHIFT A = +1 oct, SHIFT B = +2 oct. Hold A, then add B - a second voice splits off and both octaves ring together; let go of B and it merges back.",
          { { shStack, 1 }, { rise, 40 }, { fall, 120 }, { stingMix, 70 }, { panic, 10 } } },
        { "Power Stack", stingCat, "STACK with SHIFT A = -1 oct and SHIFT B = +7 (fifth): hold both for a sub-octave power chord out of one note, into SMOKE after it.",
          { { shStack, 1 }, { shiftA, -12 }, { shiftB, 7 }, { rise, 0 }, { fall, 30 }, { stingMix, 55 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 70 }, { fuzzScoop, 35 } } },
        { "Dive Bomb", stingCat, "SHIFT A = -1 oct, SHIFT B = -2 oct with a long RISE and a snappy FALL: hold for a slow dive, release to snap back. SMOKE after SWARM in the chain.",
          { { shiftA, -12 }, { shiftB, -24 }, { rise, 450 }, { fall, 60 }, { panic, 15 }, { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzzVoice, 0 }, { fuzz, 70 }, { fuzzTone, 35 }, { fuzzScoop, 30 } } },

        // ------------------------------------------------------------------ HIVE
        { "Harmony Fifth", hiveCat, "Tight, clean harmony on the modern engine (RAW off): a fifth above (DRONE) plus its octave (QUEEN).",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbSecondary, 25 }, { rbTracking, 95 }, { rbTone, 70 } } },
        { "Atonal Detune", hiveCat, "SNAP off: a quarter-tone-flat double. Instantly wrong in the best way.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -0.4f }, { rbPrimary, 85 }, { rbTracking, 100 }, { rbTone, 65 } } },
        { "Tone Clusters", hiveCat, "Low TRACKING: the harmony lags and repeats grains, smearing into rhythmic clusters.",
          { { rbOn, 1 }, { rbPitch, 5 }, { rbPrimary, 70 }, { rbSecondary, 20 }, { rbTracking, 8 }, { rbMagic, 20 }, { rbTime, 90 }, { rbTone, 55 } } },
        { "Honey Ladder", hiveCat, "TRAILS synced to 1/8 notes: an octave ladder that climbs in time with the song.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 50 }, { rbTracking, 85 }, { rbMagic, 65 }, { rbSync, 1 }, { rbDiv, 3 }, { rbTone, 60 } } },
        { "Descending Spiral", hiveCat, "Negative PITCH with TRAILS: notes fall away in a spiral of fourths.",
          { { rbOn, 1 }, { rbPitch, -5 }, { rbPrimary, 65 }, { rbTracking, 70 }, { rbMagic, 70 }, { rbTime, 240 }, { rbTone, 40 } } },
        { "Drowning Hive", hiveCat, "Atonal down-shift, loose tracking and long TRAILS through the deep SWARM: moaning, gurgling.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -1.7f }, { rbPrimary, 70 }, { rbSecondary, 30 }, { rbTracking, 35 },
            { rbMagic, 85 }, { rbTime, 320 }, { rbTone, 30 }, { swarmOn, 1 }, { swarmDeep, 1 }, { swarmMix, 40 } } },
        { "Bounce Trill", hiveCat, "STEPS 'Bounce' (DOWN / UP): the repeats flip between a fifth up and your note - a trill that stays in key and fades out.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 60 }, { rbTracking, 85 }, { rbMagic, 70 }, { trailFill, 1 }, { rbTime, 150 }, { rbTone, 60 } } },
        { "Scatter Swarm", hiveCat, "STEPS 'Scatter' (all RANDOM): every repeat jumps to a random chord tone of the fifth, octaves up and down - glitch arpeggios. Add FRENZY for pure chaos.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 55 }, { rbTracking, 80 }, { rbMagic, 75 }, { trailFill, 2 }, { rbSync, 1 }, { rbDiv, 4 }, { rbTone, 60 } } },
        { "Reverse Hive", hiveCat, "STEPS 'Reverse': an octave voice whose repeats play backwards - ghostly swells that breathe in before every note.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 55 }, { rbTracking, 85 }, { rbMagic, 65 }, { trailFill, 3 }, { rbTime, 380 }, { rbTone, 50 }, { rbMix, 60 } } },
        { "Swell Ladder", hiveCat, "STEPS 'Swell': every repeat climbs a fifth AND gets louder - the ladder fades in instead of out.",
          { { rbOn, 1 }, { rbPitch, 7 }, { rbPrimary, 50 }, { rbSecondary, 20 }, { rbTracking, 90 }, { rbMagic, 70 }, { trailFill, 4 }, { rbTime, 260 }, { rbTone, 45 } } },
        { "Angry Voices", hiveCat, "MANGLE on the voices too: ANGER and FRENZY sour and scatter the harmony and its trails - one character for the whole block.",
          { { rbOn, 1 }, { rbPitch, 5 }, { rbPrimary, 65 }, { rbSecondary, 25 }, { rbTracking, 70 }, { rbMagic, 40 }, { panic, 45 }, { chaos, 25 } } },
        { "Pitch Delay", hiveCat, "HIVE as a delay: DRY feeds the repeats from your note, every repeat climbs a fifth (all UP) - a Rainbow-Machine-style pitch delay. DRONE off.",
          { { rbOn, 1 }, { trDry, 1 }, { rbPrimary, 0 }, { rbPitch, 7 }, { rbTracking, 100 }, { rbMagic, 60 }, { rbTime, 320 }, { rbTone, 55 }, { rbMix, 50 } } },
        { "Dotted Echo", hiveCat, "A plain dotted-1/8 delay out of HIVE: DRY + all-HOLD steps (FILL 'Echo'), synced. RAW gives the repeats tape-like wobble.",
          { { rbOn, 1 }, { trDry, 1 }, { rbPrimary, 0 }, { rbTracking, 100 }, { rbMagic, 55 }, { trailFill, 5 }, { rbSync, 1 }, { rbDiv, 9 }, { rbTone, 45 }, { rbMix, 45 } } },
        { "Venom Overload", hiveCat, "Long octave TRAILS. Hold the VENOM footswitch: it takes off into self-oscillating squalls and drags SHIFT A in (LINK).",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 50 }, { rbTracking, 60 }, { rbMagic, 92 }, { rbTone, 60 }, { linkOct1, 1 } } },

        // ------------------------------------------------------------- textures
        { "Swarm Cloud", texture, "DEEP SWARM over a barely-detuned double: wide, seasick, huge.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, 0.2f }, { rbPrimary, 40 }, { rbTracking, 90 },
            { swarmOn, 1 }, { swarmDeep, 1 }, { swarmDepth, 75 }, { swarmRate, 0.35f }, { swarmMix, 55 } } },
        { "Swollen Smoke", texture, "Jumbo fuzz: MID voice, huge sustain and a deep SCOOP - the wall-of-fuzz starting point.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 90 }, { fuzzTone, 45 }, { fuzzScoop, 75 }, { fuzzSag, 55 } } },
        { "Doom Cathedral", texture, "DOWN voice: crushing low-mids and the full bottom end, flat mids, a little CLEAN under it and a lot of SAG - every note sags and blooms.",
          { { fuzzOn, 1 }, { fuzzVoice, 0 }, { fuzz, 85 }, { fuzzTone, 35 }, { fuzzScoop, 15 }, { fuzzBlend, 20 }, { fuzzSag, 80 } } },
        { "Glare Scream", texture, "UP voice with GLARE: tight, screaming upper mids and a gated octave-up that rips on hard picking.",
          { { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 75 }, { fuzzTone, 60 }, { fuzzScoop, 30 }, { fuzzGlare, 70 }, { fuzzSag, 15 } } },
        { "Smoked Out", texture, "SMOKE with GATE and heavy SAG: a dying battery - notes sag, bloom, then sputter and tear apart as they decay.",
          { { fuzzOn, 1 }, { fuzz, 85 }, { fuzzGate, 75 }, { fuzzTone, 45 }, { fuzzScoop, 50 }, { fuzzSag, 90 } } },
        { "Wing Beat Breakdown", texture, "Tempo-synced 1/16 hard WINGS gate on SMOKE. Hold SHIFT A for angry stabs.",
          { { rise, 0 }, { fall, 0 }, { panic, 30 }, { fuzzOn, 1 }, { fuzzVoice, 0 }, { fuzz, 80 }, { fuzzScoop, 60 }, { fuzzBlend, 15 }, { flowOn, 1 }, { flowSync, 1 }, { flowDiv, 4 }, { flowHard, 1 } } },
        { "Ghost Swarm", texture, "Smooth WINGS tremolo, a quiet octave-up DRONE with trails and SWARM - eerie clean parts.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 30 }, { rbMagic, 35 }, { rbTime, 300 }, { rbTracking, 85 }, { rbTone, 45 },
            { swarmOn, 1 }, { swarmMix, 35 }, { flowOn, 1 }, { flowHard, 0 }, { flowSpeed, 5.5f }, { flowAmount, 70 } } },

        // ---------------------------------------------------------- swarm attack
        { "Broken Radio", attack, "Atonal loose HIVE, dark SMOKE after it and WINGS tremolo: a dying transmission.",
          { { rbOn, 1 }, { rbSnap, 0 }, { rbPitch, -2.6f }, { rbPrimary, 80 }, { rbTracking, 15 }, { rbTone, 30 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzz, 45 }, { fuzzTone, 20 },
            { flowOn, 1 }, { flowHard, 0 }, { flowSpeed, 6.0f }, { flowAmount, 60 } } },
        { "Hive Collapse", attack, "Everything at once. HIVE runs INTO SHIFT here: one stomp on VENOM and the self-oscillating trails, gated SMOKE and all get dragged two octaves up (LINK to SHIFT B).",
          { { panic, 40 }, { chaos, 40 }, { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 60 }, { rbMagic, 100 }, { rbTracking, 55 },
            { juce::String (Chain::slotIds[Chain::shift]), 25 },
            { fuzzOn, 1 }, { slotSmoke, 35 }, { fuzz, 90 }, { fuzzScoop, 60 }, { fuzzGlare, 40 }, { fuzzGate, 30 }, { linkOct2, 1 } } },

        // ------------------------------------------------------- chain, EQ & CRYPT
        { "Crypt Doom", space, "DOWN SMOKE into a huge dark ABYSS. CARVE cuts the mud under the fuzz and DUCK keeps the riffs clear - the tail blooms in the gaps.",
          { { fuzzOn, 1 }, { fuzzVoice, 0 }, { fuzz, 85 }, { fuzzTone, 40 }, { fuzzScoop, 20 }, { fuzzSag, 70 },
            { peqOn, 1 }, { peqHpFreq, 70 }, { peqB1Freq, 300 }, { peqB1Gain, -3 }, { peqB1Q, 1.2f }, { peqLpFreq, 9000 },
            { revOn, 1 }, { revType, 3 }, { revDecay, 7 }, { revMix, 32 }, { revTone, 35 }, { revLowCut, 180 }, { revDuck, 55 }, { revPreDelay, 40 } } },
        { "Smoke in the Crypt", space, "Chain reordered: CRYPT runs INTO SMOKE. The fuzz eats the whole reverb - a sustaining, blooming wall of noise.",
          { { slotCrypt, 5 }, { revOn, 1 }, { revType, 2 }, { revDecay, 3.5f }, { revMix, 45 }, { revTone, 45 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 80 }, { fuzzScoop, 55 }, { fuzzSag, 40 } } },
        { "Tight Before Smoke", space, "CARVE moved in front of SMOKE: low cut and a mid push tighten the fuzz like a boost before an amp - palm mutes stay articulate.",
          { { slotCarve, 5 }, { peqOn, 1 }, { peqHpFreq, 120 }, { peqB2Freq, 900 }, { peqB2Gain, 5 }, { peqB2Q, 0.8f },
            { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 70 }, { fuzzScoop, 35 }, { fuzzGate, 10 } } },
        { "Comb Smile", space, "COMB after SMOKE: the classic scooped 'smile' - deep lows, carved mids, sizzling top.",
          { { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 75 }, { fuzzScoop, 20 },
            { geqOn, 1 }, { geqBands[1], 3 }, { geqBands[2], 4 }, { geqBands[3], 1 }, { geqBands[4], -4 },
            { geqBands[5], -5 }, { geqBands[6], -2 }, { geqBands[7], 3 }, { geqBands[8], 2 }, { geqBands[9], -6 } } },
        { "Hive Cathedral", space, "Octave-up HIVE voices with TRAILS drowning in a bright PLATE - choir-like, holy and wrong.",
          { { rbOn, 1 }, { rbPitch, 12 }, { rbPrimary, 45 }, { rbSecondary, 20 }, { rbMagic, 45 }, { rbTime, 260 }, { rbTracking, 85 }, { rbTone, 55 },
            { revOn, 1 }, { revType, 1 }, { revDecay, 5 }, { revMix, 45 }, { revTone, 65 }, { revMod, 50 }, { revPreDelay, 25 } } },
        { "Parallel Smoke", space, "SMOKE on parallel path A, path B left empty = your dry signal. The A/B MIX at the merge blends clean attack and low end under a full fuzz.",
          { { juce::String (Chain::laneIds[Chain::smoke]), 1 }, { Chain::parallelMixIds[0], 35 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 90 }, { fuzzScoop, 60 }, { fuzzSag, 50 } } },
        { "Split Swarm", space, "Parallel paths: SWARM on A, CRYPT on B - the chorus stays tight and the reverb never smears it; both meet at the merge.",
          { { juce::String (Chain::laneIds[Chain::swarm]), 1 }, { juce::String (Chain::laneIds[Chain::crypt]), 2 }, { Chain::parallelMixIds[0], 45 },
            { swarmOn, 1 }, { swarmDepth, 70 }, { swarmRate, 0.8f }, { swarmMix, 50 },
            { revOn, 1 }, { revType, 2 }, { revDecay, 4 }, { revMix, 100 }, { revTone, 55 } } },
        { "Twin Splits", space, "Two splits: [SMOKE || dry] -> SHIFT, HIVE -> [SWARM || CRYPT]. Parallel fuzz with clean punch, then chorus and reverb side by side.",
          { { juce::String (Chain::laneIds[Chain::smoke]), 1 }, { Chain::parallelMixIds[0], 35 },
            { juce::String (Chain::laneIds[Chain::swarm]), 1 }, { juce::String (Chain::laneIds[Chain::crypt]), 2 }, { Chain::slotIds[Chain::crypt], 35 },
            { Chain::parallelMixIds[1], 40 },
            { fuzzOn, 1 }, { fuzzVoice, 1 }, { fuzz, 85 }, { fuzzScoop, 55 },
            { swarmOn, 1 }, { swarmDepth, 65 }, { swarmMix, 50 },
            { revOn, 1 }, { revType, 2 }, { revDecay, 3.5f }, { revMix, 100 } } },
        // ------------------------------------------------------------- amps & cabs (CLEAN = BLACKFACE, CRUNCH = BRIT, LEAD = STEEL)
        { "Glass Clean", amps, "CLEAN at low gain: glassy, scooped American clean into an open 2x12, a little SWARM and a HALL behind it.",
          { { ampOn, 1 }, { ampChannel, 0 }, { ampGain, 3.5f }, { ampBass, 4.5f }, { ampMid, 5 }, { ampTreble, 6 }, { ampLevel, 9 }, { ampMaster, 4.5f },
            { cabOn, 1 }, { cabType, 1 }, { cabMic, 35 }, { cabDist, 30 },
            { swarmOn, 1 }, { swarmMix, 30 }, { swarmDepth, 40 }, { revOn, 1 }, { revType, 2 }, { revMix, 20 }, { revDecay, 2.8f } } },
        { "Blackface Breakup", amps, "CLEAN pushed: sparkly when you play soft, hair and sag when you dig in. 1x12 open back.",
          { { ampOn, 1 }, { ampChannel, 0 }, { ampGain, 7 }, { ampBass, 4.5f }, { ampMid, 5 }, { ampTreble, 6.5f }, { ampLevel, -7.5f }, { ampMaster, 6.5f },
            { cabOn, 1 }, { cabType, 0 }, { cabMic, 40 }, { cabDist, 15 }, { revOn, 1 }, { revType, 0 }, { revMix, 18 } } },
        { "Brit Crunch", amps, "CRUNCH: bright, barking upper mids and a tight bottom - rock rhythm into the British 4x12. Roll the guitar's volume back to clean it up.",
          { { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 5 }, { ampBass, 5 }, { ampMid, 7 }, { ampTreble, 6.5f }, { ampPresence, 6 }, { ampLevel, -5 }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 10 } } },
        { "Brit Doom", amps, "CRUNCH with the gain up and the lows in: thick, compressed low-mids for doom and stoner riffs. DEPTH for the thump, a dark ABYSS in the gaps.",
          { { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 8 }, { ampBass, 7 }, { ampMid, 6.5f }, { ampTreble, 4.5f }, { ampDepth, 7 }, { ampMaster, 7 }, { ampLevel, -12.5f },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 45 }, { cabDist, 25 },
            { revOn, 1 }, { revType, 3 }, { revMix, 20 }, { revDecay, 6 }, { revDuck, 60 }, { revLowCut, 200 } } },
        { "Steel Lead", amps, "LEAD: tight, cutting American high gain. GATE keeps the stops clean, PRESENCE and DEPTH up for modern metal.",
          { { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 5 }, { ampBass, 5 }, { ampMid, 5 }, { ampTreble, 6.5f }, { ampPresence, 5.5f }, { ampDepth, 6 },
            { ampGate, 35 }, { ampMaster, 5.5f }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 }, { cabLowCut, 80 } } },
        { "Hornet Lead", amps, "WASP boosting the LEAD amp, bright and tight, with the modern 4x12 miked close to the cap: the rig dialled in by ear on beta.2.",
          { { input, 3 }, { drvOn, 1 }, { drvDrive, 1 }, { drvVolume, 7 }, { drvBright, 7 }, { drvAttack, 5 }, { drvGate, 40 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 5 }, { ampBass, 3.6f }, { ampMid, 4 }, { ampTreble, 7 }, { ampPresence, 5 }, { ampDepth, 6.6f },
            { ampLevel, -2.5f }, { ampMaster, 6 }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 15 }, { cabDist, 25 }, { cabLowCut, 60 }, { cabLevel, 4.9f } } },
        { "Wasp Boost", amps, "The classic tight metal boost: WASP at DRIVE 0 with VOLUME up in front of the LEAD amp - the lows tightened before the amp, "
                              "more bite and a faster attack. Switch WASP off to hear the difference.",
          { { drvOn, 1 }, { drvDrive, 0.5f }, { drvVolume, 7.5f }, { drvBright, 5.5f }, { drvAttack, 6.5f },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 4.5f }, { ampBass, 5.5f }, { ampMid, 5.5f }, { ampTreble, 6 }, { ampPresence, 6 }, { ampDepth, 6 },
            { ampGate, 40 }, { ampMaster, 5.5f }, { ampLevel, 5 }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 }, { cabLowCut, 80 } } },
        { "Wasp Crunch", amps, "WASP as the drive: DRIVE up into the CRUNCH amp on the edge - a thick, compressed rock rhythm with the pedal doing half the work.",
          { { drvOn, 1 }, { drvDrive, 6 }, { drvVolume, 5.5f }, { drvBright, 5 }, { drvAttack, 4 },
            { ampOn, 1 }, { ampChannel, 1 }, { ampGain, 3.5f }, { ampBass, 5 }, { ampMid, 6.5f }, { ampTreble, 6 }, { ampMaster, 6 },
            { cabOn, 1 }, { cabType, 2 }, { cabMic, 30 }, { cabDist, 15 } } },
        { "Sludge Wall", amps, "LEAD scooped and huge: low MID, BASS and DEPTH up, the mic backed off the grille. Slow riffs, big stops.",
          { { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 6 }, { ampBass, 7 }, { ampMid, 3 }, { ampTreble, 6 }, { ampPresence, 5.5f }, { ampDepth, 7.5f },
            { ampGate, 30 }, { ampMaster, 6.5f }, { ampLevel, -5.5f }, { cabOn, 1 }, { cabType, 3 }, { cabMic, 35 }, { cabDist, 40 } } },
        { "Octave Panic Rig", amps, "The Noise, the whole rig: SHIFT's octaves with ANGER / FRENZY / BUZZ in front of a STEEL lead amp. Hold SHIFT A / B.",
          { { rise, 25 }, { fall, 40 }, { panic, 50 }, { chaos, 20 }, { speed, 30 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 7 }, { ampMid, 6.5f }, { ampTreble, 6 }, { ampGate, 30 },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 25 }, { cabDist, 10 } } },
        { "Slam Rig", amps, "Slam dissonance through a real rig: a voice one semitone up (MIX 50%) into the STEEL lead amp and a 4x12 - the clash gets the amp's grind.",
          { { rbOn, 1 }, { rbRaw, 0 }, { rbPitch, 1 }, { rbPrimary, 100 }, { rbTracking, 100 }, { rbMix, 50 },
            { ampOn, 1 }, { ampChannel, 2 }, { ampGain, 7 }, { ampBass, 6.5f }, { ampMid, 4 }, { ampTreble, 5.5f }, { ampGate, 45 },
            { cabOn, 1 }, { cabType, 3 }, { cabMic, 30 }, { cabDist, 15 } } },

        { "Chug Room", space, "SMOKE with a tight ROOM that ducks hard while you play - space between the chugs, dry and punchy on them.",
          { { fuzzOn, 1 }, { fuzzVoice, 2 }, { fuzz, 70 }, { fuzzScoop, 45 }, { fuzzGate, 15 },
            { revOn, 1 }, { revType, 0 }, { revDecay, 1.1f }, { revMix, 35 }, { revDuck, 80 }, { revLowCut, 250 } } },
    };

    // expand the step-pattern shortcuts
    for (auto& fp : factoryPresets)
        migrateLegacyValues (fp.values);
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
    for (auto& sc : scenes) sc.clear();
    for (auto& sc : savedScenes) sc.clear();
    currentScene = 0;
    if (auto* p = apvts.getParameter (ParamIDs::scene))
        p->setValueNotifyingHost (0.0f);
}

void PresetManager::loadScenesFromJson (const juce::var& json)
{
    resetScenes();
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
    return index == currentScene || ! scenes[(size_t) juce::jlimit (0, kScenes - 1, index)].empty();
}

void PresetManager::selectScene (int index)
{
    index = juce::jlimit (0, kScenes - 1, index);
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
    // only what differs: parameters already on the scene's value are left alone
    for (const auto& [id, v] : scenes[(size_t) index])
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
