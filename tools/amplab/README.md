# Amp lab scripts

Development tools around `SwarmnessAmpLab` (Tests/AmpLab.cpp). Captures of real amps are measured, never shipped.
Python 3 with numpy, scipy, soundfile and cma (`pip install numpy scipy soundfile cma`).

- `fit.py <channel> <side> <ref.json> <gain> <name> [iters] [full|nonlinear] [--only nl] [--init fit.json] [--fix R1=1e5] [--pop N] [--seed N]`
  CMA-ES fit of one reference circuit to a capture's `SwarmnessAmpLab fitfeat` JSON: harmonics / THD / fizz share / compression
  vs level at 110 and 440 Hz, multitone response and IMD at three levels, burst recovery, riff crest factor.
  `nonlinear` holds the small-signal tone (the circuit's own `m60`) and fits the behaviour only. Writes `fit/<name>.json`.
- `apply_fit.py fit/<name>.json` prints the values as C++ lines for `Source/DSP/AmpCircuit.h`.
- `analyze.py <di.wav> <ref.wav> <cand.wav>...` compares renders of the same DI (`SwarmnessAmpLab render <target> in out`):
  crest, envelope range, compression slope, onset shape, spectral centroid vs level, fizz, 1/3-octave LTAS difference.
  Targets chain with `|`, e.g. `amp:1:0|cab:2:0.3:0.2` or `nam:capture.nam`, `ir:cab.wav`.
- `trims.py <render dir>` after `SwarmnessTests --render <dir>`: the VOLUME trim each factory preset needs to land at -16 dBFS RMS.

Typical round: `fitfeat` the capture, fit, `apply_fit`, paste into AmpCircuit.h, rebuild, `SwarmnessTests --only amp`
(level-match `outDb` so the three amps sit within 3 dB at noon), `--render` + `trims.py`, update `presetLevelTrims()`.
