# Noise tricks — proposal (for the morning decision)

Date: 2026-10-03. Status: proposal, not built (except CRYPT FREEZE, which shipped on the branch).

## Why

The competitor's suite sells on "noise" modules (freeze, tape stop, ring mod, bit crush, glitch).
Swarmness already covers part of that ground inside its deep blocks (HIVE's STEPS with REVERSE /
RANDOM moves, SMOKE's GLARE, VENOM, WINGS patterns, the BUZZ phaser / ring-mod stage of the
pitch engine). The question is which few tricks are worth adding so the plug-in does not sprawl.

## Done

- **CRYPT FREEZE** — the reverb tail is held as a pad (lossless loop, no new input), MIDI-learnable.

## Proposed (in order of value / effort)

1. **STOP (tape stop) in HIVE** — a momentary move: the repeats slow down to a halt over 0.3–1.5 s
   (pitch falls with them) and come back when released. Fits the pitch engine (it already reads
   delay lines with modulation), needs one knob (TIME) and a MIDI-learnable button; a footswitch
   candidate. Effort: ~1 day with tests.
2. **CRUSH in SMOKE** — a bit / sample-rate reducer as SMOKE's fourth stage, one knob (CRUSH 0–100:
   16 → 4 bits and 48 → 4 kHz together, with the aliasing left in on purpose). Fits "fuzz" and avoids a
   new block. Effort: half a day.
3. **RING in SWARM** — the chorus gets a RING knob: the modulated voices are ring-modulated by the
   LFO rate (×1…×200 scaled), DEEP keeps its 8 voices. Alternative: a frequency shifter (needs a
   Hilbert pair, more CPU). Effort: ~1 day.
4. **GLITCH in HIVE** — a STEPS move "stutter" (the step repeats a 30–90 ms slice several times). HIVE's
   pattern grid already has moves, so it is one more symbol. Effort: half a day.

Not proposed: a separate "noise" block (sprawl), granular freeze (CRYPT FREEZE covers the use),
reverse delay (HIVE REVERSE already does it).

## Questions

- Which of 1–4, and in what order? My suggestion: 1 and 2 first (both are "performance" moves that
  footswitches can drive), then 3.
- Should STOP take over the VENOM footswitch's slot as a fifth footswitch, or stay a panel button?
