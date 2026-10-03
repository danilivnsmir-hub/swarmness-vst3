"""CMA-ES fit of one reference circuit to a capture's fitfeat JSON.
   fit.py <channel> <side> <ref.json> <ampknobs e.g. 5 or 7> <out-name> [iters] [mode: full|nonlinear] [--seed N]
   nonlinear: the small-signal response (m60) is held to the circuit's own current one (the tone stays)."""
import sys, json, math, subprocess, numpy as np, cma, os, time
from concurrent.futures import ThreadPoolExecutor
LAB = os.environ.get('LAB', os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'SwarmnessAmpLab_artefacts', 'Release', 'SwarmnessAmpLab'))
ch, side, refPath, knobs, outName = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
iters = int(sys.argv[6]) if len(sys.argv) > 6 else 150
mode = sys.argv[7] if len(sys.argv) > 7 else 'full'
seed = int(sys.argv[sys.argv.index('--seed') + 1]) if '--seed' in sys.argv else 1
initFile = sys.argv[sys.argv.index('--init') + 1] if '--init' in sys.argv else None   # a previous fit json: start there
popsize = int(sys.argv[sys.argv.index('--pop') + 1]) if '--pop' in sys.argv else 32
subset = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None       # 'nl' = the nonlinear values only
fixed = dict((kv.split('=')[0], float(kv.split('=')[1])) for kv in sys.argv[sys.argv.index('--fix') + 1].split(',')) if '--fix' in sys.argv else {}
ref = json.load(open(refPath))
target = f'amp:{ch}:{side}:{knobs}'

# current values (from Source/DSP/AmpCircuit.h)
BRIT = dict(paRef=27.80, gridKg=9.261e-6, inDb=33.0, bias=0.3463, satKnee=2.494, nfb=6.036, piMax=1.157, sag=0.2272, sagMs=50.0,
    R1=2.939e4, R2=1.978e6, R3=1.95e4, R4=1.16e5, C1=3.557e-10, C2=1.231e-8, C3=2.648e-8, voiceHz=737.6, voiceDb=-2.177, voiceQ=0.7432,
    brightDb=8.558, brightHz=3052.0, presenceHz=1717.0, depthHz=92.07, spkHz=106.8, spkQ=1.764, spkDb=5.749, coilHz=4044.0, coilDb=3.708, xfHp=35.25, xfLp=1.29e4,
    **{'s0.Rk':1840.0,'s0.fb':0.2339,'s0.Rs':3.52e4,'s0.cathDb':-13.08,'s0.cathHz':536.5,'s0.lpHz':1.136e4,'s0.div':0.1162,
       's1.Rk':9354.0,'s1.fb':0.6738,'s1.Rs':2.36e4,'s1.cathDb':-2.33,'s1.cathHz':35.2,'s1.lpHz':1.399e4,'s1.C':9.738e-9,'s1.div':0.4817,
       's2.Rk':977.4,'s2.fb':0.6186,'s2.Rs':3.152e5,'s2.cathDb':-0.3413,'s2.cathHz':994.1,'s2.lpHz':1.034e4,'s2.C':6.633e-8,'s2.div':0.02631,
       's3.Rk':2792.0,'s3.fb':0.4607,'s3.Rs':8.14e4,'s3.cathDb':-8.781,'s3.cathHz':69.13,'s3.lpHz':3323.0,'s3.C':1.469e-9})
VELVET = dict(paRef=2.35, gridKg=3.0e-4, inDb=0.0, bias=0.55, satKnee=1.9, nfb=2.0, piMax=1.7, sag=0.35, sagMs=70.0,
    R1=2.471e4, R2=1.146e6, R3=8921.0, R4=1.186e5, C1=9.592e-10, C2=5.393e-8, C3=3.305e-8, voiceHz=1319.0, voiceDb=-5.06, voiceQ=0.5462,
    brightDb=4.549, brightHz=3617.0, presenceHz=3500.0, depthHz=100.0, spkHz=68.5, spkQ=0.8886, spkDb=8.085, coilHz=2538.0, coilDb=12.48, xfHp=68.54, xfLp=1.272e4,
    **{'s0.Rk':1500.0,'s0.fb':0.0,'s0.Rs':68e3,'s0.cathDb':-8.617,'s0.cathHz':195.1,'s0.lpHz':1.28e4,'s0.div':1.0,
       's1.Rk':1500.0,'s1.fb':0.0,'s1.Rs':220e3,'s1.cathDb':-10.27,'s1.cathHz':1469.0,'s1.lpHz':1.179e4,'s1.div':0.45})
INIT = dict({(1,0): BRIT, (0,1): VELVET}[(ch, side)])
INIT.setdefault('paAsym', 0.0)
if initFile: INIT.update(json.load(open(initFile))['params'])
INIT.update(fixed)

# (name, lo, hi, log?) -- bounds; the tone stack / voicing stays within a factor of 3 of the current values
def spec_for(init):
    P = [('gridKg',3e-6,3e-3,1),('inDb',init['inDb']-15,init['inDb']+10,0),('paAsym',0.0,0.4,0),('paRef',init['paRef']/16,init['paRef']*4,1),('bias',0.2,0.8,0),('satKnee',1.0,3.5,0),('nfb',0.5,8.0,1),('piMax',0.8,3.0,0),('sag',0.0,0.6,0),('sagMs',20,150,1),
         ('voiceHz',250,2500,1),('voiceDb',-8,8,0),('voiceQ',0.3,2.5,1),('brightDb',0,10,0),('brightHz',1500,6000,1),('presenceHz',1200,5000,1),('depthHz',60,200,1),
         ('spkHz',60,140,1),('spkQ',0.5,2.5,1),('spkDb',3,14,0),('coilHz',1000,6000,1),('coilDb',0,14,0),('xfHp',20,100,1),('xfLp',4000,16000,1)]
    for k in ['R1','R2','R3','R4','C1','C2','C3']:
        P.append((k, init[k]/3, init[k]*3, 1))
    for s in range(4):
        if f's{s}.Rk' not in init: continue
        P += [(f's{s}.Rk',500,12000,1),(f's{s}.fb',0,1,0),(f's{s}.Rs',10e3,500e3,1),(f's{s}.cathDb',-15,0,0),(f's{s}.cathHz',30,1500,1),(f's{s}.lpHz',2500,20000,1)]
        if f's{s}.C' in init: P.append((f's{s}.C',5e-10,1e-7,1))
        if f's{s}.div' in init and s < 3: P.append((f's{s}.div',0.02,1.0,1))
    return P
SPEC = spec_for(INIT)
NL = ('gridKg','inDb','paAsym','paRef','bias','satKnee','nfb','piMax','sag','sagMs')
if subset == 'nl':
    SPEC = [p for p in SPEC if p[0] in NL or any(p[0].endswith(k) for k in ('.Rk','.fb','.Rs','.div','.C'))]
SPEC = [p for p in SPEC if p[0] not in fixed]
FIXED = {k: v for k, v in INIT.items() if k not in [p[0] for p in SPEC]}
def to_unit(v, lo, hi, lg): return (math.log(v/lo)/math.log(hi/lo)) if lg else (v-lo)/(hi-lo)
def from_unit(u, lo, hi, lg):
    u = min(1.0, max(0.0, u))
    return lo*(hi/lo)**u if lg else lo+(hi-lo)*u
x0 = [min(0.999, max(0.001, to_unit(INIT[n], lo, hi, lg))) for n, lo, hi, lg in SPEC]
def params(u):
    p = dict(FIXED); p.update({n: from_unit(ui, lo, hi, lg) for ui, (n, lo, hi, lg) in zip(u, SPEC)}); return p
def setstr(p): return f'{ch}:{side}:' + ','.join(f'{k}={v:.6g}' for k, v in p.items())

def evaluate(u):
    try:
        out = subprocess.run([LAB, '--set', setstr(params(u)), 'fitfeat', target], capture_output=True, text=True, timeout=120)
        return json.loads(out.stdout)
    except Exception as e:
        return None

own = json.loads(subprocess.run([LAB, 'fitfeat', target], capture_output=True, text=True).stdout)   # the shipped circuit's own features
def clipdb(a, floor=-50.0): return np.maximum(np.array(a, dtype=float), floor)
def mr(a): a = np.array(a, dtype=float); return a - a.mean()

def loss(f, detail=False):
    if f is None: return 1e6
    terms = {}
    for k0 in ('75', '19'):
        g = np.array(f['g'+k0]) - f['g'+k0][0]; gr = np.array(ref['g'+k0]) - ref['g'+k0][0]
        terms['g'+k0] = 1.0 * np.mean((g - gr)**2)
        terms['thd'+k0] = 0.7 * np.mean((clipdb(f['thd'+k0]) - clipdb(ref['thd'+k0]))**2)
        terms['h2'+k0] = 0.4 * np.mean((clipdb(f['h2_'+k0]) - clipdb(ref['h2_'+k0]))**2)
        terms['h3'+k0] = 0.4 * np.mean((clipdb(f['h3_'+k0]) - clipdb(ref['h3_'+k0]))**2)
        bal = clipdb(f['h2_'+k0], -45) - clipdb(f['h3_'+k0], -45); balr = clipdb(ref['h2_'+k0], -45) - clipdb(ref['h3_'+k0], -45)
        terms['bal'+k0] = 0.7 * np.mean((bal - balr)**2)
        terms['h5'+k0] = 0.25 * np.mean((clipdb(f['h5_'+k0]) - clipdb(ref['h5_'+k0]))**2)
        # the fizz share only means something where there are harmonics to share
        mask = (np.array(f['thd'+k0]) > -45) & (np.array(ref['thd'+k0]) > -45)
        hfd = (clipdb(f['hf'+k0]) - clipdb(ref['hf'+k0])) * mask
        terms['hf'+k0] = 0.4 * np.sum(hfd**2) / max(1, mask.sum())
    m60 = mr(f['m60']); r60 = mr(own['m60'] if mode == 'nonlinear' else ref['m60'])
    terms['m60'] = (3.0 if mode == 'nonlinear' else 1.0) * np.mean((m60 - r60)**2)
    if 'riff_crest' in ref and 'riff_crest' in f:
        terms['crest'] = 2.0 * (f['riff_crest'] - ref['riff_crest'])**2
        terms['range'] = 0.0 * (f['riff_range'] - ref['riff_range'])**2
    for lv in ('40', '24'):
        t = mr(np.array(f['m'+lv]) - np.array(f['m60'])); tr = mr(np.array(ref['m'+lv]) - np.array(ref['m60']))
        terms['tilt'+lv] = 0.6 * np.mean((t - tr)**2)
        terms['imd'+lv] = 1.0 * (max(-40, f['imd'+lv]) - max(-40, ref['imd'+lv]))**2
    terms['imd60'] = 1.0 * (max(-40, f['imd60']) - max(-40, ref['imd60']))**2
    rc = np.array(f['recover']); rr = np.array(ref['recover'])
    terms['recover'] = 0.5 * np.mean(((rc - rc[-1]) - (rr - rr[-1]))**2)
    if detail: return terms
    return float(sum(terms.values()))

if __name__ == '__main__':
    print('target', target, 'ref', refPath, 'mode', mode, 'params', len(SPEC))
    f0 = evaluate(x0); l0 = loss(f0)
    print('start loss %.2f' % l0, {k: round(v, 1) for k, v in loss(f0, True).items()})
    es = cma.CMAEvolutionStrategy(x0, 0.2, {'popsize': popsize, 'bounds': [0, 1], 'seed': seed, 'verbose': -9})
    pool = ThreadPoolExecutor(8)
    best = (l0, x0)
    t0 = time.time()
    for it in range(iters):
        X = es.ask()
        feats = list(pool.map(evaluate, X))
        L = [loss(f) for f in feats]
        es.tell(X, L)
        i = int(np.argmin(L))
        if L[i] < best[0]: best = (L[i], X[i])
        if it % 10 == 0 or it == iters - 1:
            print(f'it {it:4d} best {best[0]:.2f} pop-best {L[i]:.2f} mean {np.mean(L):.2f} sigma {es.sigma:.3f} {time.time()-t0:.0f}s', flush=True)
    fb = evaluate(best[1])
    print('final loss %.2f' % loss(fb), {k: round(v, 1) for k, v in loss(fb, True).items()})
    p = params(best[1])
    json.dump({'set': setstr(p), 'params': p, 'loss': best[0], 'target': target, 'ref': refPath, 'mode': mode}, open(f'fit/{outName}.json', 'w'), indent=1)
    print('--set', setstr(p))
