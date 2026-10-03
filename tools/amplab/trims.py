"""trims.py <render dir> [target dB] -> prints the VOLUME trim change per preset (whole-file RMS of the render)."""
import sys, glob, os, json, re, soundfile as sf, numpy as np
d = sys.argv[1]; target = float(sys.argv[2]) if len(sys.argv) > 2 else -16.0
src = open(os.path.join(os.path.dirname(__file__), '..', '..', 'Source', 'Preset', 'PresetManager.cpp')).read()
cur = dict(re.findall(r'\{ "([^"]+)", (-?[0-9.]+)f \}', src.split('static const std::map<juce::String, float> trims {')[1].split('};')[0]))
out = {}
for f in sorted(glob.glob(os.path.join(d, '*.wav'))):
    x, sr = sf.read(f, dtype='float32'); name = os.path.basename(f)[:-4]
    rms = 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)
    out[name] = float(rms)
    old = float(cur.get(name, 0.0))
    if abs(rms - target) > 0.7:
        print(f"{name:28s} {rms:6.1f} dB  trim {old:5.1f} -> {old + target - rms:5.1f}")
json.dump(out, open(os.path.join(d, 'levels.json'), 'w'), indent=0)
print('presets:', len(out), ' within 0.7 dB of target:', sum(abs(v - target) <= 0.7 for v in out.values()))
