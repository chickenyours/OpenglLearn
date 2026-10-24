"""Independent CPU reference for the recovered source display arithmetic.

The three-section SSA evaluator executes original DXIL branch/phi operations;
it is independent of the compact formula. No GPU or target execution is used.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import random
import re
import struct

def number(x):
    if x.startswith('0x'): return struct.unpack('>d', bytes.fromhex(x[2:]))[0]
    return float(x)

def parameters(a,m,l,toe,p):
    l0=(p-m)*l/a
    s0=m+l0
    s1=m+a*l0
    factor=-a/(p-s1)*math.log2(math.e)
    return [[a,m,l,toe],[p,s0,p-s1,factor],[-s0*factor,1/m,m*(1-a),0.]]

def curve(x,p):
    if p[2][3] != 0: return x
    u=x*p[2][1]
    w=u*u*(3-2*u) if x<p[0][1] else 1.
    sh=0. if x<p[1][1] else 1.
    t=p[0][1]*u**p[0][3]
    lin=p[0][0]*x+p[2][2]
    shoulder=p[1][0]-p[1][2]*2**(p[1][3]*x+p[2][0])
    return (1-w)*t+(w-sh)*lin+sh*shoulder

def cct_encode(x):
    return (math.log2(x)+9.720000267028809)*.05707762390375137 if x>.0078125 else x*10.540237426757812+.072905533015728

def cct_decode(x):
    if x<.155251145362854: return (x-.072905533015728)*.09487452358007431
    if x<1.4679962396621704: return 2**(x*17.520000457763672-9.720000267028809)
    return 65504.

def srgb(x,gamma=1.,scale=1.,lower=0.):
    s=1.0549999475479126*x**.4166666567325592-.054999999701976776 if x>.0031308000907301903 else x*12.920000076293945
    return s**gamma*scale+lower

class SectionSSA:
    def __init__(self,path):
        lines=path.read_text().splitlines()
        lo=next(i for i,x in enumerate(lines)if x.startswith('; <label>:139 '))
        hi=next(i for i,x in enumerate(lines)if x.strip()=='ret void' and i>lo)
        self.instructions=[];self.labels={}
        for x in lines[lo:hi]:
            label=re.match(r'; <label>:(\d+)',x)
            if label: self.labels[int(label[1])]=len(self.instructions);self.instructions.append(('label',int(label[1])))
            elif x.startswith('  '):self.instructions.append(('instruction',x.strip().split('  ;')[0]))
    def run(self,rgb,p):
        v={133:rgb[0],134:rgb[1],135:rgb[2],136:p[2]}
        def val(s):
            return v[int(s[1:])] if s.startswith('%') else number(s)
        pc=self.labels[139];current=139;previous=123
        while pc<len(self.instructions):
            kind,line=self.instructions[pc];pc+=1
            if kind=='label':current=line;continue
            if line.startswith('br '):
                m=re.match(r'br i1 %(\d+), label %(\d+), label %(\d+)',line)
                target=int(m[2 if v[int(m[1])] else 3]) if m else int(re.search(r'label %(\d+)',line)[1])
                previous=current;pc=self.labels[target];continue
            if line.startswith('call void'):continue
            target,op=re.match(r'%(\d+) = (.*)',line).groups();target=int(target)
            if 'cbufferLoadLegacy' in op:
                reg=int(re.search(r', i32 (\d+)\)',op)[1]);v[target]=p[reg]
            elif op.startswith('extractvalue'):
                src,i=re.search(r'%(\d+), (\d+)$',op).groups();v[target]=v[int(src)][int(i)]
            elif op.startswith(('fmul','fadd','fsub','fdiv')):
                tag,a,b=re.match(r'(\w+) fast float ([^,]+), (.+)',op).groups();a=val(a);b=val(b)
                v[target]={'fmul':lambda:a*b,'fadd':lambda:a+b,'fsub':lambda:a-b,'fdiv':lambda:a/b}[tag]()
            elif op.startswith('fcmp'):
                cmp,a,b=re.match(r'fcmp fast (\w+) float ([^,]+), (.+)',op).groups()
                assert cmp in ['ult','olt'];v[target]=val(a)<val(b)
            elif op.startswith('select'):
                c,a,b=re.match(r'select i1 %(\d+), float ([^,]+), float (.+)',op).groups();v[target]=val(a)if v[int(c)]else val(b)
            elif op.startswith('phi'):
                choices=re.findall(r'\[ ([^,]+), %(\d+) \]',op)
                v[target]=next(val(a)for a,pred in choices if int(pred)==previous)
            elif '@dx.op.unary.f32' in op:
                code,arg=re.search(r'i32 (\d+), float ([^)]+)',op).groups();arg=val(arg)
                assert int(code)in[21,23]
                v[target]=(2.**arg if int(code)==21 else math.log2(arg)if arg>0 else -math.inf)
            else:raise ValueError(op)
        return [v[240],v[241],v[242]]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--research',type=Path,default=Path('E:/REA/pragmata/hair_display'))
    args=ap.parse_args();s=SectionSSA(args.research/'disassembly/tonemap.PostTonemap3Section.pixel.txt')
    rng=random.Random(81731);cases=[];maxerr=0.
    for a,m,l,toe,pmax in [(1,.22,.4,1,1),(1.2,.18,.5,1.33,1),(1.1,.25,.2,.8,1.5)]:
        p=parameters(a,m,l,toe,pmax)
        inputs=[0.,1e-8,m*(1-1e-7),m,m*(1+1e-7),p[1][1]*(1-1e-7),p[1][1],p[1][1]*(1+1e-7),1.,2.,10.,100.,60000.]
        inputs += [10**rng.uniform(-7,4)for _ in range(1024)]
        for i,x in enumerate(inputs):
            rgb=[x,inputs[(i+1)%len(inputs)],inputs[(i+2)%len(inputs)]]
            original=s.run(rgb,p);compact=[curve(c,p)for c in rgb]
            error=max(abs(x-y)for x,y in zip(original,compact));maxerr=max(maxerr,error)
            assert error<2e-12 and all(math.isfinite(x)for x in compact)
        cases.append(dict(inputs=len(inputs),parameters=p))
    result=dict(schema='pragmata.source-display-reference.v1',threeSection=dict(rgbCases=sum(c['inputs']for c in cases),
        scalarEvaluations=3*sum(c['inputs']for c in cases),maximumDoubleError=maxerr,groups=cases,
        scope='Original selected shader SDR core branch/phi SSA versus compact formula, not native CPU writer, OCIO/RGC, runtime selection or GPU equivalence'),
        scalarGolden=[dict(input=x,cct=cct_encode(x),decoded=cct_decode(cct_encode(x)),srgb=srgb(x))for x in [0.,.001,.0078125,.018,.18,1.,4.,32.]],
        missing=['Runtime whitepoint/OCIO/RGC and output adjustment','Whole-game pipeline and reference screenshot variant'])
    (args.research/'reference_validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result['threeSection'].items()if k!='groups'}))

if __name__=='__main__':main()
