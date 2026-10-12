import json,glob,os,sys
d=os.path.join(os.path.dirname(__file__),'..','sheets')
S={os.path.basename(p)[:-5]:json.load(open(p)) for p in glob.glob(d+'/*.json')}
bad=[]
for name,s in S.items():
    for r in s['rows']:
        for c in s['columns']:
            if c not in r: bad.append(f"{name}.{r.get('id')}.{c}: missing")
            elif r[c] is None and not (name=='attacks' and (r['hazardAnim'] is None and c in('hazardAnim','hazardScale','hazardFootPx') or c=='voice')): bad.append(f"{name}.{r['id']}.{c}: empty")
            elif r[c] is False and c.startswith('verified'): bad.append(f"{name}.{r['id']}.{c}: unverified")
anims={r['id'] for r in S['animations']['rows']}; snds={r['id'] for r in S['sounds']['rows']}
for r in S['attacks']['rows']:
    if r['windupAnim'] not in anims: bad.append(f"attacks.{r['id']}.windupAnim -> {r['windupAnim']}")
    if r['hazardAnim'] and r['hazardAnim'] not in anims: bad.append(f"attacks.{r['id']}.hazardAnim -> {r['hazardAnim']}")
    for k in('sound','voice'):
        if r[k] and r[k] not in snds: bad.append(f"attacks.{r['id']}.{k} -> {r[k]}")
print("\n".join(bad) if bad else "preflight clean"); sys.exit(1 if bad else 0)
