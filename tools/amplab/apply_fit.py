"""apply_fit.py fit/<name>.json -> prints C++ assignment lines in the AmpCircuit.h style."""
import json, sys
d = json.load(open(sys.argv[1])); p = d['params']
def fmt(v):
    s = f'{v:.4g}'
    if 'e' in s:
        m, e = s.split('e'); return f'{m}e{int(e)}f'
    return s + ('f' if '.' in s else '.0f')
top = [k for k in p if '.' not in k]
lines, cur = [], '                '
for k in top:
    piece = f'a.{k} = {fmt(p[k])}; '
    if len(cur) + len(piece) > 118:
        lines.append(cur.rstrip()); cur = '                '
    cur += piece
lines.append(cur.rstrip())
for s in range(4):
    ks = [k for k in p if k.startswith(f's{s}.')]
    if not ks: continue
    lines.append('                ' + ' '.join(f'a.st[{s}].{k.split(".")[1]} = {fmt(p[k])};' for k in ks))
print('\n'.join(lines))
