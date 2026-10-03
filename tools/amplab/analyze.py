"""Compare amp renders of the same DI: analyze.py <di.wav> <ref.wav> <cand.wav> [cand2.wav ...]
Reports level-matched differences: LTAS (1/3 oct), crest / envelope range, envelope slope vs the
reference, onset shape (attack peak vs 40 ms later), spectral centroid vs playing level, fizz ratio."""
import sys, numpy as np, soundfile as sf
from scipy.signal import fftconvolve, stft

SR = 48000
def load(p):
    d, sr = sf.read(p, dtype='float32')
    if d.ndim > 1: d = d[:, 0]
    assert sr == SR, (p, sr)
    return d.astype(np.float64)

def align(ref, x, maxlag=480):
    n = min(len(ref), len(x), SR * 20)
    c = fftconvolve(ref[:n], x[:n][::-1], 'full')
    mid = n - 1
    w = c[mid - maxlag: mid + maxlag + 1]
    lag = int(np.argmax(np.abs(w))) - maxlag       # x is late by lag
    if lag > 0: x = x[lag:]
    elif lag < 0: x = np.concatenate([np.zeros(-lag), x])
    m = min(len(ref), len(x))
    return ref[:m], x[:m], lag

def env_db(x, win=480):
    k = np.ones(win) / win
    e = np.sqrt(np.convolve(x * x, k, 'same'))
    return 20 * np.log10(e + 1e-9)

def ltas(x, fmin=50, fmax=16000):
    f, t, Z = stft(x, SR, nperseg=8192, noverlap=4096)
    P = (np.abs(Z) ** 2).mean(axis=1)
    centres, vals = [], []
    fc = fmin
    while fc < fmax:
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        m = (f >= lo) & (f < hi)
        centres.append(fc); vals.append(10 * np.log10(P[m].sum() + 1e-20))
        fc *= 2 ** (1 / 3)
    vals = np.array(vals); return np.array(centres), vals - 10 * np.log10(np.sum(10 ** (vals / 10)))

def centroid_vs_level(x, di):
    f, t, Z = stft(x, SR, nperseg=2048, noverlap=1024)
    P = np.abs(Z) ** 2
    cen = (P * f[:, None]).sum(0) / (P.sum(0) + 1e-20)
    lvl = env_db(di, 2048)[(np.arange(len(t)) * 1024).clip(0, len(di) - 1)]
    m = lvl > -45
    lvl, cen = lvl[m], np.log2(cen[m] + 1)
    # centroid (octaves) at the 20th / 80th percentile of the DI level
    lo, hi = np.percentile(lvl, 20), np.percentile(lvl, 80)
    cl = cen[np.abs(lvl - lo) < 2].mean(); ch = cen[np.abs(lvl - hi) < 2].mean()
    return 2 ** cl, 2 ** ch

def onsets(di):
    e = env_db(di, 96)
    on = []
    last = -SR
    for i in range(SR // 10, len(e) - SR // 5, 48):
        if e[i] > -30 and e[i] - e[i - 960] > 12 and i - last > SR // 8:
            on.append(i); last = i
    return on

def onset_shape(x, ons):
    e = env_db(x, 48)   # 1 ms
    seg = np.array([e[o - 240: o + 4800] for o in ons if o + 4800 < len(e)])
    seg -= seg[:, 240:480].max(axis=1, keepdims=True)   # relative to the attack peak
    m = seg.mean(0)
    return m[240 + 480], m[240 + 1920], m[240 + 4320]   # 10 / 40 / 90 ms after the attack

def report(di, ref, cands):
    ons = onsets(di)
    print(f"{'':20s} {'rms':>6s} {'crest':>6s} {'env95-10':>8s} {'slope':>6s} {'+10ms':>6s} {'+40ms':>6s} {'+90ms':>6s} {'cen@soft':>8s} {'cen@hard':>8s} {'fizz':>6s} {'ltasdiff':>8s}")
    rows = [('REF', ref)] + cands
    fr, Lr = ltas(ref)
    actAll = env_db(di) > -45
    for name, x in rows:
        if name != 'REF':
            _, x, lag = align(ref, x)
        m = min(len(x), len(ref), len(di)) - 4800
        x = x[:m]; act = actAll[:m]
        if name != 'REF':
            x = x * np.sqrt(np.mean(ref[:m][act] ** 2) / np.mean(x[act] ** 2))
        rms = 20 * np.log10(np.sqrt(np.mean(x ** 2)))
        crest = 20 * np.log10(np.abs(x).max() / np.sqrt(np.mean(x ** 2)))
        e = env_db(x)[act]
        rng = np.percentile(e, 95) - np.percentile(e, 10)
        ed = env_db(di[:m])[act]
        slope = np.polyfit(ed, e, 1)[0]
        a10, a40, a90 = onset_shape(x, ons)
        cs, chd = centroid_vs_level(x, di[:m])
        fc, L = ltas(x)
        fizz = 10 * np.log10(np.sum(10 ** (L[(fc >= 6000)] / 10)) / np.sum(10 ** (L[(fc >= 1000) & (fc < 4000)] / 10)))
        d = L - Lr
        dm = d[(fc >= 80) & (fc <= 10000)]
        print(f"{name:20s} {rms:6.1f} {crest:6.1f} {rng:8.1f} {slope:6.2f} {a10:6.1f} {a40:6.1f} {a90:6.1f} {cs:8.0f} {chd:8.0f} {fizz:6.1f} {np.sqrt(np.mean(dm**2)):8.1f}")
        if name != 'REF':
            print('    LTAS - ref (dB):', ' '.join(f"{int(f)}:{v:+.0f}" for f, v in zip(fc, d) if 60 <= f <= 12000))

if __name__ == '__main__':
    di = load(sys.argv[1]); ref = load(sys.argv[2])
    cands = [(p.split('/')[-1].replace('.wav', ''), load(p)) for p in sys.argv[3:]]
    n = min(len(di), len(ref), *[len(c[1]) for c in cands])
    report(di[:n], ref[:n], [(a, b[:n]) for a, b in cands])
