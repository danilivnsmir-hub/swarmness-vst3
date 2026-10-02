# UI simplification, levels 1 + 2 (design)

Date: 2026-10-03. Status: approved in conversation ("как ты предлагаешь, но мы не должны потерять ничего важного").

## Why

The interface feels heavy because of a flat hierarchy: 25–40 same-sized knobs on screen, a value label
under every one, pills scattered along the headers, five footswitches plus LINK switches. Simplify the
structure, keep every parameter reachable.

## Rules (nothing lost)

- Every parameter keeps its control. Nothing is removed from the plug-in, only from the first glance.
- Hidden controls sit under a **MORE** pill in the block's title row; the pill shows a dot while any
  hidden parameter differs from its default, so a tweak is never invisible.
- Knob values appear on hover / while dragging; "Always show values" in the preset "..." menu brings
  them back for good. Both are saved with the session (`uiMore`, `uiValues`).
- Primary knobs are large (88–100 px), secondary ones small (60–66 px) and quieter.

## What changes

| Block | Always visible | Under MORE |
|---|---|---|
| SMOKE | VOICE, FUZZ, TONE, CLEAN | SCOOP, GLARE, GATE, SAG, CRUSH |
| HIVE | PITCH, DRONE, SNAP; TRAILS, TIME/DIV, grid, STOP, DRY, SYNC; MANGLE, MIX, RAW | QUEEN, TRACKING; STOP time, TONE, GATE; DETUNE |
| SHIFT | SHIFT A, SHIFT B, RISE, FALL, MIX, STACK, SNAP, RAW | ANGER, FRENZY, BUZZ, DETUNE |
| CRYPT | type, FREEZE, MIX, DECAY, TONE | SIZE, PRE-DELAY, LOW CUT, MOD, DUCK |
| HONEY, SWARM, WINGS, WASP, AMP, CAB, EQ | unchanged (≤ 5 knobs or a display-led panel) | – |

- Footer: the LINK mini switches go away (their parameters stay: VENOM's wiring menu has SHIFT A / B).
- Knob sizes: primary vs secondary per block (see the table: the first two of each row are primary).
- Collapsed sections re-flow their visible knobs over the free width (more air), expanded = today's layout.

## Not changed

MOMENTARY / LATCH stays in the header (a live setting). The chain strip, pages, scenes, MIDI learn and
the help overlay are untouched; the help overlay gets one line about MORE and values.
