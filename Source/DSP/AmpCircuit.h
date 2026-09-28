#pragma once

#include <JuceHeader.h>
#include <functional>

#include <array>
#include <cmath>
#include <complex>
#include <vector>

/**
 * The amp models of the AMP block: circuit-level simulations of six reference amps (original
 * models, voiced after the classic designs they are named for in the manual).
 *
 * Preamp - up to four gain stages, each simulated from its parts:
 *   - the coupling network in front of the grid: series capacitor, source resistance, grid
 *     leak resistor, and GRID CURRENT: when the grid swings above the cathode it conducts, the
 *     source resistance soaks up the peak and the coupling cap charges, so the stage biases
 *     itself colder after a hard hit and recovers over tens of milliseconds (the bloom, the
 *     compression, the sputter of a cranked tube amp - it follows the picking);
 *   - the 12AX7 itself (Koren's triode equations) on its load line: plate supply, plate and
 *     cathode resistors, cathode bypass (fully / partially bypassed or "cold" unbypassed), solved
 *     once into a transfer table;
 *   - Miller / plate filtering and the divider / treble-peaking network to the next stage;
 *   - solid-state stages (op-amp gain with rail clipping) for the jazz-clean reference.
 * Then (per amp) a cathode follower, the passive FMV tone stack (solved from its netlist) and
 * the MASTER.
 *
 * Power amp - long-tailed-pair phase inverter, a push-pull pair of pentodes in class AB (Child
 * law, crossover, saturation) with their own grid blocking, power-supply sag, the speaker's
 * impedance as the load (the resonance bump and the voice-coil rise) and global negative
 * feedback around it all: PRESENCE and DEPTH take feedback away at the top / bottom, exactly
 * like the feedback network of the real amps. Little feedback = loose and raw, lots = tight.
 *
 * CHARACTER morphs between the two references of a channel: every resistor, capacitor, voltage
 * and filter moves together, and the tube transfer curves cross-fade.
 */
namespace ampsim
{
    //==============================================================================
    // Parts of one reference amp
    struct StageDef
    {
        int type = 0;                          // 0 = not there, 1 = 12AX7 triode, 2 = solid state
        float B = 300.0f, Rp = 100e3f, Rk = 1.5e3f, fb = 0.0f, Rload = 470e3f;   // triode: supply, plate, cathode, unbypassed part, AC load
        float ssGain = 1.0f, ssRail = 11.0f;   // solid state
        float Rs = 68e3f, Rg = 1e6f, C = 0.0f; // coupling network (C = 0: direct)
        float cathDb = 0.0f, cathHz = 100.0f;  // partial cathode bypass: gain lost below cathHz
        float lpHz = 16000.0f;                 // Miller / plate filtering
        float div = 1.0f;                      // to the next stage
        float divShelfDb = 0.0f, divShelfHz = 1500.0f;   // treble-peaking cap across the divider
    };

    struct AmpDef
    {
        std::array<StageDef, 4> st;
        int gainPotAfter = 0;                 // the GAIN pot sits after this stage
        bool stackEarly = false;              // tone stack right after stage 1 (Fender style)
        float brightDb = 0.0f, brightHz = 2500.0f;   // bright cap across the GAIN pot
        float cf = 0.0f;                       // cathode follower in front of the stack
        float R1, R2, R3, R4, C1, C2, C3;      // FMV tone stack
        float voiceHz = 1000.0f, voiceDb = 0.0f, voiceQ = 0.8f;
        // power amp (normalised: the output tubes cut off at a grid drive of -1)
        float piMax = 1.6f;                    // phase inverter headroom
        float bias = 0.55f;                    // class AB idle point (1 = class A-ish)
        float hard = 0.0f;                     // 0 = tubes, 1 = solid state
        float satKnee = 2.2f;                  // plate saturation of each tube
        float nfb = 2.0f;                      // loop gain of the global feedback
        float sag = 0.3f, sagMs = 60.0f;       // rectifier / supply
        float presenceHz = 3000.0f, presenceMax = 0.8f, depthHz = 90.0f, depthMax = 0.7f;
        float spkHz = 100.0f, spkQ = 1.6f, spkDb = 9.0f, coilHz = 1800.0f, coilDb = 6.0f;
        float xfHp = 40.0f, xfLp = 14000.0f;
        float paRef = 1.0f;                    // volts at the phase inverter for full power
        float outDb = 0.0f;
        float gridKg = 3.0e-4f;                // preamp grid conduction (A / V^1.5): how hard the grids clamp
        float inDb = 0.0f;                     // input sensitivity (the amp's gain ahead of the first stage)
    };

    /** Development hook (the amp lab fits circuit values to measurements): edits a reference as it is built. */
    inline std::function<void (int channel, int side, AmpDef&)>& referenceTweak()
    {
        static std::function<void (int, int, AmpDef&)> f;
        return f;
    }

    /** The six references: [channel][side]. */
    inline AmpDef reference (int channel, int side)
    {
        constexpr float k = 1.0e3f, M = 1.0e6f, n = 1.0e-9f, p = 1.0e-12f;
        AmpDef a;
        auto triode = [] (float B, float Rk, float fb, float Rload, float Rs, float Rg, float C, float lpHz, float div)
        {
            StageDef s;
            s.type = 1; s.B = B; s.Rk = Rk; s.fb = fb; s.Rload = Rload; s.Rs = Rs; s.Rg = Rg; s.C = C; s.lpHz = lpHz; s.div = div;
            return s;
        };
        switch (channel * 2 + side)
        {
            case 0: // CHROME - solid-state jazz clean: op-amp preamp, flat active EQ, hard-clipping SS power amp
            {
                StageDef s1; s1.type = 2; s1.ssGain = 3.0f; s1.ssRail = 11.0f; s1.Rs = 10 * k; s1.Rg = 1 * M; s1.lpHz = 20000.0f;
                StageDef s2 = s1; s2.ssGain = 12.0f; s2.C = 1000 * n; s2.Rs = 10 * k; s2.lpHz = 18000.0f;
                a.st = { s1, s2, StageDef(), StageDef() };
                a.gainPotAfter = 0; a.stackEarly = true; a.brightDb = 3.0f; a.brightHz = 3000.0f;
                a.R1 = 250 * k; a.R2 = 250 * k; a.R3 = 50 * k; a.R4 = 10 * k; a.C1 = 680 * p; a.C2 = 47 * n; a.C3 = 47 * n;
                a.piMax = 3.0f; a.bias = 0.95f; a.hard = 1.0f; a.satKnee = 1.6f; a.nfb = 6.0f; a.sag = 0.0f;
                a.presenceHz = 4000.0f; a.presenceMax = 0.3f; a.depthHz = 90.0f; a.depthMax = 0.3f;
                a.spkHz = 95.0f; a.spkQ = 1.5f; a.spkDb = 8.0f; a.coilHz = 2000.0f; a.coilDb = 6.0f;
                a.xfHp = 30.0f; a.xfLp = 18000.0f; a.paRef = 0.12f; a.outDb = 0.0f;
                break;
            }
            case 1: // BLACKFACE - American clean / breakup: V1 -> tone stack -> volume -> V2 -> 6V6 pair, GZ34 sag
            {
                a.st[0] = triode (250.0f, 1.5f * k, 0.0f, 220 * k, 68 * k, 1 * M, 0.0f, 14000.0f, 1.0f);
                a.st[1] = triode (250.0f, 1.5f * k, 0.0f, 330 * k, 220 * k, 1 * M, 0.0f, 12000.0f, 0.45f);
                a.gainPotAfter = 0; a.stackEarly = true; a.brightDb = 6.0f; a.brightHz = 2200.0f;
                a.R1 = 250 * k; a.R2 = 250 * k; a.R3 = 10 * k; a.R4 = 100 * k; a.C1 = 250 * p; a.C2 = 100 * n; a.C3 = 47 * n;
                a.piMax = 1.7f; a.bias = 0.55f; a.hard = 0.0f; a.satKnee = 1.9f; a.nfb = 2.0f; a.sag = 0.35f; a.sagMs = 70.0f;
                a.presenceHz = 3500.0f; a.presenceMax = 0.5f; a.depthHz = 100.0f; a.depthMax = 0.5f;
                a.spkHz = 110.0f; a.spkQ = 1.8f; a.spkDb = 8.0f; a.coilHz = 1800.0f; a.coilDb = 5.0f;
                a.xfHp = 45.0f; a.xfLp = 13000.0f; a.paRef = 2.35f; a.outDb = -3.0f;
                break;
            }
            case 2: // BRIT - British crunch: hot V1a, cold-biased V1b, V2a, an extra stage, cathode follower, EL34s
            {
                a.st[0] = triode (300.0f, 2.7f * k, 0.0f, 1 * M, 68 * k, 1 * M, 0.0f, 22000.0f, 1.0f);
                a.st[0].cathDb = -6.5f; a.st[0].cathHz = 90.0f;
                a.st[1] = triode (300.0f, 10 * k, 1.0f, 470 * k, 150 * k, 470 * k, 22 * n, 10000.0f, 0.5f);
                a.st[1].divShelfDb = 6.0f; a.st[1].divShelfHz = 1200.0f;
                a.st[2] = triode (300.0f, 820.0f, 0.0f, 470 * k, 100 * k, 470 * k, 22 * n, 8000.0f, 0.12f);
                a.st[2].cathDb = -8.0f; a.st[2].cathHz = 280.0f;
                a.st[3] = triode (300.0f, 1.5f * k, 0.0f, 1 * M, 100 * k, 1 * M, 22 * n, 7000.0f, 1.0f);
                a.st[3].cathDb = -6.0f; a.st[3].cathHz = 150.0f;
                a.gainPotAfter = 0; a.brightDb = 6.0f; a.brightHz = 3000.0f; a.cf = 1.0f;
                a.R1 = 220 * k; a.R2 = 1 * M; a.R3 = 25 * k; a.R4 = 33 * k; a.C1 = 470 * p; a.C2 = 22 * n; a.C3 = 22 * n;
                a.piMax = 1.5f; a.bias = 0.5f; a.hard = 0.0f; a.satKnee = 1.8f; a.nfb = 1.6f; a.sag = 0.2f; a.sagMs = 50.0f;
                a.presenceHz = 2500.0f; a.presenceMax = 0.85f; a.depthHz = 100.0f; a.depthMax = 0.6f;
                a.spkHz = 90.0f; a.spkQ = 1.5f; a.spkDb = 8.0f; a.coilHz = 2000.0f; a.coilDb = 6.0f;
                a.xfHp = 40.0f; a.xfLp = 12000.0f; a.paRef = 27.80f; a.outDb = 4.0f;
                // fitted to captures of the real amps (SwarmnessAmpLab, JCM2000 / JVM / JCM900 crunch)
                a.gridKg = 6.567e-6f; a.inDb = 29.26f; a.bias = 0.6964f; a.satKnee = 1.246f; a.nfb = 1.868f; a.piMax = 1.283f;
                a.sag = 0.3238f; a.R1 = 1.594e5f; a.R2 = 1.929e6f; a.R3 = 2.144e4f; a.R4 = 3.877e4f; a.C1 = 1.825e-10f;
                a.C2 = 1.54e-8f; a.C3 = 7.252e-8f; a.voiceHz = 389.9f; a.voiceDb = -7.273f; a.voiceQ = 0.5008f;
                a.brightDb = 2.983f; a.brightHz = 4030.0f; a.presenceHz = 1639.0f; a.depthHz = 123.0f; a.spkHz = 79.42f;
                a.spkQ = 2.996f; a.spkDb = 0.5765f; a.coilHz = 3801.0f; a.coilDb = 0.8165f; a.xfHp = 27.9f; a.xfLp = 1.091e4f;
                a.st[0].Rk = 473.4f; a.st[0].fb = 0.1747f; a.st[0].Rs = 1.501e4f; a.st[0].cathDb = -13.61f; a.st[0].cathHz = 71.61f; a.st[0].lpHz = 2.197e4f; a.st[0].div = 0.3688f;
                a.st[1].Rk = 8818.0f; a.st[1].fb = 0.7286f; a.st[1].Rs = 4.699e5f; a.st[1].cathDb = -5.523f; a.st[1].cathHz = 38.55f; a.st[1].lpHz = 1.8e4f; a.st[1].C = 2.24e-9f; a.st[1].div = 0.8015f;
                a.st[2].Rk = 1433.0f; a.st[2].fb = 0.06965f; a.st[2].Rs = 4.632e5f; a.st[2].cathDb = -11.26f; a.st[2].cathHz = 381.8f; a.st[2].lpHz = 8116.0f; a.st[2].C = 1.108e-8f; a.st[2].div = 0.02234f;
                a.st[3].Rk = 3660.0f; a.st[3].fb = 0.578f; a.st[3].Rs = 9.092e4f; a.st[3].cathDb = -0.6461f; a.st[3].cathHz = 138.9f; a.st[3].lpHz = 2501.0f; a.st[3].C = 3.738e-9f;
                a.outDb += 15.2f;   // level-matched to the other models at noon
                break;
            }
            case 3: // CITRUS - thick British fuzz-crunch: four hot stages, big coupling caps, dark, low feedback
            {
                a.st[0] = triode (300.0f, 1.5f * k, 0.0f, 1 * M, 68 * k, 1 * M, 0.0f, 14000.0f, 1.0f);
                a.st[1] = triode (300.0f, 1.5f * k, 0.0f, 470 * k, 150 * k, 1 * M, 47 * n, 7000.0f, 0.15f);
                a.st[1].cathDb = -3.0f; a.st[1].cathHz = 60.0f;
                a.st[2] = triode (300.0f, 2.7f * k, 0.0f, 470 * k, 100 * k, 1 * M, 47 * n, 6000.0f, 0.06f);
                a.st[2].cathDb = -3.0f; a.st[2].cathHz = 80.0f;
                a.st[3] = triode (300.0f, 3.3f * k, 1.0f, 1 * M, 100 * k, 470 * k, 22 * n, 6000.0f, 1.0f);
                a.gainPotAfter = 0; a.brightDb = 1.0f; a.brightHz = 2500.0f; a.cf = 0.0f;
                a.R1 = 250 * k; a.R2 = 1 * M; a.R3 = 25 * k; a.R4 = 56 * k; a.C1 = 680 * p; a.C2 = 33 * n; a.C3 = 33 * n;
                a.voiceHz = 500.0f; a.voiceDb = 4.0f; a.voiceQ = 0.8f;
                a.piMax = 1.4f; a.bias = 0.45f; a.hard = 0.0f; a.satKnee = 1.7f; a.nfb = 0.9f; a.sag = 0.4f; a.sagMs = 60.0f;
                a.presenceHz = 3000.0f; a.presenceMax = 0.4f; a.depthHz = 110.0f; a.depthMax = 0.5f;
                a.spkHz = 95.0f; a.spkQ = 1.4f; a.spkDb = 9.0f; a.coilHz = 1800.0f; a.coilDb = 6.0f;
                a.xfHp = 35.0f; a.xfLp = 10500.0f; a.paRef = 9.40f; a.outDb = -2.5f;
                // fitted to captures of the real amps (SwarmnessAmpLab, Orange Rockerverb)
                a.gridKg = 7.468e-6f; a.inDb = 28.66f; a.bias = 0.3435f; a.satKnee = 1.682f; a.nfb = 1.377f; a.piMax = 1.048f;
                a.sag = 0.2854f; a.R1 = 3.031e4f; a.R2 = 5.817e5f; a.R3 = 2.549e4f; a.R4 = 1.373e5f; a.C1 = 2.23e-10f;
                a.C2 = 3.623e-8f; a.C3 = 1.079e-8f; a.voiceHz = 315.5f; a.voiceDb = 6.035f; a.voiceQ = 0.4475f;
                a.brightDb = 3.439f; a.brightHz = 4773.0f; a.presenceHz = 5114.0f; a.depthHz = 114.3f; a.spkHz = 93.52f;
                a.spkQ = 1.806f; a.spkDb = 6.476f; a.coilHz = 1710.0f; a.coilDb = 6.483f; a.xfHp = 30.01f; a.xfLp = 1.008e4f;
                a.st[0].Rk = 1562.0f; a.st[0].fb = 0.7438f; a.st[0].Rs = 3.916e4f; a.st[0].cathDb = -9.446f; a.st[0].cathHz = 926.6f; a.st[0].lpHz = 3014.0f; a.st[0].div = 0.4433f;
                a.st[1].Rk = 773.4f; a.st[1].fb = 0.1652f; a.st[1].Rs = 4.692e5f; a.st[1].cathDb = -13.22f; a.st[1].cathHz = 31.15f; a.st[1].lpHz = 2515.0f; a.st[1].C = 5.0e-10f; a.st[1].div = 0.456f;
                a.st[2].Rk = 514.0f; a.st[2].fb = 0.05505f; a.st[2].Rs = 8.494e4f; a.st[2].cathDb = -13.98f; a.st[2].cathHz = 48.24f; a.st[2].lpHz = 1.337e4f; a.st[2].C = 6.474e-10f; a.st[2].div = 0.6735f;
                a.st[3].Rk = 541.7f; a.st[3].fb = 0.973f; a.st[3].Rs = 3.82e5f; a.st[3].cathDb = -10.27f; a.st[3].cathHz = 31.57f; a.st[3].lpHz = 5621.0f; a.st[3].C = 9.844e-8f;
                a.outDb += -2.1f;   // level-matched to the other models at noon
                break;
            }
            case 4: // STEEL - tight American high gain: bass-cut first stages, a cold clipper, 6L6s with lots of feedback
            {
                a.st[0] = triode (300.0f, 1.8f * k, 0.0f, 1 * M, 68 * k, 1 * M, 0.0f, 16000.0f, 1.0f);
                a.st[0].cathDb = -7.0f; a.st[0].cathHz = 200.0f;
                a.st[1] = triode (300.0f, 1.8f * k, 0.0f, 470 * k, 150 * k, 1 * M, 1.5f * n, 9000.0f, 0.5f);
                a.st[1].cathDb = -6.0f; a.st[1].cathHz = 250.0f; a.st[1].divShelfDb = 4.0f; a.st[1].divShelfHz = 1500.0f;
                a.st[2] = triode (300.0f, 10 * k, 1.0f, 470 * k, 100 * k, 470 * k, 22 * n, 7000.0f, 0.4f);
                a.st[3] = triode (300.0f, 1.5f * k, 0.0f, 1 * M, 100 * k, 1 * M, 22 * n, 6500.0f, 1.0f);
                a.st[3].cathDb = -4.0f; a.st[3].cathHz = 120.0f;
                a.gainPotAfter = 0; a.brightDb = 4.0f; a.brightHz = 3000.0f; a.cf = 1.0f;
                a.R1 = 250 * k; a.R2 = 1 * M; a.R3 = 25 * k; a.R4 = 47 * k; a.C1 = 250 * p; a.C2 = 47 * n; a.C3 = 22 * n;
                a.piMax = 1.6f; a.bias = 0.55f; a.hard = 0.0f; a.satKnee = 2.2f; a.nfb = 2.5f; a.sag = 0.15f; a.sagMs = 40.0f;
                a.presenceHz = 3000.0f; a.presenceMax = 0.9f; a.depthHz = 120.0f; a.depthMax = 1.0f;
                a.spkHz = 95.0f; a.spkQ = 1.6f; a.spkDb = 10.0f; a.coilHz = 1800.0f; a.coilDb = 7.0f;
                a.xfHp = 45.0f; a.xfLp = 12000.0f; a.paRef = 39.50f; a.outDb = 4.0f;
                // fitted to captures of the real amps (SwarmnessAmpLab, 5150 / 6505+ lead channels)
                a.gridKg = 0.0004589f; a.inDb = 17.0f; a.bias = 0.9357f; a.satKnee = 2.138f; a.nfb = 0.8f; a.piMax = 2.295f;
                a.sag = 0.2293f; a.R1 = 6.214e5f; a.R2 = 1.126e6f; a.R3 = 1.779e4f; a.R4 = 3.771e4f; a.C1 = 1.177e-10f;
                a.C2 = 6.761e-8f; a.C3 = 6.552e-8f; a.voiceHz = 1380.0f; a.voiceDb = -4.076f; a.voiceQ = 0.7053f;
                a.brightDb = 4.413f; a.brightHz = 2766.0f; a.presenceHz = 1960.0f; a.depthHz = 121.1f; a.spkHz = 106.4f;
                a.spkQ = 2.548f; a.spkDb = 9.98f; a.coilHz = 1025.0f; a.coilDb = 0.1731f; a.xfHp = 23.84f; a.xfLp = 7835.0f;
                a.st[0].Rk = 1084.0f; a.st[0].fb = 0.8189f; a.st[0].Rs = 2.434e5f; a.st[0].cathDb = -4.589f; a.st[0].cathHz = 34.91f; a.st[0].lpHz = 1.354e4f; a.st[0].div = 0.852f;
                a.st[1].Rk = 2370.0f; a.st[1].fb = 0.1907f; a.st[1].Rs = 1.157e5f; a.st[1].cathDb = -6.123f; a.st[1].cathHz = 171.2f; a.st[1].lpHz = 3386.0f; a.st[1].C = 4.905e-8f; a.st[1].div = 0.9894f;
                a.st[2].Rk = 1739.0f; a.st[2].fb = 0.8711f; a.st[2].Rs = 3.991e5f; a.st[2].cathDb = -0.02684f; a.st[2].cathHz = 70.82f; a.st[2].lpHz = 2609.0f; a.st[2].C = 5.0e-10f; a.st[2].div = 0.6228f;
                a.st[3].Rk = 476.1f; a.st[3].fb = 0.4599f; a.st[3].Rs = 4.364e5f; a.st[3].cathDb = -12.81f; a.st[3].cathHz = 30.03f; a.st[3].lpHz = 9877.0f; a.st[3].C = 9.776e-8f;
                a.nfb = 1.3f;   // a little more feedback than the fit, so PRESENCE / DEPTH have a loop to work on
                a.outDb += 7.5f;   // level-matched to the other models at noon
                break;
            }
            default: // SLUDGE - loose modern high gain: four hot stages, big caps, tube-rectifier sag, deep resonance
            {
                a.st[0] = triode (300.0f, 1.5f * k, 0.0f, 1 * M, 68 * k, 1 * M, 0.0f, 18000.0f, 1.0f);
                a.st[1] = triode (300.0f, 1.5f * k, 0.0f, 470 * k, 150 * k, 1 * M, 22 * n, 12000.0f, 0.18f);
                a.st[2] = triode (300.0f, 1.5f * k, 0.0f, 470 * k, 100 * k, 1 * M, 22 * n, 10000.0f, 0.08f);
                a.st[2].cathDb = -3.0f; a.st[2].cathHz = 80.0f;
                a.st[3] = triode (300.0f, 2.7f * k, 0.5f, 1 * M, 100 * k, 470 * k, 22 * n, 9000.0f, 1.0f);
                a.gainPotAfter = 0; a.brightDb = 3.0f; a.brightHz = 3000.0f; a.cf = 1.0f;
                a.R1 = 250 * k; a.R2 = 1 * M; a.R3 = 25 * k; a.R4 = 33 * k; a.C1 = 500 * p; a.C2 = 22 * n; a.C3 = 22 * n;
                a.piMax = 1.6f; a.bias = 0.5f; a.hard = 0.0f; a.satKnee = 2.0f; a.nfb = 1.6f; a.sag = 0.5f; a.sagMs = 80.0f;
                a.presenceHz = 3500.0f; a.presenceMax = 0.9f; a.depthHz = 100.0f; a.depthMax = 1.0f;
                a.spkHz = 85.0f; a.spkQ = 1.4f; a.spkDb = 10.0f; a.coilHz = 1800.0f; a.coilDb = 7.0f;
                a.xfHp = 35.0f; a.xfLp = 13000.0f; a.paRef = 13.80f; a.outDb = -2.5f;
                // fitted to captures of the real amps (SwarmnessAmpLab, Mesa Rectifier-family, Badlander crush channel)
                a.gridKg = 3.715e-6f; a.inDb = 18.08f; a.bias = 0.304f; a.satKnee = 1.018f; a.nfb = 1.242f; a.piMax = 1.435f;
                a.sag = 0.4984f; a.R1 = 3.752e5f; a.R2 = 6.142e5f; a.R3 = 2.286e4f; a.R4 = 9913.0f; a.C1 = 1.041e-10f;
                a.C2 = 1.074e-8f; a.C3 = 2.133e-7f; a.voiceHz = 466.0f; a.voiceDb = 2.379f; a.voiceQ = 2.033f;
                a.brightDb = 0.8737f; a.brightHz = 2137.0f; a.presenceHz = 2952.0f; a.depthHz = 145.9f; a.spkHz = 103.6f;
                a.spkQ = 0.8913f; a.spkDb = 9.378f; a.coilHz = 3081.0f; a.coilDb = 11.79f; a.xfHp = 21.71f; a.xfLp = 9098.0f;
                a.st[0].Rk = 3215.0f; a.st[0].fb = 0.5298f; a.st[0].Rs = 6.623e4f; a.st[0].cathDb = -7.077f; a.st[0].cathHz = 529.8f; a.st[0].lpHz = 6099.0f; a.st[0].div = 0.9817f;
                a.st[1].Rk = 671.9f; a.st[1].fb = 0.0214f; a.st[1].Rs = 4.607e4f; a.st[1].cathDb = -5.284f; a.st[1].cathHz = 119.8f; a.st[1].lpHz = 2555.0f; a.st[1].C = 6.57e-8f; a.st[1].div = 0.4683f;
                a.st[2].Rk = 612.1f; a.st[2].fb = 0.6411f; a.st[2].Rs = 1.838e5f; a.st[2].cathDb = -13.83f; a.st[2].cathHz = 37.05f; a.st[2].lpHz = 1.984e4f; a.st[2].C = 1.169e-9f; a.st[2].div = 0.2608f;
                a.st[3].Rk = 470.1f; a.st[3].fb = 0.5161f; a.st[3].Rs = 4.697e5f; a.st[3].cathDb = -13.48f; a.st[3].cathHz = 64.09f; a.st[3].lpHz = 3931.0f; a.st[3].C = 5.664e-10f;
                a.outDb += 0.9f;   // level-matched to the other models at noon
                break;
            }
        }
        if (auto& tweak = referenceTweak())
            tweak (channel, side, a);
        return a;
    }

    //==============================================================================
    // 12AX7 (Koren) and the stage transfer tables
    inline double korenIp (double vgk, double vpk) noexcept
    {
        constexpr double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
        vpk = std::max (vpk, 1.0e-3);
        const double arg = kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk));
        const double e1 = vpk / kp * (arg > 30.0 ? arg : std::log1p (std::exp (arg)));
        return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;
    }

    struct Table
    {
        static constexpr float lo = -30.0f, hi = 30.0f;
        static constexpr int size = 16384;
        std::vector<float> y = std::vector<float> ((size_t) size + 3, 0.0f);   // one guard point each side
        float thr = 1000.0f;  // grid conduction starts here (the cathode's DC voltage; 1000 = never)

        static float xAt (int i) noexcept { return lo + (hi - lo) * (float) i / (float) size; }

        /** Cubic (Catmull-Rom) interpolation: smooth for the smallest signals too. */
        inline float operator() (float v) const noexcept
        {
            const float pos = (juce::jlimit (lo, hi, v) - lo) * ((float) size / (hi - lo));
            const int i = juce::jmin (size - 1, (int) pos);
            const float t = pos - (float) i;
            const float* p = y.data() + i;   // p[0] = point i - 1 (guard offset)
            const float c1 = 0.5f * (p[2] - p[0]);
            const float c2 = p[0] - 2.5f * p[1] + 2.0f * p[2] - 0.5f * p[3];
            const float c3 = 0.5f * (p[3] - p[0]) + 1.5f * (p[1] - p[2]);
            return ((c3 * t + c2) * t + c1) * t + p[1];
        }
        void set (int i, float v) noexcept { y[(size_t) i + 1] = v; }
        void finish() noexcept
        {
            y[0] = 2.0f * y[1] - y[2];
            y[(size_t) size + 2] = 2.0f * y[(size_t) size + 1] - y[(size_t) size];
        }
    };

    /** Plate voltage swing (AC) vs grid voltage, on the stage's AC load line. */
    inline Table makeTable (const StageDef& s)
    {
        Table t;
        if (s.type == 2)
        {
            // op-amp: gain with a firm rail (a little softer than a hard clip), inverting
            for (int i = 0; i <= Table::size; ++i)
            {
                const double g = s.ssGain * Table::xAt (i) / s.ssRail;
                t.set (i, (float) (-s.ssRail * g / std::pow (1.0 + std::pow (std::abs (g), 6.0), 1.0 / 6.0)));
            }
            t.finish();
            return t;
        }
        if (s.type != 1)
        {
            for (int i = 0; i <= Table::size; ++i)
                t.set (i, Table::xAt (i));
            t.finish();
            return t;
        }

        const double B = s.B, Rp = s.Rp, Rk = s.Rk;
        // DC operating point (grid at 0 V, cathode self-bias)
        double lo = 0.0, hi = B / Rp;
        for (int it = 0; it < 60; ++it)
        {
            const double I = 0.5 * (lo + hi);
            (korenIp (-I * Rk, B - I * (Rp + Rk)) > I ? lo : hi) = I;
        }
        const double I0 = 0.5 * (lo + hi), Vk0 = I0 * Rk, Vp0 = B - I0 * Rp;
        const double Rac = Rp * s.Rload / (Rp + s.Rload);
        t.thr = (float) Vk0;
        // Ip on the AC load line; the cathode follows Ip by its unbypassed part fb.
        // h(I) = Koren (I) - I falls with I: Newton from the neighbouring point, bisection as a fallback.
        auto h = [&] (double vg, double I)
        {
            const double vk = Vk0 + s.fb * (I - I0) * Rk;
            const double vp = std::max (1.0, Vp0 - (I - I0) * Rac);
            return korenIp (vg - vk, vp - vk) - I;
        };
        const double iMax = 4.0 * B / Rp;
        auto solve = [&] (double vg, double guess)
        {
            double I = guess;
            for (int it = 0; it < 12; ++it)
            {
                const double f = h (vg, I);
                if (std::abs (f) < 1.0e-12) return I;
                const double d = (h (vg, I + 1.0e-9) - f) / 1.0e-9;
                if (! (d < 0.0)) break;
                const double next = I - f / d;
                if (next < 0.0 || next > iMax) break;
                I = next;
            }
            if (std::abs (h (vg, I)) < 1.0e-10) return I;
            double a = 0.0, b = iMax;
            for (int it = 0; it < 60; ++it)
            {
                const double m = 0.5 * (a + b);
                (h (vg, m) > 0.0 ? a : b) = m;
            }
            return 0.5 * (a + b);
        };
        // walk out from the operating point both ways, so every guess is close
        const int zeroIndex = (int) std::lround ((0.0 - Table::lo) / (Table::hi - Table::lo) * Table::size);
        auto plate = [&] (double I) { return (float) (std::max (1.0, Vp0 - (I - I0) * Rac) - Vp0); };
        double guess = I0;
        for (int i = zeroIndex; i <= Table::size; ++i)
        {
            guess = solve (Table::xAt (i), guess);
            t.set (i, plate (guess));
        }
        guess = I0;
        for (int i = zeroIndex - 1; i >= 0; --i)
        {
            guess = solve (Table::xAt (i), guess);
            t.set (i, plate (guess));
        }
        t.finish();
        // relative to the quiescent point: 0 V in -> 0 V out
        const float zero = t (0.0f);
        for (auto& v : t.y) v -= zero;
        return t;
    }

    /** All reference tables, built once: [channel * 2 + side][stage]. */
    using TableSet = std::array<std::array<Table, 4>, 6>;
    inline TableSet buildTables()
    {
        TableSet t;
        for (int r = 0; r < 6; ++r)
        {
            const auto def = reference (r / 2, r % 2);
            for (int s = 0; s < 4; ++s)
                t[(size_t) r][(size_t) s] = makeTable (def.st[(size_t) s]);
        }
        return t;
    }
    inline TableSet& tableStore()
    {
        static TableSet built = buildTables();
        return built;
    }
    inline const TableSet& tables() { return tableStore(); }
    /** Amp lab only (after referenceTweak changed): rebuild in place, before any AmpBlock is prepared. */
    inline void rebuildTables() { tableStore() = buildTables(); }

    //==============================================================================
    // Small filters
    struct OnePole
    {
        float G = 0.0f, s = 0.0f;
        void set (double hz, double sr) noexcept
        {
            const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (1.0, sr * 0.49, hz) / sr);
            G = (float) (g / (1.0 + g));
        }
        inline float lp (float x) noexcept
        {
            const float v = (x - s) * G;
            const float y = v + s;
            s = y + v;
            return y;
        }
        inline float hp (float x) noexcept { return x - lp (x); }
    };

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void peak (double hz, double q, double db, double sr) noexcept
        {
            const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (5.0, sr * 0.45, hz) / sr;
            const double al = std::sin (w) / (2.0 * q), cw = std::cos (w);
            norm (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
        }
        void norm (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
        {
            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
        }
        inline float process (float xf) noexcept
        {
            const double x = xf, y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    /** Third-order IIR from the FMV stack (bilinear), double precision. */
    struct ToneStack
    {
        double B[4] {}, A[4] {}, z[3] {};
        void set (const double* b, const double* a, double fs) noexcept
        {
            const double c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
            double nb[4], na[4];
            nb[0] = b[0] + b[1]*c + b[2]*c2 + b[3]*c3;       na[0] = a[0] + a[1]*c + a[2]*c2 + a[3]*c3;
            nb[1] = 3*b[0] + b[1]*c - b[2]*c2 - 3*b[3]*c3;   na[1] = 3*a[0] + a[1]*c - a[2]*c2 - 3*a[3]*c3;
            nb[2] = 3*b[0] - b[1]*c - b[2]*c2 + 3*b[3]*c3;   na[2] = 3*a[0] - a[1]*c - a[2]*c2 + 3*a[3]*c3;
            nb[3] = b[0] - b[1]*c + b[2]*c2 - b[3]*c3;       na[3] = a[0] - a[1]*c + a[2]*c2 - a[3]*c3;
            for (int i = 0; i < 4; ++i) { B[i] = nb[i] / na[0]; A[i] = na[i] / na[0]; }
        }
        inline float process (float xf) noexcept
        {
            const double x = xf, y = B[0] * x + z[0];
            z[0] = B[1] * x - A[1] * y + z[1];
            z[1] = B[2] * x - A[2] * y + z[2];
            z[2] = B[3] * x - A[3] * y;
            return (float) y;
        }
    };

    // FMV tone stack: treble pot (wiper = output) fed by C1, slope resistor R4 feeding C2 (treble pot
    // bottom) and C3 (mid pot top), bass pot between them, mid pot to ground. Solved symbolically.
    inline void toneStackAnalog (double R1, double R2, double R3, double R4, double C1, double C2, double C3,
                                 float bass, float mid, float treble, double* b, double* a) noexcept
    {
        const double t = juce::jlimit (0.001, 0.999, (double) treble);
        const double m = juce::jlimit (0.002, 1.0, (double) mid);
        const double l = juce::jlimit (0.0005, 1.0, (std::exp (3.4 * (double) bass) - 1.0) / (std::exp (3.4) - 1.0));   // log taper
        b[0] = 0.0;
        b[1] = C1*R1*t + C1*R2*l + C1*R3*m + C2*R2*l + C2*R3*m + C3*R3*m;
        b[2] = C1*C2*R1*R2*l + C1*C2*R1*R3*m + C1*C2*R1*R4*t + C1*C2*R2*R4*l + C1*C2*R3*R4*m + C1*C3*R1*R3*m
             + C1*C3*R1*R4*t + C1*C3*R2*R3*l*m + C1*C3*R2*R4*l + C1*C3*R3*R4*m + C2*C3*R2*R3*l*m;
        b[3] = C1*C2*C3*R1*R2*R3*l*m + C1*C2*C3*R1*R2*R4*l*t + C1*C2*C3*R2*R3*R4*l*m;
        a[0] = 1.0;
        a[1] = C1*R1 + C1*R2*l + C1*R3*m + C2*R2*l + C2*R3*m + C2*R4 + C3*R3*m + C3*R4;
        a[2] = C1*C2*R1*R2*l + C1*C2*R1*R3*m + C1*C2*R1*R4 + C1*C2*R2*R4*l + C1*C2*R3*R4*m + C1*C3*R1*R3*m + C1*C3*R1*R4
             + C1*C3*R2*R3*l*m + C1*C3*R2*R4*l + C1*C3*R3*R4*m + C2*C3*R2*R3*l*m + C2*C3*R2*R4*l;
        a[3] = C1*C2*C3*R1*R2*R3*l*m + C1*C2*C3*R1*R2*R4*l + C1*C2*C3*R2*R3*R4*l*m;
    }

    inline std::complex<double> evalAnalog (const double* b, const double* a, double hz) noexcept
    {
        const std::complex<double> s (0.0, 2.0 * juce::MathConstants<double>::pi * hz);
        return (b[0] + s * (b[1] + s * (b[2] + s * b[3]))) / (a[0] + s * (a[1] + s * (a[2] + s * a[3])));
    }

    /**
     * Grid node: the source (through Rs) against the grid leak Rg and the grid-cathode diode
     * (Ig = kg (v - thr)^1.5). Above the threshold, with Req = Rs || Rg and c = Req kg, the excess
     * u = v - thr solves  u + c u^1.5 = a  (a = the linear solution's excess). Scaled by c^2 this
     * is one universal curve  z + z^1.5 = A,  tabulated once as w = sqrt (z) over s = sqrt (A).
     */
    struct GridCurve
    {
        static constexpr float sMax = 64.0f;
        static constexpr int size = 4096;
        std::array<float, (size_t) size + 3> w {};
        GridCurve()
        {
            for (int i = 0; i <= size; ++i)
            {
                const double sv = sMax * i / size, A = sv * sv;
                double x = std::min (sv, std::cbrt (A));     // w: w^2 + w^3 = A
                for (int it = 0; it < 40; ++it)
                    x -= (x * x + x * x * x - A) / std::max (1.0e-12, 2.0 * x + 3.0 * x * x);
                w[(size_t) i + 1] = (float) std::max (0.0, x);
            }
            w[0] = -w[2];
            w[(size_t) size + 2] = 2.0f * w[(size_t) size + 1] - w[(size_t) size];
        }
        inline float operator() (float A) const noexcept
        {
            const float sv = std::sqrt (A);
            if (sv >= sMax)
            {
                float x = std::cbrt (A);                     // far into conduction: two Newton steps
                for (int it = 0; it < 2; ++it)
                    x -= (x * x + x * x * x - A) / (2.0f * x + 3.0f * x * x);
                return x;
            }
            const float pos = sv * ((float) size / sMax);
            const int i = (int) pos;
            const float t = pos - (float) i;
            const float* p = w.data() + i;
            const float c1 = 0.5f * (p[2] - p[0]);
            const float c2 = p[0] - 2.5f * p[1] + 2.0f * p[2] - 0.5f * p[3];
            const float c3 = 0.5f * (p[3] - p[0]) + 1.5f * (p[1] - p[2]);
            return ((c3 * t + c2) * t + c1) * t + p[1];
        }
    };

    inline const GridCurve& gridCurve()
    {
        static const GridCurve curve;
        return curve;
    }

    /** The same with the per-stage constants precomputed: k = Rg / (Rs + Rg), c2 = (Req kg)^2. */
    inline float solveGridFast (float vin, float k, float thr, float c2, float invC2) noexcept
    {
        const float lin = vin * k;
        if (lin <= thr)
            return lin;
        static const GridCurve& curve = gridCurve();
        const float w = curve ((lin - thr) * c2);
        return thr + w * w * invC2;
    }

    inline float solveGrid (float vin, float Rs, float Rg, float thr, float kg) noexcept
    {
        const float lin = vin * Rg / (Rs + Rg);
        if (lin <= thr)
            return lin;
        const float c = Rs * Rg / (Rs + Rg) * kg, c2 = c * c;
        const float w = gridCurve() ((lin - thr) * c2);
        return thr + w * w / c2;
    }
}
