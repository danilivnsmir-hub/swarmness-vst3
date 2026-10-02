# HONEY — pedal-style sustainer compressor (design)

Date: 2026-10-02. Status: approved in conversation (option 1: a chain block; pedal character; name HONEY; LIMIT added on request).

## Why

The clean channel sounds dull and lifeless without compression; the competitor's suite ships one. A
pedal-style sustainer in front of the amp (or anywhere in the chain) adds sustain, evens out picking and
makes clean parts "sparkle".

## What

A new chain block **HONEY** (subtitle SUSTAIN), reorderable like every other block, with its own power
button, MIDI learn and automation. Default position: first in the chain (slot 8, before SMOKE). Off by
default, so existing sessions and presets sound exactly as before.

### Controls

| Knob | Range | Meaning |
|---|---|---|
| SUSTAIN | 0–100 % | threshold down and ratio up together, like a pedal: 0 ≈ 1.5:1 above −20 dBFS, 100 ≈ 10:1 above −45 dBFS |
| ATTACK | 0–100 % | 1–40 ms (log); slower lets the pick through before the squash |
| BLEND | 0–100 % | parallel blend, dry → compressed; 100 = compressed only |
| LEVEL | −12…+12 dB | on top of the automatic make-up, so SUSTAIN alone keeps the loudness about even |
| LIMIT | on/off | a fast peak limiter after the compressor: 0.2 ms attack, 60 ms release, ceiling −6 dBFS (the top of the IN meter's green zone), stereo-linked |

Parameter IDs: `hnOn`, `hnSustain`, `hnAttack`, `hnBlend`, `hnLevel`; chain `chainHoney` / `laneHoney`.
Automation group "Honey". All are preset / scene parameters.

### DSP (`Source/DSP/HoneyBlock.h`)

- Detector: side-chain high-passed at 120 Hz (the low E must not pump everything), peak/RMS blend
  (60 % RMS over 5 ms, 40 % peak), stereo-linked by the louder channel.
- Gain computer in dB: threshold T(s) = −20 − 25·s dBFS, ratio R(s) = 1.5 + 8.5·s² (s = SUSTAIN 0..1),
  soft knee 6 dB.
- Attack 1–40 ms (SUSTAIN-independent, exponential in the knob).
- Release programme-dependent, "optical": base 80 ms, up to 400 ms when gain reduction is deep
  (release time = 80 ms + 320 ms · min(1, GR/12 dB)); on a sudden drop of the input the release
  starts fast and slows down — no pumping on sustained notes, no hole after a chug.
- Make-up: M(s) = (−T(s) − 20)·(1 − 1/R(s)) · 0.8 dB, i.e. about 80 % of the static gain reduction of a
  −20 dBFS signal, then LEVEL. The output passes a soft safety `tanh` above −1 dBFS.
- BLEND: output = dry + blend·(wet − dry) (sample-aligned; the block has no latency).
- Idle when off (bit-transparent); on/off crossfaded over 10 ms like the other blocks.
- Metering: gain reduction (dB, 30 Hz) for the editor, lock-free atomic.

### UI

FX page, bottom row becomes HONEY | SWARM | WINGS (HONEY ≈ 400 px with four 72-px knobs). Panel title
"HONEY" with the usual LED; a thin gain-reduction bar (code-drawn, like the IN/OUT meters) under the
knobs, filling right-to-left in honey orange. Chain tile "HONEY / SUSTAIN". Tooltips, the help overlay
(a HONEY row) and the README (chain diagram, section, preset list) are updated.

### Presets

- Glass Clean and Thall Intro get HONEY on (SUSTAIN 40, ATTACK 60, BLEND 70) and a brighter cab mic
  (MIC 35 → 15): measured, the clean amp itself is flat and the 2x12 cab rolls off above 5 kHz.
- New Basics preset "Sticky Clean": HONEY into CHROME clean, SWARM light.

### Tests (`Tests/SwarmnessTests.cpp`)

- Off = bit-transparent (null test), zero latency.
- Static curve: a −10 dBFS sine at SUSTAIN 0 / 50 / 100 lands within 1 dB of the computed output level;
  SUSTAIN 100 keeps a −40 dBFS sine within 3 dB of a −10 dBFS one after make-up (sustain).
- Attack: at ATTACK 0 a 0 dBFS step is within 1 dB of its target after 5 ms; at ATTACK 100 the first
  5 ms stay at least 6 dB louder than the settled level (the pick gets through).
- No clicks: second-difference peak / rms of a compressed sine < 10 dB.
- Chain: HONEY sits first by default; a session without `chainHoney` opens with it first and off.
- CPU: HONEY adds < 1 % of a core.
